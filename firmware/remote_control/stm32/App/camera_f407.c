#include "camera_f407.h"
#include "camera_link.h"
#include "usbh_core.h"
#include "robot_pinmap.h"
#include <stdio.h>

#define NO_PIPE 0xffu
#define COMMAND_TIMEOUT_MS 500u
enum { VIDEO_CLASS = 14, VIDEO_CONTROL = 1, VIDEO_STREAMING = 2,
       CLASS_IF = 0x24, MJPEG_FORMAT = 6, MJPEG_FRAME = 7,
       UVC_FID = 1, UVC_EOF = 2, UVC_ERR = 0x40 };
enum { SET_PROBE, GET_PROBE, SET_COMMIT, NEGOTIATED };
typedef struct {
    uint8_t interface, format, frame, endpoint, alternate, type, period;
    uint16_t width, height, max_packet;
    uint32_t interval, max_payload;
} video_mode_t;

static USBH_HandleTypeDef host;
static SPI_HandleTypeDef spi;
static camera_usb_state_t state;
static video_mode_t mode;
static uint32_t usb_packet_words[16];
static uint8_t jpeg[CAMERA_LINK_MAX_JPEG];
static uint8_t probe[48], tx[CAMERA_LINK_PACKET], rx[CAMERA_LINK_PACKET];
static uint8_t probe_len, negotiation, pipe = NO_PIPE, frame_fid, payload_flags;
static uint32_t captured, pending, sent, bulk_offset;
static uint32_t last_command, last_spi, last_status, last_isoc_slot, urb_started;
static uint16_t frame_sequence;
static bool enabled, spi_ok, host_ok, active, setting_alt, urb_pending, fid_known, discard, isoc_slot_known;
static uint16_t spi_attempts, spi_acks;
static uint32_t jpeg_ok, jpeg_bad, payload_gaps, jpeg_last_size;
static uint32_t jpeg_head, jpeg_tail;
static bool reprobe;

static void debug_uart_init(void)
{
    GPIO_InitTypeDef pin = {0};
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    pin.Pin = GPIO_PIN_9;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Alternate = GPIO_AF7_USART1;
    pin.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &pin);
    USART1->BRR = (HAL_RCC_GetPCLK2Freq() + 57600u) / 115200u;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE;
}
static void debug_poll(uint32_t now)
{
    static uint32_t last_ms;
    static char line[370];
    static unsigned pos, length;
    if (pos == length && now - last_ms >= 1000u) {
        uint32_t regs[11];
        camera_usb_debug(regs);
        last_ms = now;
        int written = snprintf(line, sizeof line,
            "USB2 state=%u host=%u enum=%u req=%u ctl=%u tick=%lu port=%08lX int=%08lX ch0=%08lX char=%08lX size=%08lX urb=%02lX hc=%02lX vid=%04X pid=%04X cfg=%u mps=%u iso=%lu/%lu/%lu bytes=%lu gaps=%lu jpeg=%lu/%lu short=%lu:%04lX:%04lX\r\n",
            state, host.gState, host.EnumState, host.RequestState, host.Control.state,
            (unsigned long)host.Timer, (unsigned long)regs[0], (unsigned long)regs[1],
            (unsigned long)regs[2], (unsigned long)regs[3], (unsigned long)regs[4],
            (unsigned long)regs[5], (unsigned long)regs[6], host.device.DevDesc.idVendor,
            host.device.DevDesc.idProduct, host.device.CfgDesc.wTotalLength, mode.max_packet,
            (unsigned long)regs[7], (unsigned long)regs[8], (unsigned long)regs[9], (unsigned long)regs[10],
            (unsigned long)payload_gaps, (unsigned long)jpeg_ok, (unsigned long)jpeg_bad,
            (unsigned long)jpeg_last_size, (unsigned long)jpeg_head, (unsigned long)jpeg_tail);
        pos = 0;
        length = written > 0 && written < sizeof line ? (unsigned)written : 0;
    }
    if (pos < length && (USART1->SR & USART_SR_TXE)) USART1->DR = (uint8_t)line[pos++];
}

