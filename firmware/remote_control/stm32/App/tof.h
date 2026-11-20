#ifndef TREE_TOF_H
#define TREE_TOF_H
#include <stdint.h>
/* UART type 0x90+index; left=mm (-1 invalid), right=status.
 * 0 valid, 1..15 VL6180X range error; 256 bus,257 timeout,258 model,
 * 259 boot/initializing,260 below module's declared 20 mm blind zone. */
void tof_init(void);
void tof_tick(uint32_t now);
int tof_report(uint32_t now, unsigned *index, int16_t *mm, int16_t *status, uint16_t *seq);
#endif
