#!/usr/bin/env python3
"""
parse_functions.py — Parse func_finder.lua JSON output and generate C stubs.

Reads smk_functions.json from Mesen2 and generates:
  1. C function declarations (smk_XXXXXX style)
  2. func_table registration calls
  3. Skeleton .c files organized by bank

Usage:
    py tools/mesen/parse_functions.py smk_functions.json [--output-dir src/game/]
"""

import sys
import json
import argparse
from pathlib import Path
from collections import defaultdict


def addr_to_func_name(addr_str: str) -> str:
    """Convert '$80FF70' to 'smk_80FF70'."""
    return "smk_" + addr_str.lstrip("$")


def generate_header(functions: dict[str, dict]) -> str:
    """Generate a C header with all function declarations."""
    lines = [
        "#ifndef SMK_GAME_FUNCS_H",
        "#define SMK_GAME_FUNCS_H",
        "",
        "/* Auto-generated from Mesen2 trace analysis */",
        "/* Function declarations for recompiled game code */",
        "",
    ]

    for addr in sorted(functions.keys()):
        name = addr_to_func_name(addr)
        info = functions[addr]
        call_type = info.get("type", "?")
        count = info.get("call_count", 0)
        lines.append(f"void {name}(void);  /* {call_type} target, called {count}x */")

    lines.extend(["", "#endif /* SMK_GAME_FUNCS_H */", ""])
    return "\n".join(lines)


def generate_registration(functions: dict[str, dict]) -> str:
    """Generate func_table registration code."""
    lines = [
        "/* Auto-generated: register all discovered game functions */",
        '#include "recomp/func_table.h"',
        '#include "game/game_funcs.h"',
        "",
        "void register_game_functions(void) {",
    ]

    for addr in sorted(functions.keys()):
        name = addr_to_func_name(addr)
        addr_int = int(addr.lstrip("$"), 16)
        lines.append(f"    func_table_register(0x{addr_int:06X}, {name});")

    lines.extend(["}", ""])
    return "\n".join(lines)


def generate_bank_stubs(functions: dict[str, dict]) -> dict[int, str]:
    """Generate skeleton .c files grouped by bank."""
    banks: dict[int, list[str]] = defaultdict(list)

    for addr in sorted(functions.keys()):
        addr_int = int(addr.lstrip("$"), 16)
        bank = (addr_int >> 16) & 0xFF
        banks[bank].append(addr)

    files = {}
    for bank in sorted(banks.keys()):
        lines = [
            f"/* bank_{bank:02X}.c — Recompiled functions from bank ${bank:02X} */",
            f"/* Auto-generated stubs — fill in with recompiled code */",
            "",
            '#include "recomp/cpu.h"',
            '#include "recomp/memory.h"',
            '#include "hal/ppu.h"',
            '#include "hal/apu.h"',
            '#include "hal/dma.h"',
            '#include "hal/io.h"',
            "",
        ]

        for addr in banks[bank]:
            name = addr_to_func_name(addr)
            info = functions[addr]
            callers = info.get("callers", [])
            caller_str = ", ".join(callers[:5])
            if len(callers) > 5:
                caller_str += f", ... (+{len(callers)-5} more)"

            lines.extend([
                f"/* {addr} — called from: {caller_str} */",
                f"void {name}(void) {{",
                f"    /* TODO: recompile from trace/disassembly */",
                f"}}",
                "",
            ])

        files[bank] = "\n".join(lines)
    return files


def main():
    parser = argparse.ArgumentParser(description="Generate C stubs from Mesen2 function map")
    parser.add_argument("func_file", help="Path to smk_functions.json")
    parser.add_argument("--output-dir", "-o", default="src/game", help="Output directory")
    parser.add_argument("--dry-run", action="store_true", help="Print output without writing files")
    args = parser.parse_args()

    with open(args.func_file) as f:
        functions = json.load(f)

    print(f"Loaded {len(functions)} function targets")

    # Generate all outputs
    header = generate_header(functions)
    registration = generate_registration(functions)
    bank_files = generate_bank_stubs(functions)

    if args.dry_run:
        print("\n=== game_funcs.h ===")
        print(header)
        print("\n=== register_funcs.c ===")
        print(registration)
        for bank, content in bank_files.items():
            print(f"\n=== bank_{bank:02X}.c ===")
            print(content)
        return

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    header_dir = Path("include/game")
    header_dir.mkdir(parents=True, exist_ok=True)

    # Write header
    header_path = header_dir / "game_funcs.h"
    header_path.write_text(header, encoding="utf-8")
    print(f"  Header → {header_path}")

    # Write registration
    reg_path = out_dir / "register_funcs.c"
    reg_path.write_text(registration, encoding="utf-8")
    print(f"  Registration → {reg_path}")

    # Write bank files
    for bank, content in bank_files.items():
        bank_path = out_dir / f"bank_{bank:02X}.c"
        bank_path.write_text(content, encoding="utf-8")
        print(f"  Bank ${bank:02X} → {bank_path}")

    print(f"\nDone. {len(functions)} functions across {len(bank_files)} banks.")


if __name__ == "__main__":
    main()
