#ifndef SMK_DIRECTION_H
#define SMK_DIRECTION_H

#include <stdbool.h>
#include <stdint.h>

typedef struct SmkDirectionResult {
    uint16_t angle;
    bool undefined; /* Original carry: true only for the zero vector. */
} SmkDirectionResult;

/* Semantic A/carry result of $81:F638 in native M=X=0, D=0 mode.
 * table is 4097 bytes beginning at original RAM $7F:8FFF. It is supplied by
 * the caller, not embedded game data. CPU scratch/register side effects and
 * interrupt/bus timing are deliberately outside this pure algorithm API.
 * table must point to at least 4097 stable caller-owned bytes. */
SmkDirectionResult smk_vector_direction(int16_t x, int16_t y,
                                       const uint8_t table[4097]);

typedef struct SmkCellDirectionResult {
    int16_t delta_x;
    int16_t delta_y;
    SmkDirectionResult direction;
} SmkCellDirectionResult;

/* $81:FD22: decode a 64x64 packed cell into a 16-unit cell center,
 * subtract that center from a supplied position with 16-bit wrapping, then
 * call the direction algorithm. Position array lookup is caller-owned. */
SmkCellDirectionResult smk_direction_from_cell(uint16_t cell, uint16_t point_x,
                                              uint16_t point_y,
                                              const uint8_t table[4097]);

/* Low accumulator byte of $81:F722, native 16-bit entry with D clear.
 * This uses a different axis convention and hardware-divider quantization
 * from F638. The original switches to 8-bit A/X on return; this pure API
 * returns only the angle and does not reproduce CPU or MMIO side effects.
 * table must point to at least 256 stable caller-owned bytes. */
uint8_t smk_vector_direction8(int16_t x, int16_t y, const uint8_t table[256]);

#endif
