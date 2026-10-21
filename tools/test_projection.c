/* Verify recovered destination semantics using actual $80:86A0 ROM bytes. */
#include "smk/algorithms.h"
#include "snes/cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Memory {
    uint8_t rom[0x80000];
    uint8_t ram[0x2000];
    bool invalid;
    unsigned destination_writes;
} Memory;

static uint8_t read_byte(void *ctx, uint32_t address) {
    Memory *m = ctx;
    if (address >= 0x8086a0 && address <= 0x8086cb)
        return m->rom[address & 0x7ffff];
    if (address >= 0x7e0216 && address <= 0x7e021d)
        return m->ram[address & 0xffff];
    if (address <= 1 || (address >= 0x1ffe && address <= 0x1fff) ||
        (address >= 0x1a4 && address <= 0x1a5) ||
        (address >= 0x1ac && address <= 0x1ad)) return m->ram[address];
    m->invalid = true; return 0;
}
static void write_byte(void *ctx, uint32_t address, uint8_t value) {
    Memory *m = ctx;
    if (address <= 1) m->ram[address] = value;
    else if ((address >= 0x188 && address <= 0x189) ||
             (address >= 0x18c && address <= 0x18d)) {
        m->ram[address] = value; ++m->destination_writes;
    } else m->invalid = true;
}
static void idle(void *ctx, bool waiting) { (void)ctx; (void)waiting; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }

static bool check(Cpu *cpu, Memory *m, uint16_t mode, uint16_t flags, uint32_t x, uint32_t y) {
    m->invalid = false; m->destination_writes = 0;
    put16(&m->ram[0x1a4], mode); put16(&m->ram[0x1ac], flags);
    put16(&m->ram[0x216], (uint16_t)x); put16(&m->ram[0x218], (uint16_t)(x >> 16));
    put16(&m->ram[0x21a], (uint16_t)y); put16(&m->ram[0x21c], (uint16_t)(y >> 16));
    put16(&m->ram[0x188], 0xa55a); put16(&m->ram[0x18c], 0x5aa5);
    put16(&m->ram[0x1ffe], 0xffff);
    cpu->a = 0x1234; cpu->x = 0x100; cpu->y = 0x200;
    cpu->sp = 0x1ffd; cpu->pc = 0x86a0; cpu->dp = 0; cpu->k = 0x80; cpu->db = 0x7e;
    cpu->c = cpu->z = cpu->n = cpu->v = (x & 1) != 0;
    cpu->i = true; cpu->d = cpu->xf = cpu->mf = cpu->e = false;
    cpu->waiting = cpu->stopped = cpu->resetWanted = false;
    cpu->irqWanted = cpu->nmiWanted = cpu->intWanted = false;
    unsigned steps = 0;
    while (!(cpu->k == 0x80 && cpu->pc == 0) && steps++ < 40) cpu_runOpcode(cpu);
    uint16_t dx = 0xa55a, dy = 0x5aa5;
    bool changed = smk_project_coordinates(mode, flags, x, y, &dx, &dy);
    if (m->invalid || steps > 40 || m->destination_writes != (changed ? 4u : 0u) ||
        get16(&m->ram[0x188]) != dx || get16(&m->ram[0x18c]) != dy ||
        cpu->x != 0x100 || cpu->y != 0x200 || cpu->dp || cpu->db != 0x7e ||
        cpu->sp != 0x1fff || cpu->mf || cpu->xf || cpu->e) {
        fprintf(stderr, "Projection mismatch mode=%04X flags=%04X x=%08X y=%08X invalid=%d\n",
                mode, flags, x, y, m->invalid); return false;
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: test_projection <US ROM>\n"); return 2; }
    Memory *m = calloc(1, sizeof(*m));
    if (!m) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { free(m); return 2; }
    fseek(f, 0, SEEK_END); long size = ftell(f);
    if (size != 0x80000 && size != 0x80200) { fclose(f); free(m); return 2; }
    fseek(f, size == 0x80200 ? 512 : 0, SEEK_SET);
    size_t bytes = fread(m->rom, 1, sizeof(m->rom), f); fclose(f);
    if (bytes != sizeof(m->rom)) { free(m); return 2; }
    Cpu *cpu = cpu_init(m, read_byte, write_byte, idle);
    if (!cpu) { free(m); return 2; }
    const uint16_t lows[] = {0,1,0x3fff,0x4000,0x7fff,0x8000,0xbfff,0xffff};
    const uint16_t modes[] = {0,1,2,3,0xffff,0,1,2};
    const uint16_t flags[] = {0,0x7fff,0,0x8000,0xffff,0x8000,0xffff,0x7fff};
    unsigned long count = 0;
    for (uint32_t high = 0; high < 65536; ++high)
        for (unsigned low = 0; low < 8; ++low)
            for (unsigned guard = 0; guard < 8; ++guard) {
                uint32_t x = (high << 16) | lows[low];
                uint32_t y = ((high ^ 0xaaaa) << 16) | lows[7-low];
                if (!check(cpu, m, modes[guard], flags[guard], x, y)) goto failed;
                ++count;
            }
    printf("PASS: %lu ROM projection cases; every high word, low-bit boundaries and 8 guards\n", count);
    cpu_free(cpu); free(m); return 0;
failed:
    cpu_free(cpu); free(m); return 1;
}
