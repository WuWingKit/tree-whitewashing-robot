/* Adapted from local esp32s3_wifi.ino's WiFi/Serial setup.
 * Adds SoftAP, browser controls, leased commands and a CRC-protected UART.
 * Select ESP32S3 Dev Module; use the board's USB-UART connector to upload. */
#include <WiFi.h>
#include <WebServer.h>
#include <esp_system.h>
#include <errno.h>
#include "src/rc_protocol.h"
#include "web_ui.h"

static const char *AP_SSID="TreeRobotTest";
static const char *AP_PASS="TreeTest2026"; // Demo-only AP; change before shared use.
static const uint32_t PC_LEASE_MS=250, MCU_LEASE_MS=350;
static const int MAX_DUTY=350;
static WebServer server(80);
static HardwareSerial stm(1);
static rc_parser_t parser={};
static uint16_t txSeq=0;
static uint32_t token=0,lastPC=0,lastMCU=0,lastRelay=0,lastWebSeq=0,armedAt=0;
static bool seenMCU=false,mcuArmed=false,mcuEstop=false,awaitingArm=false;
static int16_t leftDuty=0,rightDuty=0,mcuLeft=0,mcuRight=0;
static void receiveSTM();

static void sendFrame(uint8_t type,int16_t l=0,int16_t r=0) {
  rc_frame_t f={type,++txSeq,l,r};uint8_t bytes[RC_FRAME_SIZE];
  rc_encode(bytes,&f);stm.write(bytes,sizeof(bytes));
}
static void stopAll() {
  token=0;awaitingArm=false;leftDuty=rightDuty=0;sendFrame(RC_STOP);
}
static bool mcuLive() {return seenMCU && (uint32_t)(millis()-lastMCU)<MCU_LEASE_MS;}
static void reply(int code,const String &body) {
  server.sendHeader("Cache-Control","no-store");server.send(code,"application/json",body);
}
static bool numberArg(const char *key,long min,long max,long &out) {
  if(!server.hasArg(key)) return false;
  String s=server.arg(key);if(!s.length() || s.length()>10) return false;
  char *end=nullptr;errno=0;out=strtol(s.c_str(),&end,10);
  return errno!=ERANGE && end!=s.c_str() && *end=='\0' && out>=min && out<=max;
}
#include "ota_support.h"
static void handleArm() {
  if(otaLocked) {reply(423,"{\"error\":\"firmware maintenance lock\"}");return;}
  uint32_t now=millis();
  if(token && (uint32_t)(now-lastPC)<PC_LEASE_MS) {reply(409,"{\"error\":\"controller busy\"}");return;}
  stopAll();
  if(!mcuLive() || mcuEstop) {reply(503,"{\"error\":\"STM32 offline or stop pressed\"}");return;}
  do {token=esp_random()&0x7fffffffu;} while(!token);
  lastPC=now;armedAt=now;lastWebSeq=0;awaitingArm=true;
  sendFrame(RC_ARM);reply(200,"{\"token\":"+String(token)+"}");
}
static void handleDrive() {
  if(otaLocked) {reply(423,"{\"error\":\"firmware maintenance lock\"}");return;}
  long t,s,l,r;
  if(!numberArg("token",1,2147483647L,t) || !numberArg("seq",1,2147483647L,s) ||
     !numberArg("left",-MAX_DUTY,MAX_DUTY,l) || !numberArg("right",-MAX_DUTY,MAX_DUTY,r)) {
    reply(400,"{\"error\":\"invalid command\"}");return;
  }
  uint32_t now=millis();
  if(!token || (uint32_t)t!=token || (uint32_t)s<=lastWebSeq) {
    reply(409,"{\"error\":\"expired session or old sequence\"}");return;
  }
  if((uint32_t)(now-lastPC)>=PC_LEASE_MS || !mcuLive() || mcuEstop ||
     (!mcuArmed && !awaitingArm) || (awaitingArm && (l||r))) {
    stopAll();reply(409,"{\"error\":\"not armed; release controls and arm again\"}");return;
  }
  lastPC=now;lastWebSeq=(uint32_t)s;leftDuty=(int16_t)l;rightDuty=(int16_t)r;
  sendFrame(RC_DRIVE,leftDuty,rightDuty);
  reply(200,"{\"accepted\":true}"); // UART forwarding, not physical-motion acknowledgement.
}
static void handleStatus() {
  reply(200,String("{\"online\":")+(mcuLive()?"true":"false")+
    ",\"armed\":"+(mcuArmed?"true":"false")+",\"estop\":"+(mcuEstop?"true":"false")+
    ",\"owner\":"+(token?"true":"false")+",\"left\":"+String(mcuLeft)+
    ",\"right\":"+String(mcuRight)+",\"version\":\""+FW_VERSION+
    "\",\"maintenance\":"+(otaLocked?"true":"false")+"}");
}
void setup() {
  Serial.begin(115200);
  stm.begin(115200,SERIAL_8N1,18,17); // S3 RX18 <- F407 PA2; TX17 -> PA3.
  sendFrame(RC_STOP);WiFi.mode(WIFI_AP);
  if(!WiFi.softAP(AP_SSID,AP_PASS,1,false,2)) {while(true) {sendFrame(RC_STOP);delay(100);}}
  server.on("/",HTTP_GET,[](){server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",WEB_UI);});
  server.on("/api/arm",HTTP_POST,handleArm);
  server.on("/api/drive",HTTP_POST,handleDrive);
  /* Any AP client may stop; stopping never requires an ownership token. */
  server.on("/api/stop",HTTP_POST,[](){stopAll();reply(200,"{\"stopped\":true}");});
  server.on("/api/status",HTTP_GET,handleStatus);
  otaRoutes();
  server.onNotFound([](){reply(404,"{\"error\":\"not found\"}");});
  server.begin();Serial.print("Open http://");Serial.println(WiFi.softAPIP());
  Serial.printf("Firmware %s; %s; slot=0x%lx\n",FW_VERSION,RC_OTA_MARKER,
    (unsigned long)esp_ota_get_running_partition()->address);
}
static void receiveSTM() {
  unsigned budget=128;rc_frame_t f;
  while(stm.available() && budget--) {
    if(rc_feed(&parser,(uint8_t)stm.read(),&f) &&
       (f.type==RC_STATUS_DISARMED || f.type==RC_STATUS_ARMED || f.type==RC_STATUS_ESTOP)) {
      seenMCU=true;lastMCU=millis();mcuArmed=f.type==RC_STATUS_ARMED;
      mcuEstop=f.type==RC_STATUS_ESTOP;mcuLeft=f.left;mcuRight=f.right;
      if(mcuArmed) awaitingArm=false;
    }
  }
}
void loop() {
  receiveSTM();otaService();
  uint32_t now=millis();
  if(token && ((uint32_t)(now-lastPC)>=PC_LEASE_MS || !mcuLive() || mcuEstop ||
     (!mcuArmed && !awaitingArm) || (awaitingArm && (uint32_t)(now-armedAt)>250u))) stopAll();
  if((uint32_t)(now-lastRelay)>=50u) {
    lastRelay=now;
    if(token && !awaitingArm) sendFrame(RC_DRIVE,leftDuty,rightDuty);
    else if(!token) sendFrame(RC_STOP);
  }
  server.handleClient();
  static uint32_t lastLog=0;static bool bootValidated=false;
  if(!bootValidated && now>2500u) {
    esp_ota_img_states_t state;
    const esp_partition_t *running=esp_ota_get_running_partition();
    if(esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY) {
      if(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK) Serial.println("OTA boot self-check accepted");
    }
    bootValidated=true;
  }
  if((uint32_t)(now-lastLog)>2000u) {
    lastLog=now;
    Serial.printf("STATUS fw=%s online=%u armed=%u estop=%u L=%d R=%d ota_lock=%u slot=0x%lx\n",
      FW_VERSION,mcuLive(),mcuArmed,mcuEstop,mcuLeft,mcuRight,otaLocked,
      (unsigned long)esp_ota_get_running_partition()->address);
  }
  delay(1);
}
