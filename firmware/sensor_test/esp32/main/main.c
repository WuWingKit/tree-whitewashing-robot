#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "rc_protocol.h"
#include "cameras.h"
#include "robot_pinmap.h"
typedef struct {int16_t mm,status;uint16_t seq;int64_t received,changed;bool seen;} tof_t;
static tof_t tof[3];
static SemaphoreHandle_t data_lock;
static int64_t mcu_time;
static uint8_t mcu_type;
static const char *TAG="tree_sensors";
extern const unsigned char page_start[] asm("_binary_index_html_start");
extern const unsigned char page_end[] asm("_binary_index_html_end");
static void uart_task(void *arg) {
 rc_parser_t p={0};rc_frame_t f={0};uint8_t bytes[128],raw[RC_FRAME_SIZE];uint16_t seq=0;int64_t last=0;
 for(;;) {
  int n=uart_read_bytes(UART_NUM_1,bytes,sizeof(bytes),pdMS_TO_TICKS(10));int64_t now=esp_timer_get_time();
  for(int i=0;i<n;i++) if(rc_feed(&p,bytes[i],&f)) {
   xSemaphoreTake(data_lock,portMAX_DELAY);
   if(f.type>=0x90 && f.type<=0x92) {
    tof_t *s=&tof[f.type-0x90];
    if(!s->seen || s->seq!=f.seq) s->changed=now;
    s->mm=f.left;s->status=f.right;s->seq=f.seq;s->received=now;s->seen=true;
   } else if(f.type>=RC_STATUS_DISARMED && f.type<=RC_STATUS_ESTOP) {mcu_time=now;mcu_type=f.type;}
   xSemaphoreGive(data_lock);
  }
  /* Sensor test cannot arm/drive motors. Continually disarm the existing MCU. */
  if(now-last>=100000) {f=(rc_frame_t){.type=RC_STOP,.seq=++seq};rc_encode(raw,&f);uart_write_bytes(UART_NUM_1,raw,sizeof(raw));last=now;}
 }
}
static esp_err_t send_json(httpd_req_t *r,cJSON *o) {
 if(!o) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"allocation failed");
 char *text=cJSON_PrintUnformatted(o);cJSON_Delete(o);
 if(!text) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"allocation failed");
 httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");
 esp_err_t e=httpd_resp_sendstr(r,text);free(text);return e;
}
static esp_err_t status_handler(httpd_req_t *r) {
 cJSON *o=cJSON_CreateObject();int64_t now=esp_timer_get_time();
 cJSON_AddStringToObject(o,"firmware","0.3.0-sensor-test");
 cJSON_AddStringToObject(o,"camera_mode","one_stream_at_a_time");
 cJSON_AddBoolToObject(o,"motor_control_enabled",false);
 cJSON_AddNumberToObject(o,"uptime_ms",now/1000);cJSON_AddNumberToObject(o,"free_internal",heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
 xSemaphoreTake(data_lock,portMAX_DELAY);
 cJSON_AddBoolToObject(o,"stm32_online",mcu_time && now-mcu_time<500000);
 cJSON_AddNumberToObject(o,"stm32_state",mcu_type);cJSON *a=cJSON_AddArrayToObject(o,"tof");
 for(int i=0;i<3;i++) {
  tof_t *s=&tof[i];cJSON *t=cJSON_CreateObject();cJSON_AddItemToArray(a,t);
  bool fresh=s->seen && now-s->changed<750000 && now-s->received<750000;
  bool valid=fresh && s->status==0 && s->mm>=20 && s->mm<=255;
  cJSON_AddNumberToObject(t,"id",i);cJSON_AddBoolToObject(t,"valid",valid);
  if(valid) cJSON_AddNumberToObject(t,"mm",s->mm);else cJSON_AddNullToObject(t,"mm");
  cJSON_AddNumberToObject(t,"status",s->seen?s->status:259);cJSON_AddNumberToObject(t,"seq",s->seq);
  if(s->seen) cJSON_AddNumberToObject(t,"age_ms",(now-s->changed)/1000);else cJSON_AddNullToObject(t,"age_ms");
 }
 xSemaphoreGive(data_lock);
 cJSON_AddItemToObject(o,"cameras",cameras_json());return send_json(r,o);
}
static esp_err_t page_handler(httpd_req_t *r) {
 httpd_resp_set_type(r,"text/html; charset=utf-8");return httpd_resp_send(r,(const char*)page_start,page_end-page_start-1);
}
static int query_int(httpd_req_t *r,const char *key) {
 char q[128],v[20];
 if(httpd_req_get_url_query_str(r,q,sizeof(q))!=ESP_OK || httpd_query_key_value(q,key,v,sizeof(v))!=ESP_OK) return -1;
 char *end;long x=strtol(v,&end,10);return (*v && !*end && x>=0 && x<=1000)?(int)x:-1;
}
static esp_err_t select_handler(httpd_req_t *r) {
 esp_err_t e=cameras_select(query_int(r,"id"),query_int(r,"format"));
 if(e==ESP_ERR_INVALID_ARG) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,esp_err_to_name(e));
 if(e!=ESP_OK) return sensor_busy(r,esp_err_to_name(e));
 httpd_resp_set_status(r,"202 Accepted");return httpd_resp_sendstr(r,"queued; inspect /api/status");
}
static esp_err_t stop_handler(httpd_req_t *r) {
 if(cameras_stop()!=ESP_OK) return sensor_busy(r,"queue full");
 return httpd_resp_sendstr(r,"stop queued");
}
static esp_err_t jpeg_handler(httpd_req_t *r) {return cameras_jpeg(r,query_int(r,"id"));}
static void start_http(void) {
 httpd_config_t c=HTTPD_DEFAULT_CONFIG();c.stack_size=6144;c.lru_purge_enable=true;c.recv_wait_timeout=2;c.send_wait_timeout=2;
 httpd_handle_t h=NULL;ESP_ERROR_CHECK(httpd_start(&h,&c));
 const httpd_uri_t handlers[]={
  {.uri="/",.method=HTTP_GET,.handler=page_handler},
  {.uri="/api/status",.method=HTTP_GET,.handler=status_handler},
  {.uri="/api/camera/select",.method=HTTP_POST,.handler=select_handler},
  {.uri="/api/camera/stop",.method=HTTP_POST,.handler=stop_handler},
 };
 for(unsigned i=0;i<sizeof(handlers)/sizeof(handlers[0]);i++) ESP_ERROR_CHECK(httpd_register_uri_handler(h,&handlers[i]));
 /* Separate HTTP task/port: a slow JPEG client cannot block telemetry/control API. */
 c.server_port=81;c.ctrl_port=32769;c.max_uri_handlers=2;h=NULL;ESP_ERROR_CHECK(httpd_start(&h,&c));
 httpd_uri_t image={.uri="/camera.jpg",.method=HTTP_GET,.handler=jpeg_handler};ESP_ERROR_CHECK(httpd_register_uri_handler(h,&image));
}
void app_main(void) {
 esp_err_t e=nvs_flash_init();
 if(e==ESP_ERR_NVS_NO_FREE_PAGES || e==ESP_ERR_NVS_NEW_VERSION_FOUND) {ESP_ERROR_CHECK(nvs_flash_erase());e=nvs_flash_init();}
 ESP_ERROR_CHECK(e);data_lock=xSemaphoreCreateMutex();assert(data_lock);
 uart_config_t u={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
 ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1,2048,0,0,NULL,0));ESP_ERROR_CHECK(uart_param_config(UART_NUM_1,&u));
 ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1,ROBOT_ESP_UART_TX,ROBOT_ESP_UART_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
 assert(xTaskCreate(uart_task,"tof_uart",4096,NULL,12,NULL)==pdPASS);
 ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());esp_netif_create_default_wifi_ap();
 wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));
 wifi_config_t wifi={.ap={.ssid="TreeRobotSensors",.ssid_len=16,.password="TreeTest2026",.channel=6,.max_connection=2,.authmode=WIFI_AUTH_WPA2_PSK}};
 wifi.ap.ssid_len=strlen((const char*)wifi.ap.ssid);
 ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&wifi));ESP_ERROR_CHECK(esp_wifi_start());
 cameras_init();start_http();ESP_LOGI(TAG,"Connect TreeRobotSensors; open http://192.168.4.1/");
}
