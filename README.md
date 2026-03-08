# Super Mario Kart — Static Recompilation

Static recompilation of **Super Mario Kart** (SNES, 1992) from WDC 65C816 assembly to native C code, playable on modern hardware via SDL2.

Part of the [sp00nznet](https://github.com/sp00nznet) recompilation portfolio. This is the first SNES (65816 CPU) target in the series.

## Status

### Milestone 1 — Foundation (Complete)
- [x] CMake build system (MSVC 2022, vcpkg SDL2)
- [x] 65816 CPU state struct with full register set and P flag helpers
- [x] HiROM memory map with bank routing (ROM, WRAM, SRAM, I/O dispatch)
- [x] HAL stubs for all SNES hardware (PPU, APU, DMA, DSP-1, CPU I/O)
- [x] SDL2 platform layer (256x224 → 768x672 window, 60fps vsync, keyboard input)
- [x] Hash-table function dispatch for recompiled game functions
- [x] ROM loader with copier header detection and checksum validation
- [x] ROM analysis tool (`tools/analyze/rom_info.py`)

### Milestone 2 — Emulator-Assisted Trace Pipeline (In Progress)
- [x] Mesen2 Lua trace scripts (execution trace, function finder, HW register logger)
- [x] Python trace parser (coverage analysis, function discovery, M/X flag mapping)
- [x] C stub generator from trace data (per-bank source files, func_table registration)
- [ ] Run full trace captures across game scenarios (boot, menus, race, Mode 7)
- [ ] Build `cpu_ops.h` instruction helper macros from trace-verified patterns
- [ ] Recompile boot chain: `$80FF70` → `$80803A` → `$81E000` → `$808056`
- [ ] Wire first recompiled functions into main loop

### Future Milestones
- Milestone 3 — Core game loop, NMI handler, display list processing
- Milestone 4 — PPU rendering (Mode 1/7 backgrounds, OAM sprites, HDMA)
- Milestone 5 — DSP-1 math coprocessor (track scaling, rotation, projection)
- Milestone 6 — SPC700 audio engine
- Milestone 7 — Full race gameplay, all tracks/characters

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  smk_launcher                    │
│  ┌──────────┐  ┌──────────┐  ┌───────────────┐  │
│  │ smk_     │  │ smk_hal  │  │ smk_platform  │  │
│  │ runtime  │  │          │  │               │  │
│  │          │  │ PPU stub │  │ SDL2 window   │  │
│  │ CPU state│  │ APU stub │  │ SDL2 renderer │  │
│  │ Memory   │  │ DMA xfer │  │ Keyboard→SNES │  │
│  │ FuncTable│  │ DSP-1 ALU│  │ Frame sync    │  │
│  │          │  │ CPU I/O  │  │               │  │
│  └──────────┘  └──────────┘  └───────────────┘  │
│                                                  │
│  ┌──────────────────────────────────────────┐    │
│  │ src/game/ — Recompiled 65816 functions   │    │
│  │ (generated from Mesen2 trace analysis)   │    │
│  └──────────────────────────────────────────┘    │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│              Toolchain (Python)                   │
│  tools/analyze/  — ROM header parser              │
│  tools/mesen/    — Lua trace scripts + parsers    │
│  tools/disasm/   — Disassembly tools (planned)    │
│  tools/recomp/   — 65816→C recompiler (planned)   │
└─────────────────────────────────────────────────┘
```

## Emulator-Assisted Workflow

The recompilation pipeline uses **[Mesen2](https://github.com/SourMesen/Mesen2)** as a reference emulator to generate execution traces, discover function boundaries, and capture M/X flag state at every instruction. This approach was chosen because:

1. **65816 is hard to statically disassemble** — the M/X processor flags change instruction operand sizes at runtime, making pure static analysis unreliable
2. **Trace-verified recompilation** — every recompiled function can be validated against the emulator's known-good execution
3. **Mesen2 has the right tools** — Lua scripting, headless `--testrunner` mode, Code/Data Logger, and customizable trace format

### Pipeline

```
ROM → Mesen2 (Lua scripts) → Trace logs → Python parsers → C stubs + metadata
                                                               ↓
                                              Manual/assisted recompilation
                                                               ↓
                                                    Native C game code
```

See [`tools/mesen/SETUP.md`](tools/mesen/SETUP.md) for detailed setup and usage instructions.

## Building

### Prerequisites
- CMake 3.16+
- Visual Studio 2022 (MSVC)
- SDL2 via vcpkg: `vcpkg install sdl2:x64-windows`
- Python 3.10+ (for toolchain scripts)
- Mesen2 (for trace capture, [download](https://github.com/SourMesen/Mesen2/releases))

### Build

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Debug
```

### Run

```bash
build/Debug/smk_launcher.exe "Super Mario Kart (USA).sfc"
```

The ROM file is not included — supply your own US v1.0 copy (MD5: `7f25ce5a283d902694c52fb1152fa61a`).

## ROM Details

| Field | Value |
|-------|-------|
| Title | SUPER MARIO KART |
| System | Super Nintendo (SNES) |
| CPU | WDC 65C816 @ 3.58 MHz |
| Coprocessor | DSP-1 (math) + SPC700 (audio) |
| Mapping | HiROM FastROM |
| Size | 512 KB (8 × 64 KB banks, C0–C7) |
| SRAM | 2 KB |
| Region | USA |
| CRC32 | CD80DB86 |

## Project Structure

```
├── include/
│   ├── recomp/      cpu.h, memory.h, func_table.h
│   ├── hal/         ppu.h, apu.h, dma.h, dsp1.h, io.h
│   ├── platform/    sdl_backend.h, input.h
│   └── game/        (generated function declarations)
├── src/
│   ├── recomp/      CPU reset, HiROM memory routing, func dispatch
│   ├── hal/         HAL stubs (register storage, basic DMA, ALU math)
│   ├── platform/    SDL2 window/renderer, keyboard→joypad mapping
│   ├── game/        (recompiled 65816 functions, per-bank .c files)
│   └── main/        main.c — init, main loop, shutdown
├── tools/
│   ├── analyze/     rom_info.py — ROM header parser + checksum validator
│   ├── mesen/       Lua trace scripts + Python parsers
│   ├── disasm/      (planned) disassembly tools
│   └── recomp/      (planned) 65816→C recompiler
└── docs/            (planned) technical documentation
```

## Key References

- [Yoshifanatic1/Super-Mario-Kart-Disassembly](https://github.com/Yoshifanatic1/Super-Mario-Kart-Disassembly) — Full 65816 + SPC700 disassembly (Asar)
- [jvipond/super_mario_kart_disassembly](https://github.com/jvipond/super_mario_kart_disassembly) — Trace-based disassembly with Python tooling
- [jvipond/super_mario_kart_recompilation](https://github.com/jvipond/super_mario_kart_recompilation) — Prior LLVM-based recomp attempt
- [MrL314/smk-spc700-disassembly](https://github.com/MrL314/smk-spc700-disassembly) — SPC700 audio driver disassembly
- [Mesen2](https://github.com/SourMesen/Mesen2) — SNES emulator with trace logging and Lua scripting
- [SNESRecomp](https://github.com/blueberry077/SNESRecomp) — Experimental trace-based SNES recompiler (reference)

## License

This project contains no Nintendo copyrighted material. The ROM file is not included and must be legally obtained by the user.
