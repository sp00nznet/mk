#!/usr/bin/env python3
"""
autogen.py — 65816 -> RECOMP_PATCH auto-generator (v2).

Decodes an SNES function from ROM bytes + trace-exact entry M/X flags and emits
a snesrecomp RECOMP_PATCH that reproduces it, gated byte-identical to the
emulation oracle (tools/diff_snapshots.py vs lakesnes_ref).

Inspired by SuperRecomp's per-opcode codegen, but emitting OUR runtime (g_cpu +
bus_read/write + op_* helpers) and seeded with trace-exact entry flags — the two
assets SuperRecomp/mstan lack (docs/own_recompiler_design.md).

v2 handles:
  - loads/stores/STZ across addressing modes (imm/dp/dp,x/dp,y/abs/abs,x/abs,y/
    long/long,x) via INLINE bus_read/write + N/Z flags (no per-mode C helper),
  - register/stack/flag ops via the existing op_* helpers,
  - intra-function branches (BEQ/BNE/BCS/BCC/BMI/BPL/BVS/BVC/BRA/BRL) via a CFG
    walk + C labels/gotos.
Calls (JSR/JSL), direct and computed jumps, and width-sensitive register-stack
operations are supported. Per-(M,X) re-entry with differing widths still raises
Unsupported -> the function falls back to a hand-port / interp.

Usage:
  py tools/recomp/autogen.py <rom.sfc> <bank:addr> <entry_P_hex> [func_name]
"""
import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "disasm"))
from disasm65816 import OPCODES, rom_read  # noqa: E402


class Unsupported(Exception):
    pass


TERMINALS = {0x60, 0x6B, 0x40, 0xDB}                       # RTS RTL RTI STP
BRANCH = {0xF0: ("Z", True), 0xD0: ("Z", False),          # BEQ BNE
          0xB0: ("C", True), 0x90: ("C", False),          # BCS BCC
          0x30: ("N", True), 0x10: ("N", False),          # BMI BPL
          0x70: ("V", True), 0x50: ("V", False)}          # BVS BVC
UNCOND = {0x80, 0x82}                                      # BRA BRL
CALL = {0x20, 0x22, 0xFC}                                  # JSR/JSL/JSR(abs,x) — fall through
TAILJMP = {0x4C, 0x5C, 0x6C, 0x7C, 0xDC}                   # JMP/JML/(ind)/(abs,x)/[abs] — terminal
STATIC_MEM_MODES = {"abs", "absx", "absy", "dp", "dpx", "dpy", "long", "longx"}
INDIRECT_MODES = {"dpi", "dpxi", "dpiy", "dpil", "dpily", "sr", "sriy"}
MEM_MODES = STATIC_MEM_MODES | INDIRECT_MODES


def _call_target(insn, bank):
    """24-bit target of a direct JSR/JSL/JMP/JML (long modes carry their own bank)."""
    return insn["val"] if insn["mode"] == "long" else ((bank << 16) | insn["val"])


def _transfer_stmt(insn, bank):
    """C statement for a call/jump — direct via a constant target, indirect via a
    runtime func_table_call after reading the target from the jump table."""
    op, val = insn["op"], insn["val"]
    if op in (0x20, 0x22, 0x4C, 0x5C):                     # direct
        t = _call_target(insn, bank)
        fn = "func_table_call" if op in (0x22, 0x5C) else "func_table_call_jsr"
        return f"{fn}(0x{t:06X});"
    if op in (0xFC, 0x7C):                                 # (abs,x): target = PB:[abs+X]
        return (f"{{ uint16_t _t = bus_read16(0x{bank:02X}, (uint16_t)(0x{val:04X} + g_cpu.X)); "
                f"func_table_call_jsr(((uint32_t)0x{bank:02X} << 16) | _t); }}")
    if op == 0x6C:                                         # (abs): target = $00:[abs]
        return (f"{{ uint16_t _t = bus_read16(0x00, 0x{val:04X}); "
                f"func_table_call_jsr(((uint32_t)0x{bank:02X} << 16) | _t); }}")
    if op == 0xDC:                                         # [abs]: 24-bit target = $00:[abs]
        return (f"{{ uint32_t _t = (uint32_t)bus_read8(0x00, 0x{val:04X}) | "
                f"((uint32_t)bus_read8(0x00, 0x{(val + 1) & 0xFFFF:04X}) << 8) | "
                f"((uint32_t)bus_read8(0x00, 0x{(val + 2) & 0xFFFF:04X}) << 16); "
                f"func_table_call(_t); }}")
    raise Unsupported(f"transfer ${op:02X}")


def _decode_at(data, bank, pc, m8, x8):
    op = rom_read(data, bank, pc)
    if op not in OPCODES:
        raise Unsupported(f"unknown opcode ${op:02X} at ${bank:02X}:{pc:04X}")
    name, size, mode = OPCODES[op]
    if mode == "immA":
        size = 1 if m8 else 2
    elif mode == "immX":
        size = 1 if x8 else 2
    raw = [rom_read(data, bank, pc + i) for i in range(1 + size)]
    val = 0
    if size == 1:
        val = raw[1]
    elif size == 2:
        val = raw[1] | (raw[2] << 8)
    elif size == 3:
        val = raw[1] | (raw[2] << 8) | (raw[3] << 16)
    total = 1 + size
    target = None
    if mode == "rel8":
        d = raw[1] if raw[1] < 128 else raw[1] - 256
        target = (pc + total + d) & 0xFFFF
    elif mode == "rel16":
        d = val if val < 0x8000 else val - 0x10000
        target = (pc + total + d) & 0xFFFF
    nm8, nx8 = m8, x8
    if op == 0xC2:  # REP
        if raw[1] & 0x20: nm8 = False
        if raw[1] & 0x10: nx8 = False
    elif op == 0xE2:  # SEP
        if raw[1] & 0x20: nm8 = True
        if raw[1] & 0x10: nx8 = True
    return dict(pc=pc, op=op, name=name, mode=mode, val=val, total=total,
                target=target, m8=m8, x8=x8), nm8, nx8


