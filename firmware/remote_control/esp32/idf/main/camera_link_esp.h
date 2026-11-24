#pragma once
#include "cameras.h"
void camera_spi_init(void);
cJSON *camera_spi_status(void);
void camera_spi_select(bool enabled);
bool camera_spi_idle(void);
esp_err_t camera_spi_jpeg(httpd_req_t *req);
