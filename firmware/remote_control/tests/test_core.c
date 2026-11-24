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
static void sensor_frame_tests(void) {
    rc_control_t c;rc_parser_t parser={0};rc_frame_t in,out;
    uint8_t bytes[RC_FRAME_SIZE];unsigned id,i;
    rc_control_init(&c,0);arm(&c,0);
    for(id=0;id<3;id++) {
        in.type=(uint8_t)(0x90u+id);in.seq=65535;
        in.left=id==0?42:-1;in.right=id==0?0:256;
        rc_encode(bytes,&in);
        for(i=0;i<RC_FRAME_SIZE;i++) {
            int ready=rc_feed(&parser,bytes[i],&out);
            assert(ready==(i==RC_FRAME_SIZE-1));
        }
        assert(out.type==in.type && out.seq==in.seq && out.left==in.left && out.right==in.right);
        assert(!rc_control_command(&c,&out,100));
        assert(c.armed && !c.output[0] && !c.output[1] && c.last_drive==0);
    }
    rc_control_tick(&c,300);assert(!c.armed);
    puts("PASS sensors: three IDs, valid/invalid CRC frames, telemetry cannot command motors or refresh drive lease");
}
static void upper_tests(void) {
    rc_control_t c;rc_frame_t f;unsigned mask;
    rc_control_init(&c,0);assert(!c.upper_output);
    f=frame(RC_UPPER,3,0);assert(!rc_control_command(&c,&f,0));
    arm(&c,0);
    for(mask=0;mask<=3;mask++) {
        f=frame(RC_UPPER,(int)mask,0);f.seq=(uint16_t)(100+mask);
        assert(rc_control_command(&c,&f,10+mask));assert(c.upper_output==mask);
    }
    assert(!rc_control_command(&c,&f,20));assert(c.last_upper==13); /* duplicate */
    f.seq=90;assert(!rc_control_command(&c,&f,21)); /* out of order */
    f=frame(RC_UPPER,4,0);assert(!rc_control_command(&c,&f,22));
    f=frame(RC_UPPER,-1,0);assert(!rc_control_command(&c,&f,23));
    f=frame(RC_UPPER,1,1);assert(!rc_control_command(&c,&f,24));
    /* STOP applies to pumps and chassis. ARM never restores a previous mask. */
    f=frame(RC_STOP,0,0);assert(rc_control_command(&c,&f,25));assert(!c.upper_output);
    arm(&c,30);assert(!c.upper_output);
    f=frame(RC_UPPER,3,0);assert(rc_control_command(&c,&f,31));
    rc_control_estop(&c,1,32);assert(!c.upper_output && !c.armed);
    assert(!rc_control_command(&c,&f,33));rc_control_estop(&c,0,34);
    /* Drive heartbeats alone cannot keep an old pump command alive. */
    arm(&c,1000);f=frame(RC_UPPER,3,0);assert(rc_control_command(&c,&f,1001));
    f=frame(RC_DRIVE,0,0);assert(rc_control_command(&c,&f,1200));
    rc_control_tick(&c,1301);assert(!c.upper_output && !c.armed);
    f=frame(RC_UPPER,3,0);f.seq=43;assert(!rc_control_command(&c,&f,1302));
    /* The inverse: upper packets do not renew the drive/session watchdog. */
    arm(&c,2000);f=frame(RC_UPPER,3,0);assert(rc_control_command(&c,&f,2200));
    f.seq++;assert(!rc_control_command(&c,&f,2300));assert(!c.upper_output);
    /* Reject a late upper heartbeat even before the regular tick runs. */
    arm(&c,3000);f=frame(RC_UPPER,1,0);assert(rc_control_command(&c,&f,3001));
    f=frame(RC_DRIVE,0,0);assert(rc_control_command(&c,&f,3200));
    f=frame(RC_UPPER,2,0);f.seq=43;assert(!rc_control_command(&c,&f,3301));
    assert(!c.armed && !c.upper_output);
    /* UART sequence rollover and millisecond rollover remain valid. */
    rc_control_init(&c,0xffffff80u);arm(&c,0xffffff80u);
    f=frame(RC_UPPER,1,0);f.seq=65535;assert(rc_control_command(&c,&f,0xffffff81u));
    f.seq=0;f.left=2;assert(rc_control_command(&c,&f,0xffffff82u));
    rc_control_tick(&c,0xacu);assert(!c.upper_output && !c.armed);
    /* A dedicated CH2 stop keeps the session alive for a subsequent pump start. */
    rc_control_init(&c,4000);arm(&c,4000);
    f=frame(RC_UPPER,RC_UPPER_PUMP,0);f.seq=200;
    assert(rc_control_command(&c,&f,4001) && c.upper_output==RC_UPPER_PUMP);
    f=frame(RC_UPPER,0,0);f.seq=201;
    assert(rc_control_command(&c,&f,4050) && !c.upper_output && c.armed);
    f=frame(RC_UPPER,RC_UPPER_PUMP,0);f.seq=202;
    assert(rc_control_command(&c,&f,4100) && c.upper_output==RC_UPPER_PUMP && c.armed);
    puts("PASS upper: CH1/CH2 masks, pump on/off/on, command order, safety leases, stop and rollover");
}
int main(void) {protocol_tests();control_tests();reverse_tests();sensor_frame_tests();upper_tests();puts("All core tests passed.");return 0;}