def decode_cfg(data, bank, addr, m8=None, x8=None, limit=2048, entry_states=None):
    """Walk reachable (PC,M,X) states from one or more entry variants.

    A single ROM address may legally be reached under multiple accumulator/index
    widths.  Keep those states as distinct C blocks rather than rejecting the
    function or decoding a width-dependent immediate with the wrong length.
    """
    insns = {}
    if entry_states is None:
        if m8 is None or x8 is None:
            raise ValueError("m8/x8 are required without entry_states")
        entry_states = [(addr, m8, x8)]
    work = list(entry_states)
    n = 0
    while work:
        pc, m, x = work.pop()
        key = (pc, m, x)
        if key in insns:
            continue
        n += 1
        if n > limit:
            raise Unsupported("function too large / unbounded")
        ins, nm, nx = _decode_at(data, bank, pc, m, x)
        ins["next_m8"], ins["next_x8"] = nm, nx
        insns[key] = ins
        op = ins["op"]
        if op in TERMINALS or op in TAILJMP:
            continue                                   # no in-function successor
        nxt = (pc + ins["total"]) & 0xFFFF
        if op in UNCOND:
            work.append((ins["target"], nm, nx))
        elif op in BRANCH:
            work.append((ins["target"], nm, nx)); work.append((nxt, nm, nx))
        else:
            # JSR/JSL fall through (we assume the callee preserves M/X — the diff
            # gate rejects any function where that doesn't hold).
            work.append((nxt, nm, nx))
    return insns


def _cfg_label(pc, m8, x8):
    return f"L_{pc:04X}_M{int(m8)}X{int(x8)}"


# ---- inline emit helpers -------------------------------------------------
def _ea(mode, val):
    """(bank_expr, addr_expr) for a memory operand."""
    if mode == "abs":   return ("g_cpu.DB", f"0x{val:04X}")
    if mode == "absx":  return ("g_cpu.DB", f"(uint16_t)(0x{val:04X} + g_cpu.X)")
    if mode == "absy":  return ("g_cpu.DB", f"(uint16_t)(0x{val:04X} + g_cpu.Y)")
    if mode == "dp":    return ("0x00", f"(uint16_t)(g_cpu.DP + 0x{val:02X})")
    if mode == "dpx":   return ("0x00", f"(uint16_t)(g_cpu.DP + 0x{val:02X} + g_cpu.X)")
    if mode == "dpy":   return ("0x00", f"(uint16_t)(g_cpu.DP + 0x{val:02X} + g_cpu.Y)")
    if mode == "long":  return (f"0x{(val >> 16) & 0xFF:02X}", f"0x{val & 0xFFFF:04X}")
    if mode == "longx": return (f"0x{(val >> 16) & 0xFF:02X}", f"(uint16_t)(0x{val & 0xFFFF:04X} + g_cpu.X)")
    raise Unsupported(f"addr mode {mode}")


def _wide(insn, kind):  # kind 'm' (accumulator) or 'x' (index)
    return (not insn["m8"]) if kind == "m" else (not insn["x8"])


# --- cycle-accurate cost model (master cycles per instruction) -------------
# Mirrors LakeSnes snes_getAccessTime (FastROM assumed, as SMK enables it).
def _access_time(bank, addr):
    b, a = bank & 0xFF, addr & 0xFFFF
    if (b < 0x40 or (0x80 <= b < 0xC0)) and a < 0x8000:
        if a < 0x2000 or a >= 0x6000:
            return 8          # WRAM-low / $6000-7FFF
        if a < 0x4000 or a >= 0x4200:
            return 6          # PPU/APU regs
        return 12             # $4000-41FF (joypad)
    return 6 if b >= 0x80 else 8   # ROM (FastROM bank >= $80 -> 6)

# Estimated access time of a data operand, from its addressing mode + the
# static operand (the runtime data bank isn't known, so dp/abs/index are
# treated as WRAM/IO by their offset — the dominant case in SMK game logic).
def _data_time(insn):
    mode, val = insn["mode"], insn["val"]
    if mode in ("dp", "dpx", "dpy"):
        return _access_time(0x00, val & 0xFF)          # direct page -> bank 0
    if mode in ("abs", "absx", "absy"):
        return _access_time(0x00, val)                  # DB assumed WRAM/IO bank
    if mode in ("long", "longx"):
        return _access_time((val >> 16) & 0xFF, val & 0xFFFF)
    return 8

# data operand byte count (0 for imm/imp/branches), and whether the op is RMW.
_READ = {"LDA", "LDX", "LDY", "AND", "ORA", "EOR", "ADC", "SBC", "CMP", "CPX", "CPY", "BIT"}
_WRITE = {"STA", "STX", "STY", "STZ"}
_RMW = {"INC", "DEC", "ASL", "LSR", "ROL", "ROR", "TSB", "TRB"}
_IDLE1 = {"TAX", "TAY", "TXA", "TYA", "TSX", "TXS", "TXY", "TYX", "TCD", "TDC", "TCS", "TSC",
          "INX", "INY", "DEX", "DEY", "CLC", "SEC", "CLI", "SEI", "CLD", "SED", "CLV",
          "XCE", "NOP", "REP", "SEP"}


