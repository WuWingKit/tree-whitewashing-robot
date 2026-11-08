#include "rc_control.h"
#include <string.h>
static int sign(int v) {return (v>0)-(v<0);}
void rc_control_init(rc_control_t *c,uint32_t now) {
    memset(c,0,sizeof(*c));c->last_tick=now;c->last_drive=now;
    c->zero_since[0]=now;c->zero_since[1]=now;
}
void rc_control_stop(rc_control_t *c,uint32_t now) {
    unsigned i;c->armed=0;
    for(i=0;i<2;i++) {
        if(c->output[i]) c->zero_since[i]=now;
        c->target[i]=0;c->output[i]=0;
    }
}
void rc_control_estop(rc_control_t *c,int pressed,uint32_t now) {
    c->estop=(uint8_t)(pressed!=0);
    if(pressed) rc_control_stop(c,now);
}
int rc_control_command(rc_control_t *c,const rc_frame_t *f,uint32_t now) {
    /* A newly arrived drive packet must not revive an already expired session. */
    if(c->armed && (uint32_t)(now-c->last_drive)>=RC_TIMEOUT_MS) rc_control_stop(c,now);
    if(f->type==RC_STOP) {rc_control_stop(c,now);c->last_seq=f->seq;return 1;}
    if(f->left>RC_MAX_DUTY || f->left < -RC_MAX_DUTY ||
       f->right>RC_MAX_DUTY || f->right < -RC_MAX_DUTY) return 0;
    if(f->type==RC_ARM && !f->left && !f->right && !c->estop) {
        rc_control_stop(c,now);c->armed=1;c->last_drive=now;c->last_seq=f->seq;return 1;
    }
    if(f->type!=RC_DRIVE || !c->armed || c->estop) return 0;
    c->target[0]=f->left;c->target[1]=f->right;
    c->last_drive=now;c->last_seq=f->seq;
    /* Zero commands stop immediately but keep the session armed. */
    if(!f->left && !f->right) {
        unsigned i;for(i=0;i<2;i++) {
            if(c->output[i]) c->zero_since[i]=now;
            c->output[i]=0;
        }
    }
    return 1;
}
void rc_control_tick(rc_control_t *c,uint32_t now) {
    unsigned i;
    if(!c->armed || c->estop || (uint32_t)(now-c->last_drive)>=RC_TIMEOUT_MS) {
        rc_control_stop(c,now);return;
    }
    if((uint32_t)(now-c->last_tick)<10u) return;
    c->last_tick=now; /* No catch-up burst after a delayed main loop. */
    for(i=0;i<2;i++) {
        int goal=c->target[i],old=c->output[i],next;
        if(old && goal && sign(old)!=sign(goal)) goal=0;
        if(!old && goal && c->last_sign[i] && sign(goal)!=c->last_sign[i] &&
           (uint32_t)(now-c->zero_since[i])<RC_REVERSE_DEAD_MS) continue;
        next=old;
        if(next<goal) {next+=10;if(next>goal) next=goal;}
        if(next>goal) {next-=10;if(next<goal) next=goal;}
        if(!next && old) c->zero_since[i]=now;
        if(next) c->last_sign[i]=(int8_t)sign(next);
        c->output[i]=(int16_t)next;
    }
}
