#ifndef SMK_SEMANTIC_VERIFY_H
#define SMK_SEMANTIC_VERIFY_H
#include <stdbool.h>
#include <stdint.h>
typedef struct SmkSemanticCheck {
    uint32_t address;
    uint16_t expected;
    bool carry;
    bool active;
} SmkSemanticCheck;
SmkSemanticCheck smk_semantic_begin(uint32_t address);
void smk_semantic_end(const SmkSemanticCheck *check);
bool smk_semantic_report(void);
#endif
