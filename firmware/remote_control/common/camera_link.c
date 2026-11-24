#include "camera_link.h"
#include <string.h>

static const uint8_t signature[] = {'T', 'C', 'A', 1};

uint16_t camera_read_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
uint32_t camera_read_u32(const uint8_t *p)
{
    return camera_read_u16(p) | ((uint32_t)camera_read_u16(p + 2) << 16);
}
void camera_write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
void camera_write_u32(uint8_t *p, uint32_t value)
{
    camera_write_u16(p, (uint16_t)value);
    camera_write_u16(p + 2, (uint16_t)(value >> 16));
}
static uint16_t packet_crc(const uint8_t *p)
{
    uint16_t crc = 0xffffu;
    unsigned i, bit;
    for (i = 0; i < CAMERA_LINK_PACKET - 2; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0u));
    }
    return crc;
}
void camera_link_encode(uint8_t out[CAMERA_LINK_PACKET], const camera_packet_t *packet)
{
    memset(out, 0, CAMERA_LINK_PACKET);
    if (packet->length > CAMERA_LINK_PAYLOAD || (packet->length && !packet->data)) return;
    memcpy(out, signature, sizeof signature);
    out[4] = packet->type;
    out[5] = packet->flags;
    camera_write_u16(out + 6, packet->frame);
    camera_write_u32(out + 8, packet->offset);
    camera_write_u16(out + 12, packet->length);
    if (packet->length) memcpy(out + 14, packet->data, packet->length);
    camera_write_u16(out + CAMERA_LINK_PACKET - 2, packet_crc(out));
}
bool camera_link_decode(const uint8_t in[CAMERA_LINK_PACKET], camera_packet_t *packet)
{
    uint16_t length = camera_read_u16(in + 12);
    if (memcmp(in, signature, sizeof signature) || length > CAMERA_LINK_PAYLOAD ||
        packet_crc(in) != camera_read_u16(in + CAMERA_LINK_PACKET - 2)) return false;
    packet->type = in[4];
    packet->flags = in[5];
    packet->frame = camera_read_u16(in + 6);
    packet->offset = camera_read_u32(in + 8);
    packet->length = length;
    packet->data = in + 14;
    return true;
}
bool camera_is_jpeg(const uint8_t *bytes, size_t length)
{
    return length >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8 &&
           bytes[length - 2] == 0xff && bytes[length - 1] == 0xd9;
}
