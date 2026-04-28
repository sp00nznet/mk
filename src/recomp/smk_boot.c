/*
 * Super Mario Kart — Boot chain recompilation
 *
 * Reset vector → hardware init → main loop
 * These are the first functions that run when the game starts.
 */

#include "smk/functions.h"

#include <snesrecomp/snesrecomp.h>
#include <stdio.h>

/*
 * $80:FF70 — Reset vector
 *
 * Original:
 *   SEI
 *   REP #$09       ; clear carry + decimal
 *   XCE            ; native mode (carry was cleared)
 *   SEP #$30       ; 8-bit A, 8-bit X/Y
 *   LDA #$1F
 *   XBA            ; B = $1F
 *   LDA #$FF
 *   TCS            ; S = $1FFF
 *   LDA #$01
 *   STA $420D      ; FastROM enable
 *   JML $80803A
 */
RECOMP_PATCH(smk_80FF70, 0x80FF70) {
    OP_SEI();
    op_rep(0x09);       /* clear carry + decimal */
    op_xce();           /* native mode */
    op_sep(0x30);       /* M=1, X=1 (8-bit A and index) */

    op_lda_imm8(0x1F);
    op_xba();           /* B = $1F, A = garbage */
    op_lda_imm8(0xFF);
    OP_TCS();           /* S = $1FFF */

    op_lda_imm8(0x01);
    op_sta_abs8(0x420D); /* MEMSEL = 1 (fast ROM) */

    /* JML $80803A — fall through to hardware init */
    g_cpu.PB = 0x80;
    smk_80803A();
}

/*
 * $80:803A — Hardware init entry point
 *
 * Sets DB=$80, disables interrupts/DMA, force-blanks the screen,
 * then calls the full init subroutine at $81:E000.
 * After init, enters the main loop.
 */
RECOMP_PATCH(smk_80803A, 0x80803A) {
    /* PHK / PLB — DB = $80 */
    OP_SET_DB(0x80);

    /* STZ $4200 — disable NMI/IRQ */
    op_stz_abs8(0x4200);
    /* STZ $420B — disable DMA */
    op_stz_abs8(0x420B);
    /* STZ $420C — disable HDMA */
    op_stz_abs8(0x420C);

    /* LDA #$8F / STA $2100 — force blank + max brightness */
    op_lda_imm8(0x8F);
    op_sta_abs8(0x2100);

    /* STZ $4016 — joypad strobe off */
    op_stz_abs8(0x4016);
    /* STA $4201 — WRIO = $8F */
    op_sta_abs8(0x4201);

    /* JSL $81E000 — full initialization */
    smk_81E000();

    printf("smk: boot chain complete, entering main loop\n");

    /* The main loop is now driven by main.c's frame loop,
     * not by the original spin-wait here. */
}

/*
 * $80:8056 — Main loop (one iteration)
 *
 * Original spins on DP $44 waiting for NMI. In the recomp,
 * this is called once per frame from main.c after NMI processing.
 *
 * Original:
 *   REP #$30
 *   JSL $81E067     ; frame setup / transition
 *   STZ $44         ; clear NMI flag
 *   <wait for NMI>  ; (handled by frame loop in recomp)
 *   LDX $36         ; game state index
 *   JSR ($8197,x)   ; call state handler
 */
RECOMP_PATCH(smk_808056, 0x808056) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x80);

    op_rep(0x30);       /* 16-bit A and X/Y */

    /* JSL $81E067 — frame setup */
    smk_81E067();

    /* STZ $44 — clear NMI flag (in original, waits for NMI to set it) */
    op_stz_dp16(0x44);

    /* In the recomp, NMI has already been processed for this frame.
     * DP $44 has been set by the NMI handler. */

    /* LDX $36 — load game state index */
    op_ldx_dp16(0x36);

    /* JSR ($8197,x) — call state handler via indirect table */
    {
        uint16_t table_addr = 0x8197;
        uint16_t handler = bus_read16(0x80, table_addr + g_cpu.X);
        /* Call the handler if it's registered */
        uint32_t full_addr = 0x800000 | handler;
        func_table_call(full_addr);
    }

    g_cpu.DB = saved_db;
}

