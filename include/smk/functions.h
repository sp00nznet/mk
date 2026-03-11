/*
 * Super Mario Kart — Recompiled function declarations
 */
#ifndef SMK_FUNCTIONS_H
#define SMK_FUNCTIONS_H

/* Register all recompiled functions in the function table */
void smk_register_all(void);

/* === Bank $80 === */
void smk_80FF70(void);  /* Reset vector */
void smk_80803A(void);  /* Hardware init + main loop entry */
void smk_808056(void);  /* Main loop (one iteration) */
void smk_808000(void);  /* NMI handler */
void smk_808BEA(void);  /* PPU register init + DMA tile load */

void smk_808067(void);  /* State handler for state $02 */
void smk_8080BA(void);  /* State handler for state $04 (title screen) */
void smk_808096(void);  /* Null state handler (RTS) */
void smk_8081DD(void);  /* NMI state handler for state $00/$1A */
void smk_808237(void);  /* NMI state handler for state $04 */

void smk_80B181(void);  /* NMI brightness/fade handler */
void smk_80946E(void);  /* OAM DMA transfer */
void smk_8081B5(void);  /* NMI cleanup: audio + input + misc */

/* === Bank $85 === */
void smk_858000(void);  /* Title screen sprite/palette/OAM setup */
void smk_85809B(void);  /* BG scroll + HDMA trigger */

/* === Bank $84 === */
void smk_84E09E(void);  /* VRAM data loader (data-driven DMA) */
void smk_84F38C(void);  /* PPU/display full reset */
void smk_84FCF1(void);  /* SRAM checksum validation */

/* === Bank $81 === */
void smk_81E000(void);  /* Full initialization */
void smk_81E067(void);  /* Frame setup / transition handler */
void smk_81F60A(void);  /* WRAM DMA clear utility */
void smk_81E0AD(void);  /* Transition handler: title screen init */
void smk_81E50D(void);  /* PPU register setup for title screen */
void smk_81E10A(void);  /* Load title screen tiles to VRAM */
void smk_81E118(void);  /* Load title screen tilemap to VRAM */
void smk_81E584(void);  /* Load title screen palette data */
void smk_81E933(void);  /* Title screen VRAM DMA transfers */

#endif /* SMK_FUNCTIONS_H */
