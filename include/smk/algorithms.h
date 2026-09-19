#ifndef SMK_ALGORITHMS_H
#define SMK_ALGORITHMS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Semantic result of ROM $81:BB70 with a stable ordinary-RAM input at
 * DB:$1F26, native mode and a 16-bit accumulator. These are distinct: one
 * branch stores zero but returns A=$AA55. Other status bits are unchanged.
 * This pure function does not model bus phases or interrupt suspension. */
typedef struct SmkWordMixResult {
    uint16_t state;
    uint16_t value;
    bool carry;
    bool zero;
    bool negative;
} SmkWordMixResult;

SmkWordMixResult smk_mix_word(uint16_t state);

typedef struct SmkDirectionResult {
    uint16_t angle;
    bool undefined; /* Original carry: true only for the zero vector. */
} SmkDirectionResult;

/* Semantic A/carry result of $81:F638 in native M=X=0, D=0 mode.
 * table is 4097 bytes beginning at original RAM $7F:8FFF. It is supplied by
 * the caller, not embedded game data. CPU scratch/register side effects and
 * interrupt/bus timing are deliberately outside this pure algorithm API. */
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
 * returns only the angle and does not reproduce CPU or MMIO side effects. */
uint8_t smk_vector_direction8(int16_t x, int16_t y, const uint8_t table[256]);

/* Destination semantics of $80:86A0. If its guard is false, neither output
 * is written. Otherwise extract bits 14..29 of each source word pair.
 * CPU scratch/flags are outside this semantic API. Output pointers must be
 * valid and distinct when the guard is true. */
bool smk_project_coordinates(uint16_t mode, uint16_t flags,
                             uint32_t source_x, uint32_t source_y,
                             uint16_t *dest_x, uint16_t *dest_y);

typedef struct SmkOrderObject {
    uint16_t key;
    uint16_t flags;
    uint16_t slot_offset;
} SmkOrderObject;

/* Data semantics of $80:A027 with non-overlapping ordinary RAM fields.
 * Handles are caller-normalized object indices; zero is an empty sentinel.
 * start must be a valid order index <=32767, and all nonzero handles must be
 * valid objects. Returns the final index; reverse offsets are byte offsets
 * (2 * index), matching the ROM's word order table. No CPU effects modeled. */
size_t smk_repair_object_order(uint16_t *order, SmkOrderObject *objects, size_t start);

typedef struct SmkTileDmaEntry {
    uint16_t vram_dest;
    uint16_t rom_src;
    uint16_t dma_param;
} SmkTileDmaEntry;

/* Queue and data semantics of ROM $80:8D83 (sprite tile DMA staging builder).
 * If guard_2c != 0 or *p_queue_idx >= queue_limit, returns false without writes.
 * Otherwise appends two 6-byte DMA staging descriptors into out_entries,
 * increments *p_queue_idx by 2, advances *p_buffer_pos by 12, and returns true.
 * table_8dd1, table_8de1, table_8df1 are 8-word caller-supplied ROM tables.
 * CPU scratch/flags and bus timing are outside this pure algorithm API. */
bool smk_queue_tile_dma(uint16_t guard_2c,
                        uint16_t *p_queue_idx,
                        uint16_t queue_limit,
                        uint16_t *p_buffer_pos,
                        uint16_t tile_id,
                        const uint16_t table_8dd1[8],
                        const uint16_t table_8de1[8],
                        const uint16_t table_8df1[8],
                        SmkTileDmaEntry out_entries[2]);

#endif

