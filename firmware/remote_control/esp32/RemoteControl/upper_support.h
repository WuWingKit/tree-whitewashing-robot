#pragma once
/* Same browser lease and STOP/OTA interlock as the existing chassis controls.
 * Payload is applied by STM32; ESP32 never drives the IRF module directly. */
static bool upperLive() {
  return ROBOT_HAS_UPPER_ACTUATORS && mcuLive() && seenUpper &&
    (uint32_t)(millis()-lastUpperMCU)<MCU_LEASE_MS;
}
static bool upperMaintenanceReady() {
#if ROBOT_HAS_UPPER_ACTUATORS
  return upperLive() && !mcuUpperMask && (int32_t)(lastUpperMCU-otaPreparedAt)>=0;
#else
  return true;
#endif
}
static void sendUpper() {
#if ROBOT_HAS_UPPER_ACTUATORS
  sendFrame(RC_UPPER,upperMask,0);
#endif
}
static String upperJSON() {
  bool live=upperLive();
  return String("{\"available\":")+(live?"true":"false")+
    ",\"active_high\":true,\"requested_mask\":"+String(upperMask)+
    ",\"reported_mask\":"+(live?String(mcuUpperMask):String("null"))+
    ",\"mixer_requested\":"+((upperMask&RC_UPPER_MIXER)?"true":"false")+
    ",\"pump_requested\":"+((upperMask&RC_UPPER_PUMP)?"true":"false")+
    ",\"mixer_output\":"+(live?String((mcuUpperMask&RC_UPPER_MIXER)?"true":"false"):String("null"))+
    ",\"pump_output\":"+(live?String((mcuUpperMask&RC_UPPER_PUMP)?"true":"false"):String("null"))+
    ",\"report_seq\":"+String(mcuUpperSeq)+
    ",\"report_age_ms\":"+(seenUpper?String(millis()-lastUpperMCU):String("null"))+
    ",\"feedback\":\"MCU GPIO command only; no current or motion sensor\"}";
}
static bool readUpperRequest(uint8_t &mask) {
  /* Old drive pages omit both arguments: always clear upper outputs. */
  mask=0;
  if(!server.hasArg("mixer") && !server.hasArg("pump")) return true;
  long mixer,pump;
  if(!numberArg("mixer",0,1,mixer) || !numberArg("pump",0,1,pump)) {
    reply(400,"{\"error\":\"mixer and pump must both be 0 or 1\"}");return false;
  }
  mask=(uint8_t)((mixer?RC_UPPER_MIXER:0u)|(pump?RC_UPPER_PUMP:0u));
  return true;
}
static void handleUpper() {
  if(otaLocked) {reply(423,"{\"error\":\"firmware maintenance lock\"}");return;}
  long t,s;uint8_t mask=0;
  if(!numberArg("token",1,2147483647L,t) || !numberArg("seq",1,2147483647L,s) ||
     !server.hasArg("mixer") || !server.hasArg("pump")) {
    reply(400,"{\"error\":\"token, seq, mixer and pump required\"}");return;
  }
  if(!readUpperRequest(mask)) return;
  if(!token || (uint32_t)t!=token || (uint32_t)s<=lastWebSeq) {
    reply(409,"{\"error\":\"expired session or old sequence\"}");return;
  }
  if((uint32_t)(millis()-lastPC)>=PC_LEASE_MS || !mcuLive() || mcuEstop ||
     !mcuArmed || awaitingArm || !upperLive()) {
    stopAll();reply(409,"{\"error\":\"arm and connect compatible STM32 firmware first\"}");return;
  }
  lastPC=millis();lastWebSeq=(uint32_t)s;upperMask=mask;
  sendFrame(RC_DRIVE,leftDuty,rightDuty);sendUpper();
  reply(202,String("{\"accepted\":true,\"requested_mask\":")+String(upperMask)+"}");
}
