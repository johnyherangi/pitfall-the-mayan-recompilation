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

## Two shells

The game is one; it comes in two windows, chosen when building (`-DPITFALL_SHELL=`):

* **`classic`** (Windows, the default there): the original's window as Windows 95 showed it, its
  menu bar with the levels, status bar, property sheet, dialogs, the CD's launcher and the
  "256 color mode" warning.  The look of 1995, down to the warning (`Shell_Warning256=0` in
  `pitfall.ini` leaves that out).
* **`modern`** (Windows, macOS and Linux; the default off Windows): an SDL2 window of today, the
  picture scaled by whole pixels (2x, 3x, 4x or fitted; `Shell_Scale`), full screen on F11 or
  Alt+Enter, gamepads through SDL's mapping, and in place of the menu bar and the property sheet
  an in-game menu on Esc (or a pad's Start), drawn in the game's own font over the paused picture:
  resume, new game, the levels the original's File menu offered as they are unlocked, the options
  (sound, music, ambient sounds, the volumes, the difficulty, the window, the keys) and quit.  On
  macOS it builds as `Pitfall.app`; the settings go to `~/Library/Application Support/Activision/Pitfall/`
  (Linux: `~/.local/share/Activision/Pitfall/`), the game's files are looked for in `original`
  next to the program (or inside the bundle's Resources, or given on the command line).

## Building

Windows, the classic shell (Visual Studio 2022 or 2026):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64      (or "Visual Studio 18 2026")
cmake --build build --config Release
ctest --test-dir build -C Release
```

This produces `build\Release\pitfall.exe` (the game), `pitstart.exe` (the CD's
launcher), `phdump.exe` and `ph_tests.exe`.  The code builds with MSVC at `/W4 /permissive-` without a
warning (checked with MSVC 14.50 and the Windows 10.0.26100 SDK, `ph_tests`
giving the same results as the g++ build).  No third-party dependencies are needed for the classic shell
beyond the vendored `third_party/stb/stb_vorbis.c`.

The modern shell, on any of the three (SDL 2.30 is fetched and built by CMake; on Linux the X11
and ALSA development packages are wanted, on macOS Xcode's command-line tools):

```
cmake -S . -B build-modern -DCMAKE_BUILD_TYPE=Release -DPITFALL_SHELL=modern
cmake --build build-modern
./build-modern/pitfall original                  (macOS: open build-modern/Pitfall.app)
```

A MinGW / clang cross build of either shell works too (checked with llvm-mingw 20240619):

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

`pitstart.exe` is the CD's launcher (`PITSTART.EXE`, which `AUTORUN.INF` starts):
its window, pictures and icon are read from the CD's `PITSTART.EXE` and its
music is `pitstart.wav`; Play runs the intro (`intro.avi`, through MCIWnd as
the original does it: it plays where the system still decodes Indeo 3.2;
elsewhere the launcher plays an `intro.converted.avi` beside it if there is
one, the same film in a codec the system has, made with
`ffmpeg -i INTRO.AVI -c:v cinepak -q:v 1 -c:a pcm_s16le intro.converted.avi`
(Cinepak, which Windows still ships), and without that MCI says it cannot load
the driver and the box stays black until a key, as the original's does) and
then `pitfall.exe` from the launcher's folder; More...
offers the CD's registration program, *Technical Help* (`tech.wri` in
WordPad), the two About pictures and Uninstall (here: the port's
`pitfall.ini`).  It looks for the CD's files as `pitfall.exe` looks for its data
(`pitstart.exe original`, or `pitstart.exe original U` to uninstall, as
`AUTORUN.INF`'s menu does).

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
| quit   | Esc (the modern shell: Esc opens its menu, F11 full screen) |          |
| Harry's special idle | F10 or Alt+Y |  |
| full screen | Alt+Enter (an addition; the original has it in the System page) | |

A gamepad works alongside the keyboard without any setting (an addition: the
original's JoyStick option is the joystick API's one stick and four buttons).
In the classic shell an Xbox pad is read through XInput, a PlayStation (DualShock, DualSense) or
other pad through the joystick API with its d-pad; the modern shell uses SDL's
controller mapping, and its Start button opens the menu.  The d-pad or the left stick moves, A / cross jumps, B /
circle shoots, X / square whips, Y / triangle (or Start, Back / Options,
Create) selects; in the vaults the shoot, jump and whip buttons pull levers 1,
2 and 3, and any button serves where "any key" does (the title, the scenes,
ending a pause).

The original's typed cheat codes work as they did (type them while playing),
"vampire" and "hatman" among them; `game/cheats.cpp` lists them all.

## Music

The original plays the game CD's audio tracks (2 .. 23) as music and ambient
sound.  The Win32 build plays the disc itself when it is in the drive (through
MCI, as the original); otherwise rip the tracks to files named `TrackNN.wav` or `TrackNN.ogg` (`Track 02`,
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
python tools\gen_tables.py && python tools\gen_anim_scripts.py && python tools\gen_slot_bindings.py && python tools\gen_arena_data.py   # regenerate the data tables from PITFALL.EXE / re\pitfall_decomp.c
python re\pc_dis.py 0x412fc2 0x414100                       # annotated x86 listing of a handler (needs objdump)
python tools\genesis_dis.py 66                               # the same handler in the Genesis ROM (needs capstone)
```

`re/README.md` describes the evidence, `re/drivers/README.md` the headless
debug drivers used to verify each object handler and `re/verify/README.md` the
tools that run the original under Wine and compare it with the port.

## Status

The engine half of the binary (assets, palette, renderer, input, options,
sound, CD audio, the level transitions with their fades, the HUD, the
attract-mode demos) and most of the Genesis-derived game logic are
reconstructed: Harry's whole state machine, the object system with 140
behaviour types, the camera and spawner, the triggers and level scripts, every
boss, the Loltun vaults, the continue screen, the map screen, the Atari 2600
level, the title, the intro, the reunion, the end and its credits, the
typed cheat codes, the waterfalls and water effects, the original's
window with its menu, status bar, dialogs and property sheet, and the CD's
launcher.  Run side by
side with the original (under Wine, captured frame by frame for five minutes),
the whole attract run — the title, the three demos and the intro — matches it
pixel for pixel but for the time the original's loads take (and the fades, which
the original's screen does not show on today's displays), and so do 50-second
runs of eighteen of the levels picked from the menu, with their deaths,
restarts and continue screens.  Read while it runs, the original's memory (the
logic's variables, the object records, the random generator) matches the
port's byte for byte, tick by tick, on every one of those levels, through runs
with the whip, the sling and the vaults' levers, kills, deaths and restarts
(the pointers in it too, kept as the original's addresses), and through runs
that play the original from its own memory: the four bosses' fights to their
ends, sweeps over whole levels, the vaults' Simon games and random play, on
past a level's end through the map screen into the next level, through the
vortices into the vaults, the 2600 level and Earthworm Jim's level and back,
through the continue screen's "yes" and "no" to the game over, the title and
a demo, and from the Warrior Spirit's death through the reunion and the
end's credits; on the hard difficulty too, and with Harry hurt and killed, the
sound channels' state compared as well.  Nine of those runs were recorded on
screen too (the Ceiba jaguar's fight on through the map screen into Xibalba
Falls, random play, the continue screen, a vault, Earthworm Jim's level, the
2600 level, the Warrior Spirit's fight, the reunion, the end and its credits):
every one of their 24,114 pictures matches the port's of its tick pixel for
pixel, but the original's load screens.  What still differs
from the original is listed in `docs/RECONSTRUCTION_LEDGER.md` (the platform's
parts: the mixer for Miles, the ini file for the registry, the full-screen
modes, WinHelp).

As the original, the Win32 build warns "Pitfall must be played in 256 color
mode for optimum performance." on a display that is not a 256-colour one (every
display now) and then uses the whole palette, as the original does there.

## Legal

This repository contains no original game data: `original/` must hold your
own copy of the game (and `original_genesis/` your own ROM, if used); both are
excluded by `.gitignore`.  `re/` holds disassembly, decompilation and
screenshots of the original program, so keep the repository private.  The
reconstruction is for preservation and study.