static bool find_mjpeg(const uint8_t *cfg, uint16_t length, video_mode_t *best)
{
    uint8_t interface = 0xffu, format = 0;
    uint32_t best_area = UINT32_MAX;
    uint16_t pos = 0;
    while (pos + 2u <= length) {
        const uint8_t *d = cfg + pos;
        uint8_t n = d[0];
        if (n < 2 || n > length - pos) return false;
        if (d[1] == 4 && n >= 9) {
            interface = d[5] == VIDEO_CLASS && d[6] == VIDEO_STREAMING && d[3] == 0 ? d[2] : 0xffu;
            format = 0;
            if (d[5] == VIDEO_CLASS && d[6] == VIDEO_CONTROL && d[3] == 0 && pos + n + 5u <= length) {
                const uint8_t *vc = d + n;
                if (vc[0] >= 5 && vc[1] == CLASS_IF && vc[2] == 1) {
                    uint16_t version = camera_read_u16(vc + 3);
                    probe_len = version >= 0x0150 ? 48 : (version >= 0x0110 ? 34 : 26);
                }
            }
        } else if (interface != 0xffu && d[1] == CLASS_IF && n >= 4) {
            if (d[2] == MJPEG_FORMAT) format = d[3];
            else if (d[2] == MJPEG_FRAME && format && n >= 26) {
                uint16_t w = camera_read_u16(d + 5), h = camera_read_u16(d + 7);
                uint32_t area = (uint32_t)w * h;
                uint32_t interval = camera_read_u32(d + 21);
                unsigned i;
                /* Prefer the slowest advertised interval at each resolution. */
                if (d[25] == 0 && n >= 38) interval = camera_read_u32(d + 30);
                else for (i = 0; i < d[25] && 30u + 4u * i <= n; ++i) {
                    uint32_t candidate = camera_read_u32(d + 26 + i * 4u);
                    if (candidate > interval) interval = candidate;
                }
                if (area && w <= 640 && h <= 480 && interval && d[3] &&
                    (area < best_area || (area == best_area && interval > best->interval))) {
                    best_area = area;
                    best->interface = interface;
                    best->format = format; best->frame = d[3];
                    best->width = w; best->height = h; best->interval = interval;
                }
            } else if (d[2] == 4 || d[2] == 0x0a || d[2] == 0x10 || d[2] == 0x12) format = 0;
        }
        pos += n;
    }
    return best_area != UINT32_MAX;
}
static bool find_endpoint(const uint8_t *cfg, uint16_t length, video_mode_t *m)
{
    bool matching = false;
    uint8_t alternate = 0;
    uint16_t pos = 0;
    while (pos + 2u <= length) {
        const uint8_t *d = cfg + pos;
        uint8_t n = d[0];
        if (n < 2 || n > length - pos) return false;
        if (d[1] == 4 && n >= 9) {
            matching = d[2] == m->interface && d[5] == VIDEO_CLASS && d[6] == VIDEO_STREAMING;
            alternate = d[3];
        } else if (matching && d[1] == 5 && n >= 7 && (d[2] & 0x80u)) {
            uint8_t type = d[3] & 3u;
            uint16_t bytes = camera_read_u16(d + 4);
            bool isoc = type == USBH_EP_ISO && alternate && d[6] >= 1 && d[6] <= 8 &&
                        bytes <= 1023u && bytes >= m->max_payload;
            bool bulk = type == USBH_EP_BULK && alternate == 0 && bytes <= 64;
            if (bytes && (isoc || bulk) && (!m->max_packet || bytes < m->max_packet)) {
                m->max_packet = bytes; m->type = type; m->endpoint = d[2];
                m->alternate = alternate; m->period = isoc ? (uint8_t)(1u << (d[6] - 1u)) : 0;
            }
        }
        pos += n;
    }
    return m->max_packet != 0;
}

