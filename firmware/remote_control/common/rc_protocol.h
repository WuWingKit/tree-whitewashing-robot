#ifndef RC_PROTOCOL_H
#define RC_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RC_FRAME_SIZE 12u
#define RC_VERSION 1u
enum { RC_DRIVE=1, RC_STOP=2, RC_ARM=3, RC_UPPER=4,
       RC_STATUS_DISARMED=0x80, RC_STATUS_ARMED=0x81, RC_STATUS_ESTOP=0x82,
       RC_STATUS_UPPER=0x83 };
/* RC_UPPER: left=CH1/CH2 bit mask, right=0. Reserved CH3/4 stay LOW.
 * RC_STATUS_UPPER: left=applied GPIO mask, right=1 (active-high protocol v1).
 * A report confirms MCU output commands, not motor current or motion. */
#define RC_UPPER_MIXER 1u
#define RC_UPPER_PUMP 2u
#define RC_UPPER_MASK 3u
#define RC_UPPER_STATUS_TAG 1
typedef struct { uint8_t type; uint16_t seq; int16_t left, right; } rc_frame_t;
typedef struct { uint8_t bytes[RC_FRAME_SIZE]; uint8_t used; } rc_parser_t;
uint16_t rc_crc16(const uint8_t *data, size_t size);
void rc_encode(uint8_t out[RC_FRAME_SIZE], const rc_frame_t *frame);
int rc_feed(rc_parser_t *parser, uint8_t byte, rc_frame_t *frame);
#ifdef __cplusplus
}
#endif
#endif
