idf_component_register(SRCS "RemoteControl.cpp" "ota_boot.cpp" "cameras.c"
 "src/rc_protocol.c" "src/rc_ota_image.c"
 INCLUDE_DIRS "." REQUIRES arduino usb_host_uvc esp_http_server json esp_https_ota esp_http_client
 esp_timer esp_psram app_update nvs_flash esp_event esp_netif esp_wifi lwip
 EMBED_TXTFILES "index.html")
target_compile_definitions(${COMPONENT_LIB} PRIVATE TREE_SENSOR_USB=1 ARDUINO_USB_CDC_ON_BOOT=0)
if(NOT DEFINED ROBOT_WIRING_PROFILE)
 message(FATAL_ERROR "Set ROBOT_WIRING_PROFILE explicitly: legacy=0, pcb_v1=1, pcb_v2=2")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE ROBOT_WIRING_PROFILE=${ROBOT_WIRING_PROFILE})
# Selective Arduino Update also builds HttpsOTAUpdate.cpp, whose includes need
# these IDF dependencies explicitly when COMPONENTS is restricted to main.
idf_component_get_property(arduino_lib arduino COMPONENT_LIB)
target_link_libraries(${arduino_lib} PUBLIC idf::esp_https_ota idf::esp_http_client)