static void reset_capture(void)
{
    captured = pending = sent = bulk_offset = 0;
    fid_known = urb_pending = isoc_slot_known = false;
    discard = true;
}
static void close_pipe(USBH_HandleTypeDef *h)
{
    if (pipe == NO_PIPE) return;
    USBH_ClosePipe(h, pipe);
    USBH_FreePipe(h, pipe);
    pipe = NO_PIPE;
}
static void prepare_probe(void)
{
    memset(probe, 0, sizeof probe);
    camera_write_u16(probe, 1); /* Request the advertised interval. */
    probe[2] = mode.format;
    probe[3] = mode.frame;
    camera_write_u32(probe + 4, mode.interval);
    negotiation = SET_PROBE;
}
static USBH_StatusTypeDef video_init(USBH_HandleTypeDef *h)
{
    memset(&mode, 0, sizeof mode);
    probe_len = 26;
    if (h->device.speed != USBH_SPEED_FULL ||
        h->device.CfgDesc.wTotalLength > sizeof h->device.CfgDesc_Raw ||
        !find_mjpeg(h->device.CfgDesc_Raw, h->device.CfgDesc.wTotalLength, &mode)) {
        state = CAMERA_USB_UNSUPPORTED;
        return USBH_FAIL;
    }
    reprobe = false;
    prepare_probe();
    return USBH_OK;
}
static USBH_StatusTypeDef video_deinit(USBH_HandleTypeDef *h)
{
    close_pipe(h);
    reset_capture();
    active = setting_alt = false;
    state = CAMERA_USB_WAITING;
    memset(&mode, 0, sizeof mode);
    return USBH_OK;
}
static USBH_StatusTypeDef probe_request(USBH_HandleTypeDef *h)
{
    if (h->RequestState == CMD_SEND) {
        h->Control.setup.b.bmRequestType = negotiation == GET_PROBE ? 0xa1 : 0x21;
        h->Control.setup.b.bRequest = negotiation == GET_PROBE ? 0x81 : 1;
        h->Control.setup.b.wValue.w = negotiation == SET_COMMIT ? 0x0200 : 0x0100;
        h->Control.setup.b.wIndex.w = mode.interface;
        h->Control.setup.b.wLength.w = probe_len;
    }
    return USBH_CtlReq(h, probe, probe_len);
}
static USBH_StatusTypeDef video_requests(USBH_HandleTypeDef *h)
{
    if (negotiation == NEGOTIATED) return USBH_OK;
    USBH_StatusTypeDef result = probe_request(h);
    if (result == USBH_BUSY) return result;
    if (result != USBH_OK) { state = CAMERA_USB_ERROR; return USBH_FAIL; }
    if (negotiation == GET_PROBE) {
        mode.max_payload = camera_read_u32(probe + 22);
        mode.max_packet = 0;
        if (camera_read_u32(probe + 4)) mode.interval = camera_read_u32(probe + 4);
        if (probe[2] != mode.format || probe[3] != mode.frame || mode.max_payload < 2u ||
            !find_endpoint(h->device.CfgDesc_Raw, h->device.CfgDesc.wTotalLength, &mode)) {
            state = CAMERA_USB_UNSUPPORTED;
            return USBH_FAIL;
        }
    }
    if (++negotiation != NEGOTIATED) return USBH_BUSY;
    state = CAMERA_USB_READY;
    return USBH_OK;
}

