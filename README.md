# Pitfall: The Mayan Adventure — recompilation

A modern C++20 reconstruction of the Windows 95 release of *Pitfall: The
Mayan Adventure* (Kinesoft Development / Activision, 1995), built from a
static analysis of `PITFALL.EXE` and its `.PH` asset files with
[REA (Reverse Engineer Anything)](https://github.com/morluto/rea) driving
Ghidra 12.1.

```
original/        the shipped game files (PITFALL.EXE, INIT.PH, LEVEL00-24.PH, ...) -- not in git
original_genesis/ optional: the Genesis ROM, a reading aid for the game logic -- not in git
re/              reverse-engineering evidence: Ghidra decompile, symbol tables, per-handler
                 disassembly listings (x86 and 68000), cross-references, debug drivers, renders
tools/           phtool.py asset extractor, table generators, genesis_dis.py, Ghidra export scripts
docs/            FORMATS.md, ENGINE_MAP.md, RECONSTRUCTION_LEDGER.md
src/ph/          .PH parsers: sprites, backgrounds, levels, palette, sounds
src/engine/      software renderer, input, options, audio mixer
src/game/        game driver, camera/parallax, HUD, attract-mode demos
src/app/         Win32/GDI front end (default) and SDL2 front end (optional)
src/tools/       phdump command-line tool
tests/           asset round-trip tests
```

## Building (Visual Studio 2022)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

This produces `build\Release\pitfall.exe` (the game), `phdump.exe` and
`ph_tests.exe`.  No third-party dependencies are needed for the Win32 build.
For a portable SDL2 build add `-DPITFALL_USE_SDL=ON` (SDL 2.30 is fetched by
CMake).

Run `pitfall.exe` from the repository root, or copy `original\*.PH` next to
the executable, or pass the data directory:

```
build\Release\pitfall.exe original
build\Release\pitfall.exe original -level 1
```

## Controls (original defaults, rebindable in `pitfall.ini`)

| action | key   | joystick |
|--------|-------|----------|
| move   | arrow keys | axes |
| jump   | W     | button 1 |
| shoot  | E     | button 2 |
| whip   | Q     | button 3 |
| select | Space | button 4 |
| pause  | P     |          |
| full screen | Alt+Enter |     |

The **Level** menu loads any of the 25 levels.  Gameplay levels run the
reconstructed Harry physics on the original collision data (run, jump, fall,
whip, treasure, exits, deaths) with the original object system: snakes,
monkeys, condors, flies, bats, rats, spiders, scorpions, boars, tar pits, webs,
crumbling rocks, branches and checkpoints behave as their decompiled handlers,
and Ceiba Jungle ends with the jaguar boss fight; screens without collision
data open in a free-camera explorer mode.

## Tools

```
python tools\phtool.py original\INIT.PH out\init          # every sprite as PNG + JSON
python tools\phtool.py original\LEVEL01.PH out\l1 --level 1   # background, parallax, sprites
build\Release\phdump.exe info original                     # inventory of every file
build\Release\phdump.exe frame original 1 300 1800 f.ppm   # render a game frame
build\Release\phdump.exe sprite original hyirun harry.ppm
build\Release\phdump.exe wav original 1 sound01.wav
build\Release\phdump.exe objects original 2                  # .als object ids / types of a level
tools\ghidra\export_decomp.bat                             # regenerate re\pitfall_decomp.c
tools\ghidra\export_handlers.bat                           # (headless decompiler: currently every function times out; the port is read from disassembly)
python tools\gen_tables.py && python tools\gen_anim_scripts.py && python tools\gen_slot_bindings.py   # regenerate the data tables from PITFALL.EXE / re\pitfall_decomp.c
python re\pc_dis.py 0x412fc2 0x414100                       # annotated x86 listing of a handler (needs objdump)
python tools\genesis_dis.py 66                               # the same handler in the Genesis ROM (needs capstone)
```

`re/README.md` describes the evidence and `re/drivers/README.md` the headless
debug drivers used to verify each object handler.

## Status

The asset pipeline, palette management, renderer, input, options, sound bank
and the Windows-side game flow are reconstructed from the engine half of the
binary and verified against the original data.  The level tables (collision
shapes, object placement, triggers) are decoded and Harry's core movement
runs on them with the original constants, so the levels can be run through
from start to exit.  The object system and 83 of its behaviour types
(including the Ceiba Jungle jaguar boss, bungee vines, the skull warriors,
crocodiles, rolling wheels and the lever / door / chute level script) are
ported from the Genesis-derived half; the remaining object types (mine cart,
vortex warps, the later jaguars and bosses), the sling weapons and Harry's
climbing / swinging states are the next phase.
See `docs/RECONSTRUCTION_LEDGER.md` for the exact status.

## Legal

This repository contains no original game data: `original/` must hold your
own copy of the game (and `original_genesis/` your own ROM, if used); both are
excluded by `.gitignore`.  `re/` holds disassembly, decompilation and
screenshots of the original program, so keep the repository private.  The
reconstruction is for preservation and study.