def _instr_cycles(insn, bank):
    """Best-effort master-cycle cost: fetch + data access + internal idles."""
    op, name, mode = insn["op"], insn["name"], insn["mode"]
    pbt = _access_time(bank, insn["pc"])               # program-bank fetch time
    cyc = insn["total"] * pbt                          # opcode + operand fetches
    mem = mode in MEM_MODES
    if mem:
        w = 2 if _wide(insn, "x" if name in ("LDX", "LDY", "STX", "STY", "CPX", "CPY") else "m") else 1
        dt = _data_time(insn)
        if name in _READ or name == "BIT":
            cyc += w * dt
        elif name in _WRITE:
            cyc += w * dt
        elif name in _RMW:
            cyc += 2 * w * dt + 6                       # read + write + 1 modify idle
    if name in _IDLE1:
        cyc += 6
    elif name == "XBA":
        cyc += 12
    elif name in _RMW and mode == "imp":               # accumulator RMW
        cyc += 6
    elif op == 0x20:                                    # JSR abs
        cyc += 6 + 2 * 8                                # 1 idle + 2 stack writes
    elif op == 0xFC:                                    # JSR (abs,x)
        table_time = _access_time(bank, insn["val"])
        cyc += 6 + 2 * 8 + 2 * table_time               # idle + stack + table reads
    elif op == 0x22:                                   # JSL
        cyc += 6 + 3 * 8
    elif name in ("MVN", "MVP"):                     # one repeated byte
        dest = insn["val"] & 0xFF
        src = (insn["val"] >> 8) & 0xFF
        cyc += _access_time(src, 0) + _access_time(dest, 0) + 12
    elif op == 0x6C:                                   # JMP (abs)
        cyc += (_access_time(0x00, insn["val"]) +
                _access_time(0x00, (insn["val"] + 1) & 0xFFFF))
    elif op == 0x7C:                                   # JMP (abs,X)
        cyc += 6 + 2 * _access_time(bank, insn["val"])
    elif op == 0xDC:                                   # JML [abs]
        cyc += sum(_access_time(0x00, (insn["val"] + i) & 0xFFFF)
                   for i in range(3))
    elif op == 0x60:                                    # RTS
        cyc += 18 + 2 * 8                               # 3 idles + 2 stack reads
    elif op == 0x6B:                                    # RTL
        cyc += 12 + 3 * 8                               # 2 idles + 3 stack reads
    elif op == 0x40:                                    # RTI
        cyc += 12 + 4 * 8                               # 2 idles + P/PC/PB pulls
    elif name in ("PHA", "PHX", "PHY", "PHB", "PHP", "PHK", "PHD"):
        wide = (name == "PHD" or
                (name == "PHA" and _wide(insn, "m")) or
                (name in ("PHX", "PHY") and _wide(insn, "x")))
        cyc += 6 + (16 if wide else 8)                  # idle + stack writes
    elif name in ("PLA", "PLX", "PLY", "PLB", "PLP", "PLD"):
        wide = (name == "PLD" or
                (name == "PLA" and _wide(insn, "m")) or
                (name in ("PLX", "PLY") and _wide(insn, "x")))
        cyc += 12 + (16 if wide else 8)                 # idles + stack reads
    return cyc


def _phase_tail(insn):
    """Return (internal master cycles, synthetic stack accesses) after semantics."""
    op, name, mode = insn["op"], insn["name"], insn["mode"]
    if name in _IDLE1:
        return 6, 0
    if name == "XBA":
        return 6, 0
    if name in _RMW:
        return (6, 0) if mode == "imp" else (0, 0)
    if name in ("PHA", "PHX", "PHY", "PHB", "PHP", "PHK", "PHD"):
        return 0, 0
    if name in ("PLA", "PLX", "PLY", "PLB", "PLP", "PLD"):
        return 0, 0
    return 0, 0


def _fetch_check_after(insn):
    """Opcode/operand fetch count after which LakeSnes calls checkInt.

    None means the check belongs to a later data/idle/stack microphase.  The
    immediate-width rule places 8-bit checks before the operand and 16-bit
    checks between its low and high bytes.
    """
    name, mode = insn["name"], insn["mode"]
    if insn["op"] == 0x4C:       # JMP abs: between operand bytes
        return 2
    if insn["op"] == 0x5C:       # JML long: before bank operand
        return 3
    if name in ("REP", "SEP"):
        return insn["total"]
    if mode in ("imm8", "immA", "immX") and name != "PEA":
        return 1 if insn["total"] == 2 else 2
    if name in _IDLE1 or (name in _RMW and mode == "imp"):
        return insn["total"]
    return None


def _address_phase_prelude(insn):
    """LakeSnes addressing idles that occur after operand fetches.

    Indirect modes own their idles in smk_ea_* because some penalties occur
    after pointer reads. Static modes can emit them before the data semantics.
    """
    name, mode, val = insn["name"], insn["mode"], insn["val"]
    out = []
    if mode in ("dp", "dpx", "dpy"):
        out.append("if (g_cpu.DP & 0x00FF) recomp_phase_penalty(6);")
        if mode in ("dpx", "dpy"):
            out.append("recomp_phase_penalty(6);")
    elif mode in ("absx", "absy"):
        write = name in _WRITE or name in _RMW or name in ("TSB", "TRB")
        index = "g_cpu.X" if mode == "absx" else "g_cpu.Y"
        if write:
            out.append("recomp_phase_penalty(6);")
        else:
            out.append(
                f"if (!g_cpu.flag_X || ((0x{val:04X} >> 8) != "
                f"((0x{val:04X} + (uint32_t){index}) >> 8))) "
                "recomp_phase_penalty(6);"
            )
    # Stack-relative forms are represented by smk_ea_sr/sriy helpers, which
    # own their idles so indirect timing can straddle the pointer reads.
    return out


def _phase_prelude(insn):
    """Timed microphases that precede the C-level architectural semantics."""
    name = insn["name"]
    out = _address_phase_prelude(insn)
    pushes = {"PHA", "PHX", "PHY", "PHD"}
    pulls = {"PLA", "PLX", "PLY", "PLD"}
    byte_pushes = {"PHP", "PHB", "PHK"}
    byte_pulls = {"PLB"}
    if name in pushes:
        kind = "m" if name == "PHA" else "x"
        wide = name == "PHD" or _wide(insn, kind)
        out += ["recomp_phase_idle(6);"] + ([] if wide else ["recomp_phase_check_int();"])
    if name in pulls:
        kind = "m" if name == "PLA" else "x"
        wide = name == "PLD" or _wide(insn, kind)
        out += ["recomp_phase_idle(12);"] + ([] if wide else ["recomp_phase_check_int();"])
    if name in byte_pushes:
        out += ["recomp_phase_idle(6);", "recomp_phase_check_int();"]
    if name in byte_pulls:
        out += ["recomp_phase_idle(12);", "recomp_phase_check_int();"]
    if name == "XBA":
        out += ["recomp_phase_idle(6);", "recomp_phase_check_int();"]
    return out