/*
 * $80:8000 — NMI handler
 *
 * Saves registers, reads RDNMI to acknowledge, increments frame
 * counter, runs brightness/sync handler, dispatches NMI state
 * handler, restores registers.
 */
RECOMP_PATCH(smk_808000, 0x808000) {
    /* PHP / REP #$38 / PHB / PHK / PLB / PHA / PHX / PHY */
    op_php();
    op_rep(0x38);       /* 16-bit A/X/Y, clear decimal+carry */
    op_phb();
    OP_SET_DB(0x80);    /* PHK;PLB */
    op_pha16();
    op_phx16();
    op_phy16();

    /* LDA $4210 — read RDNMI (clears NMI flag in hardware) */
    op_lda_abs16(0x4210);

    /* INC $34 — increment frame counter */
    op_inc_dp16(0x34);

    /* JSR $B181 — brightness / frame sync handler */
    smk_80B181();

    /* LDA $000036 / TAX / JSR ($81BF,x) — NMI state dispatch */
    {
        uint16_t state = bus_read16(0x00, 0x0036);
        g_cpu.X = state;
        uint16_t table_addr = 0x81BF;
        uint16_t handler = bus_read16(0x80, table_addr + g_cpu.X);
        uint32_t full_addr = 0x800000 | handler;
        func_table_call(full_addr);
    }

    /* PLY / PLX / PLA / PLB / PLP */
    op_ply16();
    op_plx16();
    op_pla16();
    op_plb();
    op_plp();
}

/*
 * $80:8067 — Main loop state handler for state $02 (initial game state handler body)
 *
 * This is one of the state handlers called from JSR ($8197,x).
 * State $02 ($36=$02) is the normal "game running" NMI-acknowledged handler.
 */
RECOMP_PATCH(smk_808067, 0x808067) {
    /* LDA $0162 / BMI -> skip if negative */
    op_lda_abs16(0x0162);
    if (g_cpu.flag_N) return; /* BMI $8097 -> RTS */

    /* INC $38 */
    op_inc_dp16(0x38);

    /* The rest calls many game logic subroutines.
     * For now, return — game logic will be added incrementally. */
}

/*
 * $80:80BA — State handler for state $04 (title screen main loop)
 *
 * Original:
 *   LDA $0162 / BMI -> RTS
 *   INC $38
 *   STA $015E (stores incremented $38)
 *   ... calls various title screen logic subroutines
 *
 * For now, implements the frame counter increment and basic flow.
 */
RECOMP_PATCH(smk_8080BA, 0x8080BA) {
    /* INC $38 — game frame counter */
    op_inc_dp16(0x38);

    /* Original: LDA $0162 / STA $015E — stores $0162 (brightness flag) to fade step.
     * smk_80853D overwrites $015E with $0060 every frame, so this is harmless. */
    op_lda_imm16(0x0040);
    op_sta_abs16(0x015E);

    /* JSL $858045 — per-frame sprite update */
    smk_858045();

    /* JSR $853D — title screen input handler */
    smk_80853D();
}

/*
 * $80:80CA — State handler for state $06 (mode select main loop)
 *
 * Original:
 *   INC $38
 *   LDA #$0060 / STA $015E
 *   JSL $8590B1
 *   RTS
 */
RECOMP_PATCH(smk_8080CA, 0x8080CA) {
    op_inc_dp16(0x38);

    op_lda_imm16(0x0060);
    op_sta_abs16(0x015E);

    /* JSL $8590B1 — mode select per-frame logic */
    func_table_call(0x8590B1);
}

/*
 * $80:824D — NMI state handler for state $06 (mode select NMI)
 *
 * Original:
 *   LDA $000044 / BNE → RTS
 *   DEC A / STA $000044
 *   JSR $946E        ; OAM DMA
 *   JSL $8590D7      ; mode select rendering
 *   JSR $81B5        ; audio/input cleanup
 *   RTS
 */
RECOMP_PATCH(smk_80824D, 0x80824D) {
    op_rep(0x30);

    op_lda_long16(0x00, 0x0044);
    if (!g_cpu.flag_Z) return;

    op_dec_a16();
    op_sta_long16(0x00, 0x0044);

    smk_80946E();

    /* JSL $8590D7 — mode select NMI rendering */
    func_table_call(0x8590D7);

    /* JSR $81B5 — audio/input/misc cleanup */
    smk_8081B5();
}

