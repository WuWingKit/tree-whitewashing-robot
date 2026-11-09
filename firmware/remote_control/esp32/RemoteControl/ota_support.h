#pragma once
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include "ota_config.h"
#include "src/rc_ota_image.h"
#include "ota_ui.h"

static const char *FW_VERSION="0.2.0-ota";
static bool otaLocked=false,otaReady=false,otaWriting=false,otaSucceeded=false;
static bool otaStandalone=false,otaAcceptedUpload=false,otaHeaderChecked=false;
static bool otaPayloadComplete=false,otaFileSeen=false;
static uint32_t otaPreparedAt=0,otaLastActivity=0,otaRestartAt=0;
static size_t otaExpected=0,otaWritten=0;
static String otaToken,otaSHA,otaError;
static rc_ota_marker_t otaMarker={};
static mbedtls_sha256_context otaHash;

static bool otaAuthenticate() {return server.authenticate(OTA_USERNAME,OTA_PASSWORD);}
static void otaChallenge() {server.requestAuthentication(BASIC_AUTH,"Tree robot firmware update");}
static void otaFail(const String &error) {
  if(Update.isRunning()) Update.abort();
  otaWriting=false;otaReady=false;otaSucceeded=false;otaError=error;
  // Stay locked until an explicit cancel or a new authenticated prepare.
  stopAll();Serial.println("OTA rejected: "+error);
}
static void otaCancel() {
  if(Update.isRunning()) Update.abort();
  otaLocked=false;otaReady=false;otaWriting=false;otaSucceeded=false;
  otaAcceptedUpload=false;otaToken="";otaError="";stopAll();
  otaPayloadComplete=false;otaFileSeen=false;
}
static void otaPrepare() {
  if(!otaAuthenticate()) {otaChallenge();return;}
  if(otaWriting || otaSucceeded) {reply(409,"{\"error\":\"update already active\"}");return;}
  long size;
  String sha=server.arg("sha256");sha.toLowerCase();
  bool good=sha.length()==64;
  for(size_t i=0;i<sha.length();i++) if(!isxdigit((unsigned char)sha[i])) good=false;
  const esp_partition_t *next=esp_ota_get_next_update_partition(nullptr);
  if(server.arg("target")!="tree-robot-esp32s3" || !next || !good ||
     !numberArg("size",4096,next?next->size:4096,size)) {
    reply(400,"{\"error\":\"invalid target, size, SHA256 or OTA partition\"}");return;
  }
  otaCancel();otaLocked=true;otaStandalone=server.arg("standalone")=="1";
  otaExpected=(size_t)size;otaSHA=sha;otaWritten=0;otaMarker={};
  otaPreparedAt=millis();otaLastActivity=millis();
  char session[33];snprintf(session,sizeof(session),"%08lx%08lx%08lx%08lx",
    (unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
  otaToken=session;
  reply(200,"{\"token\":\""+otaToken+"\",\"ready\":false}");
}
static void otaStatus() {
  if(!otaAuthenticate()) {otaChallenge();return;}
  const esp_partition_t *running=esp_ota_get_running_partition();
  const esp_partition_t *next=esp_ota_get_next_update_partition(nullptr);
  esp_ota_img_states_t state;int bootState=-1;
  if(esp_ota_get_state_partition(running,&state)==ESP_OK) bootState=(int)state;
  reply(200,String("{\"version\":\"")+FW_VERSION+"\",\"locked\":"+(otaLocked?"true":"false")+
    ",\"ready\":"+(otaReady?"true":"false")+",\"writing\":"+(otaWriting?"true":"false")+
    ",\"bytes\":"+String(otaWritten)+",\"expected\":"+String(otaExpected)+
    ",\"slot\":"+String(running?running->address:0)+",\"boot_state\":"+String(bootState)+",\"uptime_ms\":"+String(millis())+",\"max_bytes\":"+String(next?next->size:0)+
    ",\"online\":"+(mcuLive()?"true":"false")+",\"error\":\""+otaError+"\"}");
}
static void otaUploadChunk() {
  HTTPUpload &up=server.upload();
  if(up.status==UPLOAD_FILE_START) {
    if(otaFileSeen && otaWriting && otaAuthenticate() && server.header("X-OTA-Token")==otaToken) {
      otaFail("exactly one firmware file required");return;
    }
    otaAcceptedUpload=false;
    if(!otaAuthenticate()) return;
    if(!otaLocked || !otaReady || otaWriting || otaSucceeded ||
       server.header("X-OTA-Token")!=otaToken || !otaToken.length() ||
       (uint32_t)(millis()-otaLastActivity)>60000u) return;
    otaAcceptedUpload=true;otaReady=false;otaWriting=true;otaHeaderChecked=false;otaFileSeen=true;
    otaWritten=0;otaMarker={};otaError="";otaLastActivity=millis();stopAll();
    mbedtls_sha256_free(&otaHash);mbedtls_sha256_init(&otaHash);
    if(mbedtls_sha256_starts_ret(&otaHash,0)!=0 || !Update.begin(otaExpected,U_FLASH)) otaFail("cannot begin OTA");
  } else if(up.status==UPLOAD_FILE_WRITE) {
    if(!otaAcceptedUpload || !otaWriting || otaError.length()) return;
    otaLastActivity=millis();sendFrame(RC_STOP);receiveSTM();
    if(!otaHeaderChecked) {
      if(!rc_ota_header_valid(up.buf,up.currentSize)) {otaFail("not an ESP32-S3 application image");return;}
      otaHeaderChecked=true;
    }
    if(up.currentSize>otaExpected-otaWritten) {otaFail("image exceeds declared length");return;}
    if(mbedtls_sha256_update_ret(&otaHash,up.buf,up.currentSize)!=0) {otaFail("SHA256 failure");return;}
    rc_ota_scan_marker(&otaMarker,up.buf,up.currentSize);
    if(Update.write(up.buf,up.currentSize)!=up.currentSize) {otaFail("flash write failure");return;}
    otaWritten+=up.currentSize;
  } else if(up.status==UPLOAD_FILE_END) {
    if(!otaAcceptedUpload || !otaWriting || otaError.length()) return;
    if(otaWritten!=otaExpected || up.totalSize!=otaExpected || !otaHeaderChecked || !otaMarker.seen) {
      otaFail("wrong length or incompatible project image");return;
    }
    uint8_t digest[32];char hex[65];
    if(mbedtls_sha256_finish_ret(&otaHash,digest)!=0) {otaFail("SHA256 failure");return;}
    for(unsigned i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",digest[i]);
    if(otaSHA!=String(hex)) {otaFail("SHA256 mismatch");return;}
    otaPayloadComplete=true;
  } else if(up.status==UPLOAD_FILE_ABORTED && otaAcceptedUpload) otaFail("upload interrupted");
}
static void otaUploadResult() {
  if(!otaAuthenticate()) {otaChallenge();return;}
  if(!otaAcceptedUpload) {reply(409,"{\"error\":\"OTA session not ready or invalid token\"}");return;}
  if(otaPayloadComplete && otaWriting && !otaError.length()) {
    if(!Update.end(false)) otaFail("image validation or activation failure");
    else {otaWriting=false;otaSucceeded=true;otaToken="";stopAll();
      Serial.printf("OTA verified %u bytes; reboot pending\n",(unsigned)otaWritten);}
  }
  if(!otaSucceeded) {reply(400,"{\"error\":\""+(otaError.length()?otaError:String("incomplete upload"))+"\"}");return;}
  reply(200,"{\"verified\":true,\"rebooting\":true}");otaRestartAt=millis();
}
static void otaService() {
  uint32_t now=millis();
  if(otaSucceeded && otaRestartAt && (uint32_t)(now-otaRestartAt)>800u) {stopAll();ESP.restart();}
  if(!otaLocked || otaSucceeded) return;
  if((uint32_t)(now-otaLastActivity)>60000u && !otaError.length()) {otaFail("OTA session expired");return;}
  if(otaWriting || otaError.length() || otaReady) return;
  if((uint32_t)(now-otaPreparedAt)>=500u) {
    bool stopped=mcuLive() && !mcuArmed && !mcuLeft && !mcuRight;
    if(stopped || (otaStandalone && !mcuLive())) {otaReady=true;return;}
    if((uint32_t)(now-otaPreparedAt)>3000u) otaFail("cannot confirm STM32 stopped");
  }
}
static void otaRoutes() {
  mbedtls_sha256_init(&otaHash);
  const char *headers[]={"X-OTA-Token"};server.collectHeaders(headers,1);
  server.on("/update",HTTP_GET,[](){
    if(!otaAuthenticate()) {otaChallenge();return;}
    server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",OTA_UI);
  });
  server.on("/api/ota/prepare",HTTP_POST,otaPrepare);
  server.on("/api/ota/status",HTTP_GET,otaStatus);
  server.on("/api/ota/upload",HTTP_POST,otaUploadResult,otaUploadChunk);
  server.on("/api/ota/cancel",HTTP_POST,[](){
    if(!otaAuthenticate()) {otaChallenge();return;}
    if(server.header("X-OTA-Token")!=otaToken || !otaToken.length() || otaSucceeded) {
      reply(409,"{\"error\":\"invalid session\"}");return;
    }
    otaCancel();reply(200,"{\"cancelled\":true}");
  });
}
