# Super Mario Kart - Static Recompilation

## Project Overview
Static recompilation of Super Mario Kart (SNES, 1992) from 65816 assembly to native C code.
This is the first SNES (65816 CPU) project in the sp00nznet portfolio.

## Architecture
- **Source CPU**: WDC 65C816 (16-bit, 3.58 MHz) + DSP-1 coprocessor + SPC700 audio
- **ROM Layout**: HiROM FastROM, 512 KB, 8 banks (C0-C7)
- **Target**: Native x86-64 C code with SDL2 for windowing/rendering/audio

## Key References
- Yoshifanatic1/Super-Mario-Kart-Disassembly — Full 65816 + SPC700 disassembly using Asar
- jvipond/super_mario_kart_disassembly — Trace-based disassembly with Python tooling
- jvipond/super_mario_kart_recompilation — Prior LLVM-based recomp attempt
- MrL314/smk-spc700-disassembly — Dedicated SPC700 audio driver disassembly

## Build System
- CMake 3.16+, MSVC (Visual Studio 2022) primary
- SDL2 for platform layer
- Python 3.10+ for toolchain (disasm, recomp, analysis)

## ROM Details
- Internal name: SUPER MARIO KART
- MD5 (US v1): 7f25ce5a283d902694c52fb1152fa61a
- CRC32: CD80DB86
- ROM file not included — user must supply their own copy

## Code Conventions
- C17 standard for recompiled code
- 65816 register state in a global struct (A, X, Y, S, DB, DP, P flags)
- HAL layer replaces SNES hardware (PPU, APU, DMA, DSP-1)
- Function naming: smk_XXXXXX (where XXXXXX = SNES address in hex)
- Python tools use snake_case, type hints where practical
