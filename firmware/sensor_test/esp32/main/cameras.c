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
#include "usb_camera_config.h"
#ifdef TREE_CAMERA_SPI
#include "camera_link_esp.h"
#define LOCAL_CAMERAS 1
#else
#define LOCAL_CAMERAS 2
#endif
#define MAX_FORMATS 24
#define JPEG_CAP (256*1024)
typedef struct {
 bool present,streaming; uint8_t addr,uvc_index;
 size_t count, advertised; uvc_host_frame_info_t formats[MAX_FORMATS];
 uvc_host_stream_hdl_t handle;
 uint8_t *jpeg;size_t len;int64_t stamp;
 unsigned raw_width,raw_height;
 uint32_t frames,errors,oversize;esp_err_t error;int selected;
} camera_t;
typedef struct {int type,id,format;uint8_t addr,uvc_index;size_t count;} command_t;
static camera_t cams[LOCAL_CAMERAS];
static QueueHandle_t commands;
static SemaphoreHandle_t lock;
static const char *TAG="tree_uvc";
typedef struct {
 usb_device_handle_t handle;
 uint8_t address,device_class;uint16_t vid,pid;
 bool video_interface,hub_interface;
 uint16_t video_iso_min,video_iso_max;
 bool video_bulk_in;
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
  bool streaming_interface=false;
  while(offset+2<=cfg->wTotalLength && p[offset]>=2 && offset+p[offset]<=cfg->wTotalLength) {
   const uint8_t *desc=p+offset;
   if(desc[1]==4) {
    streaming_interface=desc[0]>=9 && desc[5]==14 && desc[6]==2;
    if(desc[0]>=9 && desc[5]==14) o.video_interface=true;
    if(desc[0]>=9 && desc[5]==9) o.hub_interface=true;
   } else if(streaming_interface && desc[1]==5 && desc[0]>=7 && (desc[2]&0x80)) {
    uint8_t type=desc[3]&3;
    uint16_t bytes=(desc[4]|(desc[5]<<8))&0x7ff;
    if(type==1 && bytes) {
     if(!o.video_iso_min || bytes<o.video_iso_min) o.video_iso_min=bytes;
     if(bytes>o.video_iso_max) o.video_iso_max=bytes;
    } else if(type==2) o.video_bulk_in=true;
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
   uint8_t addr=observed[i].address;
   xSemaphoreTake(lock,portMAX_DELAY);memset(&observed[i],0,sizeof(observed[i]));xSemaphoreGive(lock);
   /* An unopened stream has no UVC disconnect callback. */
   command_t cmd={.type=5,.addr=addr};
   if(xQueueSend(commands,&cmd,0)!=pdTRUE) ESP_LOGW(TAG,"Disconnect queue full");
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
 cJSON_AddNumberToObject(o,"d_minus_gpio",ROBOT_CAMERA_USB_DM_GPIO);
 cJSON_AddNumberToObject(o,"d_plus_gpio",ROBOT_CAMERA_USB_DP_GPIO);
 cJSON_AddNumberToObject(o,"pcb_camera_ports",ROBOT_CAMERA_USB_PORT_COUNT);
 cJSON_AddBoolToObject(o,"external_hub_required_for_two_cameras",ROBOT_CAMERA_USB_EXTERNAL_HUB_REQUIRED);
 cJSON_AddBoolToObject(o,"independent_usb_gpio_ports",false);
#ifdef TREE_CAMERA_SPI
 cJSON_AddStringToObject(o,"wiring","CAM1 -> ESP32 USB; CAM2 -> STM32 USB -> SPI -> ESP32");
 cJSON_AddStringToObject(o,"camera_numbering","0=ESP32 USB; 1=STM32 USB over SPI");
#else
 cJSON_AddStringToObject(o,"wiring","J_HUB1 -> hub downstream 1; J_HUB2 -> hub downstream 2; hub upstream D- -> GPIO19, D+ -> GPIO20; GND common, VBUS=5V supply (not GPIO)");
 cJSON_AddStringToObject(o,"camera_numbering","enumeration order; camera ID is not a fixed PCB port number");
#endif
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
  if(d->video_interface) {
   cJSON_AddNumberToObject(v,"video_iso_min_packet",d->video_iso_min);
   cJSON_AddNumberToObject(v,"video_iso_max_packet",d->video_iso_max);
   cJSON_AddBoolToObject(v,"video_bulk_in",d->video_bulk_in);
  }
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
 unsigned w=frame->vs_format.h_res,h=frame->vs_format.v_res;
 size_t length=frame->data_len;
 bool raw=frame->vs_format.format==UVC_VS_FORMAT_YUY2 && w && h && w<=320 && h<=240 &&
          length==(size_t)w*h*2;
 if(frame->vs_format.format==UVC_VS_FORMAT_MJPEG && length<=JPEG_CAP) {
  /* The UVC payload can contain zero padding after a complete JPEG. */
  while(length>4 && frame->data[length-1]==0) length--;
 }
 if(length>JPEG_CAP) {c->oversize++;}
 else if(raw || (length>=4 && frame->data[0]==0xff && frame->data[1]==0xd8 &&
         frame->data[length-2]==0xff && frame->data[length-1]==0xd9)) {
  memcpy(c->jpeg,frame->data,length);c->len=length;
  c->raw_width=raw?w:0;c->raw_height=raw?h:0;
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
static uint32_t preview_interval(const uvc_host_frame_info_t *f) {
 const bool raw=f->format==UVC_VS_FORMAT_YUY2;
 const uint32_t minimum=raw?2000000u:1000000u;
 uint32_t interval=f->default_interval>=minimum?f->default_interval:0;
 if(f->interval_type) {
  for(unsigned i=0;i<f->interval_type && i<CONFIG_UVC_INTERVAL_ARRAY_SIZE;i++)
   if(f->interval[i]>=minimum && (!interval || f->interval[i]<interval)) interval=f->interval[i];
 } else if(!interval && f->interval_max>=minimum) interval=f->interval_max;
 return interval?interval:(raw?0:f->default_interval);
}
static void manager_task(void *arg) {
 command_t cmd;
 for(;;) {
  if(xQueueReceive(commands,&cmd,portMAX_DELAY)!=pdTRUE) continue;
  if(cmd.type==1) {
   int id=-1;
   xSemaphoreTake(lock,portMAX_DELAY);
   for(int i=0;i<LOCAL_CAMERAS;i++) if(cams[i].present && cams[i].addr==cmd.addr) {
    if(cams[i].count || cams[i].uvc_index==cmd.uvc_index) id=-2;
    else if(!cams[i].handle) id=i;
   }
   if(id!=-2) for(int i=0;i<LOCAL_CAMERAS;i++) if(!cams[i].present && !cams[i].handle) {id=i;break;}
   xSemaphoreGive(lock);
   if(id<0 || !cmd.count || cmd.count>256) continue;
   uvc_host_frame_info_t *f=calloc(cmd.count,sizeof(*f));if(!f) continue;
   size_t n=cmd.count;
   esp_err_t e=uvc_host_get_frame_list(cmd.addr,cmd.uvc_index,(uvc_host_frame_info_t (*)[])f,&n);
   if(e==ESP_OK) {
    xSemaphoreTake(lock,portMAX_DELAY);camera_t *c=&cams[id];
    c->present=true;c->addr=cmd.addr;c->uvc_index=cmd.uvc_index;c->count=0;c->advertised=n;
    c->len=0;c->frames=0;c->errors=0;c->oversize=0;c->selected=-1;
    for(size_t i=0;i<n && c->count<MAX_FORMATS;i++) if(f[i].format==UVC_VS_FORMAT_MJPEG) c->formats[c->count++]=f[i];
    for(size_t i=0;i<n && c->count<MAX_FORMATS;i++) if(f[i].format==UVC_VS_FORMAT_YUY2 &&
        f[i].h_res && f[i].h_res<=320 && f[i].h_res%2==0 &&
        f[i].v_res && f[i].v_res<=240 && preview_interval(&f[i])) c->formats[c->count++]=f[i];
    c->error=c->count?ESP_OK:ESP_ERR_NOT_SUPPORTED;xSemaphoreGive(lock);
    ESP_LOGI(TAG,"Camera %d address %u: %u advertised, %u preview modes",id,cmd.addr,(unsigned)n,(unsigned)c->count);
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
  if(cmd.type==5) {
   for(int i=0;i<LOCAL_CAMERAS;i++) {
    camera_t *c=&cams[i];
    if(c->addr==cmd.addr && close_camera(c)==ESP_OK) {
     xSemaphoreTake(lock,portMAX_DELAY);
     c->present=false;c->len=0;c->count=0;c->advertised=0;c->addr=0;
     xSemaphoreGive(lock);
    }
   }
   continue;
  }
  /* Sequential acquisition: complete closure before opening the other camera. */
  esp_err_t closed=ESP_OK;
  for(int i=0;i<LOCAL_CAMERAS;i++) if(close_camera(&cams[i])!=ESP_OK) closed=ESP_FAIL;
  if(cmd.type==3 || closed!=ESP_OK) continue;
  camera_t *c=&cams[cmd.id];
  xSemaphoreTake(lock,portMAX_DELAY);
  bool valid=c->present && cmd.format>=0 && cmd.format<c->count;
  uvc_host_frame_info_t f=valid?c->formats[cmd.format]:(uvc_host_frame_info_t){0};
  xSemaphoreGive(lock);if(!valid) continue;
  uint32_t interval=preview_interval(&f);
  uvc_host_stream_config_t cfg={
   .event_cb=stream_event,.frame_cb=got_frame,.user_ctx=c,
   .usb={.dev_addr=c->addr,.uvc_stream_index=c->uvc_index},
   .vs_format={.h_res=f.h_res,.v_res=f.v_res,.format=f.format,.fps=interval?10000000.0f/interval:0},
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
 ESP_LOGI(TAG,"USB internal PHY: D- GPIO%d, D+ GPIO%d; local camera slots=%d",
  ROBOT_CAMERA_USB_DM_GPIO,ROBOT_CAMERA_USB_DP_GPIO,LOCAL_CAMERAS);
 lock=xSemaphoreCreateMutex();commands=xQueueCreate(12,sizeof(command_t));
 assert(lock && commands);
 for(int i=0;i<LOCAL_CAMERAS;i++) {cams[i].jpeg=heap_caps_malloc(JPEG_CAP,MALLOC_CAP_SPIRAM);assert(cams[i].jpeg);cams[i].selected=-1;}
#ifdef TREE_CAMERA_SPI
 camera_spi_init();
#endif
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
 for(int i=0;i<LOCAL_CAMERAS;i++) {
  camera_t *c=&cams[i];cJSON *o=cJSON_CreateObject();cJSON_AddItemToArray(a,o);
  cJSON_AddNumberToObject(o,"id",i);cJSON_AddBoolToObject(o,"present",c->present);
  cJSON_AddNumberToObject(o,"usb_address",c->addr);cJSON_AddNumberToObject(o,"uvc_index",c->uvc_index);
  cJSON_AddBoolToObject(o,"streaming",c->streaming);cJSON_AddNumberToObject(o,"selected",c->selected);
  cJSON_AddStringToObject(o,"error",esp_err_to_name(c->error));cJSON_AddNumberToObject(o,"frames",c->frames);
  cJSON_AddNumberToObject(o,"bad_frames",c->errors);cJSON_AddNumberToObject(o,"oversize",c->oversize);
  cJSON_AddNumberToObject(o,"advertised_modes",c->advertised);
  if(c->len) cJSON_AddNumberToObject(o,"frame_age_ms",(esp_timer_get_time()-c->stamp)/1000);
  else cJSON_AddNullToObject(o,"frame_age_ms");
  cJSON *formats=cJSON_AddArrayToObject(o,"formats");
  for(size_t j=0;j<c->count;j++) {cJSON *f=cJSON_CreateObject();cJSON_AddItemToArray(formats,f);
   cJSON_AddNumberToObject(f,"index",j);cJSON_AddNumberToObject(f,"width",c->formats[j].h_res);
   cJSON_AddNumberToObject(f,"height",c->formats[j].v_res);
   cJSON_AddStringToObject(f,"encoding",c->formats[j].format==UVC_VS_FORMAT_MJPEG?"MJPEG":"YUY2");
   cJSON_AddNumberToObject(f,"default_fps",c->formats[j].default_interval?10000000.0/c->formats[j].default_interval:0);
   uint32_t interval=preview_interval(&c->formats[j]);
   cJSON_AddNumberToObject(f,"preview_fps",interval?10000000.0/interval:0);
  }
 }
 xSemaphoreGive(lock);
#ifdef TREE_CAMERA_SPI
 cJSON_AddItemToArray(a,camera_spi_status());
#endif
 return a;
}
esp_err_t cameras_select(int id,int format) {
#ifdef TREE_CAMERA_SPI
 if(id==1 && format==0) { camera_spi_select(true); return ESP_OK; }
#endif
 if(id<0 || id>=LOCAL_CAMERAS || format<0) return ESP_ERR_INVALID_ARG;
 xSemaphoreTake(lock,portMAX_DELAY);bool valid=cams[id].present && format<cams[id].count;xSemaphoreGive(lock);
 if(!valid) return ESP_ERR_INVALID_ARG;
 command_t c={.type=2,.id=id,.format=format};return xQueueSend(commands,&c,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
esp_err_t cameras_stop(void) {
#ifdef TREE_CAMERA_SPI
 camera_spi_select(false);
#endif
 command_t c={.type=3};return xQueueSend(commands,&c,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
bool cameras_idle(void) {
 xSemaphoreTake(lock,portMAX_DELAY);
 bool idle=uxQueueMessagesWaiting(commands)==0;
 for(int i=0;i<LOCAL_CAMERAS;i++) idle=idle && !cams[i].handle;
 xSemaphoreGive(lock);
#ifdef TREE_CAMERA_SPI
 idle=idle && camera_spi_idle();
#endif
 return idle;
}
esp_err_t cameras_jpeg(httpd_req_t *req,int id) {
 httpd_resp_set_hdr(req,"Access-Control-Allow-Origin","*");
 httpd_resp_set_hdr(req,"Access-Control-Expose-Headers","X-Frame-Seq, X-Frame-Age-Ms");
#ifdef TREE_CAMERA_SPI
 if(id==1) return camera_spi_jpeg(req);
#endif
 if(id<0 || id>=LOCAL_CAMERAS) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid id");
 uint8_t *copy=heap_caps_malloc(JPEG_CAP,MALLOC_CAP_SPIRAM);
 if(!copy) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"no PSRAM");
 xSemaphoreTake(lock,portMAX_DELAY);camera_t *c=&cams[id];size_t len=c->len;
 int64_t age=(esp_timer_get_time()-c->stamp)/1000;
 if(!c->streaming || age>1500) len=0;
 if(len) memcpy(copy,c->jpeg,len);
 uint32_t seq=c->frames;unsigned w=c->raw_width,h=c->raw_height;xSemaphoreGive(lock);
 if(!len) {free(copy);return sensor_busy(req,"no fresh image; select camera first");}
 char n[32];snprintf(n,sizeof(n),"%lu",(unsigned long)seq);httpd_resp_set_hdr(req,"X-Frame-Seq",n);
 snprintf(n,sizeof(n),"%lld",(long long)age);httpd_resp_set_hdr(req,"X-Frame-Age-Ms",n);
 httpd_resp_set_hdr(req,"Cache-Control","no-store");
 if(w) {
  /* BMP palette and top-down grayscale for raw YUY2. Keep conversion off the USB task. */
  enum { PIXELS=54+256*4 };
  size_t stride=(w+3u)&~3u,size=PIXELS+stride*h;
  uint8_t *bmp=heap_caps_calloc(1,size,MALLOC_CAP_SPIRAM);
  if(!bmp) {free(copy);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"no PSRAM");}
  bmp[0]='B';bmp[1]='M';
  uint32_t header[]={size,PIXELS,40,w,(uint32_t)-(int32_t)h,0x00080001u};
  memcpy(bmp+2,&header[0],4);memcpy(bmp+10,&header[1],sizeof header-4);
  for(unsigned v=0;v<256;v++) memset(bmp+54+4*v,v,3);
  for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++)
   bmp[PIXELS+y*stride+x]=copy[(y*w+x)*2];
  free(copy);copy=bmp;len=size;
 }
 httpd_resp_set_type(req,w?"image/bmp":"image/jpeg");
 esp_err_t e=httpd_resp_send(req,(const char*)copy,len);free(copy);return e;
}
