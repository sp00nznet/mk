#!/usr/bin/env python3
"""Unit tests for width-sensitive 65816 code generation."""

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import autogen  # noqa: E402


def generate(code, p=0x00, addr=0x8100):
    """Generate one synthetic function in bank $80's FastROM window."""
    rom = bytearray(512 * 1024)
    rom[addr:addr + len(code)] = bytes(code)
    return autogen.generate(rom, 0x80, addr, p, "smk_test")


class WidthSensitiveStackTests(unittest.TestCase):
    def test_16_bit_stack_helpers_and_cycles(self):
        source = generate([0x48, 0x68, 0xDA, 0xFA, 0x5A, 0x7A, 0x6B])
        self.assertEqual(source.count("recomp_stack_push16("), 3)
        self.assertEqual(source.count("recomp_stack_pull16(true)"), 3)
        self.assertIn("recomp_phase_begin(28,", source)  # 16-bit push
        self.assertIn("recomp_phase_begin(34,", source)  # 16-bit pull

    def test_8_bit_stack_preserves_b_and_uses_one_byte(self):
        source = generate([0x48, 0x68, 0xDA, 0xFA, 0x5A, 0x7A, 0x6B], p=0x30)
        self.assertNotIn("recomp_stack_push16", source)
        self.assertIn("recomp_stack_push8", source)
        self.assertIn("g_cpu.C & 0xFF00", source)
        self.assertIn("recomp_phase_begin(20,", source)  # 8-bit push
        self.assertIn("recomp_phase_begin(26,", source)  # 8-bit pull

    def test_rep_sep_changes_following_stack_width(self):
        source = generate([0xE2, 0x30, 0x48, 0xDA, 0xC2, 0x30,
                           0x48, 0xDA, 0x6B])
        self.assertEqual(source.count("recomp_stack_push8"), 2)
        self.assertIn("recomp_stack_push16(g_cpu.C, true);", source)
        self.assertIn("recomp_stack_push16(g_cpu.X, true);", source)


