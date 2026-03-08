# Mesen2 Setup for SMK Recompilation

## Install

1. Download Mesen2 from https://github.com/SourMesen/Mesen2/releases (v2.1.1+)
2. Extract to a convenient location (e.g., `C:\Tools\Mesen2\`)
3. Run Mesen.exe, load `Super Mario Kart (USA).sfc` to verify it runs

## Trace Capture Workflow

### 1. Full Execution Trace (trace_capture.lua)

Captures every unique PC address with register state and M/X flags.

**GUI method:**
- Open Mesen2 → Debug → Script Window
- Load `tools/mesen/trace_capture.lua`
- Click Run, then play the game
- After MAX_FRAMES (default 600 = ~10 sec), `smk_trace.log` is written

**Headless method (faster):**
```
Mesen.exe --testrunner "Super Mario Kart (USA).sfc" --lua tools/mesen/trace_capture.lua
```

**Parse the output:**
```
py tools/mesen/parse_trace.py smk_trace.log -o output/
```
Produces: `coverage.json`, `functions.json`, `mx_flags.json`

### 2. Function Discovery (func_finder.lua)

Watches JSR/JSL instructions to build a call graph.

```
Mesen.exe --testrunner "Super Mario Kart (USA).sfc" --lua tools/mesen/func_finder.lua
```

**Generate C stubs:**
```
py tools/mesen/parse_functions.py smk_functions.json -o src/game/
```

### 3. Hardware Register Logger (hw_logger.lua)

Records which PPU/APU/DMA/IO registers are read/written and how often.
Helps prioritize which HAL stubs need real implementations.

```
Mesen.exe --testrunner "Super Mario Kart (USA).sfc" --lua tools/mesen/hw_logger.lua
```

## Built-in Trace Logger

Mesen2 also has a built-in trace logger (Debug → Trace Logger) with a customizable format string. Recommended format:

```
[PC,6] [ByteCode,8] [Disassembly] [EffectiveAddress] [MemoryValue] [Align,48] A:[A,4] X:[X,4] Y:[Y,4] SP:[SP,4] P:[P] [Scanline,3].[Cycle,3]
```

## Code/Data Logger (CDL)

The CDL (Debug → Code/Data Logger) tracks which ROM bytes have been executed as code vs read as data. Use "Log CDL" while playing through different game scenarios to build comprehensive coverage. Export the CDL file — it marks every byte with its access type and M/X flag state.

## Tips

- Adjust `MAX_FRAMES` in the Lua scripts for longer/shorter captures
- Set `LOG_UNIQUE_ONLY = false` in trace_capture.lua for full instruction-by-instruction logs (warning: huge files)
- Use save states to capture specific game scenarios (title screen, race, mode 7, etc.)
- The `--testrunner` flag runs at max speed with no frame limiting
