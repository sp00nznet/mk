#include "recomp/cpu.h"
#include <string.h>

SMKCpu g_cpu;

void cpu_reset(void) {
    memset(&g_cpu, 0, sizeof(g_cpu));

    /* Power-on defaults: emulation mode */
    g_cpu.flag_E = true;
    g_cpu.flag_M = true;   /* 8-bit accumulator */
    g_cpu.flag_X = true;   /* 8-bit index */
    g_cpu.flag_I = true;   /* IRQs disabled */
    g_cpu.S  = 0x01FF;     /* Stack at top of page 1 */
    g_cpu.DP = 0x0000;     /* Direct Page at zero */
    g_cpu.DB = 0x00;
    g_cpu.PB = 0x00;
}