/*
 * $80:8174 — State handler for state $14 (mode select main loop)
 *
 * Original: INC $38; JSL $088C1A; RTS
 * $088C1A does: font DMA, CGRAM palette DMA, $81:94C2 (animation),
 * 4 cursor OAM sprites, OAM high table, OAM DMA, brightness=$0F.
 *
 * Input handling is added here (original handles it elsewhere).
 */
RECOMP_PATCH(smk_808174, 0x808174) {
    op_rep(0x30);
    op_inc_dp16(0x38);

    /* === JSL $088C1A implementation === */

    /* 1. Font DMA: $84:C500 → VRAM $4000 (1KB, mode 1) */
    bus_write16(0x80, 0x2115, 0x0080);
    bus_write16(0x80, 0x2116, 0x4000);
    bus_write16(0x80, 0x4300, 0x1801);
    bus_write16(0x80, 0x4302, 0xC500);
    bus_write16(0x80, 0x4304, 0x0084);
    bus_write16(0x80, 0x4305, 0x0400);
    bus_write16(0x80, 0x420B, 0x0001);

    /* 2. CGRAM palette: decompress $C4:$1313 → $7E:$3A80, DMA → CGRAM */
    {
        extern void sub_df48(uint16_t, uint16_t, uint8_t);
        sub_df48(0x3A80, 0x1313, 0xC4);

        op_rep(0x30);
        bus_write16(0x80, 0x4305, 0x0200);
        bus_write16(0x80, 0x4302, 0x3A80);
        bus_write16(0x80, 0x4300, 0x2202);
        op_sep(0x20);
        bus_write8(0x80, 0x2121, 0x00);
        bus_write8(0x80, 0x4304, 0x7E);
        bus_write8(0x80, 0x420B, 0x01);
        op_rep(0x30);
    }

    /* 3. $B93E → $81:94C2 (hardware multiply / animation — stub for now) */


    /* 4. Set 4 cursor OAM sprites from $8C7D table.
     * Y position based on current mode selection ($2C):
     *   mode 0 (GP):     Y=$60
     *   mode 2 (Match):  Y=$70
     *   mode 4 (Battle): Y=$80 */
    {
        uint16_t mode = bus_wram_read16(g_cpu.DP + 0x2C);
        uint8_t cursor_y = 0x70;  /* default center */
        if (mode == 0) cursor_y = 0x60;
        else if (mode == 4) cursor_y = 0x80;

        uint8_t *wram = bus_get_wram();
        if (wram) {
            uint8_t oam[16] = {
                0x60, cursor_y, 0x00, 0x30,
                0x70, cursor_y, 0x02, 0x30,
                0x80, cursor_y, 0x04, 0x30,
                0x90, cursor_y, 0x06, 0x30,
            };
            for (int i = 0; i < 16; i++)
                wram[0x0200 + i] = oam[i];
            /* OAM high table */
            wram[0x0400] = 0xAA;
            wram[0x0401] = 0x55;
        }
    }

    /* 5. OAM DMA */
    smk_80946E();

    /* 6. Brightness */
    op_sep(0x20);
    bus_write8(0x80, 0x2100, 0x0F);
    op_rep(0x30);

    uint16_t fade = bus_wram_read16(g_cpu.DP + 0x48);
    if (fade == 0) {
        uint16_t edge = bus_wram_read16(g_cpu.DP + 0x28);
        uint16_t mode = bus_wram_read16(g_cpu.DP + 0x2C);

        if (edge & 0x0800) {  /* Up */
            if (mode >= 2) mode -= 2;
        }
        if (edge & 0x0400) {  /* Down */
            if (mode <= 2) mode += 2;
        }
        bus_wram_write16(g_cpu.DP + 0x2C, mode);

        if (edge & 0x9000) {  /* B or START — confirm */
            bus_wram_write16(g_cpu.DP + 0x2E, 0);
            bus_wram_write16(g_cpu.DP + 0x32, 0x0006);
            bus_wram_write16(0x015E, 0x0060);
            bus_wram_write16(g_cpu.DP + 0x48, 0x8F00);
            printf("smk: mode %d selected — transitioning to character select\n", mode / 2);
        }
    }
}