def _reg_write(reg, w, v):
    if reg == "A":
        return (f"g_cpu.C = (uint16_t)(({v}) & 0xFFFF);" if w == 16
                else f"g_cpu.C = (uint16_t)((g_cpu.C & 0xFF00) | (({v}) & 0xFF));")
    field = "g_cpu." + reg
    return (f"{field} = (uint16_t)(({v}) & 0xFFFF);" if w == 16
            else f"{field} = (uint16_t)(({v}) & 0xFF);")


def _reg_read(reg, w):
    field = "g_cpu.C" if reg == "A" else "g_cpu." + reg
    return field if w == 16 else f"(uint8_t)({field} & 0xFF)"


def _nz(w, v):
    bit = 15 if w == 16 else 7
    return (f"g_cpu.flag_N = (uint8_t)(((uint{w}_t)({v}) >> {bit}) & 1); "
            f"g_cpu.flag_Z = (uint8_t)((uint{w}_t)({v}) == 0);")


def _src(insn, w):
    """C expression for a read operand (immediate or memory)."""
    mode, val = insn["mode"], insn["val"]
    if mode in ("imm8", "immA", "immX"):
        return f"0x{val:0{w // 4}X}"
    if mode in STATIC_MEM_MODES:
        bank, addr = _ea(mode, val)
        return f"bus_read{w}_checked({bank}, {addr})"
    if mode in INDIRECT_MODES:
        write_arg = ", false" if mode == "dpiy" else ""
        return f"smk_bus_read{w}_24_checked(smk_ea_{mode}(0x{val:02X}{write_arg}))"
    raise Unsupported(f"{insn['name']} {mode}")


def _load(insn, reg, kind):
    w = 16 if _wide(insn, kind) else 8
    return f"{{ uint{w}_t _v = (uint{w}_t)({_src(insn, w)}); {_reg_write(reg, w, '_v')} {_nz(w, '_v')} }}"


def _logical(insn, c_op):  # AND/ORA/EOR: A = A <op> src; N/Z
    w = 16 if _wide(insn, "m") else 8
    return (f"{{ uint{w}_t _a = (uint{w}_t)({_reg_read('A', w)}); "
            f"uint{w}_t _v = (uint{w}_t)({_src(insn, w)}); "
            f"_a = (uint{w}_t)(_a {c_op} _v); {_reg_write('A', w, '_a')} {_nz(w, '_a')} }}")


def _arithmetic(insn, name):  # ADC/SBC, including runtime decimal mode
    w = 16 if _wide(insn, "m") else 8
    return (f"{{ uint{w}_t _v = (uint{w}_t)({_src(insn, w)}); "
            f"smk_op_{name.lower()}{w}(_v); }}")


def _block_move(insn, forward):  # one MVN/MVP byte; the CFG emits the repeat
    dest = insn["val"] & 0xFF
    src = (insn["val"] >> 8) & 0xFF
    delta = "+ 1" if forward else "- 1"
    xmask = " g_cpu.X &= 0x00FF; g_cpu.Y &= 0x00FF;" if insn["x8"] else ""
    return (f"{{ g_cpu.DB = 0x{dest:02X}; uint8_t _v = bus_read8(0x{src:02X}, g_cpu.X); "
            f"bus_write8(0x{dest:02X}, g_cpu.Y, _v); "
            f"g_cpu.C--; g_cpu.X = (uint16_t)(g_cpu.X {delta}); "
            f"g_cpu.Y = (uint16_t)(g_cpu.Y {delta});{xmask} "
            f"}}")


def _cmp(insn, reg, kind):  # CMP/CPX/CPY: flags from reg - src
    w = 16 if _wide(insn, kind) else 8
    return (f"{{ uint{w}_t _a = (uint{w}_t)({_reg_read(reg, w)}); "
            f"uint{w}_t _v = (uint{w}_t)({_src(insn, w)}); uint{w}_t _t = (uint{w}_t)(_a - _v); "
            f"g_cpu.flag_C = (uint8_t)(_a >= _v); g_cpu.flag_Z = (uint8_t)(_a == _v); "
            f"g_cpu.flag_N = (uint8_t)((_t >> {w - 1}) & 1); }}")


def _incdec(insn, reg, delta):  # INX/INY/DEX/DEY (X width)
    w = 16 if _wide(insn, "x") else 8
    sign = "+" if delta > 0 else "-"
    return (f"{{ uint{w}_t _t = (uint{w}_t)(({_reg_read(reg, w)}) {sign} 1); "
            f"{_reg_write(reg, w, '_t')} {_nz(w, '_t')} }}")


def _incdec_val(insn, delta):  # INC/DEC accumulator (imp) or memory (M width)
    w = 16 if _wide(insn, "m") else 8
    sign = "+" if delta > 0 else "-"
    if insn["mode"] == "imp":
        return (f"{{ uint{w}_t _t = (uint{w}_t)(({_reg_read('A', w)}) {sign} 1); "
                f"{_reg_write('A', w, '_t')} {_nz(w, '_t')} }}")
    if insn["mode"] in MEM_MODES:
        bank, addr = _ea(insn["mode"], insn["val"])
        return (f"{{ uint8_t _bk = (uint8_t)({bank}); uint16_t _ad = (uint16_t)({addr}); "
                f"uint{w}_t _t = (uint{w}_t)(bus_read{w}(_bk, _ad) {sign} 1); "
                f"recomp_phase_idle(6); "
                f"bus_write{w}{'_reversed' if w == 16 else ''}_checked(_bk, _ad, _t); {_nz(w, '_t')} }}")
    raise Unsupported(f"{insn['name']} {insn['mode']}")


