/*
 * Super Mario Kart — Initialization subroutines (bank $81)
 *
 * These handle WRAM clearing, PPU setup, APU init, and other
 * one-time startup tasks called from $81:E000.
 */

#include "smk/functions.h"
#include "smk/cpu_ops.h"
#include <snesrecomp/snesrecomp.h>
#include <string.h>
#include <stdio.h>

/*
 * $81:E000 — Full initialization
 *
 * Called once from $80:803A after basic hardware registers are set.
 * Clears WRAM, sets up PPU, initializes audio, configures interrupts.
 *
 * Original:
 *   PHB / PHK / PLB   ; DB = $81
 *   REP #$30          ; 16-bit
 *   JSR $E404         ; WRAM clear + SRAM validation
 *   JSL $808BEA       ; PPU register setup + tile DMA
 *   JSR $F4D9         ; APU initialization
 *   JSR $E3EA         ; DSP-1 init
 *   JSR $F7E1         ; Additional init
 *   JSL $81E576       ; ?
 *   JSL $81E4B3       ; ?
 *   JSL $00FF93       ; ? (trivial: PHB/PHK/PLB/PLB/RTL)
 *   LDA #$0F00 / STA $48   ; brightness control
 *   LDA #$001A / STA $36   ; initial game state = $1A
 *   SEP #$20
 *   CLI
 *   LDA #$B1 / STA $4200   ; enable NMI + auto-joypad + V-IRQ
 *   REP #$30
 *   LDA #$0004 / STA $32   ; transition state
 *   LDA #$8F00 / STA $48   ; force blank + brightness
 *   LDA #$0060 / STA $015E ; ?
 *   PLB / RTL
 */
void smk_81E000(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x81);
    op_rep(0x30);   /* 16-bit A and X/Y */

    /* JSR $E404 — WRAM clear + SRAM validation */
    /* Clear WRAM $7E:2000-$7E:FFFF and all of $7F:0000-$7F:FFFF via DMA */
    {
        uint8_t *wram = bus_get_wram();
        if (wram) {
            /* Clear $7E:2000 through end of WRAM (skip first 8KB = DP/stack area) */
            memset(wram + 0x2000, 0, 0x1E000);  /* $7E:2000-$7E:FFFF */
            memset(wram + 0x10000, 0, 0x10000);  /* $7F:0000-$7F:FFFF */
        }
        /* SRAM validation from E404 — read save data checksums */
        /* For now, let the default zero state stand (no save data) */
        printf("smk: WRAM cleared\n");
    }

    /* JSL $808BEA — PPU register setup + DMA tile data */
    smk_808BEA();

    /* JSR $F4D9 — APU initialization */
    /* Writes to APU ports $2140-$2143 to initialize SPC700 */
    /* The actual SPC700 in LakeSnes handles this via port communication */
    {
        /* Initial APU handshake: write $CC to $2140 */
        /* For now, skip — LakeSnes APU is already initialized from reset */
        printf("smk: APU init (using LakeSnes SPC700)\n");
    }

    /* JSR $E3EA — DSP-1 initialization */
    /* Writes $80 to $006000 128 times to reset DSP-1 */
    {
        for (int i = 0; i < 128; i++) {
            bus_write8(0x00, 0x6000, 0x80);
        }
        printf("smk: DSP-1 initialized\n");
    }

    /* JSR $F7E1 — Additional init (checks $0140, inits DP vars) */
    /* Skip for now — initializes based on save state that doesn't exist yet */

    /* JSL $81E576 — Sprite tile decompression + 2bpp→4bpp interleave */
    smk_81E576();

    /* JSL $81E4B3 — Mode 7 tile setup */
    /* Skip for now — Mode 7 not yet needed */

    /* JSL $00FF93 — trivial (PHB/PHK/PLB/PLB/RTL) */
    /* Does nothing useful */

    /* Now set up the key DP variables */
    op_rep(0x30);   /* 16-bit */

    /* LDA #$0F00 / STA $48 — brightness control word */
    op_lda_imm16(0x0F00);
    op_sta_dp16(0x48);

    /* LDA #$001A / STA $36 — initial game state index */
    op_lda_imm16(0x001A);
    op_sta_dp16(0x36);

    /* SEP #$20 — 8-bit A */
    op_sep(0x20);

    /* CLI — enable interrupts */
    OP_CLI();

    /* LDA #$B1 / STA $4200 — enable NMI + auto-joypad read + V-IRQ */
    op_lda_imm8(0xB1);
    op_sta_long8(0x00, 0x4200);

    /* REP #$30 — back to 16-bit */
    op_rep(0x30);

    /* LDA #$0004 / STA $32 — transition/init state */
    op_lda_imm16(0x0004);
    op_sta_dp16(0x32);

    /* LDA #$8F00 / STA $48 — force blank active */
    op_lda_imm16(0x8F00);
    op_sta_dp16(0x48);

    /* LDA #$0060 / STA $015E */
    op_lda_imm16(0x0060);
    op_sta_abs16(0x015E);

    /* PLB — restore DB */
    g_cpu.DB = saved_db;

    printf("smk: initialization complete (state=$%04X)\n",
           bus_wram_read16(g_cpu.DP + 0x36));
}

