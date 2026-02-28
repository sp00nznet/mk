#ifndef SMK_APU_H
#define SMK_APU_H

#include <stdint.h>

/* APU communication ports ($2140-$2143) */
#define APU_PORT0  0x2140
#define APU_PORT1  0x2141
#define APU_PORT2  0x2142
#define APU_PORT3  0x2143

typedef struct SMKApu {
    /* CPU-side I/O ports (what the 65816 writes/reads) */
    uint8_t port_out[4];  /* CPU -> SPC700 */
    uint8_t port_in[4];   /* SPC700 -> CPU */
} SMKApu;

extern SMKApu g_apu;

void    apu_init(void);
void    apu_write(uint16_t addr, uint8_t val);
uint8_t apu_read(uint16_t addr);

#endif /* SMK_APU_H */