/*
 * $80:8369 — NMI state handler for state $14 (mode select NMI)
 *
 * Original: check $44, set $44, OAM DMA, JSL $088C54, JSR $81B8
 */
RECOMP_PATCH(smk_808369, 0x808369) {
    op_rep(0x30);

    op_lda_long16(0x00, 0x0044);
    if (!g_cpu.flag_Z) return;

    op_dec_a16();
    op_sta_long16(0x00, 0x0044);

    /* JSR $946E — OAM DMA */
    smk_80946E();

    /* JSL $088C54 — mid-entry into $8C1A
     * Original $8C54 enters mid-instruction (ORA ($84,S),Y side effect).
     * Does: JSR $8413 (CGRAM DMA), JSR $B93E ($81:94C2 animation),
     * cursor sprites, OAM high table, OAM DMA, brightness=$0F.
     * Since the main handler ($8174) already does all of this, and the
     * NMI handler did OAM DMA above, we just need the animation call. */

    /* JSR $81B8 = JSR $843C (joypad) + JSR $9EB2 (misc, stub) */
    smk_80843C();
}

/*
 * $80:8096 — Null state handler (just RTS)
 * State $00 and $1A both point here.
 */
RECOMP_PATCH(smk_808096, 0x808096) {
    /* RTS — does nothing */
}

/*
 * $80:81DD — NMI state handler for state $00/$1A (minimal NMI)
 */
RECOMP_PATCH(smk_8081DD, 0x8081DD) {
    /* Sets DP $44 = 1 to wake main loop from NMI wait */
    bus_wram_write16(g_cpu.DP + 0x44, 1);
}

/*
 * $80:8237 — NMI state handler for state $04 (title screen NMI)
 *
 * Original:
 *   REP #$30       (already 16-bit from NMI handler)
 *   LDA $000044    ; check NMI flag
 *   BNE $824C      ; if already set, skip (RTS)
 *   DEC A          ; A = $FFFF
 *   STA $000044    ; set NMI flag
 *   JSR $946E      ; DMA/rendering subroutine
 *   JSL $85809B    ; more rendering
 *   JSR $81B5      ; cleanup
 *   RTS
 */
RECOMP_PATCH(smk_808237, 0x808237) {
    op_rep(0x30);

    /* LDA $000044 */
    op_lda_long16(0x00, 0x0044);

    /* BNE -> RTS (if NMI flag already set, skip) */
    if (!g_cpu.flag_Z) return;

    /* DEC A — A = $FFFF */
    op_dec_a16();

    /* STA $000044 — set NMI flag */
    op_sta_long16(0x00, 0x0044);

    /* JSR $946E — OAM DMA transfer */
    smk_80946E();

    /* JSL $85809B — BG scroll + HDMA */
    smk_85809B();

    /* JSR $81B5 — audio/input/misc cleanup */
    smk_8081B5();
}

/*
 * $80:B181 — Brightness / fade handler
 *
 * Called from NMI. Manages screen brightness fading:
 *   DP $48 > 0: fading in (add $015E each frame, cap at $0F00)
 *   DP $48 < 0: fading out (subtract $015E each frame, cap at $8000)
 *   DP $48 = 0: no change
 * Result stored to $0160 (16-bit). High byte ($0161) written to INIDISP ($2100).
 */
