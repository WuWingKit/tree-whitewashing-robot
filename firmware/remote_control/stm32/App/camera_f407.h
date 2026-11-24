#ifndef TREE_CAMERA_F407_H
#define TREE_CAMERA_F407_H
#include <stdint.h>
#include <stdbool.h>
void camera_f407_init(void);
void camera_f407_poll(void);
uint16_t camera_f407_diag(uint16_t *attempts, uint16_t *acks);
bool camera_usb_attached(void);
void camera_usb_debug(uint32_t registers[11]);
void robot_control_poll(void);
void camera_iso_start(uint16_t max_packet, uint8_t interval);
bool camera_iso_peek(const uint8_t **bytes, uint16_t *length, uint32_t *slot);
void camera_iso_release(void);
bool camera_iso_faulted(void);
#endif
