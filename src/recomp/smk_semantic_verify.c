#include "smk/semantic_verify.h"
#include "smk/algorithms.h"
#include <snesrecomp/cpu.h>
#include <snesrecomp/bus.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Counts { unsigned long entered, completed, unsupported, mismatches; } Counts;
static Counts counts[2];
static int enabled = -1;

static bool active(void) {
    if (enabled < 0) {
        const char *value = getenv("SMK_SEMANTIC_VERIFY");
        enabled = value && atoi(value) != 0;
    }
    return enabled != 0;
}
static int16_t as_signed(uint16_t word) {
    return (int16_t)(word <= 0x7fff ? (int32_t)word : (int32_t)word - 65536);
}

SmkSemanticCheck smk_semantic_begin(uint32_t address) {
    SmkSemanticCheck check = {0};
    if (!active() || (address != 0x81f638 && address != 0x81f722)) return check;
    unsigned slot = address == 0x81f722;
    if (g_cpu.flag_E || g_cpu.flag_M || g_cpu.flag_X || g_cpu.flag_D) {
        ++counts[slot].unsupported;
        return check;
    }
    const uint8_t *ram = bus_get_wram();
    if (!ram) { ++counts[slot].unsupported; return check; }
    check.address = address; check.active = true;
    ++counts[slot].entered;
    /* Direct host reads: do not fetch through the emulated bus or alter its
     * clock/open bus. Each generated invocation owns its own check token. */
    if (slot) {
        check.expected = smk_vector_direction8(as_signed(g_cpu.X), as_signed(g_cpu.Y), ram + 0x10000);
    } else {
        SmkDirectionResult r = smk_vector_direction(as_signed(g_cpu.X), as_signed(g_cpu.Y), ram + 0x18fff);
        check.expected = r.angle; check.carry = r.undefined;
    }
    return check;
}

void smk_semantic_end(const SmkSemanticCheck *check) {
    if (!check->active) return;
    unsigned slot = check->address == 0x81f722;
    ++counts[slot].completed;
    uint16_t actual = slot ? (uint8_t)g_cpu.C : g_cpu.C;
    if (actual != check->expected || (!slot && g_cpu.flag_C != check->carry)) {
        if (++counts[slot].mismatches <= 8)
            fprintf(stderr, "semantic mismatch $%06X A=%04X expected=%04X C=%d expected=%d\n",
                    check->address, actual, check->expected, g_cpu.flag_C, check->carry);
    }
}

bool smk_semantic_report(void) {
    if (!active()) return true;
    for (unsigned i = 0; i < 2; ++i)
        printf("SEMANTIC %06X entered=%lu completed=%lu unsupported=%lu mismatches=%lu\n",
               i ? 0x81f722 : 0x81f638, counts[i].entered, counts[i].completed,
               counts[i].unsupported, counts[i].mismatches);
    return counts[0].mismatches == 0 && counts[1].mismatches == 0;
}
