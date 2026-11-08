#include "rc_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static rc_frame_t frame(uint8_t type,int left,int right) {
    rc_frame_t f={0};f.type=type;f.seq=42;f.left=(int16_t)left;f.right=(int16_t)right;return f;
}
static void arm(rc_control_t *c,uint32_t now) {
    rc_frame_t f=frame(RC_ARM,0,0);assert(rc_control_command(c,&f,now));assert(c->armed);
}
static void protocol_tests(void) {
    uint8_t bytes[RC_FRAME_SIZE];rc_parser_t p={0};rc_frame_t got;
    rc_frame_t f=frame(RC_DRIVE,-350,350);unsigned i,j;int count=0;
    assert(rc_crc16((const uint8_t *)"123456789",9)==0x29b1);
    rc_encode(bytes,&f);
    for(i=0;i<RC_FRAME_SIZE;i++) count+=rc_feed(&p,bytes[i],&got);
    assert(count==1 && got.left==-350 && got.right==350 && got.seq==42);
    /* Every single-bit corruption must fail CRC or version/header checks. */
    for(i=0;i<RC_FRAME_SIZE;i++) for(j=0;j<8;j++) {
        uint8_t broken[RC_FRAME_SIZE];memcpy(broken,bytes,sizeof(bytes));broken[i]^=(uint8_t)(1u<<j);
        memset(&p,0,sizeof(p));count=0;
        for(unsigned k=0;k<RC_FRAME_SIZE;k++)count+=rc_feed(&p,broken[k],&got);
        assert(!count);
        for(unsigned k=0;k<RC_FRAME_SIZE;k++)count+=rc_feed(&p,bytes[k],&got);
        assert(count==1);
    }
    memset(&p,0,sizeof(p));
    for(i=0;i<10000;i++) {rc_feed(&p,(uint8_t)(i*37u),&got);assert(p.used<RC_FRAME_SIZE);}
    count=0;for(i=0;i<RC_FRAME_SIZE;i++)count+=rc_feed(&p,bytes[i],&got);assert(count==1);
    puts("PASS protocol: CRC known vector, signed values, 96 corruptions, recovery, noise");
}
static void control_tests(void) {
    rc_control_t c;rc_frame_t f=frame(RC_DRIVE,200,200);
    rc_control_init(&c,0);assert(!rc_control_command(&c,&f,0));assert(!c.armed);
    arm(&c,0);assert(rc_control_command(&c,&f,0));
    for(unsigned t=10;t<=100;t+=10)rc_control_tick(&c,t);
    assert(c.output[0]==100 && c.output[1]==100);
    rc_control_tick(&c,300);assert(!c.armed && !c.output[0]);
    assert(!rc_control_command(&c,&f,310));
    arm(&c,400);f.left=351;assert(!rc_control_command(&c,&f,410));
    assert(c.last_drive==400);rc_control_tick(&c,700);assert(!c.armed);
    arm(&c,800);f=frame(RC_DRIVE,200,200);assert(rc_control_command(&c,&f,800));
    assert(!rc_control_command(&c,&f,1100));assert(!c.armed); /* timeout before tick */
    arm(&c,1200);f=frame(RC_DRIVE,200,-200);rc_control_command(&c,&f,1200);
    rc_control_tick(&c,1210);assert(c.output[0]>0 && c.output[1]<0);
    rc_control_estop(&c,1,1211);assert(!c.armed && !c.output[0] && !c.output[1]);
    f=frame(RC_ARM,0,0);assert(!rc_control_command(&c,&f,1212));
    rc_control_estop(&c,0,1213);assert(!c.armed);arm(&c,1214);
    f=frame(RC_DRIVE,0,0);assert(rc_control_command(&c,&f,1215));assert(c.armed);
    f=frame(RC_STOP,0,0);assert(rc_control_command(&c,&f,1216));assert(!c.armed);
    /* millis wraparound */
    rc_control_init(&c,0xffffff80u);arm(&c,0xffffff80u);
    f=frame(RC_DRIVE,200,200);rc_control_command(&c,&f,0xffffff80u);
    rc_control_tick(&c,0xffffff90u);assert(c.output[0]>0);
    rc_control_tick(&c,0x000000acu);assert(!c.armed && !c.output[0]);
    puts("PASS control: arm, range, ramp, timeout, stale drive, stop, emergency, clock wrap");
}
static void reverse_tests(void) {
    rc_control_t c;rc_frame_t f;unsigned t;int last=0;
    rc_control_init(&c,0);arm(&c,0);f=frame(RC_DRIVE,100,100);rc_control_command(&c,&f,0);
    for(t=10;t<=100;t+=10)rc_control_tick(&c,t);
    assert(c.output[0]==100);
    f=frame(RC_DRIVE,-100,-100);rc_control_command(&c,&f,100);
    for(t=110;t<300;t+=10) {
        rc_control_tick(&c,t);assert(c.output[0]>=0);last=c.output[0];
    }
    assert(last==0);
    rc_control_tick(&c,300);assert(c.output[0]==-10);
    f=frame(RC_DRIVE,0,0);rc_control_command(&c,&f,301);assert(c.output[0]==0);
    f=frame(RC_DRIVE,100,100);rc_control_command(&c,&f,302);
    rc_control_tick(&c,310);assert(c.output[0]==0);
    rc_control_tick(&c,401);assert(c.output[0]==10);
    puts("PASS reverse: ramp to zero, 100ms dwell, immediate zero then reversed start");
}
int main(void) {protocol_tests();control_tests();reverse_tests();puts("All core tests passed.");return 0;}
