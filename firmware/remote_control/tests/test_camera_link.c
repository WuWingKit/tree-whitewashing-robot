#include "camera_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t wire[CAMERA_LINK_PACKET], piece[CAMERA_LINK_PAYLOAD];
    camera_packet_t decoded;
    camera_packet_t packet = {CAMERA_LINK_FRAME, CAMERA_LINK_LAST, 0x1234, 60000,
                              sizeof piece, piece};
    size_t i;
    for (i = 0; i < sizeof piece; ++i) piece[i] = (uint8_t)i;
    camera_link_encode(wire, &packet);
    assert(camera_link_decode(wire, &decoded));
    assert(decoded.type == packet.type && decoded.flags == packet.flags);
    assert(decoded.frame == packet.frame && decoded.offset == packet.offset);
    assert(decoded.length == sizeof piece && !memcmp(decoded.data, piece, sizeof piece));
    for (i = 0; i < sizeof wire; ++i) {
        wire[i] ^= 1;
        assert(!camera_link_decode(wire, &decoded));
        wire[i] ^= 1;
    }
    packet.type = CAMERA_LINK_COMMAND;
    packet.length = 0;
    packet.data = NULL;
    camera_link_encode(wire, &packet);
    assert(camera_link_decode(wire, &decoded) && decoded.type == CAMERA_LINK_COMMAND && !decoded.length);
    packet.length = CAMERA_LINK_PAYLOAD + 1;
    packet.data = piece;
    camera_link_encode(wire, &packet);
    assert(!camera_link_decode(wire, &decoded));
    camera_write_u32(wire, 0x12345678u);
    assert(wire[0] == 0x78 && wire[3] == 0x12 && camera_read_u32(wire) == 0x12345678u);
    piece[0] = 0xff; piece[1] = 0xd8; piece[2] = 0xff; piece[3] = 0xd9;
    assert(camera_is_jpeg(piece, 4) && !camera_is_jpeg(piece, 3) && !camera_is_jpeg(piece, 5));
    puts("Camera link: round trip, length bounds, byte order, CRC corruption and JPEG markers passed.");
    return 0;
}
