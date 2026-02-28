#include "hal/apu.h"
#include <string.h>

SMKApu g_apu;

void apu_init(void) {
    memset(&g_apu, 0, sizeof(g_apu));
    /* SPC700 initial port values (IPL boot) */
    g_apu.port_in[0] = 0xAA;
    g_apu.port_in[1] = 0xBB;
}

void apu_write(uint16_t addr, uint8_t val) {
    uint8_t port = (uint8_t)(addr - APU_PORT0) & 0x03;
    g_apu.port_out[port] = val;
}

uint8_t apu_read(uint16_t addr) {
    uint8_t port = (uint8_t)(addr - APU_PORT0) & 0x03;
    return g_apu.port_in[port];
}
