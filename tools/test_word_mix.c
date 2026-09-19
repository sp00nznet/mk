/* Execute actual supplied ROM bytes with LakeSnes's CPU, comparing the pure
 * recovered C algorithm for every input word and two initial flag patterns.
 * No ROM bytes or reference output data are embedded in this executable. */
#include "smk/algorithms.h"
#include "snes/cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestMemory {
    uint8_t rom[0x80000];
    uint8_t ram[0x2000];
    unsigned writes;
    bool invalid;
} TestMemory;

static uint8_t read_byte(void *ctx, uint32_t address) {
    TestMemory *m = ctx;
    unsigned bank = address >> 16, offset = address & 0xffff;
    if (bank == 0x81 && offset >= 0xbb70 && offset <= 0xbb9b)
        return m->rom[0x10000 + offset];
    if ((bank == 0 || bank == 0x7e) && offset < sizeof(m->ram))
        return m->ram[offset];
    m->invalid = true;
    return 0;
}

static void write_byte(void *ctx, uint32_t address, uint8_t value) {
    TestMemory *m = ctx;
    if (address == 0x7e1f26 || address == 0x7e1f27) {
        m->ram[address & 0xffff] = value;
        ++m->writes;
    } else m->invalid = true;
}

static void idle(void *ctx, bool waiting) { (void)ctx; (void)waiting; }

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: test_word_mix <US ROM>\n"); return 2; }
    TestMemory *m = calloc(1, sizeof(*m));
    if (!m) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { free(m); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size != 0x80000 && size != 0x80200) {
        fprintf(stderr, "Expected 512 KiB ROM with optional copier header\n");
        fclose(f); free(m); return 2;
    }
    fseek(f, size == 0x80200 ? 512 : 0, SEEK_SET);
    size_t bytes = fread(m->rom, 1, sizeof(m->rom), f);
    fclose(f);
    if (bytes != sizeof(m->rom)) { free(m); return 2; }
    Cpu *cpu = cpu_init(m, read_byte, write_byte, idle);
    if (!cpu) { free(m); return 2; }
    unsigned exceptional = 0;
    for (unsigned flags = 0; flags < 2; ++flags) {
        for (unsigned word = 0; word < 65536; ++word) {
            memset(m->ram, 0, sizeof(m->ram));
            m->ram[0x1f26] = (uint8_t)word;
            m->ram[0x1f27] = (uint8_t)(word >> 8);
            /* RTL returns from $00:1FFD..1FFF to $7E:0000. */
            m->ram[0x1ffd] = 0xff;
            m->ram[0x1ffe] = 0xff;
            m->ram[0x1fff] = 0x7e;
            m->writes = 0; m->invalid = false;
            cpu->a = (uint16_t)~word; cpu->x = 0x1234; cpu->y = 0x5678;
            cpu->sp = 0x1ffc; cpu->pc = 0xbb70; cpu->dp = 0x3456;
            cpu->k = 0x81; cpu->db = 0x7e;
            cpu->c = cpu->z = cpu->n = cpu->v = cpu->d = cpu->i = flags != 0;
            cpu->xf = cpu->mf = cpu->e = false;
            cpu->waiting = cpu->stopped = cpu->resetWanted = false;
            cpu->irqWanted = cpu->nmiWanted = cpu->intWanted = false;
            unsigned steps = 0;
            while (!(cpu->k == 0x7e && cpu->pc == 0) && steps++ < 32)
                cpu_runOpcode(cpu);
            SmkWordMixResult r = smk_mix_word((uint16_t)word);
            uint16_t stored = (uint16_t)(m->ram[0x1f26] | m->ram[0x1f27] << 8);
            if (m->invalid || steps > 32 || m->writes != 4 ||
                cpu->a != r.value || stored != r.state || cpu->c != r.carry ||
                cpu->z != r.zero || cpu->n != r.negative ||
                cpu->v != (flags != 0) || cpu->d != (flags != 0) ||
                cpu->i != (flags != 0) || cpu->x != 0x1234 || cpu->y != 0x5678 ||
                cpu->dp != 0x3456 || cpu->db != 0x7e || cpu->sp != 0x1fff ||
                cpu->e || cpu->mf || cpu->xf) {
                fprintf(stderr, "Mismatch input=%04X flags=%u A=%04X/%04X state=%04X/%04X\n",
                        word, flags, cpu->a, r.value, stored, r.state);
                cpu_free(cpu); free(m); return 1;
            }
            if (!flags && r.state != r.value) ++exceptional;
        }
    }
    printf("PASS: 131072 ROM executions; all 65536 inputs, two flag patterns; "
           "%u exceptional input(s)\n", exceptional);
    cpu_free(cpu); free(m); return 0;
}
