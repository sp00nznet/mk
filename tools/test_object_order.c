/* Actual ROM $80:A027 against the recovered adjacent-order repair. */
#include "smk/algorithms.h"
#include "snes/cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Memory {
    uint8_t rom[0x80000], ram[0x2000];
    bool invalid;
} Memory;
static bool data_address(uint32_t address) {
    if (address >= 0x10e && address < 0x11c) return true;
    if (address >= 0x1ff0 && address <= 0x1fff) return true;
    if (address >= 0x400 && address < 0xb00) {
        unsigned field = address & 0xff;
        return field == 0x10 || field == 0x11 || field == 0xc0 || field == 0xc1 ||
               field == 0xe6 || field == 0xe7;
    }
    return false;
}
static uint8_t read_byte(void *ctx, uint32_t address) {
    Memory *m = ctx;
    if (address >= 0x80a027 && address <= 0x80a05b)
        return m->rom[address & 0x7ffff];
    uint32_t mapped = address >= 0x7e0000 && address < 0x7e2000 ? address - 0x7e0000 : address;
    if (data_address(mapped)) return m->ram[mapped];
    m->invalid = true; return 0;
}
static void write_byte(void *ctx, uint32_t address, uint8_t value) {
    Memory *m = ctx;
    uint32_t mapped = address >= 0x7e0000 && address < 0x7e2000 ? address - 0x7e0000 : address;
    if (data_address(mapped)) m->ram[mapped] = value;
    else m->invalid = true;
}
static void idle(void *ctx, bool waiting) { (void)ctx; (void)waiting; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t handle(uint16_t id) { return id ? (uint16_t)((id + 3) * 256) : 0; }

static bool check(Cpu *cpu, Memory *m, const uint16_t input[7], unsigned start,
                  unsigned key_pattern, unsigned flag_pattern, unsigned hole) {
    SmkOrderObject objects[8] = {0};
    uint16_t order[7]; uint8_t expected[0x2000];
    memset(m->ram, 0xa5, sizeof(m->ram));
    m->invalid = false;
    for (unsigned id = 1; id <= 7; ++id) {
        objects[id].key = key_pattern == 0 ? (uint16_t)id : key_pattern == 1 ? (uint16_t)(8-id) :
                          key_pattern == 2 ? 0x8000 : (uint16_t)(id * 21845u);
        objects[id].flags = flag_pattern == 0 ? 0 : flag_pattern == 1 ? 0x20 :
                            flag_pattern == 2 ? (uint16_t)((id & 1) ? 0x20 : 0) : 0xffdf;
        objects[id].slot_offset = (uint16_t)(0x7000 + id);
    }
    for (unsigned i = 0; i < 7; ++i) {
        order[i] = hole == i ? 0 : input[i];
        if (order[i]) objects[order[i]].slot_offset = (uint16_t)(i * 2);
        put16(&m->ram[0x10e+i*2], handle(order[i]));
    }
    for (unsigned id = 1; id <= 7; ++id) {
        put16(&m->ram[handle((uint16_t)id)+0xc0], objects[id].key);
        put16(&m->ram[handle((uint16_t)id)+0x10], objects[id].flags);
        put16(&m->ram[handle((uint16_t)id)+0xe6], objects[id].slot_offset);
    }
    put16(&m->ram[0x1ffe], 0xffff);
    memcpy(expected, m->ram, sizeof(expected));
    size_t end = smk_repair_object_order(order, objects, start);
    for (unsigned i = 0; i < 7; ++i) put16(&expected[0x10e+i*2], handle(order[i]));
    for (unsigned id = 1; id <= 7; ++id)
        put16(&expected[handle((uint16_t)id)+0xe6], objects[id].slot_offset);
    cpu->a = 0x1234; cpu->x = 0x5678; cpu->y = (uint16_t)(start * 2);
    cpu->sp = 0x1ffd; cpu->pc = 0xa027; cpu->dp = 0; cpu->k = 0x80; cpu->db = 0x7e;
    cpu->c = cpu->z = cpu->n = cpu->v = true;
    cpu->i = true; cpu->d = cpu->xf = cpu->mf = cpu->e = false;
    cpu->waiting = cpu->stopped = cpu->resetWanted = false;
    cpu->irqWanted = cpu->nmiWanted = cpu->intWanted = false;
    unsigned steps = 0;
    while (!(cpu->k == 0x80 && cpu->pc == 0) && steps++ < 240) cpu_runOpcode(cpu);
    /* Compare all data RAM; only dead stack bytes may differ. */
    if (m->invalid || steps > 240 || memcmp(expected, m->ram, 0x1ff0) ||
        cpu->y != end * 2 || cpu->sp != 0x1fff || cpu->dp || cpu->db != 0x7e ||
        cpu->mf || cpu->xf || cpu->e) {
        fprintf(stderr, "Order mismatch start=%u key=%u flags=%u hole=%u invalid=%d steps=%u\n",
                start, key_pattern, flag_pattern, hole, m->invalid, steps); return false;
    }
    return true;
}
static bool next_permutation(uint16_t a[7]) {
    int i = 5;
    while (i >= 0 && a[i] >= a[i+1]) --i;
    if (i < 0) return false;
    int j = 6; while (a[j] <= a[i]) --j;
    uint16_t t = a[i]; a[i] = a[j]; a[j] = t;
    for (int l = i+1, r = 6; l < r; ++l, --r) { t=a[l]; a[l]=a[r]; a[r]=t; }
    return true;
}
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: test_object_order <US ROM>\n"); return 2; }
    Memory *m = calloc(1, sizeof(*m)); if (!m) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) { free(m); return 2; }
    fseek(f, 0, SEEK_END); long size = ftell(f);
    if (size != 0x80000 && size != 0x80200) { fclose(f); free(m); return 2; }
    fseek(f, size == 0x80200 ? 512 : 0, SEEK_SET);
    size_t bytes = fread(m->rom, 1, sizeof(m->rom), f); fclose(f);
    if (bytes != sizeof(m->rom)) { free(m); return 2; }
    Cpu *cpu = cpu_init(m, read_byte, write_byte, idle); if (!cpu) { free(m); return 2; }
    unsigned long count = 0;
    uint16_t order[7] = {1,2,3,4,5,6,7};
    do {
        for (unsigned start = 0; start < 7; ++start)
            for (unsigned keys = 0; keys < 4; ++keys)
                for (unsigned flags = 0; flags < 4; ++flags) {
                    if (!check(cpu,m,order,start,keys,flags,7)) goto failed;
                    ++count;
                }
        for (unsigned hole = 0; hole < 7; ++hole)
            for (unsigned start = 0; start < 7; ++start) {
                if (!check(cpu,m,order,start,0,0,hole)) goto failed;
                ++count;
            }
    } while (next_permutation(order));
    printf("PASS: %lu ROM order-repair cases; all 7-object permutations, keys, guards and sentinel positions\n", count);
    cpu_free(cpu); free(m); return 0;
failed:
    cpu_free(cpu); free(m); return 1;
}
