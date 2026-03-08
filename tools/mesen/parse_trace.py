#!/usr/bin/env python3
"""
parse_trace.py — Parse Mesen2 trace logs into structured data for recompilation.

Reads the trace output from trace_capture.lua and produces:
  1. A sorted list of all unique executed addresses
  2. Function boundary candidates (targets of JSR/JSL)
  3. P-flag state (M/X bits) at each address for correct instruction decoding
  4. Code coverage statistics per bank

Usage:
    py tools/mesen/parse_trace.py smk_trace.log [--output-dir output/]
"""

import re
import sys
import json
import argparse
from pathlib import Path
from collections import defaultdict
from dataclasses import dataclass, field


@dataclass
class TraceEntry:
    bank: int
    addr: int
    full_addr: int
    opcodes: bytes
    a: int
    x: int
    y: int
    sp: int
    db: int
    dp: int
    p: int
    flags: str

    @property
    def flag_m(self) -> bool:
        return (self.p & 0x20) != 0

    @property
    def flag_x(self) -> bool:
        return (self.p & 0x10) != 0


# Regex for trace_capture.lua output format:
# "80:FF70  78 00 00 00  A:0000 X:0000 Y:0000 SP:01FF DB:00 DP:0000 P:34  nvMXdIzc"
TRACE_RE = re.compile(
    r"([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+"
    r"([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+([0-9A-Fa-f]{2})\s+"
    r"A:([0-9A-Fa-f]{4})\s+X:([0-9A-Fa-f]{4})\s+Y:([0-9A-Fa-f]{4})\s+"
    r"SP:([0-9A-Fa-f]{4})\s+DB:([0-9A-Fa-f]{2})\s+DP:([0-9A-Fa-f]{4})\s+"
    r"P:([0-9A-Fa-f]{2})\s+([NnVvMmXxDdIiZzCc]{8})"
)


def parse_trace_file(path: Path) -> list[TraceEntry]:
    """Parse a trace log file into a list of TraceEntry objects."""
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        m = TRACE_RE.search(line)
        if not m:
            continue
        bank = int(m.group(1), 16)
        addr = int(m.group(2), 16)
        opcodes = bytes([int(m.group(i), 16) for i in range(3, 7)])
        entries.append(TraceEntry(
            bank=bank,
            addr=addr,
            full_addr=(bank << 16) | addr,
            opcodes=opcodes,
            a=int(m.group(7), 16),
            x=int(m.group(8), 16),
            y=int(m.group(9), 16),
            sp=int(m.group(10), 16),
            db=int(m.group(11), 16),
            dp=int(m.group(12), 16),
            p=int(m.group(13), 16),
            flags=m.group(14),
        ))
    return entries


def analyze_coverage(entries: list[TraceEntry]) -> dict:
    """Analyze code coverage by bank."""
    bank_addrs: dict[int, set[int]] = defaultdict(set)
    for e in entries:
        bank_addrs[e.bank].add(e.addr)

    coverage = {}
    for bank in sorted(bank_addrs.keys()):
        addrs = sorted(bank_addrs[bank])
        coverage[f"${bank:02X}"] = {
            "unique_addresses": len(addrs),
            "range": f"${addrs[0]:04X}-${addrs[-1]:04X}",
            "first_10": [f"${a:04X}" for a in addrs[:10]],
        }
    return coverage


def find_function_entries(entries: list[TraceEntry]) -> dict[int, dict]:
    """Identify likely function entry points from JSR/JSL targets."""
    # 65816 opcodes
    JSR_ABS = 0x20   # JSR $XXXX (3 bytes)
    JSL_LONG = 0x22  # JSL $XXXXXX (4 bytes)

    functions: dict[int, dict] = {}

    for e in entries:
        opcode = e.opcodes[0]

        if opcode == JSR_ABS:
            target = (e.bank << 16) | (e.opcodes[2] << 8) | e.opcodes[1]
            if target not in functions:
                functions[target] = {"type": "JSR", "callers": set(), "call_count": 0}
            functions[target]["callers"].add(e.full_addr)
            functions[target]["call_count"] += 1

        elif opcode == JSL_LONG:
            target = (e.opcodes[3] << 16) | (e.opcodes[2] << 8) | e.opcodes[1]
            if target not in functions:
                functions[target] = {"type": "JSL", "callers": set(), "call_count": 0}
            functions[target]["callers"].add(e.full_addr)
            functions[target]["call_count"] += 1

    # Convert sets to sorted lists for JSON serialization
    result = {}
    for addr in sorted(functions.keys()):
        info = functions[addr]
        result[f"${addr:06X}"] = {
            "type": info["type"],
            "call_count": info["call_count"],
            "callers": [f"${c:06X}" for c in sorted(info["callers"])],
        }
    return result


def build_mx_map(entries: list[TraceEntry]) -> dict[int, dict]:
    """Build a map of M/X flag state at each unique address.

    This is critical for correct 65816 instruction decoding — the M and X
    flags change the size of accumulator and index register operations.
    """
    mx_map: dict[int, dict] = {}
    for e in entries:
        addr = e.full_addr
        if addr not in mx_map:
            mx_map[addr] = {"m": e.flag_m, "x": e.flag_x, "p": e.p}
    return {f"${k:06X}": v for k, v in sorted(mx_map.items())}


def main():
    parser = argparse.ArgumentParser(description="Parse Mesen2 trace logs for SMK recompilation")
    parser.add_argument("trace_file", help="Path to trace log from trace_capture.lua")
    parser.add_argument("--output-dir", "-o", default=".", help="Output directory for analysis files")
    args = parser.parse_args()

    trace_path = Path(args.trace_file)
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Parsing trace: {trace_path}")
    entries = parse_trace_file(trace_path)
    print(f"  {len(entries)} trace entries loaded")

    if not entries:
        print("ERROR: No valid trace entries found. Check trace file format.")
        sys.exit(1)

    # Coverage analysis
    coverage = analyze_coverage(entries)
    coverage_path = out_dir / "coverage.json"
    with open(coverage_path, "w") as f:
        json.dump(coverage, f, indent=2)
    print(f"  Coverage analysis → {coverage_path}")
    for bank, info in coverage.items():
        print(f"    Bank {bank}: {info['unique_addresses']} unique addrs ({info['range']})")

    # Function entry points
    functions = find_function_entries(entries)
    funcs_path = out_dir / "functions.json"
    with open(funcs_path, "w") as f:
        json.dump(functions, f, indent=2)
    print(f"  {len(functions)} function entry points → {funcs_path}")

    # M/X flag map
    mx_map = build_mx_map(entries)
    mx_path = out_dir / "mx_flags.json"
    with open(mx_path, "w") as f:
        json.dump(mx_map, f, indent=2)
    print(f"  M/X flag map ({len(mx_map)} addrs) → {mx_path}")

    # Summary
    unique_addrs = len(set(e.full_addr for e in entries))
    banks_hit = len(set(e.bank for e in entries))
    print(f"\nSummary: {unique_addrs} unique addresses across {banks_hit} banks, "
          f"{len(functions)} function targets identified")


if __name__ == "__main__":
    main()
