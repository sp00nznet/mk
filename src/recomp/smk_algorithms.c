#include "smk/algorithms.h"

/* Recovered from the user-supplied ROM, not an original source listing.
 * Let H and L be the input bytes. The first stage stores (L, H xor L).
 * Then fold the original low byte into it and apply parity-dependent feedback.
 * See docs/binary_word_mix.md for the derivation and exhaustive ROM gate. */
SmkWordMixResult smk_mix_word(uint16_t state) {
    const uint16_t mixed_byte = (uint16_t)((state >> 8) ^ (state & 0xff));
    const uint16_t shuffled = (uint16_t)((state << 8) | mixed_byte);
    const uint16_t folded = (uint16_t)(shuffled ^ ((state & 0xff) << 1));
    uint16_t value = (uint16_t)((folded >> 1) ^ 0xff80);
    SmkWordMixResult result;

    if (folded & 1) {
        value ^= 0x8180;
        result.carry = true; /* LSR carry survives both EORs and STA. */
        result.zero = value == 0;
        result.negative = (value & 0x8000) != 0;
        result.state = value;
    } else {
        /* CMP, rather than the returned A, determines final N/Z/C here.
         * STZ does not change A or flags on the exceptional branch. */
        result.carry = value >= 0xaa55;
        result.zero = value == 0xaa55;
        result.negative = ((uint16_t)(value - 0xaa55) & 0x8000) != 0;
        result.state = result.zero ? 0 : value;
    }
    result.value = value;
    return result;
}

SmkDirectionResult smk_vector_direction(int16_t x, int16_t y,
                                       const uint8_t table[4097]) {
    SmkDirectionResult result = {0, false};
    if (!x) {
        result.undefined = !y;
        result.angle = y > 0 ? 0x8000 : 0;
        return result;
    }
    if (!y) {
        result.angle = x < 0 ? 0xc000 : 0x4000;
        return result;
    }

    /* Widen before negating: abs(-32768) must be 32768, not overflow. */
    uint32_t ax = x < 0 ? (uint32_t)-(int32_t)x : (uint32_t)x;
    uint32_t ay = y < 0 ? (uint32_t)-(int32_t)y : (uint32_t)y;
    const bool x_major = ay < ax; /* Equality goes through the Y-major table. */
    while (ax > 63 || ay > 63) { ax >>= 1; ay >>= 1; }
    const uint32_t index = x_major ? ay * 64 + ax : ax * 64 + ay;
    const uint16_t sample = (uint16_t)(table[index] | (uint16_t)table[index + 1] << 8);
    uint16_t angle;
    if (x_major) {
        if (x > 0) angle = y > 0 ? (uint16_t)(0x4000 + sample)
                                  : (uint16_t)(0x40ff - sample);
        else angle = y > 0 ? (uint16_t)(0xc0ff - sample)
                            : (uint16_t)(0xc000 + sample);
    } else {
        if (x > 0) angle = y > 0 ? (uint16_t)(0x80ff - sample) : sample;
        else angle = y > 0 ? (uint16_t)(0x8000 + sample)
                            : (uint16_t)(0 - (sample & 0xff00));
    }
    result.angle = (uint16_t)(angle & 0xff00);
    return result;
}

static int16_t signed_word(uint16_t value) {
    return (int16_t)(value <= 0x7fff ? (int32_t)value : (int32_t)value - 65536);
}

SmkCellDirectionResult smk_direction_from_cell(uint16_t cell, uint16_t point_x,
                                              uint16_t point_y,
                                              const uint8_t table[4097]) {
    const uint16_t center_x = (uint16_t)((cell & 63) * 16 + 8);
    const uint16_t center_y = (uint16_t)(((cell >> 6) & 63) * 16 + 8);
    SmkCellDirectionResult result;
    result.delta_x = signed_word((uint16_t)(point_x - center_x));
    result.delta_y = signed_word((uint16_t)(point_y - center_y));
    result.direction = smk_vector_direction(result.delta_x, result.delta_y, table);
    return result;
}

