#include "usbh_core.h"
#include "camera_f407.h"
#include "robot_pinmap.h"
#include <stdbool.h>

static HCD_HandleTypeDef controller;
static bool port_was_enabled, controller_ready;
static volatile uint8_t isoc_in_pipe = 0xff;

enum { ISO_QUEUE_SIZE = 8 };
typedef struct {
    uint32_t words[256];
    uint32_t slot;
    uint16_t length;
} iso_packet_t;
static iso_packet_t iso_queue[ISO_QUEUE_SIZE];
static volatile uint8_t iso_head, iso_tail;
static volatile bool iso_running, iso_fault;
static bool iso_pending;
static uint16_t iso_max_packet;
static uint8_t iso_period;
static uint32_t iso_last_slot;
static volatile uint32_t iso_requests, iso_received, iso_errors, iso_received_bytes;

void camera_iso_start(uint16_t max_packet, uint8_t interval)
{
    USBH_HandleTypeDef *host = controller.pData;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    iso_head = iso_tail = 0;
    iso_pending = iso_fault = false;
    iso_max_packet = max_packet;
    iso_period = interval;
    iso_last_slot = host->Timer - interval;
    iso_running = true;
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}
bool camera_iso_peek(const uint8_t **bytes, uint16_t *length, uint32_t *slot)
{
    uint8_t tail = iso_tail;
    if (tail == iso_head) return false;
    __DMB();
    *bytes = (const uint8_t *)iso_queue[tail].words;
    *length = iso_queue[tail].length;
    *slot = iso_queue[tail].slot;
    return true;
}
void camera_iso_release(void)
{
    __DMB();
    iso_tail = (iso_tail + 1u) % ISO_QUEUE_SIZE;
}
bool camera_iso_faulted(void) { return iso_fault; }

static void service_iso(USBH_HandleTypeDef *host, uint8_t pipe)
{
    if (!iso_running || !host->device.is_connected) return;
    uint32_t frame = host->Timer;
    uint8_t head = iso_head, next = (head + 1u) % ISO_QUEUE_SIZE;
    if (iso_pending) {
        HCD_URBStateTypeDef result = HAL_HCD_HC_GetURBState(&controller, pipe);
        if (result == URB_IDLE) {
            if (frame - iso_last_slot > iso_period + 20u) {
                iso_running = false;
                iso_fault = true;
            }
            return;
        }
        iso_pending = false;
        if (result == URB_DONE) {
            uint32_t count = HAL_HCD_HC_GetXferCount(&controller, pipe);
            if (count <= iso_max_packet) {
                ++iso_received;
                iso_received_bytes += count;
                iso_queue[head].slot = iso_last_slot;
                iso_queue[head].length = (uint16_t)count;
                __DMB();
                iso_head = head = next;
                next = (head + 1u) % ISO_QUEUE_SIZE;
            }
        } else ++iso_errors;
    }
    /* SOF drives USB scheduling independently of SPI and sensor work in main. */
    if (frame - iso_last_slot < iso_period || next == iso_tail) return;
    iso_last_slot = frame;
    ++iso_requests;
    iso_pending = USBH_IsocReceiveData(host, (uint8_t *)iso_queue[head].words,
                                     iso_max_packet, pipe) == USBH_OK;
}

