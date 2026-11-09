#include "rc_ota_image.h"
#include <string.h>
int rc_ota_header_valid(const uint8_t *p,size_t size) {
    if(size<36) return 0;
    return p[0]==0xe9 && p[1]>0 && p[1]<=16 &&
           p[12]==9 && p[13]==0 && /* ESP_CHIP_ID_ESP32S3 */
           p[32]==0x32 && p[33]==0x54 && p[34]==0xcd && p[35]==0xab;
}
void rc_ota_scan_marker(rc_ota_marker_t *s,const uint8_t *data,size_t size) {
    const char *marker=RC_OTA_MARKER;
    const size_t n=sizeof(RC_OTA_MARKER)-1;
    size_t i,k,j;
    for(i=0;i<size && !s->seen;i++) {
        while(s->matched && data[i]!=(uint8_t)marker[s->matched]) {
            k=s->matched-1;
            while(k) {
                for(j=0;j<k;j++) if(marker[j]!=marker[s->matched-k+j]) break;
                if(j==k) break;
                k--;
            }
            s->matched=k;
        }
        if(data[i]==(uint8_t)marker[s->matched]) s->matched++;
        if(s->matched==n) s->seen=1;
    }
}
