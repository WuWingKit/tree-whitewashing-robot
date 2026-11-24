#ifndef TREE_CAMERA_LINK_H
#define TREE_CAMERA_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Camera SPI is independent of the motion/ToF UART. Fixed-size mode-0, MSB
 * first exchanges carry one data packet and one command in full duplex. */
#define CAMERA_LINK_PACKET 256u
#define CAMERA_LINK_PAYLOAD 240u
#define CAMERA_LINK_MAX_JPEG (60u * 1024u)
enum { CAMERA_LINK_STATUS = 1, CAMERA_LINK_FRAME = 2, CAMERA_LINK_COMMAND = 3 };
enum { CAMERA_LINK_LAST = 1 };
typedef enum {
    CAMERA_USB_WAITING, CAMERA_USB_ENUMERATING, CAMERA_USB_READY,
    CAMERA_USB_STREAMING, CAMERA_USB_UNSUPPORTED, CAMERA_USB_ERROR
} camera_usb_state_t;

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint16_t frame;
    uint32_t offset;
    uint16_t length;
    const uint8_t *data;
} camera_packet_t;

void camera_link_encode(uint8_t out[CAMERA_LINK_PACKET], const camera_packet_t *packet);
bool camera_link_decode(const uint8_t in[CAMERA_LINK_PACKET], camera_packet_t *packet);
uint16_t camera_read_u16(const uint8_t *p);
uint32_t camera_read_u32(const uint8_t *p);
void camera_write_u16(uint8_t *p, uint16_t value);
void camera_write_u32(uint8_t *p, uint32_t value);
bool camera_is_jpeg(const uint8_t *bytes, size_t length);

#endif