static void finish_frame(void)
{
    uint32_t image_size = captured;
    /* Some UVC cameras pad MJPEG payloads with zeros after the EOI marker. */
    while (image_size > 4 && jpeg[image_size - 1] == 0) --image_size;
    if (!discard && !pending && camera_is_jpeg(jpeg, image_size)) {
        pending = image_size;
        sent = 0;
        ++frame_sequence;
        ++jpeg_ok;
    } else if (captured >= 2) {
        ++jpeg_bad;
        jpeg_last_size = captured;
        jpeg_head = ((uint32_t)jpeg[0]<<8) | jpeg[1];
        jpeg_tail = ((uint32_t)jpeg[captured-2]<<8) | jpeg[captured-1];
    }
    captured = 0;
    discard = true;
}
static void append_payload(const uint8_t *data, uint32_t length)
{
    if (discard) return;
    if (length > CAMERA_LINK_MAX_JPEG - captured) { discard = true; return; }
    memcpy(jpeg + captured, data, length);
    captured += length;
}
static void receive_packet(const uint8_t *data, uint32_t length)
{
    uint32_t original = length;
    if (!bulk_offset) {
        if (length < 2 || data[0] < 2 || data[0] > length) {
            if (length) discard = true;
            return;
        }
        payload_flags = data[1];
        if (!fid_known || frame_fid != (payload_flags & UVC_FID)) {
            if (fid_known) finish_frame();
            frame_fid = payload_flags & UVC_FID;
            fid_known = true;
            discard = pending != 0;
        }
        if (payload_flags & UVC_ERR) discard = true;
        length -= data[0]; data += data[0];
    }
    append_payload(data, length);
    if (mode.type == USBH_EP_BULK && original == mode.max_packet &&
        bulk_offset + original < mode.max_payload) {
        bulk_offset += original;
        return;
    }
    bulk_offset = 0;
    if (payload_flags & UVC_EOF) finish_frame();
}
static void poll_endpoint(USBH_HandleTypeDef *h, uint32_t now)
{
    if (mode.type == USBH_EP_ISO) {
        const uint8_t *bytes;
        uint16_t length;
        uint32_t slot;
        unsigned budget = 3;
        if (camera_iso_faulted()) {
            close_pipe(h);
            reset_capture();
            state = CAMERA_USB_ERROR;
            return;
        }
        while (budget-- && camera_iso_peek(&bytes, &length, &slot)) {
            if (isoc_slot_known && slot - last_isoc_slot != mode.period) {
                /* If a packet was lost at a frame boundary, require a fresh SOI. */
                captured = 0;
                discard = true;
                ++payload_gaps;
            }
            isoc_slot_known = true;
            last_isoc_slot = slot;
            receive_packet(bytes, length);
            camera_iso_release();
        }
        return;
    }
    if (urb_pending) {
        USBH_URBStateTypeDef result = USBH_LL_GetURBState(h, pipe);
        if (result == USBH_URB_IDLE) {
            if (now - urb_started < 100u) return;
            /* Stop polling a wedged pipe; changing alternate setting can reopen it. */
            close_pipe(h);
            reset_capture();
            state = CAMERA_USB_ERROR;
            return;
        }
        urb_pending = false;
        if (result == USBH_URB_DONE) {
            uint32_t bytes = USBH_LL_GetLastXferSize(h, pipe);
            if (bytes <= mode.max_packet) receive_packet((const uint8_t *)usb_packet_words, bytes);
            else discard = true;
        } else if (result != USBH_URB_NOTREADY && result != USBH_URB_NYET) {
            discard = true;
            bulk_offset = 0;
        }
    }
    USBH_BulkReceiveData(h, (uint8_t *)usb_packet_words, mode.max_packet, pipe);
    urb_started = now;
    urb_pending = true;
}
static USBH_StatusTypeDef video_poll(USBH_HandleTypeDef *h)
{
    uint32_t now = HAL_GetTick();
    bool requested = enabled && now - last_command <= COMMAND_TIMEOUT_MS;
    if (state == CAMERA_USB_ERROR && pipe == NO_PIPE && requested && !setting_alt) return USBH_OK;
    /* After alt 0, negotiate a fresh probe/commit before opening the stream again.
       Finish any in-flight control transfer even if the request arrives or goes away. */
    if (reprobe && !active && !setting_alt && (requested || negotiation != NEGOTIATED)) {
        if (negotiation == NEGOTIATED) prepare_probe();
        USBH_StatusTypeDef result = video_requests(h);
        if (result == USBH_BUSY) return USBH_BUSY;
        reprobe = false;
        if (result != USBH_OK) return USBH_OK;
    }
    if (requested != active || setting_alt) {
        static bool next_active;
        if (!setting_alt) {
            next_active = requested;
            reset_capture();
            if (!next_active) close_pipe(h);
        }
        if (mode.alternate) {
            USBH_StatusTypeDef result = USBH_SetInterface(h, mode.interface, next_active ? mode.alternate : 0);
            setting_alt = result == USBH_BUSY;
            if (setting_alt) return USBH_BUSY;
            if (result != USBH_OK) {
                active = false; state = CAMERA_USB_ERROR; return USBH_OK;
            }
        }
        active = next_active;
        if (!active) reprobe = true;
        if (active) {
            pipe = USBH_AllocPipe(h, mode.endpoint);
            if (pipe == NO_PIPE || USBH_OpenPipe(h, pipe, mode.endpoint, h->device.address,
                                                h->device.speed, mode.type, mode.max_packet) != USBH_OK) {
                close_pipe(h); state = CAMERA_USB_ERROR; return USBH_OK;
            }
            USBH_LL_SetToggle(h, pipe, 0);
            if (mode.type == USBH_EP_ISO) camera_iso_start(mode.max_packet, mode.period);
        }
        state = active ? CAMERA_USB_STREAMING : CAMERA_USB_READY;
    }
    if (active && pipe != NO_PIPE) poll_endpoint(h, now);
    return USBH_OK;
}
static USBH_StatusTypeDef video_sof(USBH_HandleTypeDef *h) { (void)h; return USBH_OK; }
static USBH_ClassTypeDef video = { "UVC", VIDEO_CLASS, video_init, video_deinit,
                                   video_requests, video_poll, video_sof, NULL };
