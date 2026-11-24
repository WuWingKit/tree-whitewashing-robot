#pragma once
/* Reuses the original Arduino control/OTA loop. Camera I/O runs in separate
 * IDF tasks; no image is sent through the motor UART. */
struct RangeSample {int16_t mm=-1,status=259;uint16_t seq=0;uint32_t at=0;bool seen=false;};
static RangeSample ranges[3];
#ifdef TREE_CAMERA_SPI
static uint16_t cameraAttempts=0,cameraAcks=0,cameraBits=0;
static uint32_t cameraDiagAt=0;
static bool cameraDiagSeen=false;
#endif
#ifdef TREE_SENSOR_USB
#include "cameras.h"
#include <esp_http_server.h>
extern const unsigned char sensor_page_start[] asm("_binary_index_html_start");
extern const unsigned char sensor_page_end[] asm("_binary_index_html_end");
static bool sensorsStarted=false;
#endif
static void sensorReceive(const rc_frame_t &f) {
#ifdef TREE_CAMERA_SPI
 if(f.type==RC_STATUS_CAMERA) {
  cameraAttempts=f.seq;cameraAcks=(uint16_t)f.left;cameraBits=(uint16_t)f.right;
  cameraDiagAt=millis();cameraDiagSeen=true;return;
 }
#endif
 if(f.type<0x90 || f.type>0x92) return;
 RangeSample &s=ranges[f.type-0x90];
 if(!s.seen || s.seq!=f.seq) s.at=millis();
 s.mm=f.left;s.status=f.right;s.seq=f.seq;s.seen=true;
}
static String sensorRangeJSON() {
 String json="[";uint32_t now=millis();
 for(unsigned i=0;i<3;i++) {
  const RangeSample &s=ranges[i];bool valid=s.seen && now-s.at<750u && s.status==0 && s.mm>=20 && s.mm<=255;
  if(i) json+=",";
  json+="{\"id\":"+String(i)+",\"valid\":"+(valid?"true":"false")+",\"mm\":"+(valid?String(s.mm):String("null"))+
   ",\"status\":"+String(s.status)+",\"seq\":"+String(s.seq)+",\"age_ms\":"+(s.seen?String(now-s.at):String("null"))+"}";
 }
 return json+"]";
}
static void sensorBeginMaintenance() {
#ifdef TREE_SENSOR_USB
 if(sensorsStarted) (void)cameras_stop();
#endif
}
static bool sensorMaintenanceReady() {
#ifdef TREE_SENSOR_USB
 return !sensorsStarted || cameras_idle();
#else
 return true;
#endif
}
static void sensorRoutes() {
#ifdef TREE_SENSOR_USB
 server.on("/api/usb/status",HTTP_GET,[](){
  cJSON *o=usb_diagnostics_json();char *text=cJSON_PrintUnformatted(o);
  reply(text?200:500,text?String(text):String("{}"));free(text);cJSON_Delete(o);
 });
#endif
 server.on("/api/sensors/status",HTTP_GET,[](){
  String json=String("{\"firmware\":\"")+FW_VERSION+"\",\"stm32_online\":"+(mcuLive()?"true":"false")+
#ifdef TREE_CAMERA_SPI
   ",\"motor_control_enabled\":true,\"camera_mode\":\"usb_and_stm32_spi\",\"tof\":"+sensorRangeJSON()+",\"cameras\":";
#else
   ",\"motor_control_enabled\":true,\"camera_mode\":\"one_stream_at_a_time\",\"tof\":"+sensorRangeJSON()+",\"cameras\":";
#endif
#ifdef TREE_SENSOR_USB
  cJSON *c=cameras_json();char *text=cJSON_PrintUnformatted(c);
  json+=text?text:"[]";free(text);cJSON_Delete(c);
#else
  json+="[]";
#endif
  json+=",\"usb\":";
#ifdef TREE_SENSOR_USB
  cJSON *u=usb_diagnostics_json();char *usbText=cJSON_PrintUnformatted(u);
  json+=usbText?usbText:"{}";free(usbText);cJSON_Delete(u);
#else
  json+="null";
#endif
#ifdef TREE_CAMERA_SPI
  if(cameraDiagSeen && millis()-cameraDiagAt<1000u) {
   json+=",\"stm32_camera_diag\":{\"spi_init\":";json+=(cameraBits&1)?"true":"false";
   json+=",\"ready_in\":";json+=(cameraBits&2)?"true":"false";
   json+=",\"usb_host_init\":";json+=(cameraBits&4)?"true":"false";
   json+=",\"usb_attached\":";json+=(cameraBits&8)?"true":"false";
   json+=",\"usb_state\":"+String((cameraBits>>4)&15)+",\"spi_attempts\":"+String(cameraAttempts)+
    ",\"spi_acks\":"+String(cameraAcks)+"}";
  }
#endif
  reply(200,json+"}");
 });
#ifdef TREE_SENSOR_USB
 server.on("/sensors",HTTP_GET,[](){server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",(const char*)sensor_page_start,sensor_page_end-sensor_page_start-1);});
 server.on("/api/camera/select",HTTP_POST,[](){
  if(otaLocked) {reply(423,"{\"error\":\"firmware maintenance lock\"}");return;}
  long id,format;
  if(!numberArg("id",0,1,id)||!numberArg("format",0,23,format)) {reply(400,"{\"error\":\"invalid camera or format\"}");return;}
  esp_err_t e=cameras_select((int)id,(int)format);
  reply(e==ESP_OK?202:400,String("{\"result\":\"")+esp_err_to_name(e)+"\"}");
 });
 server.on("/api/camera/stop",HTTP_POST,[](){
  esp_err_t e=cameras_stop();reply(e==ESP_OK?202:503,String("{\"result\":\"")+esp_err_to_name(e)+"\"}");
 });
#endif
}
static void sensorsStart() {
#ifdef TREE_SENSOR_USB
 cameras_init();sensorsStarted=true;
 httpd_config_t c=HTTPD_DEFAULT_CONFIG();c.server_port=81;c.ctrl_port=32769;c.stack_size=6144;
 c.lru_purge_enable=true;c.send_wait_timeout=2;c.recv_wait_timeout=2;
 httpd_handle_t h=nullptr;ESP_ERROR_CHECK(httpd_start(&h,&c));
 httpd_uri_t image={};image.uri="/camera.jpg";image.method=HTTP_GET;
 image.handler=[](httpd_req_t *r)->esp_err_t {
  char query[64],id[8];
  if(httpd_req_get_url_query_str(r,query,sizeof(query))!=ESP_OK || httpd_query_key_value(query,"id",id,sizeof(id))!=ESP_OK ||
     (strcmp(id,"0") && strcmp(id,"1"))) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"invalid camera id");
  return cameras_jpeg(r,id[0]-'0');
 };
 ESP_ERROR_CHECK(httpd_register_uri_handler(h,&image));
#endif
}