class AdditionalOpcodeTests(unittest.TestCase):
    def test_indirect_opcodes_are_two_bytes(self):
        # Every direct-page/stack indirect mode has one operand byte.  A bad
        # opcode-table size silently shifts the entire following CFG.
        opcodes = [0xA1, 0xB2, 0xB1, 0xA7, 0xB7, 0xA3, 0xB3]
        for opcode in opcodes:
            rom = bytearray(512 * 1024)
            rom[0x8100:0x8103] = bytes([opcode, 0x20, 0x6B])
            insn, _, _ = autogen._decode_at(rom, 0x80, 0x8100, False, False)
            self.assertEqual(insn["total"], 2, hex(opcode))

    def test_indirect_load_effective_addresses(self):
        source = generate([
            0xA1, 0x10,  # LDA ($10,X)
            0xB2, 0x11,  # LDA ($11)
            0xB1, 0x12,  # LDA ($12),Y
            0xA7, 0x13,  # LDA [$13]
            0xB7, 0x14,  # LDA [$14],Y
            0xA3, 0x15,  # LDA $15,S
            0xB3, 0x16,  # LDA ($16,S),Y
            0x6B,
        ])
        for mode in ("dpxi", "dpi", "dpiy", "dpil", "dpily", "sr", "sriy"):
            self.assertIn(f"smk_ea_{mode}", source)
        self.assertEqual(source.count("smk_bus_read16_24"), 7)

    def test_indirect_store_effective_addresses(self):
        source = generate([
            0x81, 0x10, 0x92, 0x11, 0x91, 0x12, 0x87, 0x13,
            0x97, 0x14, 0x83, 0x15, 0x93, 0x16, 0x6B,
        ], p=0x20)
        self.assertEqual(source.count("smk_bus_write8_24"), 7)

    def test_mid_function_entry_jumps_to_real_entry_block(self):
        # $8103 BRA $8100; the lower block is shared code before the entry.
        rom = bytearray(512 * 1024)
        rom[0x8100:0x8105] = bytes([0xA6, 0x4A, 0x60, 0x80, 0xFB])
        source = autogen.generate(rom, 0x80, 0x8103, 0x00, "smk_test")
        entry_jump = source.index("goto L_8103_M0X0;")
        lower_block = source.index("L_8100_M0X0:;")
        self.assertLess(entry_jump, lower_block)

    def test_cfg_keeps_distinct_mx_variants_for_one_pc(self):
        # BEQ selects SEP or REP before both paths merge at a width-neutral NOP.
        source = generate([
            0xF0, 0x04,       # BEQ $8106
            0xC2, 0x20,       # REP #$20
            0x80, 0x02,       # BRA $8108
            0xE2, 0x20,       # SEP #$20
            0xEA,             # same PC under M=0 and M=1
            0x6B,
        ], p=0x20)
        self.assertIn("L_8108_M0X0", source)
        self.assertIn("L_8108_M1X0", source)
        self.assertEqual(source.count("/* $8108 NOP */"), 2)

    def test_branch_cycles_are_path_dependent(self):
        source = generate([0xF0, 0x01, 0xEA, 0x6B])
        self.assertIn("if (g_cpu.flag_Z) { recomp_tick(6);", source)
        self.assertEqual(source.count("recomp_tick(6);"), 1)

        bra = generate([0x80, 0x01, 0xEA, 0x6B])
        self.assertIn("recomp_tick(6);", bra)
        self.assertIn("goto L_8103_M0X0", bra)

    def test_bus_phase_brackets_data_semantics(self):
        source = generate([0xA5, 0x10, 0x6B])
        begin = source.index("recomp_phase_begin(28,")
        read = source.index("bus_read16")
        end = source.index("recomp_phase_end(0, 0);")
        self.assertLess(begin, read)
        self.assertLess(read, end)
        self.assertIn("recomp_phase_interrupt_pending()", source)

    def test_indirect_jsr_pushes_before_operand_high_and_table(self):
        source = generate([0xFC, 0x00, 0x82, 0x6B])
        begin = source.index("recomp_phase_begin(52, 0x80, 0x8100, 2)")
        stack = source.index("recomp_phase_call_enter_indirect(0x8102, 0x80)")
        table = source.index("bus_read16_checked(0x80")
        end = source.index("recomp_phase_end(0, 0);", table)
        call = source.index("func_table_call_with_frame")
        self.assertLess(begin, stack)
        self.assertLess(stack, table)
        self.assertLess(table, end)
        self.assertLess(end, call)
        self.assertIn("recomp_set_redirect(((uint32_t)0x80 << 16) | _t)", source)
        self.assertIn("recomp_phase_call_leave(_frame, _frame_consumed);", source)
        self.assertIn("recomp_set_redirect(0x808103)", source)

    def test_fastrom_return_and_indirect_jsr_aggregate_cycles(self):
        def cycles(code):
            rom = bytearray(512 * 1024)
            bank, addr = 0x81, 0x8100
            offset = ((bank & 0x3F) << 16) | addr
            rom[offset:offset + len(code)] = bytes(code)
            insn, _, _ = autogen._decode_at(rom, bank, addr, False, False)
            return autogen._instr_cycles(insn, bank)

        self.assertEqual(cycles([0x60]), 40)              # RTS
        self.assertEqual(cycles([0x6B]), 42)              # RTL
        self.assertEqual(cycles([0xFC, 0x00, 0x82]), 52)  # JSR ($8200,X)

    def test_direct_calls_materialize_correct_return_frames(self):
        jsr = generate([0x20, 0x04, 0x81, 0x6B, 0x60])
        self.assertIn("recomp_phase_call_enter(0x8102, 0x80, 0x80, false, true)", jsr)
        self.assertIn("func_table_call_with_frame(0x808104, false", jsr)
        self.assertIn("recomp_phase_call_leave(_frame_8100_M0X0, _frame_consumed_8100_M0X0)", jsr)

        jsl = generate([0x22, 0x05, 0x81, 0x81, 0x6B, 0x6B])
        self.assertIn("recomp_phase_begin(54, 0x80, 0x8100, 3)", jsl)
        self.assertIn("recomp_phase_call_enter(0x8103, 0x80, 0x81, true, true)", jsl)
        self.assertIn("func_table_call_with_frame(0x818105, true", jsl)
        self.assertIn("recomp_phase_call_leave(_frame_8100_M0X0, _frame_consumed_8100_M0X0)", jsl)
        self.assertIn("recomp_set_redirect(0x818105)", jsl)

    def test_multi_entry_dispatches_from_live_mx_flags(self):
        rom = bytearray(512 * 1024)
        rom[0x8100:0x8102] = bytes([0xEA, 0x6B])
        source = autogen.generate(rom, 0x80, 0x8100,
                                  [0x00, 0x10, 0x20, 0x30], "smk_test")
        self.assertIn("g_cpu.flag_M == 0", source)
        self.assertIn("g_cpu.flag_X == 0", source)
        for m in (0, 1):
            for x in (0, 1):
                self.assertIn(f"goto L_8100_M{m}X{x};", source)
                self.assertIn(f"L_8100_M{m}X{x}:;", source)
        self.assertEqual(source.count("/* $8100 NOP */"), 4)

    def test_exact_profile_variants_avoid_impossible_entry_width(self):
        # X=16 is a calling convention here. Decoding X=8 would consume only
        # one immediate byte and mistake the high byte ($00) for BRK.
        rom = bytearray(512 * 1024)
        rom[0x8100:0x8105] = bytes([0xA2, 0x34, 0x00, 0xEA, 0x6B])
        source = autogen.generate(rom, 0x80, 0x8100, [0x00, 0x20], "smk_test")
        self.assertIn("L_8100_M0X0", source)
        self.assertIn("L_8100_M1X0", source)
        self.assertNotIn("L_8100_M0X1", source)
        self.assertEqual(source.count("BRK"), 0)

    def test_bank_and_direct_page_stack_ops(self):
        source = generate([0x4B, 0x0B, 0x2B, 0x6B])  # PHK, PHD, PLD, RTL
        self.assertIn("g_cpu.PB", source)
        self.assertIn("recomp_stack_push16(g_cpu.DP, true)", source)
        self.assertIn("g_cpu.DP = recomp_stack_pull16(true)", source)

    def test_tsb_trb_emit_read_modify_write_and_z(self):
        source = generate([0x04, 0x20, 0x14, 0x22, 0x6B])
        self.assertEqual(source.count("g_cpu.flag_Z"), 2)
        self.assertIn("_m | _a", source)
        self.assertIn("_m & (uint16_t)~_a", source)

    def test_adc_sbc_memory_width_and_nop(self):
        source16 = generate([0x65, 0x10, 0xF5, 0x20, 0xEA, 0x6B])
        self.assertIn("smk_op_adc16", source16)
        self.assertIn("smk_op_sbc16", source16)
        self.assertIn("bus_read16", source16)
        self.assertIn("(void)0;", source16)

        source8 = generate([0x65, 0x10, 0xF5, 0x20, 0x6B], p=0x20)
        self.assertIn("smk_op_adc8", source8)
        self.assertIn("smk_op_sbc8", source8)
        self.assertIn("bus_read8", source8)

    def test_block_moves_repeat_and_honor_index_width(self):
        mvn = generate([0x54, 0x7F, 0x7E, 0x6B])
        self.assertIn("recomp_phase_begin(46, 0x80, 0x8100, 3)", mvn)
        self.assertIn("bus_read8(0x7E, g_cpu.X)", mvn)
        self.assertIn("bus_write8(0x7F, g_cpu.Y", mvn)
        self.assertIn("g_cpu.DB = 0x7F", mvn)
        self.assertIn("g_cpu.X + 1", mvn)
        self.assertNotIn("g_cpu.X &= 0x00FF", mvn)
        db = mvn.index("g_cpu.DB = 0x7F")
        read = mvn.index("bus_read8(0x7E", db)
        write = mvn.index("bus_write8(0x7F", read)
        idle1 = mvn.index("recomp_phase_idle(6);", write)
        check = mvn.index("recomp_phase_check_int();", idle1)
        idle2 = mvn.index("recomp_phase_idle(6);", check)
        self.assertLess(db, read)
        self.assertLess(read, write)
        self.assertLess(write, idle1)
        self.assertLess(idle1, check)
        self.assertLess(check, idle2)
        self.assertIn("recomp_set_redirect(g_cpu.C != 0xFFFF ? 0x808100 : 0x808103)", mvn)
        self.assertIn("if (g_cpu.C != 0xFFFF) goto L_8100_M0X0;", mvn)

        mvp8 = generate([0x44, 0x7E, 0x7F, 0x6B], p=0x10)
        self.assertIn("g_cpu.X - 1", mvp8)
        self.assertIn("g_cpu.X &= 0x00FF", mvp8)
        self.assertIn("if (g_cpu.C != 0xFFFF) goto L_8100_M0X1;", mvp8)

    def test_rti_restores_frame_in_lakesnes_microphase_order(self):
        source = generate([0x40])
        self.assertIn("recomp_phase_begin(50, 0x80, 0x8100, 1)", source)
        idle = source.index("recomp_phase_idle(12);")
        pull_p = source.index("_p = recomp_stack_pull8()", idle)
        flags = source.index("cpu_set_p(_p);", pull_p)
        pull_pc = source.index("_pc = recomp_stack_pull16(false)", flags)
        check = source.index("recomp_phase_check_int();", pull_pc)
        pull_pb = source.index("_pb = recomp_stack_pull8()", check)
        redirect = source.index("recomp_set_redirect(((uint32_t)_pb << 16) | _pc)", pull_pb)
        self.assertLess(idle, pull_p)
        self.assertLess(pull_p, flags)
        self.assertLess(flags, pull_pc)
        self.assertLess(pull_pc, check)
        self.assertLess(check, pull_pb)
        self.assertLess(pull_pb, redirect)

    def test_remaining_register_transfers(self):
        source = generate([0xBA, 0x9A, 0x9B, 0xBB, 0x5B, 0x7B,
                           0x1B, 0x3B, 0x6B])
        self.assertIn("g_cpu.S & 0xFF00", source)  # TXS handles 8-bit X safely
        self.assertIn("g_cpu.DP = g_cpu.C", source)
        self.assertIn("g_cpu.C = g_cpu.DP", source)
        self.assertIn("g_cpu.C = g_cpu.S", source)


