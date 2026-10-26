/* ROM-free fixed expectations: no floating point or copied reference algorithm. */
#include "smk/direction.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

static void expect(const char *name, unsigned actual, unsigned expected) {
    ++checks;
    if (actual != expected) {
        fprintf(stderr, "%s: got %04X, expected %04X\n", name, actual, expected);
        ++failures;
    }
}

int main(void) {
    /* Columns: zero table, FF table, table[i] = (73*i + (i>>4)) mod 256.
     * E.g. index 67 contains word $681F; byte index 85 contains $42.
     * These literal expectations retain the distinct axis conventions. */
    static const struct {
        int16_t x, y;
        uint16_t word[3];
        uint8_t byte[3];
    } vectors[] = {
        {0,0, {0,0,0}, {0,0,0}},
        {1,0, {0x4000,0x4000,0x4000}, {0x40,0x40,0x40}},
        {-1,0, {0xc000,0xc000,0xc000}, {0xc0,0xc0,0xc0}},
        {0,1, {0x8000,0x8000,0x8000}, {0,0,0}},
        {0,-1, {0,0,0}, {0x80,0x80,0x80}},
        {-32768,0, {0xc000,0xc000,0xc000}, {0xc0,0xc0,0xc0}},
        {0,-32768, {0,0,0}, {0x80,0x80,0x80}},
        {32767,0, {0x4000,0x4000,0x4000}, {0x40,0x40,0x40}},
        {0,32767, {0x8000,0x8000,0x8000}, {0,0,0}},
        {3,1, {0x4000,0x3f00,0xa800}, {0x40,0x41,0xfe}},
        {3,-1, {0x4000,0x4100,0xd800}, {0x40,0x3f,0x82}},
        {-3,1, {0xc000,0xc100,0x5800}, {0xc0,0xbf,0x02}},
        {-3,-1, {0xc000,0xbf00,0x2800}, {0xc0,0xc1,0x7e}},
        {1,3, {0x8000,0x8100,0x1800}, {0,0xff,0x42}},
        {1,-3, {0,0xff00,0x6800}, {0x80,0x81,0x3e}},
        {-1,3, {0x8000,0x7f00,0xe800}, {0,0x01,0xbe}},
        {-1,-3, {0,0x0100,0x9800}, {0x80,0x7f,0xc2}},
        {1,1, {0x8000,0x8100,0xaa00}, {0x40,0x41,0x7a}},
        {1,-1, {0,0xff00,0xd600}, {0x40,0x3f,0x06}},
        {-1,1, {0x8000,0x7f00,0x5600}, {0xc0,0xbf,0x86}},
        {-1,-1, {0,0x0100,0x2a00}, {0xc0,0xc1,0xfa}},
        {255,1, {0x4000,0x3f00,0x8400}, {0x40,0x41,0xf7}},
        {256,1, {0x4000,0x3f00,0xab00}, {0x40,0x41,0x7a}},
        {257,1, {0x4000,0x3f00,0xab00}, {0x40,0x41,0xf7}},
        {512,1, {0x4000,0x3f00,0xab00}, {0x40,0x41,0x7a}},
        {513,3, {0x4000,0x3f00,0xab00}, {0x40,0x41,0x7a}},
        {32767,1, {0x4000,0x3f00,0x8400}, {0x40,0x41,0x40}},
        {-32768,1, {0xc000,0xc100,0x5500}, {0xc0,0xbf,0x86}},
        {-32768,-32768, {0,0x0100,0x1500}, {0xc0,0xc1,0xfa}},
        {32767,32767, {0x8000,0x8100,0x8000}, {0x40,0x41,0x7a}},
        {-32768,32767, {0xc000,0xc100,0x1900}, {0xc0,0xbf,0x86}},
        {32767,-32768, {0,0xff00,0xa700}, {0x80,0x81,0xba}},
        {64,63, {0x4000,0x3f00,0xe700}, {0x40,0x41,0x55}},
        {63,64, {0x8000,0x8100,0xd900}, {0,0xff,0xeb}},
        {65,64, {0x4000,0x3f00,0x2b00}, {0x40,0x41,0x55}},
        {64,65, {0x8000,0x8100,0x9500}, {0,0xff,0xeb}},
        {64,64, {0x8000,0x8100,0x9500}, {0x40,0x41,0x7a}}
    };
    static const struct {
        uint16_t cell, px, py;
        int16_t dx, dy;
        uint16_t angle[3];
    } cells[] = {
        {0,8,8, 0,0, {0,0,0}},
        {0xf000,8,8, 0,0, {0,0,0}},
        {0xffff,1016,1016, 0,0, {0,0,0}},
        {0x003f,0,8, -1016,0, {0xc000,0xc000,0xc000}},
        {0,0xfff8,8, -16,0, {0xc000,0xc000,0xc000}},
        {0,0x8008,8, -32768,0, {0xc000,0xc000,0xc000}},
        {0,0x8007,8, 32767,0, {0x4000,0x4000,0x4000}},
        {0,0xffff,8, -9,0, {0xc000,0xc000,0xc000}},
        {0xffff,0xffff,0xffff, -1017,-1017, {0,0x0100,0}},
        {0xffff,0x83f8,0x83f8, -32768,-32768, {0,0x0100,0x1500}}
    };
    uint8_t table[4097], table8[256];
    char name[100];
    for (unsigned pattern = 0; pattern < 3; ++pattern) {
        for (unsigned j = 0; j < sizeof(table); ++j)
            table[j] = pattern == 0 ? 0 : pattern == 1 ? 255 : (uint8_t)(j * 73 + (j >> 4));
        memcpy(table8, table, sizeof(table8));
        for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
            SmkDirectionResult r = smk_vector_direction(vectors[v].x, vectors[v].y, table);
            snprintf(name, sizeof(name), "vector x=%d y=%d pattern=%u", vectors[v].x, vectors[v].y, pattern);
            expect(name, r.angle, vectors[v].word[pattern]);
            expect("undefined/carry", r.undefined, !vectors[v].x && !vectors[v].y);
            expect(name, smk_vector_direction8(vectors[v].x, vectors[v].y, table8), vectors[v].byte[pattern]);
        }
        for (unsigned c = 0; c < sizeof(cells) / sizeof(cells[0]); ++c) {
            SmkCellDirectionResult r = smk_direction_from_cell(cells[c].cell, cells[c].px, cells[c].py, table);
            expect("cell delta_x", (uint16_t)r.delta_x, (uint16_t)cells[c].dx);
            expect("cell delta_y", (uint16_t)r.delta_y, (uint16_t)cells[c].dy);
            expect("cell angle", r.direction.angle, cells[c].angle[pattern]);
            expect("cell undefined/carry", r.direction.undefined, !cells[c].dx && !cells[c].dy);
        }
        static const uint16_t expected[] = {0x8000, 0x8100, 0x1800};
        for (unsigned high = 0; high < 16; ++high) {
            SmkCellDirectionResult r = smk_direction_from_cell((uint16_t)((high << 12) | 0x41), 25, 27, table);
            expect("cell high nibble dx", (uint16_t)r.delta_x, 1);
            expect("cell high nibble dy", (uint16_t)r.delta_y, 3);
            expect("cell high nibble angle", r.direction.angle, expected[pattern]);
            expect("cell high nibble carry", r.direction.undefined, false);
        }
    }
    memset(table, 0, sizeof(table));
    table[65] = 0; table[66] = 0x12;
    expect("mask before negate, low=0", smk_vector_direction(-1,-1,table).angle, 0xee00);
    table[65] = 0xff;
    expect("mask before negate, low=FF", smk_vector_direction(-1,-1,table).angle, 0xee00);
    table[67] = 0x34; table[68] = 0x12;
    expect("little endian word", smk_vector_direction(3,1,table).angle, 0x5200);
    table[4095] = 0xaa; table[4096] = 0x5c;
    expect("last byte read", smk_vector_direction(63,63,table).angle, 0x2400);

    memset(table8, 0, sizeof(table8));
    table8[0] = 7; table8[1] = 9; table8[255] = 0x5a;
    expect("major=256 zero divisor/index255", smk_vector_direction8(256,1,table8), 0xe6);
    expect("shifted major=256", smk_vector_direction8(512,1,table8), 0xe6);
    expect("equal major=256", smk_vector_direction8(256,256,table8), 0xe6);
    expect("equal quotient clamp", smk_vector_direction8(1,1,table8), 0xe6);
    expect("fraction retained", smk_vector_direction8(257,1,table8), 0x37);
    expect("fraction truncated to zero", smk_vector_direction8(32767,1,table8), 0x39);
    printf("%s: %u fixed-expectation checks, %u failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
