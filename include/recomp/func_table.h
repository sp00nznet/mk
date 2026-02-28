#ifndef SMK_FUNC_TABLE_H
#define SMK_FUNC_TABLE_H

#include <stdint.h>
#include <stdbool.h>

/* All recompiled functions take no args and return void (use globals) */
typedef void (*smk_func_t)(void);

/* Initialize the function table */
void func_table_init(void);

/* Register a recompiled function at its SNES 24-bit address */
void func_table_register(uint32_t snes_addr, smk_func_t func);

/* Look up and call a function by SNES address. Returns true if found. */
bool func_table_call(uint32_t snes_addr);

/* Look up a function pointer (returns NULL if not registered) */
smk_func_t func_table_lookup(uint32_t snes_addr);

#endif /* SMK_FUNC_TABLE_H */
