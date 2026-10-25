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
