#include <string.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "camera_link.h"
#include "camera_link_esp.h"
#include "robot_pinmap.h"

enum { SPI_READY_GPIO = ROBOT_ESP_CAMERA_SPI_READY, SPI_FRESH_MS = 1500, SPI_STOP_MS = 700 };
static DMA_ATTR uint8_t spi_rx[CAMERA_LINK_PACKET], spi_tx[CAMERA_LINK_PACKET];
static SemaphoreHandle_t lock;
static uint8_t *completed, *assembling;
static size_t frame_len, partial;
static uint16_t frame_id, width, height;
static uint32_t interval, frames, bad_packets, transfers;
static int64_t frame_ms, link_ms, state_ms, stop_ms;
static camera_usb_state_t usb_state;
static bool enabled, ever_received;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void spi_ready(spi_slave_transaction_t *t)
{
    (void)t;
    gpio_set_level(SPI_READY_GPIO, 1);
}
static void spi_done(spi_slave_transaction_t *t)
{
    (void)t;
    gpio_set_level(SPI_READY_GPIO, 0);
}
static void consume_frame(const camera_packet_t *p, int64_t now)
{
    if (!enabled) { partial = 0; return; }
    if (p->offset == 0) { partial = 0; frame_id = p->frame; }
    if (p->frame != frame_id || p->offset != partial || !p->length ||
        p->length > CAMERA_LINK_MAX_JPEG - partial) {
        ++bad_packets; partial = 0; return;
    }
    memcpy(assembling + partial, p->data, p->length);
    partial += p->length;
    if (p->flags & CAMERA_LINK_LAST) {
        if (camera_is_jpeg(assembling, partial)) {
            uint8_t *old = completed;
            completed = assembling; assembling = old;
            frame_len = partial; frame_ms = now; ++frames;
        } else ++bad_packets;
        partial = 0;
    }
}
static void receiver(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(lock, portMAX_DELAY);
        camera_packet_t command = {.type = CAMERA_LINK_COMMAND, .flags = enabled};
        xSemaphoreGive(lock);
        camera_link_encode(spi_tx, &command);
        spi_slave_transaction_t t = {
            .length = sizeof spi_rx * 8, .rx_buffer = spi_rx, .tx_buffer = spi_tx
        };
        esp_err_t err = spi_slave_transmit(SPI2_HOST, &t, portMAX_DELAY);
        camera_packet_t p;
        int64_t now = now_ms();
        xSemaphoreTake(lock, portMAX_DELAY);
        if (err == ESP_OK) ++transfers;
        if (err != ESP_OK || t.trans_len != t.length || !camera_link_decode(spi_rx, &p)) {
            ++bad_packets; partial = 0;
        } else {
            ever_received = true; link_ms = now;
            if (p.type == CAMERA_LINK_STATUS && p.length == 8 && p.flags <= CAMERA_USB_ERROR) {
                usb_state = (camera_usb_state_t)p.flags;
                state_ms = now;
                width = camera_read_u16(p.data);
                height = camera_read_u16(p.data + 2);
                interval = camera_read_u32(p.data + 4);
                if (usb_state != CAMERA_USB_STREAMING) partial = 0;
            } else if (p.type == CAMERA_LINK_FRAME) {
                consume_frame(&p, now);
            }
        }
        xSemaphoreGive(lock);
        if (err != ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void camera_spi_init(void)
{
    lock = xSemaphoreCreateMutex();
    completed = heap_caps_malloc(CAMERA_LINK_MAX_JPEG, MALLOC_CAP_SPIRAM);
    assembling = heap_caps_malloc(CAMERA_LINK_MAX_JPEG, MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK(lock && completed && assembling ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(gpio_reset_pin(SPI_READY_GPIO));
    ESP_ERROR_CHECK(gpio_set_level(SPI_READY_GPIO, 0));
    ESP_ERROR_CHECK(gpio_set_direction(SPI_READY_GPIO, GPIO_MODE_OUTPUT));
    spi_bus_config_t bus = {
        .mosi_io_num = ROBOT_ESP_CAMERA_SPI_MOSI, .miso_io_num = ROBOT_ESP_CAMERA_SPI_MISO,
        .sclk_io_num = ROBOT_ESP_CAMERA_SPI_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = CAMERA_LINK_PACKET
    };
    spi_slave_interface_config_t slave = {
        .spics_io_num = ROBOT_ESP_CAMERA_SPI_CS, .queue_size = 1, .mode = 0,
        .post_setup_cb = spi_ready, .post_trans_cb = spi_done
    };
    ESP_ERROR_CHECK(gpio_set_pull_mode(ROBOT_ESP_CAMERA_SPI_CS, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(spi_slave_initialize(SPI2_HOST, &bus, &slave, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(xTaskCreate(receiver, "camera_spi", 4096, NULL, 6, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
}
void camera_spi_select(bool value)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (enabled != value) {
        enabled = value;
        partial = frame_len = 0;
        if (!value) stop_ms = now_ms();
    }
    xSemaphoreGive(lock);
}
bool camera_spi_idle(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool idle = !enabled && (!stop_ms || (state_ms >= stop_ms && usb_state != CAMERA_USB_STREAMING) ||
                 now_ms() - stop_ms >= SPI_STOP_MS);
    xSemaphoreGive(lock);
    return idle;
}
cJSON *camera_spi_status(void)
{
    static const char *states[] = {"waiting for camera", "enumerating", "ready", "streaming",
                                    "no supported MJPEG mode", "USB transfer failed"};
    int64_t now = now_ms();
    xSemaphoreTake(lock, portMAX_DELAY);
    bool connected = ever_received && now - link_ms <= SPI_FRESH_MS;
    bool present = connected && (usb_state == CAMERA_USB_READY || usb_state == CAMERA_USB_STREAMING);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", 1);
    cJSON_AddStringToObject(o, "source", "stm32-spi");
    cJSON_AddBoolToObject(o, "spi_online", connected);
    cJSON_AddBoolToObject(o, "present", present);
    cJSON_AddNumberToObject(o, "usb_address", 0);
    cJSON_AddBoolToObject(o, "streaming", present && enabled && usb_state == CAMERA_USB_STREAMING);
    cJSON_AddNumberToObject(o, "selected", enabled ? 0 : -1);
    cJSON_AddStringToObject(o, "error", connected ? states[usb_state] : "no SPI data from STM32");
    cJSON_AddNumberToObject(o, "frames", frames);
    cJSON_AddNumberToObject(o, "bad_frames", bad_packets);
    cJSON_AddNumberToObject(o, "spi_transfers", transfers);
    if (frame_len) cJSON_AddNumberToObject(o, "frame_age_ms", now - frame_ms);
    else cJSON_AddNullToObject(o, "frame_age_ms");
    cJSON *formats = cJSON_AddArrayToObject(o, "formats");
    if (present && width && height) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddItemToArray(formats, f);
        cJSON_AddNumberToObject(f, "index", 0);
        cJSON_AddNumberToObject(f, "width", width);
        cJSON_AddNumberToObject(f, "height", height);
        cJSON_AddStringToObject(f, "encoding", "MJPEG");
        cJSON_AddNumberToObject(f, "default_fps", interval ? 10000000.0 / interval : 0);
    }
    xSemaphoreGive(lock);
    return o;
}
esp_err_t camera_spi_jpeg(httpd_req_t *req)
{
    uint8_t *copy = heap_caps_malloc(CAMERA_LINK_MAX_JPEG, MALLOC_CAP_SPIRAM);
    if (!copy) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no PSRAM");
    xSemaphoreTake(lock, portMAX_DELAY);
    int64_t age = now_ms() - frame_ms;
    size_t len = enabled ? frame_len : 0;
    uint32_t seq = frames;
    if (len) memcpy(copy, completed, len);
    xSemaphoreGive(lock);
    if (!len) { free(copy); return sensor_busy(req, "waiting for first STM32 JPEG"); }
    char n[32];
    snprintf(n, sizeof n, "%lu", (unsigned long)seq);
    httpd_resp_set_hdr(req, "X-Frame-Seq", n);
    char age_text[32];
    snprintf(age_text, sizeof age_text, "%lld", (long long)age);
    httpd_resp_set_hdr(req, "X-Frame-Age-Ms", age_text);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, (const char *)copy, len);
    free(copy);
    return e;
}
