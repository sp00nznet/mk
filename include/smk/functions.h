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
void smk_80843C(void);  /* Joypad reading (auto-joypad → WRAM $0020+) */
void smk_80853D(void);  /* Title screen input handler */
void smk_808174(void);  /* State handler for state $14 (mode select) */
void smk_808369(void);  /* NMI state handler for state $14 */
void smk_8080CA(void);  /* State handler for state $06 (character select) */
void smk_80824D(void);  /* NMI state handler for state $06 */

/* === Bank $85 === */
void smk_858000(void);  /* Title screen sprite/palette/OAM setup */
void smk_858045(void);  /* Per-frame sprite update */
void smk_85809B(void);  /* BG scroll + HDMA trigger */
void smk_8591DE(void);  /* Mode select display setup */
void smk_85915F(void);  /* Mode select tile DMA + palette loading */
void smk_859239(void);  /* Mode select HDMA/sprite slot init */
void smk_85909B(void);  /* Mode select transition init */
void smk_8590B1(void);  /* Mode select per-frame logic */
void smk_8590D7(void);  /* Mode select NMI rendering */

/* === Bank $84 === */
void smk_84E09E(void);  /* VRAM data loader (data-driven DMA) */
void smk_84F38C(void);  /* PPU/display full reset */
void smk_84F421(void);  /* Viewport/HDMA parameter setup */
void smk_84F45A(void);  /* PPU Mode 0 register setup */
void smk_84FCF1(void);  /* SRAM checksum validation */
void smk_84FD25(void);  /* "PUSH START" text blink + menu */

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
void smk_81E576(void);  /* Sprite tile decompression + 2bpp→4bpp interleave */
void smk_81CB35(void);  /* NMI sprite tile DMA (stub) */
void smk_81E126(void);  /* Transition handler: character select init */
void smk_81E398(void);  /* Transition handler: mode select init */

#endif /* SMK_FUNCTIONS_H */
