# Debug drivers

Small headless programs used to verify the object handlers: they start a level, move Harry
with the debug fly (`allowCheat`), feed scripted input and print object state.  Each port in
`src/game/handlers.cpp` was checked with one of them.  Run them from the repository root —
they load the game data from `original/`.

Build against the static libraries of a CMake/Ninja build (g++ or clang; MSVC needs its own
link line):

```
cmake -S . -B build -G Ninja && ninja -C build
g++ -std=c++20 -O1 -Isrc re/drivers/dbg11.cpp \
    build/libpitfall_game.a build/libpitfall_engine.a build/libpitfall_ph.a -o dbg11
./dbg11 3 1290 1430 600 66 25        # Copan: fly to (1290,1430), whip every 25 ticks, list type 66
```

| driver | arguments | what it does |
|--------|-----------|--------------|
| `dbg11` | `<level> <x> <y> <ticks> <type\|-1> <whipEvery> [walkDir] [every]` | fly to (x, y), then stand / walk (`walkDir` -1 / 1) and whip every N ticks; prints Harry and every object of `type` (-1 = all) with its state bytes. `VIA="x,y;x,y"` flies through waypoints first, `FLYLOG=1` logs the fly-in |
| `dbg10` | `<level> <x> <y> <ticks> <type> <upAt> [walk]` | press "up" at tick `upAt` (levers), then walk; prints the level-script flags and doors / chutes / levers |
| `dbg9` | `<level> <x> <y> <ticks> <type> <whipEvery>` | stand and whip periodically; lists `type` and everything near Harry |
| `dbg8` | `<level> <x> <y> <ticks> <type>` | hold right from (x, y); `SNAP=<tick>` writes `snap8.ppm` |
| `dbg7` | `<level> <x> <y> <ticks> <jumpAt> [<every> ...]` | bungee vine runs (type 27); `SNAP=<tick>` writes `bungee.ppm` |
| `dbg6` | `<level> <x> <y> <ticks> <mode>` | the jaguar fight with scripted modes 0..5; writes `snap<tick>.ppm` |
| `dbg5` | `<level> <ticks> <x> <y> <type>` | hover at (x, y) and dump the state bytes of `type` every tick |
| `dbg2` | `<level> <ticks> [keys]` | run a level with held keys and report objects of interest every 30 ticks |
| `dbg` | — | level 0, hold left: Harry's and the pouch's display-list positions |
| `where` | `<level> <id>` | `.als` placements of an object id |
| `dbg3` | `<level> <id>` | the same with the template offsets and sprite applied |
| `trg` / `trgraw` | `<level> <col0> <col1>` / `<level> <col>` | `.trg` entries per column (decoded / raw) |
| `floor` | `<level> <x0> <x1> <y>` | floor probe every 8 px plus the `.det` column lists |
| `cols` | `<level> <col0> <col1>` | `.dt2` column lists |
| `dbg4` | `<level> <row0> <row1>` | ASCII map of the `.det` collision rows |
| `sprites` | `<level>` | sprite names and frame counts of a level |