RECOMP_PATCH(smk_80B181, 0x80B181) {
    op_rep(0x30);

    /* LDA $48 */
    int16_t brightness = (int16_t)bus_wram_read16(g_cpu.DP + 0x48);

    if (brightness == 0) {
        /* No fade — just write current INIDISP */
    } else if (brightness > 0) {
        /* Fading in: add $015E */
        uint16_t step = bus_wram_read16(g_cpu.DP + 0x015E);
        uint16_t result = (uint16_t)brightness + step;
        if (result >= 0x0F00) {
            /* Fully bright */
            bus_wram_write16(g_cpu.DP + 0x48, 0);
            result = 0x0F00;
        } else {
            bus_wram_write16(g_cpu.DP + 0x48, result);
        }
        bus_wram_write16(0x0160, result);
    } else {
        /* Fading out: subtract $015E */
        uint16_t step = bus_wram_read16(g_cpu.DP + 0x015E);
        uint16_t uval = (uint16_t)brightness;
        uint16_t result = uval - step;
        if ((result & 0x8000) == 0) {
            /* Underflowed past $8000 */
            bus_wram_write16(g_cpu.DP + 0x48, 0);
            result = 0x8000;
        } else {
            bus_wram_write16(g_cpu.DP + 0x48, result);
            result &= 0x7FFF;
        }
        bus_wram_write16(0x0160, result);
    }

    /* SEP #$20 / LDA $0161 / STA $2100 */
    op_sep(0x20);
    uint8_t inidisp = bus_wram_read8(0x0161);
    bus_write8(g_cpu.DB, 0x2100, inidisp);
    op_rep(0x20);

}

/*
 * $80:946E — OAM DMA transfer
 *
 * Transfers OAM (sprite) data from WRAM $0002:0200 to PPU OAM registers
 * via DMA channel 0.
 *
 * Original:
 *   SEP #$30
 *   STZ $4302 / LDA #$02 / STA $4303   ; source = $00:0200
 *   STZ $4304                           ; source bank = $00
 *   STZ $2102 / STZ $2103               ; OAM address = 0
 *   STZ $4300                           ; DMA ctrl = 0 (1-byte, A→B)
 *   LDA #$04 / STA $4301                ; B-bus dest = $2104 (OAMDATA)
 *   LDA #$20 / STA $4305                ; size low = $20
 *   LDA #$02 / STA $4306                ; size high = $02 → $0220 bytes
 *   LDA #$01 / STA $420B                ; trigger DMA ch0
 *   REP #$30 / RTS
 */
RECOMP_PATCH(smk_80946E, 0x80946E) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x80);

    op_sep(0x30);

    /* DMA source = $00:0200 */
    op_stz_abs8(0x4302);        /* src low = $00 */
    op_lda_imm8(0x02);
    op_sta_abs8(0x4303);        /* src high = $02 */
    op_stz_abs8(0x4304);        /* src bank = $00 */

    /* OAM address = 0 */
    op_stz_abs8(0x2102);
    op_stz_abs8(0x2103);

    /* DMA channel 0 setup */
    op_stz_abs8(0x4300);        /* ctrl = $00 (1-byte transfer, A→B) */
    op_lda_imm8(0x04);
    op_sta_abs8(0x4301);        /* B-bus dest = $2104 (OAMDATA) */
    op_lda_imm8(0x20);
    op_sta_abs8(0x4305);        /* size low = $20 */
    op_lda_imm8(0x02);
    op_sta_abs8(0x4306);        /* size high = $02 → total $0220 */
    op_lda_imm8(0x01);
    op_sta_abs8(0x420B);        /* trigger DMA channel 0 */

    op_rep(0x30);
    g_cpu.DB = saved_db;
}

/*
 * $85:809B — BG scroll write + HDMA trigger
 *
 * Original:
 *   PHB / PHK / PLB   ; DB = $85
 *   SEP #$30
 *   LDA $64 / STA $00210F   ; BG1 horizontal scroll (write twice)
 *   LDA $65 / STA $00210F
 *   LDA #$02 / STA $420C    ; enable HDMA channel 1
 *   REP #$30
 *   JSL $81CB35              ; additional HDMA/scroll setup
 *   PLB / RTL
 */
RECOMP_PATCH(smk_85809B, 0x85809B) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x85);

    op_sep(0x30);

    /* Write BG1HOFS ($210F) — two writes (low then high byte) */
    op_lda_dp8(0x64);
    op_sta_long8(0x00, 0x210F);
    op_lda_dp8(0x65);
    op_sta_long8(0x00, 0x210F);

    /* Enable HDMA channel 1 */
    op_lda_imm8(0x02);
    op_sta_abs8(0x420C);

    op_rep(0x30);

    /* JSL $81CB35 — additional scroll/HDMA setup */
    func_table_call(0x81CB35);  /* not yet recompiled, will silently fail */

    g_cpu.DB = saved_db;
}

