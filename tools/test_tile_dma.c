/* Verify recovered Tile DMA staging queue semantics using actual ROM bytes. */
#include "smk/algorithms.h"
#include "snes/cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Memory {
    uint8_t rom[0x80000];
    uint8_t ram[0x2000];
    bool invalid;
    unsigned data_writes;
} Memory;

static bool valid_address(uint32_t address) {
    /* Direct page */
    if ((address >= 0x2c && address <= 0x2d) ||
        (address >= 0x4a && address <= 0x4b)) return true;
    /* Return stack */
    if (address >= 0x1ffd && address <= 0x1fff) return true;
    /* RAM variables */
    if (address >= 0x12e && address <= 0x13f) return true;
    if (address >= 0x144 && address <= 0x145) return true;
    /* DMA staging buffer at $0EA0 */
    if (address >= 0xea0 && address <= 0x1000) return true;
    return false;
}

static uint32_t map_ram(uint32_t address) {
    uint8_t bank = (uint8_t)(address >> 16);
    uint16_t offset = (uint16_t)address;
    if (bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf) || bank == 0x7e) {
        if (offset < 0x2000) return offset;
    }
    return 0xffffffff;
}

static uint8_t read_byte(void *ctx, uint32_t address) {
    Memory *m = ctx;
    /* Code and table in bank $80 / LoROM mapping */
    if (address >= 0x808d83 && address <= 0x808e00)
        return m->rom[address & 0x7ffff];
    uint32_t mapped = map_ram(address);
    if (mapped != 0xffffffff && valid_address(mapped))
        return m->ram[mapped];
    m->invalid = true;
    return 0;
}

static void write_byte(void *ctx, uint32_t address, uint8_t value) {
    Memory *m = ctx;
    uint32_t mapped = map_ram(address);
    if (mapped != 0xffffffff) {
        if (mapped >= 0x1ffd && mapped <= 0x1fff) {
            m->ram[mapped] = value;
            return;
        }
        if ((mapped >= 0x4a && mapped <= 0x4b) ||
            (mapped >= 0x12e && mapped <= 0x12f) ||
            (mapped >= 0xea0 && mapped <= 0x1000)) {
            m->ram[mapped] = value;
            ++m->data_writes;
            return;
        }
    }
    m->invalid = true;
}



