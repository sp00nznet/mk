#ifndef SMK_ALGORITHMS_H
#define SMK_ALGORITHMS_H

#include <stdbool.h>
#include <stdint.h>

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

#endif
