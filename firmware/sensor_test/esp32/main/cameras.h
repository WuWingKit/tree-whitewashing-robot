#pragma once
#include "esp_http_server.h"
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline esp_err_t sensor_busy(httpd_req_t *r,const char *message) {
 httpd_resp_set_status(r,"503 Service Unavailable");return httpd_resp_sendstr(r,message);
}
void cameras_init(void);
bool cameras_idle(void);
cJSON *cameras_json(void);
cJSON *usb_diagnostics_json(void);
esp_err_t cameras_select(int id,int format);
esp_err_t cameras_stop(void);
esp_err_t cameras_jpeg(httpd_req_t *req,int id);
#ifdef __cplusplus
}
#endif