static USBH_StatusTypeDef host_result(HAL_StatusTypeDef result)
{
    if (result == HAL_OK) return USBH_OK;
    if (result == HAL_BUSY) return USBH_BUSY;
    return USBH_FAIL;
}
static uint32_t port_status(void)
{
    return *(__IO uint32_t *)((uintptr_t)USB_OTG_FS + USB_OTG_HOST_PORT_BASE);
}
void camera_usb_debug(uint32_t registers[11])
{
    USB_OTG_HostChannelTypeDef *ch = (USB_OTG_HostChannelTypeDef *)(
        (uintptr_t)USB_OTG_FS + USB_OTG_HOST_CHANNEL_BASE);
    registers[0] = port_status();
    registers[1] = USB_OTG_FS->GINTSTS;
    registers[2] = ch->HCINT;
    registers[3] = ch->HCCHAR;
    registers[4] = ch->HCTSIZ;
    registers[5] = controller.hc[0].urb_state | ((uint32_t)controller.hc[1].urb_state << 4);
    registers[6] = controller.hc[0].state | ((uint32_t)controller.hc[1].state << 4);
    registers[7] = iso_requests;
    registers[8] = iso_received;
    registers[9] = iso_errors;
    registers[10] = iso_received_bytes;
}
bool camera_usb_attached(void)
{
    return controller_ready && (port_status() & USB_OTG_HPRT_PCSTS) != 0;
}
void OTG_FS_IRQHandler(void)
{
    USBH_HandleTypeDef *host = controller.pData;
    uint8_t pipe = isoc_in_pipe;
    uint32_t isoc_events = 0;
    if (pipe < controller.Init.Host_channels && (USB_OTG_FS->GINTSTS & USB_OTG_GINTSTS_HCINT)) {
        USB_OTG_HostChannelTypeDef *channel = (USB_OTG_HostChannelTypeDef *)(
            (uintptr_t)USB_OTG_FS + USB_OTG_HOST_CHANNEL_BASE + pipe * USB_OTG_HOST_CHANNEL_SIZE);
        isoc_events = channel->HCINT & channel->HCINTMSK;
    }
    HAL_HCD_IRQHandler(&controller);
    /* This HAL sets HC_XFRC for an isochronous IN completion but only
       publishes URB_DONE for control, bulk, or interrupt endpoints. */
    if (isoc_events & (USB_OTG_HCINT_FRMOR | USB_OTG_HCINT_TXERR | USB_OTG_HCINT_AHBERR |
                       USB_OTG_HCINT_BBERR | USB_OTG_HCINT_DTERR))
        controller.hc[pipe].urb_state = URB_ERROR;
    else if (isoc_events & USB_OTG_HCINT_XFRC)
        controller.hc[pipe].urb_state = URB_DONE;
    if (pipe < controller.Init.Host_channels) service_iso(host, pipe);
    /* The older F407 HAL handles port speed but exposes no enable callback. */
    bool enabled = (port_status() & USB_OTG_HPRT_PENA) != 0;
    if (enabled != port_was_enabled) {
        port_was_enabled = enabled;
        if (enabled) USBH_LL_PortEnabled(host);
        else USBH_LL_PortDisabled(host);
    }
}
void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hcd)
{
    USBH_LL_IncTimer(hcd->pData);
}
void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hcd)
{
    USBH_LL_Connect(hcd->pData);
}
void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hcd)
{
    USBH_LL_Disconnect(hcd->pData);
}
USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *host)
{
    GPIO_InitTypeDef pins = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    pins.Pin = ROBOT_STM_CAMERA_USB_DM_PIN | ROBOT_STM_CAMERA_USB_DP_PIN;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    pins.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(ROBOT_STM_CAMERA_USB_DM_PORT, &pins);
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    controller.Instance = USB_OTG_FS;
    controller.Init.Host_channels = 8;
    controller.Init.speed = HCD_SPEED_FULL;
    controller.Init.phy_itface = HCD_PHY_EMBEDDED;
    controller.Init.Sof_enable = ENABLE;
    controller.Init.dma_enable = DISABLE;
    controller.Init.vbus_sensing_enable = DISABLE; /* VBUS comes from the carrier's 5 V rail. */
    controller.pData = host;
    host->pData = &controller;
    controller_ready = HAL_HCD_Init(&controller) == HAL_OK;
    if (!controller_ready) return USBH_FAIL;
    /* 320 FS FIFO words. Receive one 1023-byte packet plus status words. */
    USB_OTG_FS->GRXFSIZ = 272u;
    USB_OTG_FS->DIEPTXF0_HNPTXFSIZ = (32u << 16) | 272u;
    USB_OTG_FS->HPTXFSIZ = (16u << 16) | 304u;
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    return USBH_OK;
}
USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *host)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    return host_result(HAL_HCD_DeInit(host->pData));
}
USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *host)
{
    if (!controller_ready) return USBH_FAIL;
    return host_result(HAL_HCD_Start(host->pData));
}
USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *host)
{
    return host_result(HAL_HCD_Stop(host->pData));
}
USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *host)
{
    switch (HAL_HCD_GetCurrentSpeed(host->pData)) {
    case HCD_SPEED_HIGH: return USBH_SPEED_HIGH;
    case HCD_SPEED_LOW: return USBH_SPEED_LOW;
    default: return USBH_SPEED_FULL;
    }
}
USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *host)
{
    return host_result(HAL_HCD_ResetPort(host->pData));
}
uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *host, uint8_t pipe)
{
    return HAL_HCD_HC_GetXferCount(host->pData, pipe);
}
USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *host, uint8_t state)
{
    (void)host; (void)state;
    return USBH_OK; /* V4.2 has no software-controlled switch on this VBUS. */
}
USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *host, uint8_t pipe, uint8_t ep,
                                    uint8_t addr, uint8_t speed, uint8_t type, uint16_t mps)
{
    USBH_StatusTypeDef result = host_result(HAL_HCD_HC_Init(host->pData, pipe, ep, addr, speed, type, mps));
    if (result == USBH_OK && type == USBH_EP_ISO && (ep & 0x80u)) isoc_in_pipe = pipe;
    return result;
}
USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *host, uint8_t pipe)
{
    if (isoc_in_pipe == pipe) {
        iso_running = false;
        isoc_in_pipe = 0xff;
    }
    return host_result(HAL_HCD_HC_Halt(host->pData, pipe));
}
USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *host, uint8_t pipe, uint8_t direction,
                                    uint8_t type, uint8_t token, uint8_t *buf, uint16_t len,
                                    uint8_t ping)
{
    return host_result(HAL_HCD_HC_SubmitRequest(host->pData, pipe, direction, type,
                                                token, buf, len, ping));
}
USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *host, uint8_t pipe)
{
    switch (HAL_HCD_HC_GetURBState(host->pData, pipe)) {
    case URB_DONE: return USBH_URB_DONE;
    case URB_NOTREADY: return USBH_URB_NOTREADY;
    case URB_NYET: return USBH_URB_NYET;
    case URB_STALL: return USBH_URB_STALL;
    case URB_ERROR: return USBH_URB_ERROR;
    default: return USBH_URB_IDLE;
    }
}
USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *host, uint8_t pipe, uint8_t toggle)
{
    HCD_HandleTypeDef *hcd = host->pData;
    if (hcd->hc[pipe].ep_is_in) hcd->hc[pipe].toggle_in = toggle;
    else hcd->hc[pipe].toggle_out = toggle;
    return USBH_OK;
}
uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *host, uint8_t pipe)
{
    HCD_HandleTypeDef *hcd = host->pData;
    return hcd->hc[pipe].ep_is_in ? hcd->hc[pipe].toggle_in : hcd->hc[pipe].toggle_out;
}
void USBH_Delay(uint32_t delay)
{
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < delay) robot_control_poll();
}
