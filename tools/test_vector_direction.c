/* Original ROM CPU execution versus the semantic direction algorithm.
 * Synthetic table contents deliberately exercise word reads, wrapping and
 * every octant without redistributing the game's runtime table.
 * Migrated from Zaxaerith/mk e30516b and 7cb1fff. Default: bounded sample.
 * Only --exhaustive enables the historical dense grid/full-range sweeps. */
#include "smk/direction.h"
#include "snes/cpu.h"
#include "snes/snes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Memory {
    uint8_t rom[0x80000];
    uint8_t ram[0x20000];
    bool invalid;
    uint32_t invalid_address;
    unsigned long divides, zero_divides, last_index_reads;
    Snes *hardware;
} Memory;

static uint8_t read_byte(void *ctx, uint32_t address) {
    Memory *m = ctx;
    if ((address >= 0x81f638 && address <= 0x81f71d) ||
        (address >= 0x81fd22 && address <= 0x81fd5c) ||
        (address >= 0x81f722 && address <= 0x81f7d2))
        return m->rom[address & 0x7ffff];
    if (address <= 0x15 || (address >= 0x1ff0 && address <= 0x1fff))
        return m->ram[address];
    if ((address >= 0x7e0900 && address <= 0x7e0901) ||
        (address >= 0x7e0a00 && address <= 0x7e0a01))
        return m->ram[address & 0xffff];
    if (address >= 0x7f8fff && address <= 0x7f9fff)
        return m->ram[address - 0x7e0000];
    if (address >= 0x7f0000 && address <= 0x7f00ff) {
        if (address == 0x7f00ff) ++m->last_index_reads;
        return m->ram[address - 0x7e0000];
    }
    if (address == 0x4214 || address == 0x4215)
        return snes_read(m->hardware, address);
    if (!m->invalid) m->invalid_address = address;
    m->invalid = true;
    return 0;
}

static void write_byte(void *ctx, uint32_t address, uint8_t value) {
    Memory *m = ctx;
    if (address <= 0x15 || (address >= 0x1ff0 && address <= 0x1fff))
        m->ram[address] = value;
    else if (address >= 0x4204 && address <= 0x4207) {
        if (address == 0x4206) {
            ++m->divides;
            if (!value) ++m->zero_divides;
        }
        snes_write(m->hardware, address, value);
    } else {
        if (!m->invalid) m->invalid_address = address;
        m->invalid = true;
    }
}

static void idle(void *ctx, bool waiting) { (void)ctx; (void)waiting; }

static bool check(Cpu *cpu, Memory *m, int16_t x, int16_t y, unsigned pattern) {
    m->invalid = false;
    memset(m->ram, 0xa5, 4);
    m->ram[0x1ffd] = 0xff; m->ram[0x1ffe] = 0xff; m->ram[0x1fff] = 0x7e;
    cpu->a = 0x1234; cpu->x = (uint16_t)x; cpu->y = (uint16_t)y;
    cpu->sp = 0x1ffc; cpu->pc = 0xf638; cpu->dp = 0;
    cpu->k = 0x81; cpu->db = 0x7e;
    cpu->c = cpu->z = cpu->n = cpu->v = (pattern & 1) != 0;
    cpu->i = true; cpu->d = cpu->xf = cpu->mf = cpu->e = false;
    cpu->waiting = cpu->stopped = cpu->resetWanted = false;
    cpu->irqWanted = cpu->nmiWanted = cpu->intWanted = false;
    unsigned steps = 0;
    while (!(cpu->k == 0x7e && cpu->pc == 0) && steps++ < 160)
        cpu_runOpcode(cpu);
    SmkDirectionResult r = smk_vector_direction(x, y, &m->ram[0x18fff]);
    if (m->invalid || steps > 160 || cpu->a != r.angle || cpu->c != r.undefined ||
        cpu->sp != 0x1fff) {
        fprintf(stderr, "Mismatch x=%d y=%d pattern=%u A=%04X/%04X C=%d/%d invalid=%d steps=%u\n",
                x, y, pattern, cpu->a, r.angle, cpu->c, r.undefined, m->invalid, steps);
        return false;
    }
    return true;
}