uint8_t smk_vector_direction8(int16_t x, int16_t y, const uint8_t table[256]) {
    if (!y) return x == 0 ? 0 : x < 0 ? 0xc0 : 0x40;
    if (!x) return y < 0 ? 0x80 : 0;
    const uint32_t ax = x < 0 ? (uint32_t)-(int32_t)x : (uint32_t)x;
    const uint32_t ay = y < 0 ? (uint32_t)-(int32_t)y : (uint32_t)y;
    const bool y_major = ax < ay;
    uint32_t major = y_major ? ay : ax;
    uint32_t minor = y_major ? ax : ay;
    uint16_t fraction = 0;
    while (major > 256) {
        major >>= 1;
        fraction = (uint16_t)((fraction >> 1) | ((minor & 1) << 15));
        minor >>= 1;
    }
    /* LDA $09 spans fraction's high byte and minor's low byte. The divider
     * takes only the low byte of major; 256 therefore means division by zero
     * ($FFFF), subsequently clamped to index 255 by the original routine. */
    const uint16_t dividend = (uint16_t)((minor << 8) | (fraction >> 8));
    const uint8_t divisor = (uint8_t)major;
    uint32_t index = divisor ? dividend / divisor : 0xffff;
    if (index > 255) index = 255;
    const bool reflect = y_major ? ((x < 0) != (y < 0)) : ((x < 0) == (y < 0));
    const uint8_t bias = y_major ? (y < 0 ? 0x80 : 0) : (x < 0 ? 0xc0 : 0x40);
    const uint8_t sample = table[index];
    return (uint8_t)((reflect ? (uint8_t)(0 - sample) : sample) + bias);
}

bool smk_project_coordinates(uint16_t mode, uint16_t flags,
                             uint32_t source_x, uint32_t source_y,
                             uint16_t *dest_x, uint16_t *dest_y) {
    if (mode != 2 && !(flags & 0x8000)) return false;
    *dest_x = (uint16_t)(source_x >> 14);
    *dest_y = (uint16_t)(source_y >> 14);
    return true;
}

size_t smk_repair_object_order(uint16_t *order, SmkOrderObject *objects, size_t start) {
    while (start > 0) {
        const uint16_t current = order[start];
        const uint16_t previous = order[start - 1];
        if (!current || !previous || objects[current].key <= objects[previous].key ||
            (objects[previous].flags & 0x20)) break;
        order[start] = previous;
        objects[previous].slot_offset = (uint16_t)(start * 2);
        order[start - 1] = current;
        objects[current].slot_offset = (uint16_t)((start - 1) * 2);
        --start;
    }
    return start;
}

bool smk_queue_tile_dma(uint16_t guard_2c,
                        uint16_t *p_queue_idx,
                        uint16_t queue_limit,
                        uint16_t *p_buffer_pos,
                        uint16_t tile_id,
                        const uint16_t table_8dd1[8],
                        const uint16_t table_8de1[8],
                        const uint16_t table_8df1[8],
                        SmkTileDmaEntry out_entries[2]) {
    if (guard_2c != 0) return false;
    if (!p_queue_idx) return false;
    const uint16_t q_idx = *p_queue_idx;
    if (q_idx >= queue_limit || q_idx >= 16) return false;

    const uint16_t vram_base = table_8dd1[q_idx >> 1];
    const uint16_t tile_idx = (tile_id >> 1) & 7;
    const uint16_t tile_src = (q_idx < 8) ? table_8de1[tile_idx] : table_8df1[tile_idx];

    if (out_entries) {
        out_entries[0].vram_dest = vram_base;
        out_entries[0].rom_src = tile_src;
        out_entries[0].dma_param = 0x407F;

        out_entries[1].vram_dest = (uint16_t)(vram_base + 0x0100);
        out_entries[1].rom_src = (uint16_t)(tile_src + 0x0200);
        out_entries[1].dma_param = 0x407F;
    }

    if (p_buffer_pos) {
        *p_buffer_pos = (uint16_t)(*p_buffer_pos + 12);
    }
    *p_queue_idx = (uint16_t)(q_idx + 2);
    return true;
}