def _shift_xform(name, w):  # (carry_from_x, result_from_x[+oldC])
    if name == "ASL": return (f"(uint8_t)((_x >> {w - 1}) & 1)", f"(uint{w}_t)(_x << 1)")
    if name == "LSR": return ("(uint8_t)(_x & 1)", f"(uint{w}_t)(_x >> 1)")
    if name == "ROL": return (f"(uint8_t)((_x >> {w - 1}) & 1)",
                              f"(uint{w}_t)((_x << 1) | (g_cpu.flag_C ? 1 : 0))")
    if name == "ROR": return ("(uint8_t)(_x & 1)",
                              f"(uint{w}_t)((_x >> 1) | ((g_cpu.flag_C ? 1u : 0u) << {w - 1}))")
    raise Unsupported(name)


def _shift(insn, name):  # ASL/LSR/ROL/ROR, accumulator (imp) or memory (M width)
    w = 16 if _wide(insn, "m") else 8
    ce, re = _shift_xform(name, w)
    if insn["mode"] == "imp":
        return (f"{{ uint{w}_t _x = (uint{w}_t)({_reg_read('A', w)}); uint8_t _c = {ce}; "
                f"uint{w}_t _r = {re}; g_cpu.flag_C = _c; {_reg_write('A', w, '_r')} {_nz(w, '_r')} }}")
    if insn["mode"] in MEM_MODES:
        bank, addr = _ea(insn["mode"], insn["val"])
        return (f"{{ uint8_t _bk = (uint8_t)({bank}); uint16_t _ad = (uint16_t)({addr}); "
                f"uint{w}_t _x = bus_read{w}(_bk, _ad); uint8_t _c = {ce}; uint{w}_t _r = {re}; "
                f"recomp_phase_idle(6); "
                f"bus_write{w}{'_reversed' if w == 16 else ''}_checked(_bk, _ad, _r); "
                f"g_cpu.flag_C = _c; {_nz(w, '_r')} }}")
    raise Unsupported(f"{name} {insn['mode']}")


def _bit(insn):  # BIT: Z from A&M; BIT # only touches Z; BIT mem also sets N,V from M
    w = 16 if _wide(insn, "m") else 8
    if insn["mode"] in ("imm8", "immA"):
        return (f"g_cpu.flag_Z = (uint8_t)(((uint{w}_t)({_reg_read('A', w)}) & "
                f"(uint{w}_t)0x{insn['val']:0{w // 4}X}) == 0);")
    if insn["mode"] in MEM_MODES:
        return (f"{{ uint{w}_t _m = (uint{w}_t)({_src(insn, w)}); "
                f"uint{w}_t _a = (uint{w}_t)({_reg_read('A', w)}); "
                f"g_cpu.flag_Z = (uint8_t)((_a & _m) == 0); "
                f"g_cpu.flag_N = (uint8_t)((_m >> {w - 1}) & 1); "
                f"g_cpu.flag_V = (uint8_t)((_m >> {w - 2}) & 1); }}")
    raise Unsupported(f"BIT {insn['mode']}")


_FLAGOP = {"CLC": "g_cpu.flag_C = 0;", "SEC": "g_cpu.flag_C = 1;",
           "CLI": "g_cpu.flag_I = 0;", "SEI": "g_cpu.flag_I = 1;",
           "CLD": "g_cpu.flag_D = 0;", "SED": "g_cpu.flag_D = 1;",
           "CLV": "g_cpu.flag_V = 0;"}


def _store(insn, reg, kind):
    w = 16 if _wide(insn, kind) else 8
    mode, val = insn["mode"], insn["val"]
    if mode not in MEM_MODES:
        raise Unsupported(f"{insn['name']} {mode}")
    v = "0" if reg is None else _reg_read(reg, w)
    if mode in INDIRECT_MODES:
        write_arg = ", true" if mode == "dpiy" else ""
        return f"smk_bus_write{w}_24_checked(smk_ea_{mode}(0x{val:02X}{write_arg}), (uint{w}_t)({v}));"
    bank, addr = _ea(mode, val)
    return f"bus_write{w}_checked({bank}, {addr}, (uint{w}_t)({v}));"


def _stack(insn):
    """Width-correct register and bank/direct-page stack operations."""
    name = insn["name"]
    if name in ("PHA", "PHX", "PHY"):
        reg = {"PHA": "A", "PHX": "X", "PHY": "Y"}[name]
        kind = "m" if name == "PHA" else "x"
        w = 16 if _wide(insn, kind) else 8
        if w == 16:
            return f"recomp_stack_push16({_reg_read(reg, 16)}, true);"
        return f"recomp_stack_push8((uint8_t)({_reg_read(reg, 8)}));"
    if name in ("PLA", "PLX", "PLY"):
        reg = {"PLA": "A", "PLX": "X", "PLY": "Y"}[name]
        kind = "m" if name == "PLA" else "x"
        w = 16 if _wide(insn, kind) else 8
        if w == 16:
            return (f"{{ uint16_t _v = recomp_stack_pull16(true); "
                    f"{_reg_write(reg, 16, '_v')} {_nz(16, '_v')} }}")
        return ("{ uint8_t _v = recomp_stack_pull8(); "
                f"{_reg_write(reg, 8, '_v')} {_nz(8, '_v')} }}")
    if name == "PHK":
        return "recomp_stack_push8(g_cpu.PB);"
    if name == "PHD":
        return "recomp_stack_push16(g_cpu.DP, true);"
    if name == "PLD":
        return ("{ g_cpu.DP = recomp_stack_pull16(true); "
                f"{_nz(16, 'g_cpu.DP')} }}")
    if name == "PHP":
        return "recomp_stack_push8(cpu_get_p());"
    if name == "PHB":
        return "recomp_stack_push8(g_cpu.DB);"
    if name == "PLP":
        return ("{ uint8_t _p = recomp_stack_pull8(); recomp_phase_check_int(); "
                "cpu_set_p(_p); }")
    if name == "PLB":
        return ("{ g_cpu.DB = recomp_stack_pull8(); "
                f"{_nz(8, 'g_cpu.DB')} }}")
    raise Unsupported(f"stack op {name}")