static bool check_cell(Cpu *cpu, Memory *m, uint16_t cell, uint16_t px, uint16_t py) {
    m->invalid = false;
    memset(m->ram, 0xa5, 0x16);
    m->ram[0x900] = (uint8_t)px; m->ram[0x901] = (uint8_t)(px >> 8);
    m->ram[0xa00] = (uint8_t)py; m->ram[0xa01] = (uint8_t)(py >> 8);
    m->ram[0x1ffe] = 0xff; m->ram[0x1fff] = 0xff;
    cpu->a = 0x1234; cpu->x = cell; cpu->y = 0;
    cpu->sp = 0x1ffd; cpu->pc = 0xfd22; cpu->dp = 0;
    cpu->k = 0x81; cpu->db = 0x7e;
    cpu->c = cpu->z = cpu->n = cpu->v = true;
    cpu->i = true; cpu->d = cpu->xf = cpu->mf = cpu->e = false;
    cpu->waiting = cpu->stopped = cpu->resetWanted = false;
    cpu->irqWanted = cpu->nmiWanted = cpu->intWanted = false;
    unsigned steps = 0;
    while (!(cpu->k == 0x81 && cpu->pc == 0) && steps++ < 210)
        cpu_runOpcode(cpu);
    SmkCellDirectionResult r = smk_direction_from_cell(cell, px, py, &m->ram[0x18fff]);
    uint16_t dx = (uint16_t)(m->ram[0x12] | (uint16_t)m->ram[0x13] << 8);
    uint16_t dy = (uint16_t)(m->ram[0x14] | (uint16_t)m->ram[0x15] << 8);
    if (m->invalid || steps > 210 || cpu->a != r.direction.angle ||
        cpu->c != r.direction.undefined || dx != (uint16_t)r.delta_x ||
        dy != (uint16_t)r.delta_y || cpu->sp != 0x1fff) {
        fprintf(stderr, "Cell mismatch cell=%04X pos=%04X,%04X A=%04X/%04X invalid=%d steps=%u\n",
                cell, px, py, cpu->a, r.direction.angle, m->invalid, steps);
        return false;
    }
    return true;
}

static bool check_byte(Cpu *cpu, Memory *m, int16_t x, int16_t y) {
    m->invalid = false;
    memset(m->ram, 0xa5, 0x16);
    m->ram[0x1ffe] = 0xff; m->ram[0x1fff] = 0xff;
    cpu->a = 0x1234; cpu->x = (uint16_t)x; cpu->y = (uint16_t)y;
    cpu->sp = 0x1ffd; cpu->pc = 0xf722; cpu->dp = 0;
    cpu->k = 0x81; cpu->db = 0;
    cpu->c = cpu->z = cpu->n = cpu->v = true;
    cpu->i = true; cpu->d = cpu->xf = cpu->mf = cpu->e = false;
    cpu->waiting = cpu->stopped = cpu->resetWanted = false;
    cpu->irqWanted = cpu->nmiWanted = cpu->intWanted = false;
    unsigned steps = 0;
    while (!(cpu->k == 0x81 && cpu->pc == 0) && steps++ < 180)
        cpu_runOpcode(cpu);
    uint8_t expected = smk_vector_direction8(x, y, &m->ram[0x10000]);
    if (m->invalid || steps > 180 || (uint8_t)cpu->a != expected ||
        cpu->sp != 0x1fff) {
        fprintf(stderr, "Byte direction mismatch x=%d y=%d A=%02X/%02X invalid=%d steps=%u\n",
                x, y, (uint8_t)cpu->a, expected, m->invalid, steps);
        return false;
    }
    return true;
}

typedef struct Counts {
    unsigned long vector, cell, byte;
} Counts;

static bool check_pair(Cpu *cpu, Memory *m, int16_t x, int16_t y,
                       unsigned pattern, Counts *counts) {
    if (!check(cpu, m, x, y, pattern)) return false;
    ++counts->vector;
    if (!check_byte(cpu, m, x, y)) return false;
    ++counts->byte;
    return true;
}

static bool check_cell_points(Cpu *cpu, Memory *m, uint16_t cell, Counts *counts) {
    const uint16_t cx = (uint16_t)((cell & 63) * 16 + 8);
    const uint16_t cy = (uint16_t)(((cell >> 6) & 63) * 16 + 8);
    const uint16_t points[][2] = {
        {0,0}, {65535,65535}, {32768,32767}, {cx,cy},
        {cx,(uint16_t)(cy+1)}, {(uint16_t)(cx-1),cy},
        {(uint16_t)(cx+32768),(uint16_t)(cy-32768)},
        {(uint16_t)(cell*73u),(uint16_t)(cell*137u)}
    };
    for (unsigned p = 0; p < sizeof(points) / sizeof(points[0]); ++p) {
        if (!check_cell(cpu, m, cell, points[p][0], points[p][1])) return false;
        ++counts->cell;
    }
    return true;
}