/*
 * $80:8BEA — PPU register initialization + DMA tile data load
 *
 * Sets up PPU registers (OBJ base, mosaic, windows, main/sub screen,
 * color math, SETINI) then DMAs tile data from ROM to VRAM.
 */
void smk_808BEA(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x80);

    /* SEP #$30 — 8-bit A and X/Y */
    op_sep(0x30);

    /* OBSEL = $02 (8x8/16x16 sprites, name base addr $4000) */
    op_lda_imm8(0x02);
    op_sta_abs8(0x2101);

    /* Clear mosaic, windows, SETINI, color math */
    op_stz_abs8(0x2106);  /* MOSAIC = 0 */
    op_stz_abs8(0x2123);  /* W12SEL = 0 */
    op_stz_abs8(0x2124);  /* W34SEL = 0 */
    op_stz_abs8(0x2125);  /* WOBJSEL = 0 */

    /* TM = $10 (BG4+OBJ on main screen... wait, $10 = OBJ only) */
    /* Actually $10 = bit 4 = OBJ layer enabled on main screen */
    op_lda_imm8(0x10);
    op_sta_abs8(0x212C);  /* TM */
    op_sta_abs8(0x212D);  /* TS */
    op_stz_abs8(0x212E);  /* TMW = 0 */
    op_stz_abs8(0x212F);  /* TSW = 0 */
    op_stz_abs8(0x2133);  /* SETINI = 0 */
    op_stz_abs8(0x2130);  /* CGWSEL = 0 */
    op_stz_abs8(0x2131);  /* CGADSUB = 0 */
    op_stz_abs8(0x2132);  /* COLDATA = 0 */

    /* REP #$30 — 16-bit */
    op_rep(0x30);

    /* Set up VRAM for DMA: VMAIN=$80 (word access, increment on high),
     * VMADD=$4000 */
    op_lda_imm16(0x0080);
    op_sta_abs16(0x2115);   /* VMAIN */
    op_lda_imm16(0x4000);
    op_sta_abs16(0x2116);   /* VMADDL/H */

    /* Set up DMA channel 0 to transfer tile data:
     * $4300 = $1801 (mode 1: write to $2118/$2119, A-bus increment)
     * $4302 = $C500 (source addr low/high)
     * $4304 = $0084 (source bank $84, but only low byte matters for bank)
     *   wait — $4304 should be source bank. Let me re-read.
     *   STA $4304 stores 16-bit: low byte = bank ($84), high byte = size low
     *   Actually: $4302-$4304 = source address (24-bit). $4305-$4306 = size.
     *   So: $4300=$01 (ctrl), $4301=$18 (B-bus dest),
     *       $4302=$00, $4303=$C5, $4304=$84 → source = $84:C500
     *       $4305=$00, $4306=$04 → size = $0400
     */

    /* DMA channel 0 control + dest */
    op_lda_imm16(0x1801);
    op_sta_abs16(0x4300);   /* ctrl=$01, dest=$18 */

    /* DMA source address */
    op_lda_imm16(0xC500);
    op_sta_abs16(0x4302);   /* src low=$00, src high=$C5 */
    op_lda_imm16(0x0084);
    op_sta_abs16(0x4304);   /* src bank=$84 (low byte) */

    /* DMA size */
    op_lda_imm16(0x0400);
    op_sta_abs16(0x4305);   /* size = $0400 (1024 bytes) */

    /* Trigger DMA channel 0 */
    op_ldx_imm16(0x0001);
    op_stx_abs16(0x420B);   /* MDMAEN = $01 */

    /* The rest of $80:8BEA calls further subroutines to load more
     * tile data, palettes, etc. We'll add those incrementally.
     * For now, the basic PPU setup is done. */

    printf("smk: PPU registers initialized, font tiles DMA'd to VRAM\n");

    g_cpu.DB = saved_db;
}