def _transfer(insn):
    """Transfers not covered by cpu_ops.h, with their architectural width."""
    name = insn["name"]
    if name == "TSX":
        w = 16 if _wide(insn, "x") else 8
        return f"{{ uint{w}_t _v = (uint{w}_t)g_cpu.S; {_reg_write('X', w, '_v')} {_nz(w, '_v')} }}"
    if name == "TXS":
        return ("if (g_cpu.flag_X) { g_cpu.S = (uint16_t)((g_cpu.S & 0xFF00) | (g_cpu.X & 0xFF)); "
                "if (g_cpu.flag_E) g_cpu.S = (uint16_t)(0x0100 | (g_cpu.S & 0xFF)); } "
                "else g_cpu.S = g_cpu.X;")
    if name in ("TXY", "TYX"):
        src, dst = ("X", "Y") if name == "TXY" else ("Y", "X")
        w = 16 if _wide(insn, "x") else 8
        return f"{{ uint{w}_t _v = (uint{w}_t)({_reg_read(src, w)}); {_reg_write(dst, w, '_v')} {_nz(w, '_v')} }}"
    if name == "TCD":
        return f"g_cpu.DP = g_cpu.C; {_nz(16, 'g_cpu.DP')}"
    if name == "TDC":
        return f"g_cpu.C = g_cpu.DP; {_nz(16, 'g_cpu.C')}"
    if name == "TCS":
        return ("g_cpu.S = g_cpu.flag_E ? (uint16_t)(0x0100 | (g_cpu.C & 0xFF)) "
                ": g_cpu.C;")
    if name == "TSC":
        return f"g_cpu.C = g_cpu.S; {_nz(16, 'g_cpu.C')}"
    raise Unsupported(f"transfer op {name}")


def _tsb_trb(insn, set_bits):
    """TSB/TRB: Z reflects A & old memory; memory receives A | M / ~A & M."""
    w = 16 if _wide(insn, "m") else 8
    if insn["mode"] not in MEM_MODES:
        raise Unsupported(f"{insn['name']} {insn['mode']}")
    bank, addr = _ea(insn["mode"], insn["val"])
    expr = "(uint{w}_t)(_m | _a)" if set_bits else "(uint{w}_t)(_m & (uint{w}_t)~_a)"
    expr = expr.format(w=w)
    return (f"{{ uint8_t _bk = (uint8_t)({bank}); uint16_t _ad = (uint16_t)({addr}); "
            f"uint{w}_t _m = bus_read{w}(_bk, _ad); uint{w}_t _a = (uint{w}_t)({_reg_read('A', w)}); "
            f"g_cpu.flag_Z = (uint8_t)((_a & _m) == 0); recomp_phase_idle(6); "
            f"bus_write{w}{'_reversed' if w == 16 else ''}_checked(_bk, _ad, {expr}); }}")


# register/flag ops that already have an op_* helper
_SIMPLE = {
    "XBA": "op_xba();", "XCE": "op_xce();",
    "TAX": "op_tax();", "TAY": "op_tay();", "TXA": "op_txa();", "TYA": "op_tya();",
}
_STACK = {"PHA", "PLA", "PHX", "PLX", "PHY", "PLY", "PHK", "PHD", "PLD",
          "PHP", "PLP", "PHB", "PLB"}
_TRANSFER = {"TSX", "TXS", "TXY", "TYX", "TCD", "TDC", "TCS", "TSC"}
_LOADS = {"LDA": ("A", "m"), "LDX": ("X", "x"), "LDY": ("Y", "x")}
_STORES = {"STA": ("A", "m"), "STX": ("X", "x"), "STY": ("Y", "x"), "STZ": (None, "m")}
_LOGIC = {"AND": "&", "ORA": "|", "EOR": "^"}
_INCDEC = {"INX": ("X", +1), "INY": ("Y", +1), "DEX": ("X", -1), "DEY": ("Y", -1)}


def emit_body(insn, bank=0):
    op, name = insn["op"], insn["name"]
    if name in ("REP", "SEP"):
        return f"op_{name.lower()}(0x{insn['val']:02X});"
    if name in _SIMPLE:
        return _SIMPLE[name]
    if name in _STACK:
        return _stack(insn)
    if name in _TRANSFER:
        return _transfer(insn)
    if name in _LOADS:
        return _load(insn, *_LOADS[name])
    if name in _STORES:
        return _store(insn, *_STORES[name])
    if name in _LOGIC:
        return _logical(insn, _LOGIC[name])
    if name in ("ADC", "SBC"):
        return _arithmetic(insn, name)
    if name in ("MVN", "MVP"):
        return _block_move(insn, name == "MVN")
    if name == "CMP":
        return _cmp(insn, "A", "m")
    if name == "CPX":
        return _cmp(insn, "X", "x")
    if name == "CPY":
        return _cmp(insn, "Y", "x")
    if name in _INCDEC:
        return _incdec(insn, *_INCDEC[name])
    if name in _FLAGOP:
        return _FLAGOP[name]
    if name in ("ASL", "LSR", "ROL", "ROR"):
        return _shift(insn, name)
    if name == "BIT":
        return _bit(insn)
    if name in ("TSB", "TRB"):
        return _tsb_trb(insn, name == "TSB")
    if name in ("INC", "DEC"):
        return _incdec_val(insn, +1 if name == "INC" else -1)
    if name == "PEA":  # push 16-bit immediate operand
        return f"recomp_stack_push16(0x{insn['val']:04X}, true);"
    if name == "NOP":
        return "(void)0;"
    raise Unsupported(f"no emit rule for {name} {insn['mode']} (${op:02X}) at ${insn['pc']:04X}")