/*
 * $80:81B5 — NMI cleanup: audio update + controller read + misc
 *
 * Original:
 *   JSR $9632    ; APU port update (writes to $2140-$2143)
 *   JSR $843C    ; controller/input processing
 *   JSR $9EB2    ; misc (mode 7 related?)
 *   RTS
 *
 * For now, stubs the sub-calls — these handle audio and input which
 * LakeSnes processes internally.
 */
RECOMP_PATCH(smk_8081B5, 0x8081B5) {
    /* JSR $9632 — APU port update
     * Writes DP $42/$43 to APU ports $2142/$2143, handles SPC700 transfers.
     * LakeSnes APU runs internally, so we write the ports to keep state in sync. */
    {
        uint8_t saved_db = g_cpu.DB;
        OP_SET_DB(0x80);
        op_sep(0x30);

        uint8_t val42 = bus_wram_read8(g_cpu.DP + 0x42);
        bus_write8(0x80, 0x2142, val42);
        uint8_t val43 = bus_wram_read8(g_cpu.DP + 0x43);
        bus_write8(0x80, 0x2143, val43);

        op_rep(0x30);
        g_cpu.DB = saved_db;
    }

    /* JSR $843C — controller/input processing */
    smk_80843C();

    /* JSR $9EB2 — misc processing (stub) */
}

/*
 * $81:CB35 → $80:BA28 → $80:8E01 — NMI sprite tile DMA
 *
 * Processes the staging buffer at $0EA0 (built by the main-loop OAM builder).
 * Each 6-byte entry: VRAM dest (2), source addr (2), bank (1), size (1).
 * Entries are processed in reverse order, then $4A is cleared.
 *
 * Original $80:8E01:
 *   SEP #$30 / set DMA ch0 mode=$01 dest=$18 / VMAIN=$80
 *   loop (X=$4A downto 6, step -6):
 *     read entry at $0E9A+X → set VRAM addr, source, bank, size
 *     trigger DMA ch0
 *   STZ $4A
 */
RECOMP_PATCH(smk_81CB35, 0x81CB35) {
    /* INC $0D1A (frame counter for CB35 calls) */
    uint16_t cnt = bus_wram_read16(0x0D1A);
    bus_wram_write16(0x0D1A, cnt + 1);

    /* Process $0EA0 staging buffer via $80:8E01 logic */
    uint8_t *wram = bus_get_wram();
    if (!wram) return;

    uint8_t buf_idx = wram[0x004A];  /* DP $4A = buffer write index */
    if (buf_idx == 0) return;  /* nothing staged */

    /* Set up DMA: mode=$01 (2-reg sequential), B-bus=$18 (VMDATAL) */
    /* VMAIN=$80 (word access, increment after high byte write) */
    bus_write8(0x00, 0x2115, 0x80);

    /* Process entries in reverse: X from $4A down to 6, step -6 */
    int x = buf_idx;
    while (x > 0) {
        /* Entry at $0E9A + X = $0EA0 + (X - 6) when X counts from $4A */
        uint8_t vram_lo  = wram[0x0E9A + x + 0];
        uint8_t vram_hi  = wram[0x0E9A + x + 1];
        uint8_t src_lo   = wram[0x0E9A + x + 2];
        uint8_t src_hi   = wram[0x0E9A + x + 3];
        uint8_t src_bank = wram[0x0E9A + x + 4];
        uint8_t size_lo  = wram[0x0E9A + x + 5];

        /* Set VRAM address */
        bus_write8(0x00, 0x2116, vram_lo);
        bus_write8(0x00, 0x2117, vram_hi);

        /* Set DMA channel 0: ctrl=$01, dest=$18 */
        bus_write8(0x00, 0x4300, 0x01);
        bus_write8(0x00, 0x4301, 0x18);

        /* Source address and bank */
        bus_write8(0x00, 0x4302, src_lo);
        bus_write8(0x00, 0x4303, src_hi);
        bus_write8(0x00, 0x4304, src_bank);

        /* DMA size: byte 5 = low byte, high = 0 (matches original STZ $4306) */
        bus_write8(0x00, 0x4305, size_lo);
        bus_write8(0x00, 0x4306, 0x00);

        /* Trigger DMA channel 0 */
        bus_write8(0x00, 0x420B, 0x01);

        x -= 6;
    }

    /* Clear buffer index */
    wram[0x004A] = 0;
    wram[0x004B] = 0;
}

