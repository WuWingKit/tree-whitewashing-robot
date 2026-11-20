idf_component_register(SRCS "main.c" "cameras.c" "../../../remote_control/common/rc_protocol.c"
 INCLUDE_DIRS "." "../../../remote_control/common"
 REQUIRES usb_host_uvc esp_http_server esp_wifi nvs_flash esp_netif esp_event
 esp_driver_uart esp_timer json esp_psram
 EMBED_TXTFILES "index.html")
if(NOT DEFINED ROBOT_WIRING_PROFILE)
 set(ROBOT_WIRING_PROFILE 0)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE ROBOT_WIRING_PROFILE=${ROBOT_WIRING_PROFILE})
