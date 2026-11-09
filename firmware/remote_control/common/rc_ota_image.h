#ifndef RC_OTA_IMAGE_H
#define RC_OTA_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RC_OTA_MARKER "TREE_ROBOT_S3_RC_OTA_V1"
typedef struct { size_t matched; uint8_t seen; } rc_ota_marker_t;
/* ESP32-S3 app image, not a merged USB image/bootloader or STM32 binary. */
int rc_ota_header_valid(const uint8_t *data,size_t size);
void rc_ota_scan_marker(rc_ota_marker_t *scan,const uint8_t *data,size_t size);
#ifdef __cplusplus
}
#endif
#endif
