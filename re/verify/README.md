# Checking the port against the original

The tools here run the original `PITFALL.EXE` under Wine and compare it with the port, by its
frames (`capframes.sh`, `cmpframes.cpp`) and by its memory (`capmem.sh`, `memdump.cpp`,
`mkpad.py`, `cmpmem.py`, with `verify_level.sh` doing the whole round for one level).  What they
showed is in `docs/RECONSTRUCTION_LEDGER.md`.

## Setting up

Linux with Wine 9, Xvfb, xdotool, bc, ffmpeg (for the frames), Python 3 and gcc / g++.  The
original's files (`PITFALL.EXE`, `WAIL32.DLL`, `INIT.PH`, `LEVEL00.PH` ..) go in a folder of their own
(`PF_GAME`, by default `original/`), and Wine needs a 32-bit prefix in which the game runs without
its CD (and, for the driven runs below, with its fly cheat, which only Ctrl brings on):

```
export WINEPREFIX=$HOME/.wine-pitfall WINEARCH=win32
wineboot -i
wine reg add 'HKLM\Software\Activision\Pitfall' /v Option_Slow /t REG_DWORD /d 1 /f
wine reg add 'HKLM\Software\Activision\Pitfall' /v Debug_AllowCheat /t REG_DWORD /d 1 /f
```

The scripts start their own virtual display (`PF_DISPLAY`, `:91` by default, 1024x768), dismiss
the "256 color mode" warning with Return, and pick a level from the File menu 7 s after the start
by clicking the menu (157, 150) and then the level's item at x 200 and this y:

| level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 16 | 17 | 7 | 18 | 8 | 9 | 10 | 13 | 11 | 12 | 20 |
|-------|---|---|---|---|---|---|---|----|----|---|----|---|---|----|----|----|----|----|
| y     |211|227|243|259|275|291|307|323 |339 |355|371 |387|403|419 |444 |460 |476 |492 |

The port's tools are built as `re/drivers`' are, against a CMake / Ninja build, and run from the
repository root (they load `original/`):

```
g++ -std=c++20 -O2 -Isrc re/verify/memdump.cpp \
    build/libpitfall_game.a build/libpitfall_engine.a build/libpitfall_ph.a -o memdump
```

## By memory

`capmem.sh <menu-item-y> <seconds> <out.txt> [T:down|up:KEY ...]` samples the running original's
memory through `/proc/<pid>/mem` about every 300 microseconds (`memsample.c`), a record whenever
anything changed: the engine's tick count (`0x46fb74`), the logic's (`0x4601d9`), the random
generator, the level, a5 (`DAT_004601b4`), the logic's memory `0x465800 .. 0x4668ff`, the 80 object
records and the joypad (`WATCH` gives other ranges: with `456000:2528` after them, the cutscenes'
data too, which `cmpmem.py` then compares).  With `JOYOUT=<file>` a second sampler reads the two
tick counts, the joypad, the "vampire" cheat's flag (`DAT_0046d054`), the lives and the engine's
start key (`DAT_0046fb88`) alone without a pause, so that every tick's joypad is seen.

`memdump` runs the port with the level picked from the menu (`MENU=<level>@<step>`) and the
original's joypad given to it tick by tick (`PAD=<file>`, through `Game::joypadHook`), and prints its
memory and records in the original's layout; `cmpmem.py` compares the two tick by tick from the
level's start, every byte of the logic's memory and of every record, the free ones included.

`verify_level.sh <level> <menu-item-y>` does all of it for one level, with 50 s of play: the right
arrow held from 14 s, the whip every 0.6 s, a jump every 2.3 s and four sling shots (in the vaults,
11 to 13, the three levers in turn every 0.9 s).  Every level of the menu came out the same as the
original's, every byte, on every tick whose end the sampler saw (97 to 100%).

What has to be right for the comparison to mean something:

* **A tick's state.**  Within an engine tick the engine's count goes up first, then the joypad is
  polled, then the logic runs, its count going up at its end; a sample taken in between is half a
  tick.  `ticks.py` keeps, for each tick, its last sample whose logic count is the steady distance
  ahead of the engine's (it goes up with a load's ticks and down for good with an engine exit), and
  leaves out a tick seen only before its end: the game was not scheduled between the two, which can
  take a whole tick's time, or it ran ticks back to back after a pause and the sampler saw each of
  them half done (the distance comes back within 60 ticks); `memsample` drops a record during
  which a tick began (it reads the engine's count again after the rest).  The original's timing
  suffers when anything else keeps the machine busy during a capture: run them alone.
* **The joypad.**  Under Wine the original runs 38 to 58 ticks a second, and after a pause (a death's
  waits, a load) it runs the missed ticks back to back, a tenth of a millisecond each; the keys it
  reads with `GetAsyncKeyState` can change in the middle of such a run.  Only the joypad sampler
  sees those ticks; `mkpad.py` takes each tick's joypad from the record that shows its logic's end,
  else from the tick's last record (the game can stop between the count's step and the poll for tens
  of milliseconds while the keys change; but a 0 there may be a load's clearing, which the records of
  the tick's first 5 ms tell apart), and for a tick the joypad sampler missed, from the main
  capture's sample of its end.  The engine's start key, which any key's `WM_KEYDOWN` sets between
  two ticks (a held key's repeats too) and the logic reads and clears (the map, the scenes, the 2600
  game's end, the title), is replayed as it stood before each tick (`memdump` sets it at the poll).
* **The level's start.**  The first tick after the one in which the level variable (`0x451a2c`)
  turned to the level; levels 0, 2, 5, 8 and 16 run 13 more ticks in their load (`PRETICKS=13`).
  The port's menu step for it is that tick less those 13, and 2 more: the port's intro (which runs
  tick for tick as the original's from its boot) is left where the original's was, its cutscene's
  state too, which the next scene starts with, and from there the logic's memory, the records,
  the random generator and the scenes' data are the original's, the port's ticks the original's.
* **The pointers.**  The port keeps what the original's memory holds as addresses as those
  addresses: Harry's sprite word and a record's sprite (the sprite's place in the load block, which
  sits where it sat in the original's runs, `ph::Arena::kAddress`), the records' links to each
  other and their scripts, the list's links and the .als entries, so they are compared like the
  rest (`PTRSKIP=1` looks past such fields).  The player's numbers the port keeps in `PlayerStats`
  and the level-script flags in `Game::levelFlags_` are put at their addresses by `memdump`.

## By frames

`capframes.sh <menu-item-y> <seconds> <out.raw> [keys]` records the original's game area at 20
frames a second (a menu-item-y of 0 for the attract run, which is what the original does when
nobody touches it); `cmpframes <out.raw> <result.txt> <anchor frame> 2.941 <ticks>` matches each frame
with the port's, frame by frame (`MENU`, `KEYS` and `ANCHOR_T` as described in the source).  A
level picked from the menu matches only with the port's menu step at the tick the original's
click came, which the logic's tick count and the clouds go by (try the steps around 115), and a
held key only from the tick it went down (try the ticks before the frame where Harry first moves).
The original's loads (its dark red screen while the files load) and its boot do not match: they
take wall time without ticks.

## Driven runs

`capmem.sh` with `DRIVE="<level> <plan>"` hands the level, once it runs, to `drive.py`, which plays
it from the original's memory: after each logic tick it reads Harry's record and the object list
through `/proc/<pid>/mem` and presses the keys the plan wants for the next tick through XTest.  It
flies Harry with the game's own cheat (Ctrl and the arrows, 7 px a tick, `Debug_AllowCheat` in the
registry), types its cheats once the level runs (`CHEATS`, by default "vampire": no damage; with
"meowmeowlikemeowman" too, 10 lives), and then follows its plan:

| plan | what it does |
|------|--------------|
| `jaguar` | level 0: over the arena to its right edge and down, then the whip at the jaguar until it dies and the level ends |
| `stones` | level 3: to the stone jaguars' ledge, the whip at the awake ones, then up the rope their death leaves and out |
| `jaguarman` | level 8: down beside the arena and back up, the whip at the jaguar man, then into the stone face's mouth |
| `spirit` | level 10: the whip and the sling at the Warrior Spirit |
| `end` | level 10: the Warrior Spirit shot down from the fly (Harry let go 27 px under its head and 31 px before it, the sling fired as he falls, left alone while it stuns or holds him), the last letter picked up, the reunion and the end's credits played out (`END` ticks of them, 2000, then a button) |
| `sweep` | any level: rows across the whole level, top to bottom, `ROW` px apart (160) |
| `vault` | levels 11 to 13: the Simon game played right, from the combination in memory, round after round |
| `play` | any level: dropped at spots across it (`PLAYX` x `PLAYY` px apart) and played at random there for `PLAY` ticks each (`SEED`) |
| `vortex` | levels 2, 3, 4 and 8: dropped onto the level's vortex; through it to a vault (its game played as `vault` plays it, to the way out, back to the level) or, from level 3, to the 2600 level (played at random, `SEED`, until its game is over and back) |

`mkpad.py` turns the cheat's flag in the joypad capture into a line `type <tick> vampire` (the cheat
came in after that tick: `WM_CHAR` runs between two ticks), and the lives turning 99 into one for
"meowmeowlikemeowman", which `memdump` types into the port before the same tick (`Game::typeChar`); the rest is the joypad, the fly cheat's Ctrl bit (`0x100`)
in it.  A run is compared as a level's: `mkpad.py`, `memdump` with the level's menu step and as many
steps as the capture has ticks, `cmpmem.py`; with `FOLLOW=1` the comparison goes on past the level's
end, the ticks counted on as the engine's, through the map screen and the next level (a run that
ends its level: the bosses' plans, or random play that reaches the exit).