/*
 * $81:E067 — Frame setup / transition handler
 *
 * Checks DP $32 for pending transitions. If $32 != 0, runs the
 * transition logic. Otherwise returns.
 *
 * For the initial boot, $32 = $0004, which triggers the title screen
 * initialization sequence.
 */
void smk_81E067(void) {
    uint8_t saved_db = g_cpu.DB;

    /* LDA $32 / BEQ -> RTL (no transition pending) */
    uint16_t transition = bus_wram_read16(g_cpu.DP + 0x32);
    if (transition == 0) {
        g_cpu.DB = saved_db;
        return;
    }

    /* There's a pending transition. The original code does:
     *   LDA $0160 / CMP #$00 / BRA ...
     *   Then: REP #$30 / PHB / PHK / PLB
     *   STZ $420B / STZ $420C
     *   LDA $36 / STA $0106  (save old state)
     *   STZ $36 / STZ $D0
     *   SEI / STZ $4200      (disable NMI)
     *   REP #$30
     *   JSR $F3AA             (cleanup)
     *   LDX $32 / JSR ($E049,x)  (transition handler)
     *   LDA #$0001 / STA $48
     *   LDA $32 / STA $36    (new state = transition)
     *   STZ $32              (clear transition)
     *   SEP #$30 / CLI
     *   LDA #$B1 / STA $4200 (re-enable NMI)
     *   REP #$30 / PLB / RTL
     */

    OP_SET_DB(0x81);
    op_rep(0x30);

    /* Disable DMA/HDMA */
    op_stz_abs8(0x420B);
    op_stz_abs8(0x420C);

    /* Save current state, clear state */
    uint16_t old_state = bus_wram_read16(g_cpu.DP + 0x36);
    bus_wram_write16(0x0106, old_state);
    bus_wram_write16(g_cpu.DP + 0x36, 0);
    bus_wram_write16(g_cpu.DP + 0xD0, 0);

    /* Disable NMI */
    OP_SEI();
    op_stz_abs8(0x4200);

    op_rep(0x30);

    /* Call transition handler via table at $E049 */
    {
        uint16_t handler = bus_read16(0x81, 0xE049 + transition);
        uint32_t full_addr = 0x810000 | handler;
        if (!func_table_call(full_addr)) {
            printf("smk: transition handler $%06X not yet recompiled (state=$%04X)\n",
                   full_addr, transition);
        }
    }

    /* Set brightness, transfer state */
    bus_wram_write16(g_cpu.DP + 0x48, 0x0001);
    bus_wram_write16(g_cpu.DP + 0x36, transition);
    bus_wram_write16(g_cpu.DP + 0x32, 0);

    /* Re-enable NMI */
    op_sep(0x30);
    OP_CLI();
    op_lda_imm8(0xB1);
    op_sta_long8(0x00, 0x4200);
    op_rep(0x30);

    g_cpu.DB = saved_db;
}

