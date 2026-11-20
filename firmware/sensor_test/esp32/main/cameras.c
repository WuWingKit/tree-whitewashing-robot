/* Application adapted to Espressif usb_host_uvc 2.6.0 API and the official
 * usb_hub_dual_camera example. Original application code; upstream driver is
 * Apache-2.0 and fetched separately at a pinned commit. See SOURCES.md. */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "usb/uvc_host.h"
#include "soc/soc.h"
#include "soc/usb_reg.h"
#include "soc/usb_struct.h"
#include "cameras.h"
#define MAX_FORMATS 24
#define JPEG_CAP (256*1024)
typedef struct {
 bool present,streaming; uint8_t addr,uvc_index;
 size_t count; uvc_host_frame_info_t formats[MAX_FORMATS];
 uvc_host_stream_hdl_t handle;
 uint8_t *jpeg;size_t len;int64_t stamp;
 uint32_t frames,errors,oversize;esp_err_t error;int selected;
} camera_t;
typedef struct {int type,id,format;uint8_t addr,uvc_index;size_t count;} command_t;
static camera_t cams[2];
static QueueHandle_t commands;
static SemaphoreHandle_t lock;
static const char *TAG="tree_uvc";
typedef struct {
 usb_device_handle_t handle;
 uint8_t address,device_class;uint16_t vid,pid;
 bool video_interface,hub_interface;
} observed_t;
static observed_t observed[8];
static usb_host_client_handle_t observer_client;
static void observe_address(uint8_t address) {
 int slot=-1;
 for(int i=0;i<8;i++) {
  if(observed[i].handle && observed[i].address==address) return;
  if(!observed[i].handle && slot<0) slot=i;
 }
 if(slot<0) return;
 usb_device_handle_t h=NULL;
 if(usb_host_device_open(observer_client,address,&h)!=ESP_OK) return;
 const usb_device_desc_t *d=NULL;const usb_config_desc_t *cfg=NULL;
 observed_t o={.handle=h,.address=address};
 if(usb_host_get_device_descriptor(h,&d)==ESP_OK) {o.vid=d->idVendor;o.pid=d->idProduct;o.device_class=d->bDeviceClass;}
 if(usb_host_get_active_config_descriptor(h,&cfg)==ESP_OK) {
  const uint8_t *p=(const uint8_t*)cfg;size_t offset=0;
  while(offset+2<=cfg->wTotalLength && p[offset]>=2 && offset+p[offset]<=cfg->wTotalLength) {
   if(p[offset+1]==4 && p[offset]>=9) {
    if(p[offset+5]==14) o.video_interface=true;
    if(p[offset+5]==9) o.hub_interface=true;
   }
   offset+=p[offset];
  }
 }
 xSemaphoreTake(lock,portMAX_DELAY);observed[slot]=o;xSemaphoreGive(lock);
 ESP_LOGI(TAG,"USB enumerated addr=%u VID=%04x PID=%04x class=%u video=%u hub=%u",
  address,o.vid,o.pid,o.device_class,o.video_interface,o.hub_interface);
}
static void observer_event(const usb_host_client_event_msg_t *event,void *arg) {
 if(event->event==USB_HOST_CLIENT_EVENT_NEW_DEV) observe_address(event->new_dev.address);
 else if(event->event==USB_HOST_CLIENT_EVENT_DEV_GONE) {
  for(int i=0;i<8;i++) if(observed[i].handle==event->dev_gone.dev_hdl) {
   usb_device_handle_t h=observed[i].handle;
   xSemaphoreTake(lock,portMAX_DELAY);memset(&observed[i],0,sizeof(observed[i]));xSemaphoreGive(lock);
   (void)usb_host_device_close(observer_client,h);break;
  }
 }
}
static void observer_task(void *arg) {
 usb_host_client_config_t c={.is_synchronous=false,.max_num_event_msg=16,
  .async={.client_event_callback=observer_event}};
 ESP_ERROR_CHECK(usb_host_client_register(&c,&observer_client));
 uint8_t addresses[8];int count=0;
 if(usb_host_device_addr_list_fill(8,addresses,&count)==ESP_OK)
  for(int i=0;i<count;i++) observe_address(addresses[i]);
 uint32_t previous_port=0xffffffffu;int previous_count=-1;
 for(;;) {
  esp_err_t e=usb_host_client_handle_events(observer_client,pdMS_TO_TICKS(1000));
  if(e!=ESP_OK && e!=ESP_ERR_TIMEOUT) ESP_LOGE(TAG,"Observer events: %s",esp_err_to_name(e));
  usb_host_lib_info_t info={0};uint32_t port=USB0.hprt;
  if(usb_host_lib_info(&info)==ESP_OK && (port!=previous_port || info.num_devices!=previous_count)) {
   ESP_LOGI(TAG,"USB bus: devices=%d clients=%d root_connected=%u root_enabled=%u controller_power=%u (not measured VBUS)",
    info.num_devices,info.num_clients,!!(port&USB_PRTCONNSTS),!!(port&USB_PRTENA),!!(port&USB_PRTPWR));
   previous_port=port;previous_count=info.num_devices;
  }
 }
}
cJSON *usb_diagnostics_json(void) {
 usb_host_lib_info_t info={0};esp_err_t e=usb_host_lib_info(&info);
 uint32_t port=USB0.hprt;
 cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"host_query",esp_err_to_name(e));
 cJSON_AddNumberToObject(o,"enumerated_devices",info.num_devices);cJSON_AddNumberToObject(o,"host_clients",info.num_clients);
 cJSON_AddBoolToObject(o,"root_connected",(port&USB_PRTCONNSTS)!=0);
 cJSON_AddBoolToObject(o,"root_enabled",(port&USB_PRTENA)!=0);
 cJSON_AddBoolToObject(o,"controller_power_flag",(port&USB_PRTPWR)!=0);
 cJSON_AddNullToObject(o,"vbus_voltage"); /* No ADC measures physical USB VBUS. */
 cJSON_AddStringToObject(o,"power_note","Controller power flag is NOT a 5V measurement");
 cJSON *a=cJSON_AddArrayToObject(o,"devices");xSemaphoreTake(lock,portMAX_DELAY);
 for(int i=0;i<8;i++) if(observed[i].handle) {
  observed_t *d=&observed[i];cJSON *v=cJSON_CreateObject();cJSON_AddItemToArray(a,v);
  cJSON_AddNumberToObject(v,"address",d->address);cJSON_AddNumberToObject(v,"vid",d->vid);cJSON_AddNumberToObject(v,"pid",d->pid);
  cJSON_AddNumberToObject(v,"device_class",d->device_class);cJSON_AddBoolToObject(v,"video_interface",d->video_interface);
  cJSON_AddBoolToObject(v,"hub_interface",d->hub_interface);
 }
 xSemaphoreGive(lock);return o;
}
static void library_task(void *arg) {
 for(;;) {uint32_t flags=0;esp_err_t e=usb_host_lib_handle_events(portMAX_DELAY,&flags);
  if(e!=ESP_OK) {ESP_LOGE(TAG,"Host events: %s",esp_err_to_name(e));vTaskDelay(pdMS_TO_TICKS(100));}
 }
}
static void connected(const uvc_host_driver_event_data_t *event,void *arg) {
 if(event->type!=UVC_HOST_DRIVER_EVENT_DEVICE_CONNECTED) return;
 command_t c={.type=1,.addr=event->device_connected.dev_addr,
  .uvc_index=event->device_connected.uvc_stream_index,.count=event->device_connected.frame_info_num};
 if(xQueueSend(commands,&c,0)!=pdTRUE) ESP_LOGW(TAG,"Connect queue full; reconnect camera");
}
static bool got_frame(const uvc_host_frame_t *frame,void *arg) {
 camera_t *c=arg;
 if(xSemaphoreTake(lock,pdMS_TO_TICKS(5))!=pdTRUE) return true;
 if(frame->data_len>JPEG_CAP) {c->oversize++;}
 else if(frame->data_len>=4 && frame->data[0]==0xff && frame->data[1]==0xd8 &&
         frame->data[frame->data_len-2]==0xff && frame->data[frame->data_len-1]==0xd9) {
  memcpy(c->jpeg,frame->data,frame->data_len);c->len=frame->data_len;
  c->stamp=esp_timer_get_time();c->frames++;
 } else c->errors++;
 xSemaphoreGive(lock);return true; /* Never retain a driver's frame. */
}
static void stream_event(const uvc_host_stream_event_data_t *event,void *arg) {
 camera_t *c=arg;
 if(event->type==UVC_HOST_DEVICE_DISCONNECTED) {
  command_t cmd={.type=4,.id=(int)(c-cams)};
  /* The worker is not this callback task. Queue disconnect without blocking USB. */
  if(xQueueSend(commands,&cmd,0)!=pdTRUE) ESP_LOGE(TAG,"Disconnect queue full; reboot test firmware");
 } else {
  if(xSemaphoreTake(lock,0)==pdTRUE) {c->errors++;xSemaphoreGive(lock);}
 }
}
static esp_err_t close_camera(camera_t *c) {
 if(!c->handle) return ESP_OK;
 /* Do not hold lock: stop/close can wait for callbacks taking the same lock. */
 (void)uvc_host_stream_stop(c->handle);
 esp_err_t e=uvc_host_stream_close(c->handle);
 xSemaphoreTake(lock,portMAX_DELAY);c->error=e;
 if(e==ESP_OK) {c->handle=NULL;c->streaming=false;c->len=0;}
 xSemaphoreGive(lock);return e;
}
static void manager_task(void *arg) {
 command_t cmd;
 for(;;) {
  if(xQueueReceive(commands,&cmd,portMAX_DELAY)!=pdTRUE) continue;
  if(cmd.type==1) {
   int id=-1;
   xSemaphoreTake(lock,portMAX_DELAY);
   for(int i=0;i<2;i++) if(cams[i].present && cams[i].addr==cmd.addr && cams[i].uvc_index==cmd.uvc_index) id=-2;
   if(id!=-2) for(int i=0;i<2;i++) if(!cams[i].present && !cams[i].handle) {id=i;break;}
   xSemaphoreGive(lock);
   if(id<0 || !cmd.count || cmd.count>256) continue;
   uvc_host_frame_info_t *f=calloc(cmd.count,sizeof(*f));if(!f) continue;
   size_t n=cmd.count;
   esp_err_t e=uvc_host_get_frame_list(cmd.addr,cmd.uvc_index,(uvc_host_frame_info_t (*)[])f,&n);
   if(e==ESP_OK) {
    xSemaphoreTake(lock,portMAX_DELAY);camera_t *c=&cams[id];
    c->present=true;c->addr=cmd.addr;c->uvc_index=cmd.uvc_index;c->count=0;
    c->len=0;c->frames=0;c->errors=0;c->oversize=0;c->selected=-1;
    for(size_t i=0;i<n && c->count<MAX_FORMATS;i++) if(f[i].format==UVC_VS_FORMAT_MJPEG) c->formats[c->count++]=f[i];
    c->error=c->count?ESP_OK:ESP_ERR_NOT_SUPPORTED;xSemaphoreGive(lock);
    ESP_LOGI(TAG,"Camera %d address %u: %u MJPEG formats",id,cmd.addr,(unsigned)c->count);
   }
   free(f);continue;
  }
  if(cmd.type==4) {
   camera_t *c=&cams[cmd.id];
   if(close_camera(c)==ESP_OK) {
    xSemaphoreTake(lock,portMAX_DELAY);c->present=false;c->len=0;c->count=0;xSemaphoreGive(lock);
   }
   continue;
  }
  /* Sequential acquisition: complete closure before opening the other camera. */
  esp_err_t closed=ESP_OK;
  for(int i=0;i<2;i++) if(close_camera(&cams[i])!=ESP_OK) closed=ESP_FAIL;
  if(cmd.type==3 || closed!=ESP_OK) continue;
  camera_t *c=&cams[cmd.id];
  xSemaphoreTake(lock,portMAX_DELAY);
  bool valid=c->present && cmd.format>=0 && cmd.format<c->count;
  uvc_host_frame_info_t f=valid?c->formats[cmd.format]:(uvc_host_frame_info_t){0};
  xSemaphoreGive(lock);if(!valid) continue;
  /* Prefer <=10 fps only when the descriptor explicitly offers that interval. */
  uint32_t interval=f.default_interval;
  if(f.interval_type) {
   for(unsigned i=0;i<f.interval_type && i<CONFIG_UVC_INTERVAL_ARRAY_SIZE;i++)
    if(f.interval[i]>=1000000u && (interval<1000000u || f.interval[i]<interval)) interval=f.interval[i];
  } else if(f.interval_max>=1000000u) interval=f.interval_max;
  uvc_host_stream_config_t cfg={
   .event_cb=stream_event,.frame_cb=got_frame,.user_ctx=c,
   .usb={.dev_addr=c->addr,.uvc_stream_index=c->uvc_index},
   .vs_format={.h_res=f.h_res,.v_res=f.v_res,.format=UVC_VS_FORMAT_MJPEG,.fps=interval?10000000.0f/interval:0},
   .advanced={.number_of_frame_buffers=2,.frame_size=JPEG_CAP,
    .frame_heap_caps=MALLOC_CAP_SPIRAM,.number_of_urbs=3,.urb_size=4096},
  };
  esp_err_t e=uvc_host_stream_open(&cfg,pdMS_TO_TICKS(2000),&c->handle);
  if(e==ESP_OK) e=uvc_host_stream_start(c->handle);
  xSemaphoreTake(lock,portMAX_DELAY);c->error=e;c->selected=cmd.format;c->streaming=(e==ESP_OK);xSemaphoreGive(lock);
  if(e!=ESP_OK) {
   ESP_LOGE(TAG,"Camera %d start: %s",cmd.id,esp_err_to_name(e));(void)close_camera(c);
   xSemaphoreTake(lock,portMAX_DELAY);c->error=e;xSemaphoreGive(lock);
  }
 }
}
void cameras_init(void) {
 lock=xSemaphoreCreateMutex();commands=xQueueCreate(12,sizeof(command_t));
 assert(lock && commands);
 for(int i=0;i<2;i++) {cams[i].jpeg=heap_caps_malloc(JPEG_CAP,MALLOC_CAP_SPIRAM);assert(cams[i].jpeg);cams[i].selected=-1;}
 const usb_host_config_t host={.skip_phy_setup=false,.intr_flags=ESP_INTR_FLAG_LEVEL1};
 ESP_ERROR_CHECK(usb_host_install(&host));
 assert(xTaskCreate(library_task,"usb_events",4096,NULL,15,NULL)==pdPASS);
 assert(xTaskCreate(observer_task,"usb_observer",4096,NULL,4,NULL)==pdPASS);
 assert(xTaskCreate(manager_task,"camera_manager",6144,NULL,5,NULL)==pdPASS);
 uvc_host_driver_config_t driver={.driver_task_stack_size=6144,.driver_task_priority=10,
  .xCoreID=1,.create_background_task=true,.event_cb=connected};
 ESP_ERROR_CHECK(uvc_host_install(&driver));
}
cJSON *cameras_json(void) {
 cJSON *a=cJSON_CreateArray();xSemaphoreTake(lock,portMAX_DELAY);
 for(int i=0;i<2;i++) {
  camera_t *c=&cams[i];cJSON *o=cJSON_CreateObject();cJSON_AddItemToArray(a,o);
  cJSON_AddNumberToObject(o,"id",i);cJSON_AddBoolToObject(o,"present",c->present);
  cJSON_AddNumberToObject(o,"usb_address",c->addr);cJSON_AddNumberToObject(o,"uvc_index",c->uvc_index);
  cJSON_AddBoolToObject(o,"streaming",c->streaming);cJSON_AddNumberToObject(o,"selected",c->selected);
  cJSON_AddStringToObject(o,"error",esp_err_to_name(c->error));cJSON_AddNumberToObject(o,"frames",c->frames);
  cJSON_AddNumberToObject(o,"bad_frames",c->errors);cJSON_AddNumberToObject(o,"oversize",c->oversize);
  if(c->len) cJSON_AddNumberToObject(o,"frame_age_ms",(esp_timer_get_time()-c->stamp)/1000);
  else cJSON_AddNullToObject(o,"frame_age_ms");
  cJSON *formats=cJSON_AddArrayToObject(o,"formats");
  for(size_t j=0;j<c->count;j++) {cJSON *f=cJSON_CreateObject();cJSON_AddItemToArray(formats,f);
   cJSON_AddNumberToObject(f,"index",j);cJSON_AddNumberToObject(f,"width",c->formats[j].h_res);
   cJSON_AddNumberToObject(f,"height",c->formats[j].v_res);
   cJSON_AddNumberToObject(f,"default_fps",c->formats[j].default_interval?10000000.0/c->formats[j].default_interval:0);
  }
 }
 xSemaphoreGive(lock);return a;
}
esp_err_t cameras_select(int id,int format) {
 if(id<0 || id>1 || format<0) return ESP_ERR_INVALID_ARG;
 xSemaphoreTake(lock,portMAX_DELAY);bool valid=cams[id].present && format<cams[id].count;xSemaphoreGive(lock);
 if(!valid) return ESP_ERR_INVALID_ARG;
 command_t c={.type=2,.id=id,.format=format};return xQueueSend(commands,&c,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
esp_err_t cameras_stop(void) {
 command_t c={.type=3};return xQueueSend(commands,&c,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
bool cameras_idle(void) {
 xSemaphoreTake(lock,portMAX_DELAY);
 bool idle=!cams[0].handle && !cams[1].handle && uxQueueMessagesWaiting(commands)==0;
 xSemaphoreGive(lock);return idle;
}
esp_err_t cameras_jpeg(httpd_req_t *req,int id) {
 httpd_resp_set_hdr(req,"Access-Control-Allow-Origin","*");
 if(id<0 || id>1) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid id");
 uint8_t *copy=heap_caps_malloc(JPEG_CAP,MALLOC_CAP_SPIRAM);
 if(!copy) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"no PSRAM");
 xSemaphoreTake(lock,portMAX_DELAY);camera_t *c=&cams[id];size_t len=c->len;
 int64_t age=(esp_timer_get_time()-c->stamp)/1000;
 if(!c->streaming || age>1500) len=0;
 if(len) memcpy(copy,c->jpeg,len);
 uint32_t seq=c->frames;xSemaphoreGive(lock);
 if(!len) {free(copy);return sensor_busy(req,"no fresh JPEG; select camera first");}
 char n[32];snprintf(n,sizeof(n),"%lu",(unsigned long)seq);httpd_resp_set_hdr(req,"X-Frame-Seq",n);
 snprintf(n,sizeof(n),"%lld",(long long)age);httpd_resp_set_hdr(req,"X-Frame-Age-Ms",n);
 httpd_resp_set_type(req,"image/jpeg");httpd_resp_set_hdr(req,"Cache-Control","no-store");
 esp_err_t e=httpd_resp_send(req,(const char*)copy,len);free(copy);return e;
}