int main(int argc, char **argv) {
    bool exhaustive = false;
    const char *rom_path;
    if (argc == 2 && strncmp(argv[1], "--", 2) != 0) {
        rom_path = argv[1];
    } else if (argc == 3 &&
               (!strcmp(argv[1], "--sample") || !strcmp(argv[1], "--exhaustive"))) {
        exhaustive = !strcmp(argv[1], "--exhaustive");
        rom_path = argv[2];
    } else {
        fprintf(stderr, "Usage: test_vector_direction [--sample|--exhaustive] <US v1.0 ROM>\n");
        return 2;
    }
    Memory *m = calloc(1, sizeof(*m));
    if (!m) return 2;
    FILE *f = fopen(rom_path, "rb");
    if (!f) { perror(rom_path); free(m); return 2; }
    fseek(f, 0, SEEK_END); long size = ftell(f);
    if (size != 0x80000 && size != 0x80200) {
        fprintf(stderr, "Expected 512 KiB US v1.0 ROM, optional 512-byte copier header\n");
        fclose(f); free(m); return 2;
    }
    fseek(f, size == 0x80200 ? 512 : 0, SEEK_SET);
    size_t bytes = fread(m->rom, 1, sizeof(m->rom), f); fclose(f);
    if (bytes != sizeof(m->rom)) { free(m); return 2; }
    Cpu *cpu = cpu_init(m, read_byte, write_byte, idle);
    if (!cpu) { free(m); return 2; }
    m->hardware = snes_init();
    if (!m->hardware) { cpu_free(cpu); free(m); return 2; }
    snes_reset(m->hardware, true);
    Counts counts = {0, 0, 0};
    const int16_t edges[] = {-32768,-32767,-16384,-8192,-256,-64,-63,-1,0,1,63,64,256,8192,16384,32767};
    const int16_t sample[] = {
        -32768,-32767,-513,-512,-257,-256,-255,-65,-64,-63,-3,-1,
        0,1,3,63,64,65,255,256,257,512,513,32767
    };
    const uint16_t cells[] = {
        0,1,63,64,65,4095,0xf000,0xf001,0xf03f,0xf040,0xf041,0xffff
    };
    for (unsigned pattern = 0; pattern < 3; ++pattern) {
        for (unsigned j = 0; j < 4097; ++j)
            m->ram[0x18fff + j] = pattern == 0 ? 0 : pattern == 1 ? 255 : (uint8_t)(j * 73 + (j >> 4));
        for (unsigned j = 0; j < 256; ++j)
            m->ram[0x10000 + j] = pattern == 0 ? 0 : pattern == 1 ? 255 : (uint8_t)(j * 73 + (j >> 4));
        if (!exhaustive) {
            for (unsigned x = 0; x < sizeof(sample) / sizeof(sample[0]); ++x)
                for (unsigned y = 0; y < sizeof(sample) / sizeof(sample[0]); ++y)
                    if (!check_pair(cpu, m, sample[x], sample[y], pattern, &counts)) goto failed;
            for (unsigned c = 0; c < sizeof(cells) / sizeof(cells[0]); ++c)
                if (!check_cell_points(cpu, m, cells[c], &counts)) goto failed;
            continue;
        }
        for (int x = -128; x <= 128; ++x)
            for (int y = -128; y <= 128; ++y) {
                if (!check_pair(cpu, m, (int16_t)x, (int16_t)y, pattern, &counts)) goto failed;
            }
        for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i)
            for (int v = -32768; v <= 32767; ++v) {
                if (!check_pair(cpu, m, edges[i], (int16_t)v, pattern, &counts) ||
                    !check_pair(cpu, m, (int16_t)v, edges[i], pattern, &counts)) goto failed;
            }
        for (unsigned cell = 0; cell < 65536; ++cell) {
            if (!check_cell_points(cpu, m, (uint16_t)cell, &counts)) goto failed;
        }
    }
    printf("PASS (%s, 3 synthetic tables): %lu vector + %lu cell + %lu byte = %lu ROM executions\n",
           exhaustive ? "exhaustive" : "sample", counts.vector, counts.cell, counts.byte,
           counts.vector + counts.cell + counts.byte);
    printf("LakeSnes divider writes: %lu, zero divisors: %lu; byte table index255 reads: %lu\n",
           m->divides, m->zero_divides, m->last_index_reads);
    snes_free(m->hardware); cpu_free(cpu); free(m); return 0;
failed:
    fprintf(stderr, "Stopped: %lu vector, %lu cell, %lu byte passed; first unexpected address=%06X\n",
            counts.vector, counts.cell, counts.byte, m->invalid_address);
    snes_free(m->hardware); cpu_free(cpu); free(m); return 1;
}