/*
 * $81:E126 — Transition handler for state $06 (character select)
 *
 * Original:
 *   JSR $E118    ; load tilemap
 *   JSL $85909B  ; character select init
 *   RTS
 */
void smk_81E126(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x81);
    op_rep(0x30);

    smk_81E118();
    smk_85909B();

    g_cpu.DB = saved_db;
}

/*
 * $81:E398 — Transition handler for state $14 (mode select)
 *
 * Original:
 *   JSR $E3A0    ; graphics loading + setup
 *   JSL $088BF5  ; PPU init
 *   RTS
 *
 * $E3A0 calls:
 *   JSR $E68E → JSR $E6B9 (decompress $C4:0000 → $7F:0000)
 *             → DMA $7F:0070 → VRAM $3000 (4KB, VMDATAH)
 *   JSR $EC5E → mode config lookup
 *   JSR $E627 → more graphics loading
 */
void smk_81E398(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x81);
    op_rep(0x30);

    /* === $E3A0 sub-routine === */

    /* JSR $E68E — Initial decompress + DMA (first pass) */
    /* Decompress $C4:0000 → $7F:0000, post-process → $7F:7000,
     * DMA $7F:0070 → VRAM $3000 (4KB, high bytes) */
    g_cpu.Y = 0x0000;
    CPU_SET_A16(0x00C4);
    g_cpu.X = 0x0000;
    smk_84E09E();

    op_rep(0x30);
    bus_write16(0x81, 0x2115, 0x0080);
    bus_write16(0x81, 0x2116, 0x3000);
    bus_write16(0x81, 0x4300, 0x0000);
    bus_write16(0x81, 0x4301, 0x0019);
    bus_write16(0x81, 0x4303, 0x7F70);
    bus_write16(0x81, 0x4305, 0x1000);
    bus_write16(0x81, 0x420B, 0x0001);

    /* JSR $EC5E — Mode config lookup (set $0126 from table) */
    {
        uint16_t idx = bus_wram_read16(0x0124);
        uint8_t val = bus_read8(0x81, 0xEC2F + idx);
        bus_wram_write16(0x0126, val);
        printf("smk: EC5E config lookup — $0124=%04X → $0126=%02X\n", idx, val);
    }

    /* JSR $E627 — Full graphics loading chain */
    smk_81E627();

    /* === End of $E3A0 === */

    /* JSL $088BF5 — PPU register init
     * Clears windows, color math. We keep the register clears but override
     * TM to $1F (all layers) since E627 loaded all BG data. */
    op_sep(0x30);

    bus_write8(0x80, 0x2123, 0x00);  /* W12SEL */
    bus_write8(0x80, 0x2124, 0x00);  /* W34SEL */
    bus_write8(0x80, 0x2125, 0x00);  /* WOBJSEL */
    bus_write8(0x80, 0x212E, 0x00);  /* TMW */
    bus_write8(0x80, 0x212F, 0x00);  /* TSW */
    bus_write8(0x80, 0x2133, 0x00);  /* SETINI */
    bus_write8(0x80, 0x2130, 0x00);  /* CGWSEL */
    bus_write8(0x80, 0x2131, 0x00);  /* CGADSUB */
    bus_write8(0x80, 0x2132, 0x00);  /* COLDATA */

    /* DMA font tiles from $84:C500 → VRAM $4000 (1KB) */
    op_rep(0x30);
    bus_write16(0x80, 0x2115, 0x0080);
    bus_write16(0x80, 0x2116, 0x4000);
    bus_write16(0x80, 0x4300, 0x1801);
    bus_write16(0x80, 0x4302, 0xC500);
    bus_write16(0x80, 0x4304, 0x0084);
    bus_write16(0x80, 0x4305, 0x0400);
    bus_write16(0x80, 0x420B, 0x0001);

    /* OAM DMA */
    smk_80946E();

    /* Configure PPU Mode 0 with all BG layers enabled.
     * E627 VRAM layout analysis:
     *   $0000-$3FFF: tilemap data (low bytes from E7B5, high bytes from E769#1/E68E)
     *                4 × 32×32 tilemaps at $0000/$0800/$1000/$1800
     *   $4000-$43FF: font tiles (from $088BF5 DMA above)
     *   $7000-$7FFF: 2bpp tile character data (E769 DMA #2 + E7DA/E7F6 patches)
     * DF48 put additional BG config at $7E:C000 but our simplified main handler
     * doesn't transfer it, so we configure BG registers directly. */
    /* Decompress the CORRECT CGRAM palette (from $8C1A main handler flow).
     * The transition's E72E decompresses $C4:$117F → $7E:C000 (512 bytes).
     * But the main handler decompresses $C4:$1313 → $7E:$3A80 for the
     * actual display palette. We need to do BOTH. */
    {
        extern void sub_df48(uint16_t out_start, uint16_t data_ptr, uint8_t data_bank);
        sub_df48(0x3A80, 0x1313, 0xC4);

        /* DMA 512 bytes from $7E:3A80 → CGRAM (same as $80:8413) */
        op_rep(0x30);
        bus_write16(0x80, 0x4305, 0x0200);  /* size = 512 */
        bus_write16(0x80, 0x4302, 0x3A80);  /* src lo=$80, hi=$3A */
        bus_write16(0x80, 0x4300, 0x2202);  /* ctrl=$02 (1-reg), dest=$22 (CGDATA) */
        op_sep(0x20);
        bus_write8(0x80, 0x2121, 0x00);     /* CGADD = 0 */
        bus_write8(0x80, 0x4304, 0x7E);     /* bank = $7E */
        bus_write8(0x80, 0x420B, 0x01);     /* trigger DMA ch0 */
        op_rep(0x30);
        printf("smk: loaded CGRAM from $7E:3A80 (main handler palette)\n");
    }

    /* Original game uses TM=$10 (OBJ only) for mode select!
     * The text and cursor are rendered as sprites, not BG layers.
     * E627 pre-loads graphics for later screens (character select).
     * Set up 4 cursor sprites from $8C7D table (same as original $8C1A). */
    op_sep(0x20);
    bus_write8(0x80, 0x212C, 0x10);  /* TM: OBJ only (original value) */
    bus_write8(0x80, 0x212D, 0x00);  /* TS: none */
    bus_write8(0x80, 0x2101, 0x02);  /* OBSEL: 8x8/16x16 sprites */
    bus_write8(0x80, 0x2115, 0x80);  /* VMAIN */

    /* Set up 4 OAM sprites from $8C7D table (cursor/text indicators).
     * Original: 4 entries × 4 bytes at $8C7D → OAM mirror $0200.
     * Sprites at Y=$70, X=$60/$70/$80/$90, tiles 0/2/4/6, attr $30 (pal 6). */
    op_rep(0x30);
    {
        static const uint8_t oam_data[16] = {
            0x60, 0x70, 0x00, 0x30,  /* sprite 0: X=$60, Y=$70, tile=$00, pal6 */
            0x70, 0x70, 0x02, 0x30,  /* sprite 1: X=$70, Y=$70, tile=$02, pal6 */
            0x80, 0x70, 0x04, 0x30,  /* sprite 2: X=$80, Y=$70, tile=$04, pal6 */
            0x90, 0x70, 0x06, 0x30,  /* sprite 3: X=$90, Y=$70, tile=$06, pal6 */
        };
        uint8_t *wram = bus_get_wram();
        if (wram) {
            for (int i = 0; i < 16; i++)
                wram[0x0200 + i] = oam_data[i];
            /* OAM high table: $55AA pattern (original sets this) */
            wram[0x0400] = 0xAA;
            wram[0x0401] = 0x55;
        }
    }

    /* OAM DMA + brightness */
    smk_80946E();
    op_sep(0x20);
    bus_write8(0x80, 0x2100, 0x0F);  /* Full brightness */
    op_rep(0x30);

    g_cpu.DB = saved_db;
    printf("smk: mode select transition (state $14) complete\n");
}

