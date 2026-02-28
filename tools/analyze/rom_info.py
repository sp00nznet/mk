#!/usr/bin/env python3
"""
ROM analysis tool for Super Mario Kart (SNES HiROM).

Parses the internal header, validates checksums, verifies MD5,
and dumps interrupt vectors and bank layout.
"""

import sys
import hashlib
import struct
from pathlib import Path

# Expected MD5 for the US v1 ROM
EXPECTED_MD5 = "7f25ce5a283d902694c52fb1152fa61a"
ROM_SIZE = 512 * 1024  # 512 KB

# HiROM header offset (within the ROM, no copier header)
HEADER_BASE = 0xFFB0


def read_rom(path: str) -> bytes:
    """Read a .sfc ROM file, stripping any copier header."""
    data = Path(path).read_bytes()
    if len(data) % 1024 == 512:
        print(f"Copier header detected ({len(data)} bytes), stripping 512 bytes")
        data = data[512:]
    return data


def parse_header(rom: bytes) -> dict:
    """Parse the HiROM internal header at $FFB0-$FFDF."""
    info = {}

    # Maker code ($FFB0-$FFB1)
    info["maker_code"] = rom[0xFFB0:0xFFB2].decode("ascii", errors="replace")

    # Game code ($FFB2-$FFB5)
    info["game_code"] = rom[0xFFB2:0xFFB6].decode("ascii", errors="replace")

    # Internal ROM name ($FFC0-$FFD4, 21 bytes)
    raw_title = rom[0xFFC0:0xFFD5]
    info["title"] = "".join(chr(b) if 0x20 <= b < 0x7F else "" for b in raw_title).rstrip()

    # Map mode ($FFD5)
    map_mode = rom[0xFFD5]
    info["map_mode_raw"] = map_mode
    speed = "FastROM" if (map_mode & 0x10) else "SlowROM"
    mapping = {0x21: "HiROM", 0x31: "HiROM", 0x20: "LoROM", 0x30: "LoROM"}
    info["map_mode"] = f"{mapping.get(map_mode & 0x3F, f'Unknown(${map_mode:02X})')} {speed}"

    # Chipset ($FFD6)
    info["chipset"] = rom[0xFFD6]

    # ROM size ($FFD7) - 2^n KB
    rom_size_byte = rom[0xFFD7]
    info["rom_size_kb"] = (1 << rom_size_byte) if rom_size_byte < 16 else 0

    # RAM size ($FFD8) - 2^n KB
    ram_size_byte = rom[0xFFD8]
    info["ram_size_kb"] = (1 << ram_size_byte) if ram_size_byte > 0 else 0

    # Country ($FFD9)
    countries = {0: "Japan", 1: "USA", 2: "Europe"}
    info["country"] = countries.get(rom[0xFFD9], f"Unknown(${rom[0xFFD9]:02X})")

    # Developer ID ($FFDA)
    info["developer_id"] = rom[0xFFDA]

    # Version ($FFDB)
    info["version"] = rom[0xFFDB]

    # Checksum complement ($FFDC-$FFDD) and checksum ($FFDE-$FFDF)
    info["checksum_complement"] = struct.unpack_from("<H", rom, 0xFFDC)[0]
    info["checksum"] = struct.unpack_from("<H", rom, 0xFFDE)[0]

    return info


def validate_checksum(rom: bytes, header: dict) -> bool:
    """Validate the internal checksum pair and compute actual checksum."""
    complement = header["checksum_complement"]
    checksum = header["checksum"]

    pair_valid = (complement + checksum) & 0xFFFF == 0xFFFF
    print(f"  Checksum pair: ${checksum:04X} + ${complement:04X} = "
          f"${(checksum + complement) & 0xFFFF:04X} {'(valid)' if pair_valid else '(INVALID)'}")

    # Compute actual checksum (sum of all bytes mod 0x10000)
    # Zero out the checksum fields first
    rom_copy = bytearray(rom)
    rom_copy[0xFFDC:0xFFE0] = b'\xFF\xFF\x00\x00'  # complement=FFFF, checksum=0000
    actual = sum(rom_copy) & 0xFFFF
    actual_match = actual == checksum
    print(f"  Computed checksum: ${actual:04X} {'(matches)' if actual_match else f'(expected ${checksum:04X})'}")

    return pair_valid and actual_match


