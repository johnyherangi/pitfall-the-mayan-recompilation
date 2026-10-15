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
src/game/        game driver, camera/parallax, HUD, title, attract-mode demos, the game logic
src/app/         Win32/GDI front end (default) and SDL2 front end (optional)
src/tools/       phdump command-line tool
tests/           asset round-trip and gameplay tests
third_party/stb/ stb_vorbis (public domain / MIT), for Ogg rips of the CD's music
```

## Building (Visual Studio 2022)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

This produces `build\Release\pitfall.exe` (the game), `phdump.exe` and
`ph_tests.exe`.  No third-party dependencies are needed for the Win32 build
beyond the vendored `third_party/stb/stb_vorbis.c`.  For a portable SDL2 build
add `-DPITFALL_USE_SDL=ON` (SDL 2.30 is fetched by CMake).  A MinGW / clang
cross build works too (checked with llvm-mingw 20240619):

```
cmake -S . -B build-win -G Ninja -DCMAKE_SYSTEM_NAME=Windows ^
      -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres
```

Run `pitfall.exe` from the repository root, or copy `original\*.PH` next to
the executable, or pass the data directory:

```
build\Release\pitfall.exe original
build\Release\pitfall.exe original -level 1     (a debugging start on a level)
build\Release\pitfall.exe original J            (the original's "J": the joystick on)
```

## The window

The Win32 build is the original's window: its menu (*File*: New Game,
Properties..., the levels, the Loltun vaults and the 2600 level, Exit; *Help*:
the original's help file, About), the status bar with the level's name and the
five icons that open the property sheet at their page, the "Loading..." box of
a level load, Esc's "quit this adventure?" and the property sheet itself
(System: the screen size, the game's priority; Sound; Difficulty; JoyStick;
Keyboard: an action's box takes the key or button pressed in it).  A level
comes on in the menu once it has been reached (outside the demos) and stays on
from game to game; "Are you sure you want to give up the current game?" asks
first when there are points to lose.  The pictures of the About box, the status
bar, the property sheet and the cheats' dialogs come from `PITFALL.EXE` in the
data directory (they are not in this repository); without it those places stay
blank.  The settings are kept in `pitfall.ini` next to the executable, under the
original's registry value names.

## Controls (original defaults, changed in the Keyboard and JoyStick pages)

| action | key   | joystick |
|--------|-------|----------|
| move   | arrow keys | axes |
| jump   | W     | button 1 |
| shoot  | E     | button 2 |
| whip   | Q     | button 3 |
| select | Space | button 4 |
| pause  | Pause (any other key ends it) | |
| help   | F1    |          |
| quit   | Esc   |          |
| Harry's special idle | F10 or Alt+Y |  |
| full screen | Alt+Enter (an addition; the original has it in the System page) | |

The original's typed cheat codes work as they did (type them while playing),
"vampire" and "hatman" among them; `game/cheats.cpp` lists them all.

## Music

The original plays the game CD's audio tracks (2 .. 23) as music and ambient
sound.  Rip them to files named `TrackNN.wav` or `TrackNN.ogg` (`Track 02`,
`02 - title.ogg` and the like work too) in `original\ASSETS\MUSIC` (or
`original\MUSIC`, or any folder named by the `PITFALL_MUSIC_DIR` environment
variable).  The Sound page's *CD Music* and *CD Ambient Sounds* are the
original's two CD options (one at a time); without the files the game is
silent where the CD would play, as the original is without its disc.

The game starts as the original: the intro, the title, and while nobody
presses a key the three attract demos and the intro again with the credits.
`-level N` on the command line starts any of the 25 levels directly, the
cutscenes (21 the intro, 22 the reunion, 23 the end and its credits) and
Earthworm Jim's secret level 24 included.

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

The engine half of the binary (assets, palette, renderer, input, options,
sound, CD audio, the level transitions with their fades, the HUD, the
attract-mode demos) and most of the Genesis-derived game logic are
reconstructed: Harry's whole state machine, the object system with 140
behaviour types, the camera and spawner, the triggers and level scripts, every
boss, the Loltun vaults, the continue screen, the map screen, the Atari 2600
level, the title, the intro, the reunion, the end and its credits, the
typed cheat codes, the waterfalls and water effects, and the original's
window with its menu, status bar, dialogs and property sheet.  Still to do:
the real CD through MCI and a build with MSVC itself.  See
`docs/RECONSTRUCTION_LEDGER.md` for the exact status.

## Legal

This repository contains no original game data: `original/` must hold your
own copy of the game (and `original_genesis/` your own ROM, if used); both are
excluded by `.gitignore`.  `re/` holds disassembly, decompilation and
screenshots of the original program, so keep the repository private.  The
reconstruction is for preservation and study.
