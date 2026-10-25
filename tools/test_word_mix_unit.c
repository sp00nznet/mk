/* Hand-calculated examples of the documented mathematics, not ROM output.
 * This C17 check needs neither a ROM nor the hardware runtime. */
#include "smk/algorithms.h"
#include <stdio.h>

int main(void) {
    static const struct {
        uint16_t input;
        SmkWordMixResult expected;
    } cases[] = {
        /* input, stored state, returned A, C, Z, N */
        {0x0000, {0xff80, 0xff80, true,  false, false}},
        {0x0001, {0x7e81, 0x7e81, true,  false, false}},
        {0x0100, {0x7e00, 0x7e00, true,  false, false}},
        {0x00ff, {0x0100, 0x0100, true,  false, false}},
        {0xff00, {0x7e7f, 0x7e7f, true,  false, false}},
        {0x7fff, {0x80bf, 0x80bf, false, false, true }},
        {0x8000, {0xffc0, 0xffc0, true,  false, false}},
        {0xffff, {0x80ff, 0x80ff, false, false, true }},
        /* Returned values immediately above, at and below the CMP threshold. */
        {0x52aa, {0xaa56, 0xaa56, true,  false, false}},
        {0x54aa, {0x0000, 0xaa55, true,  true,  false}},
        {0x56aa, {0xaa54, 0xaa54, false, false, true }},
        /* The feedback path can return zero too, without a CMP. */
        {0x06fd, {0x0000, 0x0000, true,  true,  false}},
    };
    const size_t count = sizeof(cases) / sizeof(cases[0]);
    for (size_t i = 0; i < count; ++i) {
        SmkWordMixResult actual = smk_mix_word(cases[i].input);
        const SmkWordMixResult *expected = &cases[i].expected;
        if (actual.state != expected->state || actual.value != expected->value ||
            actual.carry != expected->carry || actual.zero != expected->zero ||
            actual.negative != expected->negative) {
            fprintf(stderr, "FAIL input=%04X state=%04X/%04X A=%04X/%04X "
                    "C=%d/%d Z=%d/%d N=%d/%d\n", cases[i].input,
                    actual.state, expected->state, actual.value, expected->value,
                    actual.carry, expected->carry, actual.zero, expected->zero,
                    actual.negative, expected->negative);
            return 1;
        }
    }
    printf("PASS: %zu word-mixing examples (all result fields)\n", count);
    return 0;
}