/* Register all recompiled functions */
void smk_register_all(void) {
    func_table_register(0x80FF70, smk_80FF70);
    func_table_register(0x80803A, smk_80803A);
    func_table_register(0x808056, smk_808056);
    func_table_register(0x808000, smk_808000);
    func_table_register(0x808BEA, smk_808BEA);
    func_table_register(0x81E000, smk_81E000);
    func_table_register(0x81E067, smk_81E067);

    /* NMI sub-calls */
    func_table_register(0x80B181, smk_80B181);  /* brightness/fade */
    func_table_register(0x80946E, smk_80946E);  /* OAM DMA */
    func_table_register(0x85809B, smk_85809B);  /* BG scroll + HDMA */
    func_table_register(0x8081B5, smk_8081B5);  /* audio/input/misc */

    /* State handlers */
    func_table_register(0x808067, smk_808067);
    func_table_register(0x8080BA, smk_8080BA);  /* state $04 (title screen) */
    func_table_register(0x808096, smk_808096);  /* null state (RTS) */
    func_table_register(0x8081DD, smk_8081DD);  /* NMI state $00/$1A */
    func_table_register(0x808237, smk_808237);  /* NMI state $04 */

    /* Transition handlers */
    func_table_register(0x81E0AD, smk_81E0AD);  /* title screen init */
    func_table_register(0x81E50D, smk_81E50D);  /* PPU setup for title */
    func_table_register(0x81E10A, smk_81E10A);  /* title tiles */
    func_table_register(0x81E118, smk_81E118);  /* title tilemap */
    func_table_register(0x81E584, smk_81E584);  /* title palette data */
    func_table_register(0x81E933, smk_81E933);  /* title VRAM DMA */
    func_table_register(0x81E576, smk_81E576);  /* sprite tile interleave */
    func_table_register(0x84E09E, smk_84E09E);  /* VRAM data loader */
    func_table_register(0x84F38C, smk_84F38C);  /* PPU reset */
    func_table_register(0x84FCF1, smk_84FCF1);  /* SRAM checksum */
    func_table_register(0x84FD25, smk_84FD25);  /* PUSH START text */
    func_table_register(0x858000, smk_858000);  /* title gfx setup */
    func_table_register(0x858045, smk_858045);  /* per-frame sprite update */
    func_table_register(0x81CB35, smk_81CB35);  /* NMI sprite tile DMA (stub) */

    /* Joypad + title input */
    func_table_register(0x80843C, smk_80843C);  /* joypad reading */
    func_table_register(0x80853D, smk_80853D);  /* title screen input */

    /* State $14 — mode select */
    func_table_register(0x81E398, smk_81E398);  /* transition $14 handler */
    func_table_register(0x81E627, smk_81E627);  /* mode select graphics chain */
    func_table_register(0x808174, smk_808174);  /* state $14 main handler */
    func_table_register(0x808369, smk_808369);  /* NMI state $14 */

    /* State $06 — character select */
    func_table_register(0x81E126, smk_81E126);  /* transition $06 handler */
    func_table_register(0x8080CA, smk_8080CA);  /* state $06 main handler */
    func_table_register(0x80824D, smk_80824D);  /* NMI state $06 */
    func_table_register(0x84F421, smk_84F421);  /* viewport/HDMA setup */
    func_table_register(0x84F45A, smk_84F45A);  /* PPU Mode 0 setup */
    func_table_register(0x8591DE, smk_8591DE);  /* mode select display setup */
    func_table_register(0x85915F, smk_85915F);  /* tile DMA + palette */
    func_table_register(0x859239, smk_859239);  /* sprite slot init */
    func_table_register(0x85909B, smk_85909B);  /* mode select init */
    func_table_register(0x8590B1, smk_8590B1);  /* mode select main logic */
    func_table_register(0x8590D7, smk_8590D7);  /* mode select NMI rendering */

    printf("smk: registered %d recompiled functions\n", 47);
}
