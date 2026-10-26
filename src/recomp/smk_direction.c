#include "smk/direction.h"

/* Independently recovered semantics, not original Nintendo source.
 * Migrated from Zaxaerith/mk commits e30516b and 7cb1fff; see
 * docs/binary_direction.md for the derivation and validation boundaries. */
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