static void idle(void *ctx, bool waiting) { (void)ctx; (void)waiting; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

static bool check(Cpu *cpu, Memory *m, uint16_t guard_2c, uint16_t q_idx,
                  uint16_t limit, uint16_t buf_pos, uint16_t tile_id) {
    m->invalid = false;
    m->data_writes = 0;
    memset(m->ram, 0xaa, sizeof(m->ram));

    put16(&m->ram[0x2c], guard_2c);
    put16(&m->ram[0x4a], buf_pos);
    put16(&m->ram[0x12e], q_idx);
    put16(&m->ram[0x144], limit);
    put16(&m->ram[0x130 + (q_idx & 0x0e)], tile_id);
    put16(&m->ram[0x1ffe], 0xffff);

    /* Tables from ROM */
    uint16_t t_8dd1[8], t_8de1[8], t_8df1[8];
    for (int i = 0; i < 8; ++i) {
        t_8dd1[i] = get16(&m->rom[0x8dd1 + i * 2]);
        t_8de1[i] = get16(&m->rom[0x8de1 + i * 2]);
        t_8df1[i] = get16(&m->rom[0x8df1 + i * 2]);
    }

    uint16_t c_q_idx = q_idx;
    uint16_t c_buf_pos = buf_pos;
    SmkTileDmaEntry entries[2] = {0};
    bool queued = smk_queue_tile_dma(guard_2c, &c_q_idx, limit, &c_buf_pos,
                                    tile_id, t_8dd1, t_8de1, t_8df1, entries);

    cpu->a = 0x1234; cpu->x = 0x5678; cpu->y = 0x9abc;
    cpu->sp = 0x1ffd; cpu->pc = 0x8d83; cpu->dp = 0; cpu->k = 0x80; cpu->db = 0x80;
    cpu->c = cpu->z = cpu->n = cpu->v = false;
    cpu->i = true; cpu->d = cpu->xf = cpu->mf = cpu->e = false;
    cpu->waiting = cpu->stopped = cpu->resetWanted = false;
    cpu->irqWanted = cpu->nmiWanted = cpu->intWanted = false;

    unsigned steps = 0;
    while (!(cpu->k == 0x80 && cpu->pc == 0) && steps++ < 45) cpu_runOpcode(cpu);

    if (m->invalid || steps > 45 || cpu->sp != 0x1fff || cpu->dp != 0 ||
        cpu->db != 0x80 || cpu->mf || cpu->xf || cpu->e) {
        fprintf(stderr, "CPU failure guard=%04X q=%u limit=%u buf=%u tile=%u invalid=%d steps=%u\n",
                guard_2c, q_idx, limit, buf_pos, tile_id, m->invalid, steps);
        return false;
    }

    if (!queued) {
        if (m->data_writes != 0 || get16(&m->ram[0x12e]) != q_idx || get16(&m->ram[0x4a]) != buf_pos) {
            fprintf(stderr, "Skipped branch modified state: guard=%04X q=%u writes=%u\n",
                    guard_2c, q_idx, m->data_writes);
            return false;
        }
    } else {
        /* Taken branch: 12 bytes of descriptors + 2 bytes for DP $4A + 4 bytes for 2x INC $012E = 18 writes */
        if (m->data_writes != 18 || get16(&m->ram[0x12e]) != c_q_idx || get16(&m->ram[0x4a]) != c_buf_pos) {
            fprintf(stderr, "Taken branch state mismatch: q=%u c_q=%u ram_q=%u buf=%u c_buf=%u ram_buf=%u writes=%u\n",
                    q_idx, c_q_idx, get16(&m->ram[0x12e]), buf_pos, c_buf_pos, get16(&m->ram[0x4a]), m->data_writes);
            return false;
        }
        /* Verify staging buffer content at $0EA0 + buf_pos */
        const uint8_t *stg = &m->ram[0xea0 + buf_pos];
        if (get16(&stg[0]) != entries[0].vram_dest ||
            get16(&stg[2]) != entries[0].rom_src ||
            get16(&stg[4]) != entries[0].dma_param ||
            get16(&stg[6]) != entries[1].vram_dest ||
            get16(&stg[8]) != entries[1].rom_src ||
            get16(&stg[10]) != entries[1].dma_param) {
            fprintf(stderr, "Staging mismatch at offset %04X: q=%u tile=%u\n"
                            "  actual:   %04X %04X %04X %04X %04X %04X\n"
                            "  expected: %04X %04X %04X %04X %04X %04X\n",
                    buf_pos, q_idx, tile_id,
                    get16(&stg[0]), get16(&stg[2]), get16(&stg[4]),
                    get16(&stg[6]), get16(&stg[8]), get16(&stg[10]),
                    entries[0].vram_dest, entries[0].rom_src, entries[0].dma_param,
                    entries[1].vram_dest, entries[1].rom_src, entries[1].dma_param);
            return false;
        }

    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: test_tile_dma <US ROM>\n"); return 2; }
    Memory *m = calloc(1, sizeof(*m)); if (!m) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) { free(m); return 2; }
    fseek(f, 0, SEEK_END); long size = ftell(f);
    if (size != 0x80000 && size != 0x80200) { fclose(f); free(m); return 2; }
    fseek(f, size == 0x80200 ? 512 : 0, SEEK_SET);
    size_t bytes = fread(m->rom, 1, sizeof(m->rom), f); fclose(f);
    if (bytes != sizeof(m->rom)) { free(m); return 2; }
    Cpu *cpu = cpu_init(m, read_byte, write_byte, idle); if (!cpu) { free(m); return 2; }

    const uint16_t guards[] = {0, 1, 2, 0x0080, 0x0100, 0x7fff, 0x8000, 0xffff};
    const uint16_t limits[] = {0, 2, 4, 6, 8, 10, 12, 14, 16};
    const uint16_t q_indices[] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 0x7ffe, 0x8000, 0xfffe};
    const uint16_t buf_positions[] = {0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 0x00c0};
    const uint16_t tile_ids[] = {0, 2, 4, 6, 8, 10, 12, 14};




    unsigned long count = 0;
    for (unsigned g = 0; g < sizeof(guards)/sizeof(guards[0]); ++g) {
        for (unsigned l = 0; l < sizeof(limits)/sizeof(limits[0]); ++l) {
            for (unsigned q = 0; q < sizeof(q_indices)/sizeof(q_indices[0]); ++q) {
                for (unsigned b = 0; b < sizeof(buf_positions)/sizeof(buf_positions[0]); ++b) {
                    for (unsigned t = 0; t < sizeof(tile_ids)/sizeof(tile_ids[0]); ++t) {
                        if (!check(cpu, m, guards[g], q_indices[q], limits[l], buf_positions[b], tile_ids[t]))
                            goto failed;
                        ++count;
                    }
                }
            }
        }
    }

    printf("PASS: %lu ROM tile-DMA queue cases; all guard/limit/queue/buffer/tile permutations\n", count);
    cpu_free(cpu); free(m); return 0;
failed:
    cpu_free(cpu); free(m); return 1;
}
