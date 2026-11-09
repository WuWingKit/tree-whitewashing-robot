#include "rc_ota_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint8_t header[36]={0};const uint8_t marker[]=RC_OTA_MARKER;
    header[0]=0xe9;header[1]=5;header[12]=9;
    header[32]=0x32;header[33]=0x54;header[34]=0xcd;header[35]=0xab;
    assert(rc_ota_header_valid(header,sizeof(header)));
    for(unsigned i=0;i<36;i++)assert(!rc_ota_header_valid(header,i));
    header[12]=0;assert(!rc_ota_header_valid(header,36));header[12]=9;
    header[32]=0;assert(!rc_ota_header_valid(header,36));header[32]=0x32;
    header[0]=0;assert(!rc_ota_header_valid(header,36));
    for(size_t split=0;split<sizeof(marker);split++) {
        rc_ota_marker_t s={0};rc_ota_scan_marker(&s,(const uint8_t *)"TTREETREE_RTR",12);
        rc_ota_scan_marker(&s,marker,split);rc_ota_scan_marker(&s,marker+split,sizeof(marker)-1-split);
        assert(s.seen);
    }
    rc_ota_marker_t s={0};rc_ota_scan_marker(&s,(const uint8_t *)"unrelated firmware",18);assert(!s.seen);
    puts("PASS OTA header: S3 app, truncated/wrong-chip/merged rejection; marker across every chunk split");
    return 0;
}
