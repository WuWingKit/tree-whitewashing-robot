#include "rc_protocol.h"
#include <string.h>
/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xffff; integers little endian. */
uint16_t rc_crc16(const uint8_t *data, size_t size) {
    uint16_t crc=0xffffu;
    size_t i; unsigned b;
    for(i=0;i<size;i++) {
        crc^=(uint16_t)((uint16_t)data[i]<<8);
        for(b=0;b<8;b++) crc=(uint16_t)((crc&0x8000u)?(crc<<1)^0x1021u:crc<<1);
    }
    return crc;
}
static void put16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static uint16_t get16(const uint8_t *p) {return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static int16_t signed16(uint16_t v) {
    return v<=32767u?(int16_t)v:(int16_t)((int32_t)v-65536);
}
void rc_encode(uint8_t out[RC_FRAME_SIZE], const rc_frame_t *f) {
    out[0]=0xaa;out[1]=0x55;out[2]=RC_VERSION;out[3]=f->type;
    put16(out+4,f->seq);put16(out+6,(uint16_t)f->left);put16(out+8,(uint16_t)f->right);
    put16(out+10,rc_crc16(out,10));
}
int rc_feed(rc_parser_t *p,uint8_t b,rc_frame_t *f) {
    p->bytes[p->used++]=b;
    while(p->used && p->bytes[0]!=0xaa) {
        memmove(p->bytes,p->bytes+1,--p->used);
    }
    if(p->used<RC_FRAME_SIZE) return 0;
    if(p->bytes[1]==0x55 && p->bytes[2]==RC_VERSION &&
       get16(p->bytes+10)==rc_crc16(p->bytes,10)) {
        f->type=p->bytes[3];f->seq=get16(p->bytes+4);
        f->left=signed16(get16(p->bytes+6));f->right=signed16(get16(p->bytes+8));
        p->used=0;return 1;
    }
    memmove(p->bytes,p->bytes+1,RC_FRAME_SIZE-1);p->used=RC_FRAME_SIZE-1;
    return 0;
}