def parse_vectors(rom: bytes) -> dict:
    """Parse native and emulation mode interrupt vectors."""
    vectors = {}

    # Native mode vectors ($FFE0-$FFEF)
    native_names = [
        ("COP", 0xFFE4), ("BRK", 0xFFE6), ("ABORT", 0xFFE8),
        ("NMI", 0xFFEA), ("RESET", 0xFFEC), ("IRQ", 0xFFEE),
    ]

    # Emulation mode vectors ($FFF0-$FFFF)
    emu_names = [
        ("COP", 0xFFF4), ("ABORT", 0xFFF8),
        ("NMI", 0xFFFA), ("RESET", 0xFFFC), ("IRQ", 0xFFFE),
    ]

    vectors["native"] = {}
    for name, offset in native_names:
        addr = struct.unpack_from("<H", rom, offset)[0]
        vectors["native"][name] = addr

    vectors["emulation"] = {}
    for name, offset in emu_names:
        addr = struct.unpack_from("<H", rom, offset)[0]
        vectors["emulation"][name] = addr

    return vectors


def dump_bank_layout(rom_size: int) -> None:
    """Print HiROM bank layout for the given ROM size."""
    num_banks = rom_size // (64 * 1024)  # 64 KB per full bank
    print(f"\n  HiROM Bank Layout ({num_banks} x 64KB banks):")
    print(f"  {'Bank Range':<14} {'Mirror':<14} {'Content'}")
    print(f"  {'-'*14} {'-'*14} {'-'*30}")

    # ROM banks C0-C7 (for 512KB = 8 banks)
    for i in range(num_banks):
        bank = 0xC0 + i
        mirror = 0x40 + i
        print(f"  ${bank:02X}:0000-FFFF  ${mirror:02X}:0000-FFFF  ROM bank {i} (offset ${i*0x10000:06X}-${(i+1)*0x10000-1:06X})")

    print(f"  $00-3F:8000+                 ROM (upper half mapped)")
    print(f"  $80-BF:8000+                 ROM (upper half mapped, fast)")
    print(f"  $7E:0000-FFFF                WRAM bank 0 (64 KB)")
    print(f"  $7F:0000-FFFF                WRAM bank 1 (64 KB)")
    print(f"  $00-3F:0000-1FFF             WRAM mirror (8 KB)")
    print(f"  $00-1F:6000-7FFF             DSP-1")
    print(f"  $20-3F:6000-7FFF             SRAM (2 KB)")


def main() -> None:
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <rom.sfc>")
        sys.exit(1)

    rom_path = sys.argv[1]
    print(f"Analyzing: {rom_path}\n")

    rom = read_rom(rom_path)
    print(f"ROM size: {len(rom)} bytes ({len(rom) // 1024} KB)")

    if len(rom) != ROM_SIZE:
        print(f"WARNING: Expected {ROM_SIZE} bytes, got {len(rom)}")

    # MD5
    md5 = hashlib.md5(rom).hexdigest()
    md5_match = md5 == EXPECTED_MD5
    print(f"MD5: {md5} {'(matches US v1)' if md5_match else '(does NOT match expected)'}")

    # CRC32
    import binascii
    crc = binascii.crc32(rom) & 0xFFFFFFFF
    print(f"CRC32: {crc:08X}")

    # Parse header
    print("\n--- Internal Header ---")
    header = parse_header(rom)
    print(f"  Title:    {header['title']}")
    print(f"  Mapping:  {header['map_mode']}")
    print(f"  Chipset:  ${header['chipset']:02X}")
    print(f"  ROM size: {header['rom_size_kb']} KB")
    print(f"  RAM size: {header['ram_size_kb']} KB")
    print(f"  Country:  {header['country']}")
    print(f"  Version:  1.{header['version']}")

    # Checksums
    print("\n--- Checksum Validation ---")
    validate_checksum(rom, header)

    # Vectors
    print("\n--- Interrupt Vectors ---")
    vectors = parse_vectors(rom)
    print("  Native mode:")
    for name, addr in vectors["native"].items():
        print(f"    {name:6s} -> ${addr:04X}")
    print("  Emulation mode:")
    for name, addr in vectors["emulation"].items():
        print(f"    {name:6s} -> ${addr:04X}")

    # Bank layout
    dump_bank_layout(len(rom))

    print()


if __name__ == "__main__":
    main()
