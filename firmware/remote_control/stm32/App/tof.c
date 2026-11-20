/* VL6180X register sequence adapted from the supplier's Adafruit_VL6180X
 * example (ladyada / Adafruit Industries, BSD); see THIRD_PARTY_TOF.md.
 * Unlike readRange(), this port never waits indefinitely for a sensor.
 */
#include "stm32f4xx_hal.h"
#include "tof.h"
#include "robot_pinmap.h"
typedef struct {
 GPIO_TypeDef *port; uint16_t scl,sda;
 uint8_t phase,setting,dirty; uint32_t stamp,reported,poll;
 int16_t mm,status; uint16_t seq;
} sensor_t;
static sensor_t sensors[3];
static unsigned active;
static const uint16_t settings[][2] = {
 {0x0207,0x01},{0x0208,0x01},{0x0096,0x00},{0x0097,0xfd},{0x00e3,0x00},{0x00e4,0x04},{0x00e5,0x02},{0x00e6,0x01},{0x00e7,0x03},{0x00f5,0x02},{0x00d9,0x05},{0x00db,0xce},{0x00dc,0x03},{0x00dd,0xf8},{0x009f,0x00},{0x00a3,0x3c},{0x00b7,0x00},{0x00bb,0x3c},{0x00b2,0x09},{0x00ca,0x09},{0x0198,0x01},{0x01b0,0x17},{0x01ad,0x00},{0x00ff,0x05},{0x0100,0x05},{0x0199,0x05},{0x01a6,0x1b},{0x01ac,0x3e},{0x01a7,0x1f},{0x0030,0x00},{0x0011,0x10},{0x010a,0x30},{0x003f,0x46},{0x0031,0xFF},{0x0041,0x63},{0x002e,0x01},{0x003e,0x31},{0x0014,0x24},{0x001b,0x09},{0x0016,0x00}
};
static void delay_us(unsigned us) {
 uint32_t start=DWT->CYCCNT, cycles=(SystemCoreClock/1000000u)*us;
 while((uint32_t)(DWT->CYCCNT-start)<cycles) {}
}
static void sda(sensor_t *s,int high) {HAL_GPIO_WritePin(s->port,s->sda,high?GPIO_PIN_SET:GPIO_PIN_RESET);delay_us(4);}
static void low(sensor_t *s) {HAL_GPIO_WritePin(s->port,s->scl,GPIO_PIN_RESET);delay_us(4);}
static int high(sensor_t *s) {
 uint32_t t=DWT->CYCCNT;
 HAL_GPIO_WritePin(s->port,s->scl,GPIO_PIN_SET);
 while(HAL_GPIO_ReadPin(s->port,s->scl)==GPIO_PIN_RESET)
  if((uint32_t)(DWT->CYCCNT-t)>SystemCoreClock/10000u) return 0;
 delay_us(4);return 1;
}
static int start(sensor_t *s) {
 sda(s,1);if(!high(s)) return 0;
 if(!HAL_GPIO_ReadPin(s->port,s->sda)) return 0;
 sda(s,0);low(s);return 1;
}
static void stop(sensor_t *s) {low(s);sda(s,0);(void)high(s);sda(s,1);}
static int put(sensor_t *s,uint8_t b) {
 unsigned i;int ack;
 for(i=0;i<8;i++) {sda(s,(b&0x80)!=0);if(!high(s)) return 0;low(s);b<<=1;}
 sda(s,1);if(!high(s)) return 0;ack=!HAL_GPIO_ReadPin(s->port,s->sda);low(s);return ack;
}
static int get(sensor_t *s,uint8_t *v) {
 unsigned i;uint8_t b=0;sda(s,1);
 for(i=0;i<8;i++) {if(!high(s)) return 0;b=(uint8_t)((b<<1)|HAL_GPIO_ReadPin(s->port,s->sda));low(s);}
 sda(s,1);if(!high(s)) return 0;low(s);*v=b;return 1; /* NACK last byte */
}
static int write8(sensor_t *s,uint16_t reg,uint8_t val) {
 int ok=start(s)&&put(s,0x52)&&put(s,(uint8_t)(reg>>8))&&put(s,(uint8_t)reg)&&put(s,val);
 stop(s);return ok;
}
static int read8(sensor_t *s,uint16_t reg,uint8_t *v) {
 int ok=start(s)&&put(s,0x52)&&put(s,(uint8_t)(reg>>8))&&put(s,(uint8_t)reg);
 stop(s);if(ok) ok=start(s)&&put(s,0x53)&&get(s,v);stop(s);return ok;
}
static void result(sensor_t *s,uint32_t now,int16_t mm,int16_t status) {
 s->mm=mm;s->status=status;s->dirty=1;s->seq++;s->stamp=now;
}
static void failed(sensor_t *s,uint32_t now,int16_t status) {
 result(s,now,-1,status);s->phase=0;active=(active+1u)%3u;
}
void tof_init(void) {
 unsigned i;GPIO_InitTypeDef g={0};
 __HAL_RCC_GPIOC_CLK_ENABLE();__HAL_RCC_GPIOD_CLK_ENABLE();__HAL_RCC_GPIOF_CLK_ENABLE();
 CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
 DWT->CYCCNT=0;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
 sensors[0].port=ROBOT_TOF0_SCL_PORT;sensors[0].scl=ROBOT_TOF0_SCL_PIN;sensors[0].sda=ROBOT_TOF0_SDA_PIN;
 sensors[1].port=ROBOT_TOF1_SCL_PORT;sensors[1].scl=ROBOT_TOF1_SCL_PIN;sensors[1].sda=ROBOT_TOF1_SDA_PIN;
 sensors[2].port=ROBOT_TOF2_SCL_PORT;sensors[2].scl=ROBOT_TOF2_SCL_PIN;sensors[2].sda=ROBOT_TOF2_SDA_PIN;
 g.Mode=GPIO_MODE_OUTPUT_OD;g.Pull=GPIO_PULLUP;g.Speed=GPIO_SPEED_FREQ_MEDIUM;
 for(i=0;i<3;i++) {
  sensor_t *s=&sensors[i];g.Pin=s->scl|s->sda;
  HAL_GPIO_WritePin(s->port,g.Pin,GPIO_PIN_SET);HAL_GPIO_Init(s->port,&g);
  s->mm=-1;s->status=259;s->dirty=1;s->stamp=HAL_GetTick()-2000u;
 }
}
void tof_tick(uint32_t now) {
 sensor_t *s=&sensors[active];uint8_t value,status;
 if((uint32_t)(now-s->poll)<5u) return;s->poll=now;
 if(s->phase==0) {
  unsigned i;
  if((uint32_t)(now-s->stamp)<1000u) {active=(active+1u)%3u;return;}
  /* Recover SDA stuck by an interrupted byte. All clocks have bounded waits. */
  sda(s,1);for(i=0;i<9;i++) {low(s);if(!high(s)) break;}stop(s);
  if(!read8(s,0x0000,&value)) {failed(s,now,256);return;}
  if(value!=0xb4) {failed(s,now,258);return;}
  s->setting=0;s->phase=1;return;
 }
 if(s->phase==1) {
  if(!write8(s,settings[s->setting][0],(uint8_t)settings[s->setting][1])) {failed(s,now,256);return;}
  if(++s->setting==sizeof(settings)/sizeof(settings[0])) {s->phase=2;s->stamp=now;}
  return;
 }
 if(s->phase==2) {
  if(!read8(s,0x004d,&value)) {failed(s,now,256);return;}
  if(!(value&1)) {if((uint32_t)(now-s->stamp)>1000u) failed(s,now,257);return;}
  if(!write8(s,0x0015,7)||!write8(s,0x0018,1)) {failed(s,now,256);return;}
  s->stamp=now;s->phase=3;return;
 }
 if(!read8(s,0x004f,&value)) {failed(s,now,256);return;}
 if(!(value&4)) {if((uint32_t)(now-s->stamp)>100u) failed(s,now,257);return;}
 if(!read8(s,0x004d,&status)||!read8(s,0x0062,&value)||!write8(s,0x0015,7)) {failed(s,now,256);return;}
 status>>=4;
 result(s,now,status?-1:(value<20?-1:value),status?status:(value<20?260:0));
 s->phase=2;active=(active+1u)%3u;
}
int tof_report(uint32_t now,unsigned *index,int16_t *mm,int16_t *status,uint16_t *seq) {
 unsigned i;
 for(i=0;i<3;i++) {
  sensor_t *s=&sensors[i];
  if(s->dirty || (uint32_t)(now-s->reported)>1000u) {
   s->dirty=0;s->reported=now;*index=i;*mm=s->mm;*status=s->status;*seq=s->seq;return 1;
  }
 }
 return 0;
}
