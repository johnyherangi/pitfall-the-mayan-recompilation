# Pitfall: The Mayan Adventure — recompilation

The 1995 Windows game *Pitfall: The Mayan Adventure* (Kinesoft / Activision), rebuilt as modern
C++ that runs on Windows, macOS and Linux. It was reconstructed from the original `PITFALL.EXE`
and its data files with [REA (Reverse Engineer Anything)](https://github.com/morluto/rea) and
Ghidra, and checked frame by frame against the original.

**You need your own copy of the game.** No game data is included.

## Quick start

1. **Install the tools**
   - Windows: Visual Studio 2022 or 2026 with *Desktop development with C++*
   - macOS: Xcode's command-line tools and CMake 3.24 or newer (`brew install cmake`)
   - Linux: CMake, a C++20 compiler and SDL's development packages (`./build.sh` names them)
   - Optional: `ffmpeg`, to save the music as Ogg rather than WAV (`brew install ffmpeg` on a Mac)
2. **Add your game:** put your copy of the disc in a folder called `assets-in` here. It can be
   the CD's files, a `.zip` of them, an `.iso`, or a `.cue` with its `.img` or `.bin`. Only a
   full disc image (`.cue`) includes the music.
3. **Build and play:**
   - Windows: `build.bat run`
   - macOS and Linux: `./build.sh run`

The game ends up in `dist/`. Other options for the build scripts:

| Option | What it does |
|--------|--------------|
| `test` | runs the tests against your game files (`build.sh` only) |
| `pack` | copies your game files next to the build, so the folder can move to another computer |
| `modern`, `both` | `build.bat` only: builds the modern version, or both versions |

## Two versions

**Classic** (Windows only): the game as it looked in 1995, in the original window with its menus,
status bar, settings dialogs and CD launcher. It even shows the original's "256 color mode"
warning; set `Shell_Warning256=0` in `pitfall.ini` to skip it.

**Modern** (Windows, macOS and Linux): a resizable window with sharp, whole-pixel scaling, full
screen, gamepad support, and an in-game menu (Esc, or Start on a gamepad) for a new game, level
select, options and keys. On a Mac it is a single app for Apple silicon and Intel (macOS 10.15 or
later).

Settings are kept in `pitfall.ini`: next to the game on Windows, in
`~/Library/Application Support/Activision/Pitfall/` on macOS, and in
`~/.local/share/Activision/Pitfall/` on Linux.

## Controls

| Action | Keyboard | Gamepad |
|--------|----------|---------|
| Move | Arrow keys | D-pad or left stick |
| Jump | W | A / Cross |
| Sling | E | B / Circle |
| Whip | Q | X / Square |
| Switch weapon | Space | Y / Triangle |
| Pause | Pause | |
| Menu (modern) or quit (classic) | Esc | Start (modern) |
| Full screen | Alt+Enter, or F11 in the modern version (Mac: Control-Command-F) | |
| Help (classic) | F1 | |

You can change the keys in the settings (classic) or the menu (modern). The original's cheat
codes work too: type them while playing. `src/game/cheats.cpp` lists them.

## Music

The original plays its music from the CD's audio tracks. The build saves them as
`original/ASSETS/MUSIC/TrackNN.ogg` (WAV files without ffmpeg), and the classic version can also
play a real disc in the drive. If you have the tracks as separate files instead, name them by
track number (`Track02.ogg`, `Track03.wav` and so on) and put them in that folder. Without music,
the game is silent wherever the CD would play.

If you install ffmpeg after building, run `phimport --force assets-in original` (it's built next
to the game) to redo the import with Ogg files.

## Your game files

The build copies your game from `assets-in/` into `original/`, keeping the disc's own folders,
and never overwrites what's already there. The game finds `original/` next to itself or a few
folders up (so it runs straight from `dist/`), inside `Pitfall.app` after `pack`, or wherever you
point it on the command line.

The European CD has no `LEVEL24.PH`, Earthworm Jim's secret level. The tests skip it, and the
cheat that leads there ends with "Could not find file", just as it does on that disc.

## Building by hand

Windows, classic version:

```
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

This makes `pitfall.exe`, `pitstart.exe` (the CD's launcher), `phimport.exe`, `phdump.exe` and
`ph_tests.exe` in `build\Release`.

Modern version on macOS or Linux (on Windows, add `-A x64` and `--config Release`):

```
cmake -S . -B build-modern -DCMAKE_BUILD_TYPE=Release -DPITFALL_SHELL=modern
cmake --build build-modern
ctest --test-dir build-modern --output-on-failure
```

Run it with `build-modern/pitfall original`, or `open build-modern/Pitfall.app` on a Mac. Add
`-level N` to start on any level from 0 to 24.

**Sharing the Mac app:** it isn't notarized, so another Mac blocks it the first time. Allow it in
System Settings › Privacy & Security › Open Anyway, or run
`xattr -dr com.apple.quarantine Pitfall.app`.

## Tools

`phdump` is built next to the game.

```
phdump info original                         # list everything in the game's data files
phdump frame original 1 300 1800 frame.ppm   # render a frame of level 1
phdump sprite original hyirun harry.ppm      # save one of Harry's sprites
python3 tools/phtool.py original/ASSETS/INIT.PH out/init   # every sprite as PNG + JSON
```

## What's where

| Folder | Contents |
|--------|----------|
| `src/ph` | readers for the game's data files: sprites, levels, sounds, palette |
| `src/engine` | renderer, input, sound mixer and settings |
| `src/game` | the game: Harry, the objects, the bosses, the levels, the menus and cutscenes |
| `src/app` | the classic and modern windows, and the CD launcher |
| `src/tools` | `phdump` and `phimport` |
| `tests` | tests that play parts of the game (they need your game files) |
| `docs` | the file formats, a map of the engine, and what differs from the original |
| `tools`, `re` | scripts used in the reconstruction, and the checks against the original |
| `third_party` | stb_vorbis and miniz |

## How faithful is it?

Harry, all 140 kinds of object, every boss, the vaults, the map screen, the hidden Atari 2600
level, the cutscenes, the cheats, and the original's window and launcher have all been rebuilt.

It was checked against the original running under Wine:

- The attract mode and 50-second runs of 18 levels match frame for frame, apart from the
  original's loading pauses and fades.
- The game's memory matches byte for byte, tick by tick. That covers boss fights, whole levels,
  the vaults, the 2600 level and the ending, on both difficulties.
- Nine long recorded runs, 24,114 frames in all, match pixel for pixel apart from loading
  screens.

The remaining differences come from the platform: a software sound mixer instead of Miles Sound
System, an ini file instead of the registry, the full-screen modes, and the help files. They're
listed in `docs/RECONSTRUCTION_LEDGER.md`.

## Legal

This project contains no game data: `original/` and `assets-in/` are for your own copy and are
ignored by git. It is made for preservation and study, and is not affiliated with Activision. The
code and documentation are under the MIT licence (`LICENSE`), which covers nothing of the game
itself.