static void host_event(USBH_HandleTypeDef *h, uint8_t event)
{
    (void)h;
    if (event == HOST_USER_CONNECTION) state = CAMERA_USB_ENUMERATING;
    else if (event == HOST_USER_DISCONNECTION) state = CAMERA_USB_WAITING;
}

static bool spi_init(void)
{
    GPIO_InitTypeDef pins = {0};
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    HAL_GPIO_WritePin(ROBOT_STM_CAMERA_SPI_CS_PORT, ROBOT_STM_CAMERA_SPI_CS_PIN, GPIO_PIN_SET);
    pins.Pin = ROBOT_STM_CAMERA_SPI_CS_PIN; pins.Mode = GPIO_MODE_OUTPUT_PP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ROBOT_STM_CAMERA_SPI_CS_PORT, &pins);
    pins.Mode = GPIO_MODE_AF_PP; pins.Alternate = GPIO_AF5_SPI2;
    pins.Pin = ROBOT_STM_CAMERA_SPI_SCK_PIN; HAL_GPIO_Init(ROBOT_STM_CAMERA_SPI_SCK_PORT, &pins);
    pins.Pin = ROBOT_STM_CAMERA_SPI_MISO_PIN; HAL_GPIO_Init(ROBOT_STM_CAMERA_SPI_MISO_PORT, &pins);
    pins.Pin = ROBOT_STM_CAMERA_SPI_MOSI_PIN; HAL_GPIO_Init(ROBOT_STM_CAMERA_SPI_MOSI_PORT, &pins);
    pins.Mode = GPIO_MODE_INPUT; pins.Pin = ROBOT_STM_CAMERA_SPI_READY_PIN; pins.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(ROBOT_STM_CAMERA_SPI_READY_PORT, &pins);
    spi.Instance = SPI2;
    spi.Init.Mode = SPI_MODE_MASTER;
    spi.Init.Direction = SPI_DIRECTION_2LINES;
    spi.Init.DataSize = SPI_DATASIZE_8BIT;
    spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    spi.Init.NSS = SPI_NSS_SOFT;
    spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16; /* 42 MHz / 16 = 2.625 MHz. */
    spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spi.Init.TIMode = SPI_TIMODE_DISABLE;
    spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    spi.Init.CRCPolynomial = 7;
    return HAL_SPI_Init(&spi) == HAL_OK;
}
static void spi_poll(uint32_t now)
{
    camera_packet_t packet = {0}, response;
    uint8_t data[8];
    /* Each exchange <1 ms normally. Reserve SPI for control/status even with no camera. */
    if (!spi_ok || HAL_GPIO_ReadPin(ROBOT_STM_CAMERA_SPI_READY_PORT, ROBOT_STM_CAMERA_SPI_READY_PIN) != GPIO_PIN_SET ||
        now - last_spi < (pending ? 1u : 20u)) return;
    last_spi = now;
    if (pending && now - last_status < 100u) {
        uint32_t left = pending - sent;
        packet.type = CAMERA_LINK_FRAME;
        packet.frame = frame_sequence;
        packet.offset = sent;
        packet.length = (uint16_t)(left > CAMERA_LINK_PAYLOAD ? CAMERA_LINK_PAYLOAD : left);
        packet.flags = left == packet.length ? CAMERA_LINK_LAST : 0;
        packet.data = jpeg + sent;
    } else {
        packet.type = CAMERA_LINK_STATUS;
        packet.flags = state;
        camera_write_u16(data, mode.width);
        camera_write_u16(data + 2, mode.height);
        camera_write_u32(data + 4, mode.interval);
        packet.data = data;
        packet.length = sizeof data;
        last_status = now;
    }
    camera_link_encode(tx, &packet);
    HAL_GPIO_WritePin(ROBOT_STM_CAMERA_SPI_CS_PORT, ROBOT_STM_CAMERA_SPI_CS_PIN, GPIO_PIN_RESET);
    ++spi_attempts;
    HAL_StatusTypeDef exchanged = HAL_SPI_TransmitReceive(&spi, tx, rx, sizeof tx, 4);
    HAL_GPIO_WritePin(ROBOT_STM_CAMERA_SPI_CS_PORT, ROBOT_STM_CAMERA_SPI_CS_PIN, GPIO_PIN_SET);
    if (exchanged == HAL_OK && camera_link_decode(rx, &response) &&
        response.type == CAMERA_LINK_COMMAND && response.length == 0) {
        ++spi_acks;
        enabled = (response.flags & 1u) != 0;
        last_command = now;
        if (packet.type == CAMERA_LINK_FRAME) {
            sent += packet.length;
            if (sent == pending) sent = pending = 0;
        }
    } else {
        /* A camera frame is disposable. Restart cleanly after a broken exchange. */
        sent = pending = 0;
    }
}
void camera_f407_init(void)
{
    debug_uart_init();
    spi_ok = spi_init();
    host_ok = USBH_Init(&host, host_event, 0) == USBH_OK &&
              USBH_RegisterClass(&host, &video) == USBH_OK && USBH_Start(&host) == USBH_OK;
    if (!host_ok) state = CAMERA_USB_ERROR;
}
void camera_f407_poll(void)
{
    if (host_ok) {
        USBH_Process(&host);
        if (host.gState == HOST_ABORT_STATE && state == CAMERA_USB_ENUMERATING)
            state = CAMERA_USB_UNSUPPORTED;
    }
    spi_poll(HAL_GetTick());
    debug_poll(HAL_GetTick());
}
uint16_t camera_f407_diag(uint16_t *attempts, uint16_t *acks)
{
    *attempts = spi_attempts;
    *acks = spi_acks;
    return (uint16_t)(state << 4) |
           (spi_ok ? 1u : 0u) |
           (HAL_GPIO_ReadPin(ROBOT_STM_CAMERA_SPI_READY_PORT, ROBOT_STM_CAMERA_SPI_READY_PIN) == GPIO_PIN_SET ? 2u : 0u) |
           (host_ok ? 4u : 0u) |
           (camera_usb_attached() ? 8u : 0u);
}
