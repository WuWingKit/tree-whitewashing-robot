#ifndef RC_CONTROL_H
#define RC_CONTROL_H
#include "rc_protocol.h"
#define RC_MAX_DUTY 350 /* permille: test cap 35%, not a current limit */
#define RC_TIMEOUT_MS 300u
#define RC_REVERSE_DEAD_MS 100u
typedef struct {
    int16_t target[2], output[2];
    int8_t last_sign[2];
    uint32_t zero_since[2], last_drive, last_tick;
    uint8_t armed, estop;
    uint16_t last_seq;
} rc_control_t;
void rc_control_init(rc_control_t *c,uint32_t now);
void rc_control_stop(rc_control_t *c,uint32_t now);
void rc_control_estop(rc_control_t *c,int pressed,uint32_t now);
int rc_control_command(rc_control_t *c,const rc_frame_t *f,uint32_t now);
void rc_control_tick(rc_control_t *c,uint32_t now);
#endif