def generate(data, bank, addr, P, name):
    """Generate for one P, an iterable of observed P values, or all if P is None."""
    Ps = list((0x00, 0x10, 0x20, 0x30) if P is None else
              (P,) if isinstance(P, int) else P)
    states = sorted(set((bool(p & 0x20), bool(p & 0x10)) for p in Ps))
    entries = [(addr, m, x) for m, x in states]
    if len(entries) > 1:
        insns = decode_cfg(data, bank, addr, entry_states=entries)
    else:
        m8, x8 = states[0]
        insns = decode_cfg(data, bank, addr, m8, x8)
    out = []
    pc24 = (bank << 16) | addr
    out.append(f"/* Auto-generated by tools/recomp/autogen.py from ${bank:02X}:{addr:04X}")
    if len(entries) > 1:
        p_text = ",".join(f"${p:02X}" for p in sorted(set(Ps)))
        out.append(f" * observed entry P(M/X)={p_text}; selected from live CPU flags. Validate with the diff harness. */")
    else:
        out.append(f" * entry M={int(m8)} X={int(x8)} (P=${P:02X}). Validate with the diff harness. */")
    out.append(f"RECOMP_PATCH({name}, 0x{pc24:06X}) {{")
    # Every decoded instruction is an explicit CFG block. This is slightly more
    # verbose than C fallthrough, but it makes address order irrelevant and lets
    # one PC have separate M/X-width variants safely.
    if len(entries) > 1:
        for index, (_, m, x) in enumerate(entries[:-1]):
            prefix = "if" if index == 0 else "else if"
            out.append(f"    {prefix} (g_cpu.flag_M == {int(m)} && g_cpu.flag_X == {int(x)}) goto {_cfg_label(addr, m, x)};")
        _, m, x = entries[-1]
        out.append(f"    else goto {_cfg_label(addr, m, x)};")
    else:
        out.append(f"    goto {_cfg_label(addr, m8, x8)};")
    for key in sorted(insns):
        pc, state_m, state_x = key
        ins = insns[key]
        nm, nx = ins["next_m8"], ins["next_x8"]
        nxt = (pc + ins["total"]) & 0xFFFF
        out.append(f"  {_cfg_label(pc, state_m, state_x)}:;")
        op = ins["op"]
        # JSL defers its bank operand until after PB push + idle. JSR
        # (abs,X) defers operand-high until after its return-word push.
        phase_fetches = ins["total"] - 1 if op in (0x22, 0xFC) else ins["total"]
        if op in BRANCH:
            flag, want = BRANCH[op]
            branch_cond = f"g_cpu.flag_{flag}" if want else f"!g_cpu.flag_{flag}"
            check_after = f"({branch_cond}) ? {ins['total']} : 1"
        elif op in UNCOND:
            check_after = str(ins["total"])
        else:
            check_after = _fetch_check_after(ins)
        if check_after is None:
            out.append(f"    recomp_phase_begin({_instr_cycles(ins, bank)}, 0x{bank:02X}, 0x{pc:04X}, {phase_fetches});")
        else:
            out.append(f"    recomp_phase_begin_checked({_instr_cycles(ins, bank)}, 0x{bank:02X}, 0x{pc:04X}, {phase_fetches}, {check_after});")
        idle, stack = _phase_tail(ins)
        if ins["name"] in ("MVN", "MVP"):
            # The hardware rewinds PC and refetches all three bytes for each
            # transferred byte.  End every iteration with idle/check/idle so
            # an interrupt or video boundary resumes at the correct PC.
            out.append(f"    {emit_body(ins, bank):<46s} /* ${pc:04X} {ins['name']} */")
            out.append("    recomp_phase_idle(6);")
            out.append("    recomp_phase_check_int();")
            out.append("    recomp_phase_idle(6);")
            out.append("    recomp_phase_end(0, 0);")
            out.append(f"    if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(g_cpu.C != 0xFFFF ? 0x{(bank << 16) | pc:06X} : 0x{(bank << 16) | nxt:06X}); return; }}")
            out.append(f"    if (g_cpu.C != 0xFFFF) goto {_cfg_label(pc, state_m, state_x)};")
            out.append(f"    goto {_cfg_label(nxt, nm, nx)};")
        elif op in TERMINALS:
            if op in (0x60, 0x6B):
                out.append(f"    recomp_phase_return({'true' if op == 0x6B else 'false'});")
            elif op == 0x40:
                out.append("    recomp_phase_idle(12);")
                out.append("    { uint8_t _p = recomp_stack_pull8();")
                out.append("      cpu_set_p(_p);")
                out.append("      uint16_t _pc = recomp_stack_pull16(false);")
                out.append("      recomp_phase_check_int();")
                out.append("      uint8_t _pb = recomp_stack_pull8();")
                out.append("      g_cpu.PB = _pb;")
                out.append("      recomp_phase_end(0, 0);")
                out.append("      recomp_set_redirect(((uint32_t)_pb << 16) | _pc); }")
            else:
                out.append(f"    recomp_phase_end({idle}, {stack});")
            out.append("    (void)recomp_phase_interrupt_pending();")
            out.append(f"    return;            /* ${pc:04X} {ins['name']} */")
        elif op in CALL:
            if op == 0xFC:
                # JSR (abs,X): opcode+operand-low, return-word push,
                # operand-high, idle, checked jump-table word.
                out.append(f"    {{ recomp_call_frame_t _frame = recomp_phase_call_enter_indirect(0x{(nxt - 1) & 0xFFFF:04X}, 0x{bank:02X});")
                out.append(f"      uint16_t _t = bus_read16_checked(0x{bank:02X}, (uint16_t)(0x{ins['val']:04X} + g_cpu.X));")
                out.append("      recomp_phase_end(0, 0);")
                out.append(f"      if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(((uint32_t)0x{bank:02X} << 16) | _t); return; }}")
                out.append(f"      bool _frame_consumed = func_table_call_with_frame(((uint32_t)0x{bank:02X} << 16) | _t, false, &_frame);")
                out.append("      if (recomp_redirect_pending()) return;")
                out.append(f"      recomp_phase_call_leave(_frame, _frame_consumed);")
                out.append(f"      if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(0x{(bank << 16) | nxt:06X}); return; }} }}  /* ${pc:04X} {ins['name']} */")
            else:
                is_long = op == 0x22
                frame_var = f"_frame_{pc:04X}_M{int(state_m)}X{int(state_x)}"
                direct_target = _call_target(ins, bank)
                target_bank = (direct_target >> 16) & 0xFF
                consumed_var = f"_frame_consumed_{pc:04X}_M{int(state_m)}X{int(state_x)}"
                out.append(f"    recomp_call_frame_t {frame_var} = recomp_phase_call_enter(0x{(nxt - 1) & 0xFFFF:04X}, 0x{bank:02X}, 0x{target_bank:02X}, {'true' if is_long else 'false'}, true);")
                out.append(f"    if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(0x{direct_target:06X}); return; }}")
                out.append(f"    bool {consumed_var} = func_table_call_with_frame(0x{direct_target:06X}, {'true' if is_long else 'false'}, &{frame_var});  /* ${pc:04X} {ins['name']} */")
                out.append("    if (recomp_redirect_pending()) return;")
                out.append(f"    recomp_phase_call_leave({frame_var}, {consumed_var});")
                out.append(f"    if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(0x{(bank << 16) | nxt:06X}); return; }}")
            out.append(f"    goto {_cfg_label(nxt, nm, nx)};")
        elif op in TAILJMP:
            if op in (0x4C, 0x5C):
                direct_target = _call_target(ins, bank)
                out.append("    recomp_phase_end(0, 0);")
                out.append(f"    if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(0x{direct_target:06X}); return; }}")
                local_key = (direct_target & 0xFFFF, nm, nx)
                if op == 0x4C and local_key in insns:
                    out.append(f"    goto {_cfg_label(direct_target & 0xFFFF, nm, nx)};  /* ${pc:04X} {ins['name']} (local tail) */")
                else:
                    out.append(f"    {_transfer_stmt(ins, bank)} return;  /* ${pc:04X} {ins['name']} (tail) */")
            elif op in (0x6C, 0x7C):
                ptr_bank = 0x00 if op == 0x6C else bank
                ptr_expr = (f"0x{ins['val']:04X}" if op == 0x6C else
                            f"(uint16_t)(0x{ins['val']:04X} + g_cpu.X)")
                out.append(f"    {{ uint16_t _ad = {ptr_expr};")
                if op == 0x7C:
                    out.append("      recomp_phase_idle(6);")
                out.append(f"      uint8_t _lo = bus_read8(0x{ptr_bank:02X}, _ad);")
                out.append("      recomp_phase_check_int();")
                out.append(f"      uint8_t _hi = bus_read8(0x{ptr_bank:02X}, (uint16_t)(_ad + 1));")
                out.append("      uint16_t _t = (uint16_t)(_lo | ((uint16_t)_hi << 8));")
                out.append("      recomp_phase_end(0, 0);")
                out.append(f"      uint32_t _target = ((uint32_t)0x{bank:02X} << 16) | _t;")
                out.append("      if (recomp_phase_interrupt_pending()) { recomp_set_redirect(_target); return; }")
                out.append(f"      func_table_call_jsr(_target); return; }}  /* ${pc:04X} {ins['name']} (tail) */")
            else:  # JML [abs]
                out.append(f"    {{ uint16_t _ad = 0x{ins['val']:04X};")
                out.append("      uint8_t _lo = bus_read8(0x00, _ad);")
                out.append("      uint8_t _hi = bus_read8(0x00, (uint16_t)(_ad + 1));")
                out.append("      recomp_phase_check_int();")
                out.append("      uint8_t _bk = bus_read8(0x00, (uint16_t)(_ad + 2));")
                out.append("      uint32_t _target = (uint32_t)_lo | ((uint32_t)_hi << 8) | ((uint32_t)_bk << 16);")
                out.append("      recomp_phase_end(0, 0);")
                out.append("      if (recomp_phase_interrupt_pending()) { recomp_set_redirect(_target); return; }")
                out.append(f"      func_table_call(_target); return; }}  /* ${pc:04X} {ins['name']} (tail) */")
        elif op in UNCOND:
            out.append(f"    recomp_phase_end({idle}, {stack});")
            # BRA/BRL spend one internal cycle after fetching the displacement.
            out.append("    recomp_tick(6);")
            out.append(f"    if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(0x{(bank << 16) | ins['target']:06X}); return; }}")
            out.append(f"    goto {_cfg_label(ins['target'], nm, nx)};   /* ${pc:04X} {ins['name']} */")
        elif op in BRANCH:
            out.append(f"    recomp_phase_end({idle}, {stack});")
            flag, want = BRANCH[op]
            cond = f"g_cpu.flag_{flag}" if want else f"!g_cpu.flag_{flag}"
            # A taken conditional branch has one extra internal cycle. Page-cross
            # penalties only apply in emulation mode and are added separately once
            # E-mode entry profiling is available.
            out.append(f"    if ({cond}) {{ recomp_tick(6); if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(0x{(bank << 16) | ins['target']:06X}); return; }} goto {_cfg_label(ins['target'], nm, nx)}; }}  /* ${pc:04X} {ins['name']} */")
            out.append(f"    if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(0x{(bank << 16) | nxt:06X}); return; }}")
            out.append(f"    goto {_cfg_label(nxt, nm, nx)};")
        else:
            for prelude in _phase_prelude(ins):
                out.append(f"    {prelude}")
            out.append(f"    {emit_body(ins, bank):<46s} /* ${pc:04X} {ins['name']} */")
            out.append(f"    recomp_phase_end({idle}, {stack});")
            out.append(f"    if (recomp_phase_interrupt_pending()) {{ recomp_set_redirect(0x{(bank << 16) | nxt:06X}); return; }}")
            out.append(f"    goto {_cfg_label(nxt, nm, nx)};")
    out.append("}")
    return "\n".join(out)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    rom_path, loc, p_hex = sys.argv[1], sys.argv[2], sys.argv[3]
    bank, addr = (int(v, 16) for v in loc.split(":"))
    if p_hex.lower() in ("multi", "all"):
        P = None
    elif "," in p_hex:
        P = [int(p, 16) for p in p_hex.split(",")]
    else:
        P = int(p_hex, 16)
    name = sys.argv[4] if len(sys.argv) > 4 else f"smk_{bank:02X}{addr:04X}"
    data = open(rom_path, "rb").read()
    if len(data) % 1024 == 512:
        data = data[512:]
    try:
        print(generate(data, bank, addr, P, name))
    except Unsupported as e:
        print(f"// UNSUPPORTED: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
