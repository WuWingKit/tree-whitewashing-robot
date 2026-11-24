#pragma once
/* ESP32 has one internal USB PHY in either wiring profile. */
#define ROBOT_CAMERA_USB_PORT_COUNT 2
#ifdef TREE_CAMERA_SPI
#include "robot_pinmap.h"
#define ROBOT_CAMERA_USB_DM_GPIO ROBOT_ESP_CAMERA_USB_DM
#define ROBOT_CAMERA_USB_DP_GPIO ROBOT_ESP_CAMERA_USB_DP
#define ROBOT_CAMERA_USB_EXTERNAL_HUB_REQUIRED 0
#else
#define ROBOT_CAMERA_USB_DM_GPIO 19
#define ROBOT_CAMERA_USB_DP_GPIO 20
#define ROBOT_CAMERA_USB_EXTERNAL_HUB_REQUIRED 1
#endif

#include "sdkconfig.h"
#include "soc/usb_pins.h"
#if !defined(CONFIG_IDF_TARGET_ESP32S3) || !CONFIG_IDF_TARGET_ESP32S3
#error "Camera USB configuration requires ESP32-S3"
#endif
#if ROBOT_CAMERA_USB_EXTERNAL_HUB_REQUIRED && !CONFIG_USB_HOST_HUBS_SUPPORTED
#error "Two PCB camera ports require CONFIG_USB_HOST_HUBS_SUPPORTED and a physical external hub"
#endif
#if ROBOT_CAMERA_USB_DM_GPIO != USBPHY_DM_NUM || ROBOT_CAMERA_USB_DP_GPIO != USBPHY_DP_NUM
#error "Internal USB PHY pins are fixed: D- GPIO19, D+ GPIO20; do not remap to arbitrary GPIOs"
#endif