class CheckIntMicrophaseTests(unittest.TestCase):
    def test_immediate_checks_match_operand_width(self):
        source16 = generate([0xA9, 0x34, 0x12, 0x6B])
        self.assertIn("recomp_phase_begin_checked(18, 0x80, 0x8100, 3, 2)", source16)

        source8 = generate([0xA9, 0x34, 0x6B], p=0x20)
        self.assertIn("recomp_phase_begin_checked(12, 0x80, 0x8100, 2, 1)", source8)

    def test_memory_checks_are_attached_to_final_data_access(self):
        source16 = generate([0xA5, 0x10, 0x85, 0x12, 0x6B])
        self.assertIn("bus_read16_checked", source16)
        self.assertIn("bus_write16_checked", source16)

        source8 = generate([0xA5, 0x10, 0x85, 0x12, 0x6B], p=0x20)
        self.assertIn("bus_read8_checked", source8)
        self.assertIn("bus_write8_checked", source8)

    def test_rmw_checks_between_reversed_writes(self):
        source = generate([0x46, 0x10, 0x6B])  # LSR $10
        read = source.index("bus_read16(")
        idle = source.index("recomp_phase_idle(6);", read)
        write = source.index("bus_write16_reversed_checked", idle)
        self.assertLess(read, idle)
        self.assertLess(idle, write)

    def test_direct_page_and_index_penalties_precede_data_access(self):
        dp = generate([0xA5, 0x10, 0x6B], p=0x30)
        begin = dp.index("recomp_phase_begin(20,")
        penalty = dp.index("if (g_cpu.DP & 0x00FF) recomp_phase_penalty(6);", begin)
        read = dp.index("bus_read8_checked", penalty)
        self.assertLess(begin, penalty)
        self.assertLess(penalty, read)

        dpx = generate([0xB5, 0x10, 0x6B], p=0x30)
        dp_penalty = dpx.index("if (g_cpu.DP & 0x00FF) recomp_phase_penalty(6);")
        index_penalty = dpx.index("recomp_phase_penalty(6);", dp_penalty + 1)
        read = dpx.index("bus_read8_checked", index_penalty)
        self.assertLess(dp_penalty, index_penalty)
        self.assertLess(index_penalty, read)

    def test_absolute_index_penalty_distinguishes_reads_and_writes(self):
        read = generate([0xBD, 0xF0, 0x12, 0x6B], p=0x30)
        condition = ("if (!g_cpu.flag_X || ((0x12F0 >> 8) != "
                     "((0x12F0 + (uint32_t)g_cpu.X) >> 8))) "
                     "recomp_phase_penalty(6);")
        self.assertIn(condition, read)
        self.assertLess(read.index(condition), read.index("bus_read8_checked"))

        write = generate([0x9D, 0xF0, 0x12, 0x6B], p=0x30)
        self.assertNotIn("!g_cpu.flag_X", write)
        self.assertLess(write.index("recomp_phase_penalty(6);"),
                        write.index("bus_write8_checked"))

    def test_indirect_index_helpers_receive_access_kind(self):
        read = generate([0xB1, 0x10, 0x6B], p=0x30)
        self.assertIn("smk_ea_dpiy(0x10, false)", read)
        write = generate([0x91, 0x10, 0x6B], p=0x30)
        self.assertIn("smk_ea_dpiy(0x10, true)", write)

        stack = generate([0xA3, 0x11, 0xB3, 0x12, 0x6B], p=0x30)
        self.assertIn("smk_ea_sr(0x11)", stack)
        self.assertIn("smk_ea_sriy(0x12)", stack)

    def test_dma_trigger_is_followed_by_real_instruction_fetch_phases(self):
        # STA $420B starts DMA only after its own write. The following REP's
        # opcode fetch arms LakeSnes's state machine and its operand fetch lets
        # the transfer stall the CPU, so both fetches must remain explicit.
        source = generate([0x8D, 0x0B, 0x42, 0xC2, 0x30, 0x60], p=0x30)
        trigger = source.index("bus_write8_checked(g_cpu.DB, 0x420B")
        end = source.index("recomp_phase_end(0, 0);", trigger)
        next_fetches = source.index(
            "recomp_phase_begin_checked(18, 0x80, 0x8103, 2, 2);", end)
        rep = source.index("op_rep(0x30);", next_fetches)
        self.assertLess(trigger, end)
        self.assertLess(end, next_fetches)
        self.assertLess(next_fetches, rep)

    def test_branch_check_depends_on_taken_path(self):
        source = generate([0xF0, 0x01, 0xEA, 0x6B])
        self.assertIn("recomp_phase_begin_checked(12, 0x80, 0x8100, 2, (g_cpu.flag_Z) ? 2 : 1)", source)
        bra = generate([0x80, 0x01, 0xEA, 0x6B])
        self.assertIn("recomp_phase_begin_checked(12, 0x80, 0x8100, 2, 2)", bra)

    def test_returns_use_ordered_return_microphase(self):
        rts = generate([0x60])
        self.assertIn("recomp_phase_return(false);", rts)
        rtl = generate([0x6B])
        self.assertIn("recomp_phase_return(true);", rtl)

    def test_plp_samples_after_pull_before_new_flags(self):
        source = generate([0x28, 0x6B])
        pull = source.index("recomp_stack_pull8()")
        check = source.index("recomp_phase_check_int();", pull)
        flags = source.index("cpu_set_p(_p);", check)
        self.assertLess(pull, check)
        self.assertLess(check, flags)

    def test_direct_tail_jumps_check_during_operand_fetch(self):
        jmp = generate([0x4C, 0x34, 0x12])
        self.assertIn("recomp_phase_begin_checked(18, 0x80, 0x8100, 3, 2)", jmp)
        self.assertIn("recomp_set_redirect(0x801234)", jmp)

        jml = generate([0x5C, 0x34, 0x12, 0x81])
        self.assertIn("recomp_phase_begin_checked(24, 0x80, 0x8100, 4, 3)", jml)
        self.assertIn("recomp_set_redirect(0x811234)", jml)

    def test_indirect_tail_jumps_order_table_checks(self):
        indirect = generate([0x6C, 0x00, 0x20])
        low = indirect.index("_lo = bus_read8(0x00, _ad)")
        check = indirect.index("recomp_phase_check_int();", low)
        high = indirect.index("_hi = bus_read8(0x00", check)
        self.assertLess(low, check)
        self.assertLess(check, high)

        indexed = generate([0x7C, 0x00, 0x82])
        idle = indexed.index("recomp_phase_idle(6);")
        low = indexed.index("_lo = bus_read8(0x80, _ad)")
        self.assertLess(idle, low)
        self.assertIn("recomp_phase_begin(36,", indexed)

    def test_indirect_long_jump_checks_before_bank_byte(self):
        source = generate([0xDC, 0x00, 0x20])
        high = source.index("_hi = bus_read8")
        check = source.index("recomp_phase_check_int();", high)
        bank = source.index("_bk = bus_read8", check)
        self.assertLess(high, check)
        self.assertLess(check, bank)
        self.assertIn("recomp_phase_begin(36,", source)

    def test_direct_tail_jump_keeps_reachable_local_target_in_c(self):
        # BNE reaches $8105 directly; the fallthrough JMP reaches the same
        # already-decoded block and should not leave C through func_table.
        source = generate([0xD0, 0x03, 0x4C, 0x05, 0x81, 0x6B])
        self.assertIn("goto L_8105_M0X0;  /* $8102 JMP (local tail) */", source)
        self.assertNotIn("func_table_call_jsr(0x808105)", source)


if __name__ == "__main__":
    unittest.main()