/*
 * $80:843C — Joypad reading routine
 *
 * Reads auto-joypad registers $4218/$421A, stores current button state
 * to DP $20/$22, computes newly-pressed (edge-detected) to DP $28/$2A,
 * and saves previous state to DP $24/$26.
 *
 * Memory layout (with DP=0):
 *   $0020/$0022 = current buttons (joy1/joy2)
 *   $0024/$0026 = previous frame's buttons
 *   $0028/$002A = newly pressed (rising edge)
 *
 * Original:
 *   LDX #$0000 / JSR $8445 / LDX #$0002
 *   $8445: LDA $4218,x / STA $20,x / PHA
 *          EOR $24,x / AND $20,x / STA $28,x
 *          ... (demo mode check, skipped if $0E32=0) ...
 *          PLA / STA $24,x / RTS
 */
RECOMP_PATCH(smk_80843C, 0x80843C) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x80);
    op_rep(0x30);

    /* Process joy1 (X=0) and joy2 (X=2) */
    for (uint16_t x = 0; x <= 2; x += 2) {
        /* LDA $4218,x — read auto-joypad register */
        uint16_t current = bus_read16(0x80, 0x4218 + x);

        /* STA $20,x — store current button state */
        bus_wram_write16(g_cpu.DP + 0x20 + x, current);

        /* EOR $24,x — XOR with previous frame */
        uint16_t prev = bus_wram_read16(g_cpu.DP + 0x24 + x);
        uint16_t changed = current ^ prev;

        /* AND $20,x — keep only bits that are pressed NOW (rising edge) */
        uint16_t newly_pressed = changed & current;

        /* STA $28,x — store edge-detected buttons */
        bus_wram_write16(g_cpu.DP + 0x28 + x, newly_pressed);

        /* STA $24,x — save current as previous for next frame */
        bus_wram_write16(g_cpu.DP + 0x24 + x, current);
    }

    g_cpu.DB = saved_db;
}

/*
 * $80:853D — Title screen input handler
 *
 * Called each frame during state $04.
 * Checks for button presses to advance past the title screen.
 *
 * Original checks $0E68 (SRAM validation flag) before processing
 * Select+button combos. For the recomp, we check START directly.
 *
 * START → transition $14 (player/cup select)
 * Timer $1040 >= $0642 → attract/demo mode (not yet implemented)
 */
RECOMP_PATCH(smk_80853D, 0x80853D) {
    op_rep(0x30);

    /* LDA #$0060 / STA $015E — fade step */
    bus_wram_write16(0x015E, 0x0060);

    /* LDA $32 / BNE → RTS (transition already pending) */
    if (bus_wram_read16(g_cpu.DP + 0x32) != 0) return;

    /* LDA $1040 / CMP #$0642 / BCS → attract mode
     * $1040 is sprite slot 0's frame counter, used as title screen timer. */
    uint16_t timer = bus_wram_read16(0x1040);
    if (timer >= 0x0642) {
        /* Attract/demo mode timeout — skip for now */
        return;
    }

    /* Check for START button on either controller.
     * Original gates on $0E68 (SRAM signature) then checks Select+button.
     * We simplify to just START for the recomp. */
    for (int x = 2; x >= 0; x -= 2) {
        uint16_t buttons = bus_wram_read16(g_cpu.DP + 0x20 + x);

        if (buttons & 0x1000) {  /* START (bit 12) */
            /* Transition to state $14 — mode select (GP/Match/Battle) */
            bus_wram_write16(g_cpu.DP + 0x32, 0x0014);
            bus_wram_write16(0x015E, 0x0060);
            bus_wram_write16(g_cpu.DP + 0x48, 0x8F00);  /* fade out */
            printf("smk: START pressed — transitioning to state $14\n");
            return;
        }
    }
}
