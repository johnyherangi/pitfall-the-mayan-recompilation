# PITFALL.EXE → source map

`PITFALL.EXE` (710,144 bytes, PE32 x86, image base 0x400000) has two very
different halves:

| region              | functions | origin                                         | status                      |
|---------------------|-----------|------------------------------------------------|-----------------------------|
| 0x438000 – 0x449881 | 97        | hand-written MSVC Windows engine (16-byte aligned) | ported (this tree)       |
| 0x401000 – 0x437fff | 196       | machine-translated Sega Genesis 68000 game code  | reference only (`re/`)     |
| 0x449882 – 0x44f3ff | 130       | MSVC C run-time                                  | not needed                 |

The Genesis half is recognisable by odd function addresses, 16-bit arithmetic,
CPU flags passed between functions (`in_ZF`) and *continuation* code pointers
stored in globals (e.g. `DAT_00465b69`, Harry's resume address).  Ghidra's
function boundaries are unreliable there; the full decompilation is kept in
`re/pitfall_decomp.c` for the port of that half.

## Engine functions and where they went

| address      | role                                                   | new home                              |
|--------------|--------------------------------------------------------|---------------------------------------|
| `0x43c210`   | `WinMain`: window, DIB, arena, timer, loads init.ph    | `src/app/main_win32.cpp`, `Game::Game` |
| `0x4388c0`   | `WndProc` (menus, property sheet, WM_0x658 CD control) | `main_win32.cpp` (menu subset)        |
| `0x439fd0`   | message loop                                           | `main_win32.cpp` / `main_sdl.cpp`     |
| `0x4448c0`   | 17 ms multimedia-timer tick                            | tick pacing in the front ends         |
| `0x443370`   | per-tick engine update: input, logic dispatch, parallax camera | `Game::tick`, `game/camera.cpp` |
| `0x443c90`   | frame composition, level transitions, attract sequencing | `Game::render`, `Game::tickAttract` |
| `0x443b90`   | present (BitBlt / StretchBlt of the 320×224 view)      | `present()` in the front ends         |
| `0x43e1c0`   | DIB section + system palette setup                     | `engine/framebuffer.h`, `ph::Palette::setFreeRange` |
| `0x444fe0`   | open a .PH file                                        | `ph::PhStream::fromFile`              |
| `0x4453a0`   | load next sprite record, allocate colours, remap       | `ph::Sprite`, `ph::loadInit/loadLevel` |
| `0x4455f0`   | load next raw image (font / score digits)              | `ph::RawImage`                        |
| `0x4451a0`   | palette colour allocator                               | `ph::Palette::allocate`               |
| `0x43ebc0`   | set a palette entry                                    | `ph::Palette::setColour`              |
| `0x4457d0`   | INIT.PH load order                                     | `ph::initManifest()`                  |
| `0x4460e0`   | level load: bg/trg/det/dt2/als/parallax, limits, music | `ph::loadLevel`, `Game::startLevel`   |
| `0x447190`   | per-level sprite load order                            | `ph::levelSprites()`                  |
| `0x43ce10`   | WAIL32 (Miles) init + sound bank load                  | `ph::loadInit` (sounds), `engine::Mixer` |
| `0x43cc10`   | play sound id                                          | `Game::playSound`                     |
| `0x436fb4`   | tile layer renderer (41×29 tiles, deferred hi-priority) | `Renderer::drawTileLayer`            |
| `0x436b00` / `0x436b2c` | deferred tile drain / one tile (flips, see-through) | `Renderer::drawDeferredTiles`, `Renderer::drawTile` |
| `0x437078` / `0x437128` | `FUN_00436fb4` on level 20: the 2600 screen, two 40-tile strips of the map picked by the screen's descriptor | `Game::renderAtari2600`, `Renderer::drawTileStrip` |
| `0x436534`   | parallax renderer (line-scrolled bitmap / tiles)       | `Renderer::drawParallaxLinear/Tiles`  |
| `0x438140`   | RLE sprite blitter, 4 flip variants                    | `Renderer::drawSprite`                |
| `0x4426e0`   | display-list walk (+ title animation)                  | `Game::render`, `Game::renderTitle`   |
| `0x442dc0`   | "front" sprites after hi-priority tiles                | `Game::render` second pass            |
| `0x4387c0`   | raw cell draw (font / digits)                          | `Renderer::drawCell`                  |
| `0x442600`   | text drawing                                           | `Renderer::drawText`                  |
| `0x442fa0`   | HUD (score, lives, weapon, ammo)                       | `game/hud.cpp`                        |
| `0x444940`   | joypad from keyboard / joystick / demo script          | `engine::Input`, `Game::tick`         |
| `0x4400f0` / `0x4404a0` | registry options load / save                | `engine::Options`                     |
| `0x43ee50`   | string resources (titles, errors)                      | `ph::levelName`, literals             |
| `0x441c50`   | palette fade                                           | not ported (TODO)                     |
| `0x441b80`   | MCI CD audio track play                                | `levelMusicTrack()` table only        |
| `0x43a140` / `0x43a320` | window sizing / full-screen (DISPDIB)       | `applyWindowSize()`                   |

## Global variables worth knowing

| address      | meaning                                  | new name                     |
|--------------|------------------------------------------|------------------------------|
| `0x451a2c`   | current level                            | `Game::levelNo_`             |
| `0x450504/8` | camera scroll x / y (world px)           | `FrameState::scrollX/Y`      |
| `0x450fb8/bc`| camera limits                            | `FrameState::maxScrollX/Y`   |
| `0x450f94/8` | parallax scroll x / y                    | `FrameState::parX/Y`         |
| `0x4517ec`   | vertical bob offset                      | `FrameState::bob`            |
| `0x4513e0`   | per-line parallax offsets (`0x46fb44` count) | `FrameState::lineScroll` |
| `0x46aa94`   | sprite display list (20-byte entries), end `0x46aa90` | `FrameState::displayList` |
| `0x4517f8`   | joypad bits (Genesis layout)             | `FrameState::joypad`         |
| `0x45190c`   | ticks since level start                  | `FrameState::frame`          |
| `0x46fb74`   | global tick counter                      | `FrameState::globalFrame`    |
| `0x46fbbc` / `0x46fb8c` | demo index / demo position     | `Game::demoIndex_/demoPos_`  |
| `0x46d050`   | paused                                   | `Mode::Paused`               |
| `0x46b1a8`   | pending "*n" level-transition command string | `Game::requestLevel`     |
| `0x453080`   | palette entry flags                      | `ph::Palette::flags_`        |
| `0x4503c0`   | local→global colour remap table          | `ph::Palette::remap_`        |
| `0x46d3e8`   | 448×256 DIB bits                         | `engine::Framebuffer`        |
| `0x4504f4`   | view origin inside the DIB               | `Framebuffer::viewBase()`    |

## Level transition commands (`FUN_00443c90` switch on `DAT_0046b1a8[1]`)

The game logic requests a transition by setting a string `"*<digit>..."` and jumping to the engine exit:
`*0` continue screen (level 14, the level kept in `DAT_0045050c`), `*1` back to that level, `*2` game over,
`*3` the level logic starts again at the checkpoint without the level being loaded (`FUN_004360e9`), `*4` level 10 →
room 22 else map 19, `*50` the 2600 level 20 (level 24 with the `DAT_0046d05c` cheat) with `DAT_00450fc8` = 0xb,
`*51`..`*53` the vaults 11..13 with `DAT_00450fc8` = the level left, `*6` out of the 2600 level (back to level 3 with
`DAT_004601c4` when `DAT_00450fc8` is 0xb), `*7` out of a vault (back to `DAT_00450fc8` with `DAT_004504cc`), `*8`
title, `*9` credits.  The engine adds the shake (`DAT_004517ec`) to the main scroll on nearly every level; the vaults
draw their room as the parallax layer at the main scroll (rounded to 8 px, `FUN_0043731c`, its priority tiles after the
sprites) and their ceiling as the main layer over everything at the parallax scroll (`0x4371a5`).  A level load runs one logic tick in `FUN_00435fc0` and one engine tick (thirteen more on levels
0, 2, 5, 8 and 16, the table `0x46ffa0`) before its first frame; `*3` runs the one logic tick and the thirteen.
Title → attract: after 0x5a0 ticks play demo 0 (level 0), demo 1 (level 1),
demo 2 (level 20 — the Atari 2600 easter-egg level), then the intro (21); `DAT_0046fbbc` (the demo index) is -1 in
the data section, so the first demo is demo 0, and each demo starts a new game (`FUN_004324c6`, 0x44484d).

## Genesis half — symbols decoded so far

| address / global | meaning | new home |
|------------------|---------|----------|
| `FUN_00435fc0` / `FUN_004360e9` | level logic init / restart (bridge from the engine) | `Game::startLevel` + `PlayLogic::enter` |
| `FUN_0043628c` | per-tick logic dispatcher | `PlayLogic::tick` |
| `FUN_004263f3` | joypad → direction/button bytes (`0x466765`, `0x466761`) | `FrameState::joypad` |
| `FUN_0043256f` / `FUN_004331d8` | level init, start position from the `^` trigger | `PlayLogic::enter` |
| `FUN_0042571f` / `FUN_00425d6f` | collision probes (`.det`), `FUN_004258a2` / `FUN_00425f88` (`.dt2`) | `ph::probeCell/probeNear` |
| `FUN_00425455` / `69` / `7d` | Harry ground probes (centre / front foot) | `PlayLogic::probeFeet` |
| `FUN_00428dfa`, `FUN_0042babc`, `FUN_00426810` | Harry state machine (run, jump, whip, …) | `PlayLogic::step*` (subset) |
| `FUN_004300e0` | velocity decay helper | `stepHorizontal` |
| `FUN_004237a5` | animation script interpreter (`0x46166b` run, …) | `PlayLogic::animate` |
| `FUN_00404714` | the game-logic camera: lead `DAT_004658ba` (-0x44 .. 0x44), look offset `DAT_004658a6`, 1/8 (sideways) and 1/4 (up / down) of the distance a tick capped at 7 px, the locks `DAT_00465b09 / 0b / 0d` (the right one tested before the move), boss and swing leads, the Balankanche ride branch (`DAT_00465c56`) | `PlayLogic::camera` (`game/scroll.cpp`) |
| `0x404dcb` / `0x404ea2` / `0x404f75` / `0x40503e` | scroll left / right / up / down; crossing into another 16-px cell flags that edge (`DAT_00465894 / 95 / 92 / 93`), the next tick that scrolls the same way without crossing spawns it | `scrollLeft / scrollRight / scrollUp / scrollDown` |
| `FUN_00403fa0` / `FUN_00404016` / `FUN_00404269` / `FUN_004042fe` | spawn the `.als` column 0x40 px left / 0x160 px right of the camera (rows `camY/16 - 3 < row <= camY/16 + 16`), the row two above / sixteen below its top (29 columns) | `spawnLeftColumn / spawnRightColumn / spawnTopRow / spawnBottomRow` |
| `FUN_0040454e` | object init from the `.als` template (`0x4678c4`; byte +9 picks the head or the tail of the list); the entry's id is cleared while the object lives | `PlayLogic::spawnEntry`, `ph::kObjectTemplates` |
| `FUN_00405114` / `FUN_00433d0b` | the level start's 33-column spawn; the ids without templates placed by the level start (rope spawners 0x15 / 0x16, drifting platforms 0xc9, a pushed mine cart where it was left) | `spawnAll / preSpawn` |
| `[DAT_004504e4]` | the level's `.als` data (4-byte header, a big-endian word per column with the offset of its `(row, id)` pairs, 0xff ending them), worked on in place; past the data the PC keeps Genesis variables (+0x384) | `PlayLogic::alsMem_` (padded to 4 KB), `alsLong / setAlsLong` |
| `FUN_00435780` / `FUN_00435905` | object pool alloc / free | `std::vector<LevelObject>` |
| `FUN_00434eef` | object → display-list entry (+0x10, flags 0x40 hflip / 0x80 vflip / 0x20 front) | `PlayLogic::emitSprites` |
| `FUN_004251f9` | trigger lookup at `(x + 0x20, y)` | `PlayLogic::checkTriggers` |
| `FUN_00430229` | trigger handlers (`_` exit, plane switch, …) | `checkTriggers` |
| `FUN_00430a69` | death / respawn, `.als` + `.trg` restore, game over | `Game::loseLife` |
| `0x465b0f` / `0x465b13` | Harry x / y | `Harry::x / y` |
| `0x465ba4` / `0x465ba8` | Harry vx / vy (16.16) | `Harry::vx / vy` |
| `0x465b70` / `0x465bb3` / `0x465aff` | on ground / collision plane / jump hold | `Harry` fields |
| `0x465ae3`, `0x466526`, `0x4466527`, `0x46653b` | lives, treasures, continues, score | `PlayerStats` |
| `0x4658ac` | level width in 16-px columns | `LevelData::columns` |
| `0x4668c4` | collision shape heights | `ph::kCollisionHeights` |
| `0x461f8e` | trigger class table | `ph::kTriggerClass` |

## The map screen (level 19, `src/game/map_screen.cpp`)

A level's exit (`0x430912`) keeps the level in `DAT_0046b1ac` and asks for `*4`, which loads level 19 (level 22 after the
Warrior Spirit).  The level starts as any other (`FUN_00435fc0` → `FUN_0043256f`), and the end of the restart
(`0x432cd6`) runs the map's set-up `0x431c99`; `FUN_0043628c` then runs the joypad and `FUN_00432038` instead of the
usual tick.  The tables: `0x46aa34` the path by the level left, `0x46aa4e` the next level by the path (kept in
`DAT_0045050c` for `*1`), `0x461c44` the 13 paths ({x, y, frame | 0x8000 h-flip | 0x4000 v-flip} words), `0x46a88e` the
names.  The tour this gives: 0, 1, 2, 3, 4, 5, 6, 16, 17, 7, 18, 8, 9, 10.

| address | role | port |
|---------|------|------|
| `0x431c99` | the set-up: the pegs of up to three paths before, the camera at the path's first point (0x15a / 0x196 to its left / above, kept +0x200 at `0x4661e0` / `0x4661e4` as 16.16), 0x28 ticks to the first step | `PlayLogic::mapSetup` |
| `FUN_00432038` | a peg every 16 ticks (sounds 9 / 0xa by turns), the camera a sixteenth of the way to it each tick; the buttons count after `DAT_0046a88a` (30) ticks | `PlayLogic::mapTick` |
| `0x4323b4` | the path's end: the name (`FUN_00431ad5`) rises 3 px a tick to the camera's y + 0xb4; `DAT_004601c0` (180) ticks or a button: `*1` | `PlayLogic::mapArrived` |
| `FUN_00431a67` | the jungle sounds 0x9b .. 0xa1 every 0x80 .. 0xff ticks on channel 1 | `PlayLogic::mapAmbient` |
| `FUN_00431ad5` / `FUN_00431b73` | a string in `ltr_lg` letters (type 2 objects) centred on the screen; the glyph and its width by character | `PlayLogic::mapText`, `mapGlyph` |
| `FUN_00435174` | the display: Harry, then every object from the tail of the list | `PlayLogic::listAll` |

## The Atari 2600 level (level 20, `src/game/atari2600.cpp`)

A game of its own in the Genesis half (0x401e00 .. 0x403f92): `FUN_00435fc0` runs `FUN_00401e00` for it, `FUN_004360e9`
(`*3`) `0x401f3c` and `FUN_0043628c` the joypad and `0x4020d1`; the first two run on into the third.  It keeps Harry's
record (the sprite `als2600`), its own object pass and display, and leaves through `*3` (a life lost) and `*6`.

| address | role | port |
|---------|------|------|
| `FUN_00401e00` | the level's start: 20:00, two lives, 2000 points, screen 0, waiting for the first input | `PlayLogic::a26Start` |
| `0x401f3c` | a screen starts (an edge crossed, or `*3`): the box object, the camera (0x108, 0xf6), the screen's objects | `PlayLogic::a26Screen` |
| `0x4020d1` | the tick: the end countdown (`DAT_00457410`), the pause after a death (`DAT_00457408` / `0c`), the start wait, the objects, Harry, the deaths, the clock | `PlayLogic::a26Tick`, `a26Deferred` |
| `FUN_0040218b` | the copyright line's rise behind the bars of the logo | `PlayLogic::a26Copyright` |
| `FUN_004024ee` / `FUN_00435174` | display: the rolling logs first, then Harry and the objects from the tail of the list | `PlayLogic::a26Show` |
| `FUN_0040254e` / `FUN_00402526` | the vine's swing (`[DAT_004504e4] + 0x12c .. 0x137`), score, lives, clock, logo, copyright, the holes (glyphs of `chrs2600` through `FUN_00434e8a`) | `PlayLogic::a26Hud`, `a26Glyph` |
| `FUN_0040291c` | its object pass: crocodiles (7), rolling logs (6), the scorpion (5), the vine (4), pits / swamps / the pond (3), things on the ground (2: logs, fire, cobra, treasures; table `0x403138`) | `PlayLogic::a26Objects` |
| `FUN_004031f7` | a screen's objects from its descriptor (`0x4695a8` + screen * 4: trees, ground, object, vine) | `PlayLogic::a26Populate` |
| `FUN_004036fc` / `FUN_00403b0d` | Harry: the edges (a screen on, three underground; screen 0xff skipped), ladders, walls, holes; the vine's flight, dropping in, climbing, falling, the jump (`0x403f22`), running (`0x403f0c`) | `PlayLogic::a26Harry`, `a26Move` |
| `FUN_00403f68` | the trip sound, every fourth tick | `PlayLogic::a26Trip` |

Its variables live at 0x465e76 .. 0x46627c (listed at the top of `atari2600.cpp`); the screen is the low word of Harry's
vy (`DAT_00465ba8`), the jump step and dx the two words of his vx.  Two control-flow tricks are kept: crossing an edge
pops the return address and jumps to `0x401f3c`, so the next screen starts and runs a whole tick in the same frame (the
old screen's display entries stay first in the list for that frame); and a jump's start jumps back to `0x4036fc`, so
Harry's code runs twice that tick.  `DAT_00465b02` is 0x10 throughout, which `FUN_00434eef` takes off the y: the
sprites sit at their y.

## Object system (ported in `src/game/objects.*`, `src/game/handlers.cpp`)

| address / global | meaning | new home |
|------------------|---------|----------|
| `0x405170` / `0x4051e5` / `0x405313` | object dispatcher: walks the list from `DAT_00462848` (the next record taken before each handler), appends each object to one display array at `0x464bfc` (behind Harry from entry 0, in front from entry 0x50; -1 ends a list, -2 is an entry cancelled by a free; `DAT_00465b8e` = the running object's entry), jumps through the handler table | `ObjectSystem::tick / displayArray`, `PlayLogic::displayList` |
| `0x462350` | 256-entry handler table indexed by `obj+8` (type) | `ObjectSystem::handler` |
| `0x462854 .. 0x464ab4` | object pool, 0x50 records of 0x6e bytes | `ObjectSystem` (`Obj`) |
| `FUN_00435905` / `FUN_00435780` / `FUN_0043598f` | allocate at the head / the tail, free a record. `FUN_004356bc` / `FUN_0043546e` also copy the parent's x / y, and with `d0.b != 0` (nearly every call site) call the VRAM-slot stub `FUN_004263a6`, which marks the record with `w26 = 0x6663` | `ObjectSystem::allocHead / allocTail / free`, `ObjCtx::spawnFrom(parent, tail, d0)` |
| `FUN_00423134` / `FUN_00423150` | animation script interpreter (frame bytes, `f0` loop, `fc/fd/ee/ef/fb/ed` sounds, `f7` flip, `f9` unflip) | `animStep / animPeek` |
| `FUN_004237a5` | pointer-based script interpreter (Harry's; objects use frames, `f0`, sounds, `f3` jump, `f1` set sprite) | `anim2Step` |
| `0x424171` | weapon hits: sling list `0x466808`, stone `0x46681c`, whip box `0x465aeb..` against the sprite box; `obj+0xa` accumulates, `d5` = hit points | `weaponHits` |
| `0x4081fc` | enemy killed: class table `0x45b6c3` (bit 1 = permanent), score table `0x461aca`, death-effect table `0x45b814`, kill list `0x465a40` | `killObject`, `Game::permanentlyGone` |
| `FUN_00424af1` | sprite box overlap test; boxes come from the `0x468824` table through the sprite header word at +8 | `ObjCtx::boxesOverlap / touchesHarry`, `ph::kHitboxes` |
| `0x407aff` | Harry contact every 6th tick, hurt by `d5` | `harryContact` |
| `FUN_00428233` | hurt Harry: `+d5` damage, cap 0xa0, 0x46 ticks invulnerable, hit sounds 0x17/0x2f/0x30/0x31 | `PlayLogic::hurt` |
| `0x4301ce` | triggers 0x30 / 0x31 toggle the in-front flag; it leaves `FUN_004251f9`'s results, which several callers test next | `priorityTrigger` (returns the `Trg`) |
| `0x424d6a` + `0x424ec4`, `0x424fbb` + `0x425051` | handler exits: release when 120 px / one screen beyond the camera | `exitDespawnFar / exitDespawnWide` |
| `0x424cd4` | "Harry stands on this platform" box test | `harryOnBox`, `PlayLogic::standOn` |
| `0x412629` | stop-watch freeze sparkle | `freezeSparkle` |
| `0x433914` / `0x433747` / `0x433706` | one-shot effect object (type 0x68) / sparkle bursts (type 0x10) | `ObjCtx::spawnEffect / sparkleBurst` |
| `0x45a8d4` | pickup effect table (sprite, frame, routine) | `kPickupEffects` |
| `0x4612e0` | whip hit boxes per Harry sprite and frame | `PlayLogic::runObjects` |
| `0x4658b4/b6` | the game logic's camera (left/top edge) | `FrameState::scrollX/Y` |
| `0x46652f` / `0x4652b` / `0x46652d` | stop-watch timer / chili timer / invulnerability (read live: a watch picked up earlier in a pass freezes the objects after it) | `ObjCtx::frozen()`, `Harry::chiliTimer / invulnerable` |
| `0x466527` / `0x465ae3` / `0x466526` | lives / continues / treasure count | `PlayerStats` |
| `0x4285d7` / `0x428625` | Harry grabbed by a bat (hyidazed, 12 input changes to break free) | `PlayLogic::grabHarry / tickGrabbed` |
| `0x465b7e` / `0x465b4d` / `0x429fe2` | Harry on a bungee vine: the vine moves him; `0x46676a` (jump edge) is consumed by the handler | `Harry::bungee`, `PlayLogic::grabBungee`, `ObjCtx::padPressed` |
| `0x424ec4` | "far from the camera" test on its own | `farFromCamera` |
| `0x424f40` | more than 0x20 px outside the screen (thrown weapons, the pouch respawner) | `offScreen` |
| `0x466808 .. 0x466818` / `0x46681c` | the five in-flight stone slots (the stone's +0x3a points at its slot) / the boomerang's; `weaponHit` looks only at these | `leaveSlot`, `weaponHit` |
| `0x461a90` | the boomerang's flight path, `{dx, dy}` words from where it was thrown, ended by 0x8000 | `h_boomerang` |
| `DAT_004658a0` / `0x4658a2` | the big stone's blast has happened in this object pass / by which piece (cleared by each pass) | `h_stonePiece`, `stoneBlast` |
| `FUN_00425d6f` (d3 after the call) | the probe leaves `shape * 16 + (x & 15)` on a hit, the row byte of the entry that ended the scan on a miss; the one-way door reads its "already shut" class through it | `floorProbeD3` |
| `0x415028` | pouch respawner (type 74): template 7 (`mi_pouc2`) spawned off-screen while `DAT_00466528` (sling stones) < 10 | `h_pouchRespawner` |
| `FUN_0042e25e` / `FUN_0042ea52` (`0x42e49b..`, `0x42ec8f..`) | invisible walls: `.trg` cells whose class byte has bit 0, probed 8 / 24 px ahead of Harry at y - 4 and 24 px ahead at y - 0x1c; he snaps to `(x & ~15) + 7` (left) / `+ 9` (right) | `PlayLogic::stepHorizontal` |
| `FUN_00430229` + `FUN_004251f9` | Harry's trigger cell is `((x + 0x10) >> 4, max(4, y - 0x10) >> 4)`; `.trg` range entries `{0xfe, from, to, id}` cover `from <= row < to` | `PlayLogic::checkTriggers`, `ph::ColumnTable` |
| `0x46679e..a4` | checkpoint: x, y, the lit marker's x, facing; set to the `^` start by `FUN_004331d8` at each level start (unless Harry comes out of a vortex), moved by the fx_mls markers, where `FUN_00432615` puts Harry back | Harry memory (`h_.w(0x46679e)` ..) |
| `FUN_004288ec` / `0x4288a2`, `0x4601dd` / `0x4611dd`, `0x465af3..afd`, `0x46a860..66`, `0x46a884..88` | a vortex warp's save: the 4 KB `.als` buffer, the 256 runtime trigger classes, the level, Harry's x + 4 / y / facing, the checkpoint, and the x of each vault vortex taken (it stays shut; cleared by a new game) | `Game::Warp` (`warp()`) |
| `[DAT_00450510] + 0x1f4 ..` | Genesis RAM the PC keeps in the trigger buffer, read big-endian: the boss pointer (+0x1f4, boss levels), the vaults' ceiling (+0x1f4, 16.16) and its speed (+0x1f8), "a round is on" (+0x1fc), a 128-tick timer (+0x1fe), failed / solved (+0x214 / +0x215), timers (+0x21c), Harry's squash script (+0x2f0, native) | `PlayLogic::tb / tw / tl / tln` (`trgRam_`), `boss_` |
| `FUN_00434a30` / `0x434d2d` | text in letter objects of type `DAT_00465c58` (ltr_lg capitals, ltr_md for type 0x11, ltr_sm small letters and digits; widths `0x46b0d8`), each line centred on x + 0xa0; a space after a capital is one of two random large glyphs | `PlayLogic::spawnText / textGlyph` |
| `DAT_00450fc8` / `DAT_004504cc` / `DAT_004601c4` / `DAT_0046d05c` | the level `*7` returns to (1: none, 0xb: back from the 2600 level), "coming out of a vault", "coming out of the 2600 level" (Harry at 0x6e5, 0x587 on level 3), the cheat that sends `*50` to level 24 | `Game::Warp::returnLevel / fromVault / fromAtari / cheat` |
| `0x466507` / `trg+0x1f4` | boss fight on / the boss record (heart beat script, exit trigger ignored, meter source) | `PlayLogic::setBoss / bossObject / bossActive` |
| `0x465b09` | camera left lock (the jaguar arena: `0x9fa`) read by `0x404e17` | `PlayLogic::lockCameraLeft` |
| `0x465d49` + `FUN_0043426d` | boss outro: controls dropped for 0x50 ticks, then `DAT_00466767` (left) held until the exit | `PlayLogic::startBossOutro`, `outroTicks_` |
| `0x427167` / `0x427207` | Harry shoved by a boss: 7 px/tick (2 with `DAT_00465b59 = 0xc00`), slowing by `DAT_00465b59` per tick | `PlayLogic::knockBack`, `Harry::knockDecel` |
| `0x41018c` + `0x4335ce` | boss health meter: three `scoline` digits + `mi_prcnt` at `camX + 0x82`, `camY + 0xde + slide`, `(hp - hits) * 100 / hp` through `FUN_00431a3f` | `h_bossMeter` (type 0x30) |
| `0x415b92` / `0x415d15` | boss death: score, `fx_deat2` cloud with the pointer script `0x45b323`, then type 0x51 bursts 22 `fx_stone` sparkles | `jaguar::bossDeath`, `h_bossDeathBurst` |
| `0x40528a` / `0x4051ab` / `0x4268b6` | trigger id 2 is class 3 (a wall) for every object handler, set before each dispatch; the thrown things (29, 57, 62 and the unported 4 / 5 / 65) clear it on entry. Id 3 is cleared for the object pass and set to `DAT_00465b70` (on the ground) at the top of Harry's update, so it stops Harry only on foot (level 8's sky barriers for the birds are id-2 cells) | `ObjectSystem::tick`, `PlayLogic::tick / runObjects`, the handlers |
| `DAT_00465b02` / `DAT_00465b0b` | vertical screen shake (copied to `DAT_004517ec`) / the camera's right limit (narrowed by the pit wheel's arena) | `PlayLogic::setShake`, `lockCameraRight` |
| `0x427528` | Harry drowning (sinks; the crocodiles let go) | `Harry::drowning`, `HarryState::Drowning` |
| `FUN_004351c5` | spawn from a 9-byte template `{type, frame, flags, sprite slot, class}` at the parent's position (`FUN_0043546e` copies x / y) | `spawnFromTemplate` |
| `DAT_00466504` | the level's hidden 1-up has been revealed (types 113 / 174); cleared at the exit trigger and on a new game | `PlayerStats::secret1up` |
| `FUN_004261e2` / `FUN_004262d9` / `FUN_0042633d` | the two sound channels: 1 (priority `0x4627c0`, skipped while Harry dies or the stop-watch runs) and 0 (priority `0x4627be`, an 8-tick hold `0x4627c2`); a lower-priority sound is dropped while the channel plays; `FUN_004262c8` / `FUN_004261d1` test whether a channel is playing | `PlayLogic::sound0 / sound1 / sound1Now / channel0Busy / channel1Busy` |
| `0x45b1fc` | level script table, 12-byte entries `{leverTrgId, doorTrgId, flagAddr, w6, w8, param}`; the lever handler (`FUN_0041804b`) finds the entry by the trigger under the lever and sets its flag, the door / chute handlers look their own trigger up to see whether the flag is set | `kLevelScript[16]`, `scriptByLever / scriptByDoor`, `levelScriptPull` |
| `DAT_00466510..25` | level-script flags (lever pulled, one-way door shut, crate blown): a level start clears 0x00..0x13 (`FUN_0043256f`), a lost life 0x00..0x11 (`FUN_00430eb6`), a new game all (`FUN_004324c6`: 0x14 / 0x15, the crates of level 2, last the game) | `Game::levelFlags_` (index = address - 0x466510) |
| `DAT_004658c0` | runtime copy of the trigger class table, rebuilt from `0x461f8e` at level start and restart (`FUN_00432713`): doors of pulled levers cleared, flag 0x15 clears id 0x5d, one-way doors 0x90..0xaf with flag 3 become class 3 | `PlayLogic::trgClass_`, `triggerClassOf / clearTriggerClass / setTriggerClass` |
| `DAT_0046650d` + `0x45b9c0 / 0x45b9d0` | temple piston cycle (0x50 steps, one every 5 ticks), phase offsets by trigger id 0xa0..0xa7 and the frame table | `PlayLogic::pistonPhase_ / pistonTick_`, `kPistonPhase / kPistonFrames` |
| `FUN_00447190` | binds the Genesis sprite slots to the level's sprites by name (slot `obzpit2a` -> `obrpit2a` in level 3, `obtpit2a` in level 4, `obfpit2a` in levels 5 / 8, `fxleaf*2` leaves, the level-8 `obftung2 / fxpltof2 / ob_door2`, ...) | `ph::kSlotBindings` (generated `slot_bindings_data.cpp`), aliased in `ph::loadLevel` |
| `DAT_00465b41` / `0x42a6e7`, `0x41aaa0` / `0x46168e`, `0x417c93` / `0x42cdb6`, `DAT_00465b9a` | Harry pulling a lever (`hyipull`, 11 steps x 4 ticks, movement frozen), tripping over a skull (`hyitrip1`, 8 frames x 5 ticks), somersaulting off a tongue plant (`hyisomr2`), ticks spent running (>= 0x14 = running) | `Harry::lever / leverTicks / tripTicks / somersault / runTicks`, `PlayLogic::pullLever / trip / somersault / running` |

### Handlers ported (type: name in `handlers.cpp` `PC address`)

1 static `0x405322`, 2 item `0x405328`, 3 checkpoint `0x405aae`, 4 bigStone `0x405c86`, 5 slingStone `0x40613c`, 6
boomerang `0x406487`, 7 rideWarning `0x406b32`, 8 web `0x406bbc`, 9 dropBall `0x406ea6`, 10 condor `0x406f62`, 11
turtle `0x4078b0`, 12 roamingSnake `0x407bfc`, 13 crocSwimmer `0x4085fe`, 14 rope `0x4088ba`, 15 mineCart `0x408f5a`,
16 sparkle `0x40a2c8`, 18 bat `0x40a4be`, 19 pushCart `0x40ab62`, 20 crocLurker `0x40b08a`, 21 vaultDoor `0x40b390`, 23 rat
`0x40b5d2`, 26
lever `0x40b9e0`, 27 bungee `0x40c5fe`, 28 spear `0x40d322`, 29 projectile `0x40d3b0`, 30 piston `0x40d77a`, 31 door
`0x40d9e6`, 32 verticalWeb `0x40dc2e`, 33 chute `0x40de86`, 34 tnt `0x40e18d`, 35 monkey `0x40e394`, 36 spit
`0x40ebb0`, 37 cartSpark `0x40eca2`, 38 pot `0x40ed62`, 39 firePit `0x40f2ba`, 40 pit `0x40f620`, 41 hollow
`0x40f900`, 43 sleepingBat `0x40f9dc`, 44 fireball `0x40fa04`, 45 firePriest `0x40fc84`, 46 torch `0x4100a0`, 47 fish
`0x4100ca`, 48 bossMeter `0x41018c`, 50 explosion `0x41052a`, 51 floorSpikes `0x41061a`, 52 stoneBlock `0x410946`, 55
jaguar `0x410ce6`, 56 templeArcher `0x411aae`, 57 arrow `0x412124`, 59 pitCreature `0x412444`, 60 treeSnakeBody
`0x412710`, 61 gargoyle `0x41271a`, 62 axe `0x412b2c`, 63 leafSpawner `0x412c52`, 64 feather `0x412d6a`, 65 dust
`0x412f14`, 66 skullWarrior `0x412fc2`, 67 wheel `0x414100`, 68 swingBall `0x41479e`, 70 climbingRat `0x414ab1`, 71
vortex `0x414c58`, 72 vineMonkey `0x414dac`, 74 pouchRespawner `0x415028`, 76 fallingRock `0x41514e`, 78 batRoost `0x4153e0`, 79 rippleTwin
`0x41562c`, 80 boar `0x415640`, 81 bossDeathBurst `0x415d15`, 82 dripper `0x415e92`, 83 drip `0x415f4a`, 84
leapingCroc `0x4160b2`, 85 hawk `0x4165f2`, 87 explodingPlant `0x416be0`, 88 limpSnake `0x416f48`, 91 tonguePlant
`0x417a54`, 92 pushLever `0x417d0e`, 96 ceilingDripper `0x41836e`, 97 ceilingSpikes `0x418474`, 98 cartSpawner
`0x4185ce`, 99 runawayCart `0x4186c4`, 100 fireJet `0x418834`, 101 crocSplash `0x4188e0`, 102 fly `0x418992`, 103
vaultLetter `0x418e24`, 104 effect `0x418ec6`, 105 warriorSpirit `0x420bd0`, 106 spiritPart `0x422c7a`, 107 vaultIntro `0x418f00`, 108 textLetter `0x418f98`, 109 snake `0x418fd0`, 110 spider `0x419622`, 111 floatPlatform `0x419d00`, 112 rat2 `0x419ec8`, 113
hidden1up `0x41a092`, 115 rideDirector `0x41a2ae`, 116 floorSkull `0x41a9fa`, 117 hoppingSkull `0x41aade`, 118
markerFlame `0x41ae9a`, 119 whipLever `0x41aee0`, 121 rideMarker `0x41b208`, 122 harrySplash `0x41b24e`, 123 branch
`0x41b2da`, 124 branchTip `0x41b424`, 125 branchPiece `0x41b5d8`, 126 rideLaneLight `0x41b628`, 127 rideCart
`0x41b644`, 128 rideVapor `0x41b736`, 129 ripple `0x41b92e`, 130 deathCloud `0x41b9d1`, 132 / 133 scorpion `0x41baa0`,
135 boomerangBack `0x41bcb2`, 137 stonePiece `0x41bfe2`, 138 stoneSpark `0x41c420`, 140 stoneJaguar `0x41c4c2`, 141
stoneJaguarBase `0x41d47a`, 142 heart `0x41d520`, 147 coin
`0x41d876`, 148 pitWheel `0x41d9e6`, 149 wheelSpawner `0x41dbf2`, 150 ceilingRock `0x41dc50`, 151 hudText `0x41dd6e`,
153 jaguarFight `0x41ddac`, 154 jaguarRope `0x41e032`, 155 hudGo `0x41e110`, 156 hangingPouch `0x41e132`, 158 jagMan
`0x41e462`, 159 jagManClaw `0x41f24c`, 160 eyes `0x41f2c2`, 161 dot `0x41f320`, 164 snakeRespawner
`0x41f5e6`, 166 vaultReward `0x41f6ae`, 169 ropeSpawner `0x41fc1a`, 170 oneUpFly `0x41fd26`, 172 treeSnake `0x420090`, 173 oneWayDoor `0x42068c`,
174 leanLever `0x4208f8`, 176 lastLetter `0x420abf`.

Notes on some of them: the jaguar boss's states are INTRO `0x411391`, GO `0x41124d`, POUNCE `0x411054`, REVERSE
`0x41190f`, RUN `0x411651`, CROUCH `0x410f1f`, ROAR `0x41119c`, STOP `0x41156b`; the bungee vine drives Harry's state
`0x429fe2` with the triple-format scripts at `0x45aaa4` and releases through `0x42c3bc` / `0x42c2cc`; the condor and the
hawk share the stun tumble `0x4073d3` / `0x407555`, the wall turn `0x40769b` and the climb `0x40775b`.  On the vault levels
11..13 the lever (26, `0x40b9e0`) runs the Simon game: the first lever to run leads (`DAT_0046617c`, whose top byte
`DAT_0046617f` doubles as "showing the combination"), collects the three letters (103) into `0x466186`, and each round
shows the combination at `0x466194` (one more random lever a round, a step every 0x18 ticks) through the code pointer
at +0x52 (`0x40bc8d` wait, `0x40bd36` show, `0x40be67`, `0x40be92` check); Harry's state `0x42756d` walks him to the
levers on the keys 1..3 (queued at `0x4661a8`) and pulls them (`0x427d92`); ten rounds open the right door (21) and
drop a 1-up, rounds 3 / 5 / 7 / 9 a coin, silver, gold and a ring (166); a wrong lever or the timer brings the ceiling
down (the vault's main layer) and a message (`FUN_00434a30`, type 108 letters), then `*7`.  "wrong" is spelt in type
0x6f letters, which run the float platform's handler and are never shown on the PC.

The vortex (71) catches Harry standing in its box `0x45bbce` and hands him to his state `0x428780` (spins away in the
script `0x428a43`, 6 ticks a frame); the trigger under it then picks the exit: 0x89 the 2600 level (`*50`, level 3),
0x8a / 0x8b / 0x8c the vaults (`*53` from level 2, `*51` from level 4, `*52` from level 8), anything else the
checkpoint (`FUN_00432615`).

The stone jaguars of the Lost City of Copan (140) keep their state as the continuation address at +0x32:
STATUE `0x41cb3a`, WAKE `0x41cdef`, RUN `0x41ce7c`, CROUCH `0x41cb8b`, POUNCE `0x41cccb`, REVERSE `0x41d356`, LEDGE
LEAP `0x41d067`, LEDGE `0x41d195`, JUMP DOWN `0x41d251`.  A statue sleeps (released, its `.als` id put back) until
Harry is at x >= 0x7ba above y 0xcb, so it is met again only while the camera comes left with Harry beside it; the
first statue to see Harry in the arena (or a stone bounce off it) starts the fight: the controller (153, at the
list's tail), a health meter for each jaguar (type 0x30 with b5a 1 / 2 and l50 = the jaguar, 0x26 hit points),
`DAT_00466507`, and the jaguars at `0x465c58` / `0x465c5c`.  The controller wakes the first after 0x30 ticks (its
base, 141, crumbles; 0x27 ticks later it roars) and the second after the first's sixth hit, then swaps the roles
b5b (charge), b5c (attack: crouch and pounce, the leap onto the ledge) and b5d (come down) every 750 / 700 ticks;
with both dead it turns into the head of the rope (154) with eight type-0 segments, which Harry climbs to jump
onto the walkway that leads to the exit trigger 0x5f.  The fight start also copies the jaguars' colours into the
Genesis palette line at `0x462654` and sets the Genesis music byte `DAT_004627bd`; the PC reads neither.

The jaguar man of level 8 (158) keeps its state the same way: HIDDEN `0x41ecc3` (until Harry is up at x >= 0x1640
above y 0xba; it then shows 0xf0 px to the right, sets the boss (trg+0x1f4, `DAT_00466507`), the camera locks and
`DAT_00465d49`, which walks Harry right until he lands at y 0x132), WAKE `0x41ef24`, ROAR `0x41ee76`, RUN `0x41efd1`,
REVERSE `0x41f140`, CROUCH `0x41e80c` (into the man form), READY `0x41e8bd`, SWIPE `0x41eac3`, WAIT `0x41eaa1`,
MORPH `0x41eb76` and CHARGE `0x41ebf4` (back to the jaguar, slow or at 6 px/tick); 0x3e hit points (w6c >> 3).  Its
tests of Harry's damage byte are signed (`js`), so the claw (159) comes with every swipe at an unhurt Harry, and a
touch on a Harry whose damage byte is below 0x16 would count as a fatal one.  Its death and the type 55's are
`call 0x415b92`s that never return: the code after them runs when the pass's `ret` pops their return addresses
(`PlayLogic::strayReturn`): 0x410e6a starts the Ceiba Jungle walk-out, 0x41e62c stands a dazed Harry up.  After
the fight the stone face's tongue (91) throws Harry up to the walkway that leads to the exit (0x5f).

The Warrior Spirit (105) is a controller (never drawn) with a chain of 16 body parts (106, l3c from part to part,
each l64 the one before it); its pose animator `FUN_00422806` reads a script of pose numbers (0x460090..) with the
sound opcodes 0xfc / 0xfd / 0xfb, any other negative byte ending it, and sends every part towards its place in the
pose from one of eight tables (0x457420..0x45a814: word offsets, then {dx, dy, frame} for each part) in 8 steps;
`FUN_004226e6` moves the body and the places with it.  Its states (l46): INTRO `0x4214d6`, DECIDE `0x42134a`, THROW
`0x42164e` (fire, type 0x1d), SLAM `0x421ec2`, POUND `0x421800` (Harry stunned, his state 0x427316), SUCK
`0x42195b` (Harry pulled in by `0x4220f7`, lifted in the hand at `DAT_00465c64` in his state 0x42c2ff and thrown;
the throw ends with a `ret` that ends the object pass, `ObjectSystem::endPass`), PULL `0x421ff9` / `0x421fb6`, WALK
`0x422260` / `0x422423`, WALK BACK `0x422359` / `0x422479`, JUMP OFF `0x421b3e` (Harry behind it: up off the screen,
then RAIN `0x421d88` / `0x421e44`, its parts falling about until they gather where it lands) and IDLE `0x4214a0`;
`0x42154a` / `0x4217bd` and the despawn at `0x4224b9` are never reached.  The head (the last part with a frame
below 3) takes the hits (w6a >> 4, 0x30 kill); the decision's long-range path reads d1 without setting it (taken as
the weapon test's low byte, or 0x60 after a hit's sound).  Dead (`0x421bd3`, 50000 points), the parts fly apart and
burst, and the last letter (176) falls at x 0xf0: picking it up is the end of the game (0x430912).  It keeps its
own pointers in the `.als` buffer: +0x1c6 "dead", +0x1ca the pouch of stones it drops (or itself).

Still `h_default` (drawn in the template frame, never act): the types whose templates no level places (58
`0x412282`, 77 `0x4152bc`, 94 `0x41829c`, 157 `0x41e250`).  The boss meter (48) has all its branches: the type 0x69
boss on level 10, a type 0x8c jaguar at l50, the boss at trg+0x1f4 (0x3e hit points on level 8).

Every ported handler has been checked against its disassembly, in address order.  Quirks of the original that
are kept: the boar's grazing test reads a `d1` the contact test may have clobbered; the fire jet's roar count
tests `o+0x65` (its `a0` still points at the animation cursor); the tree snake's contact routine compares
Harry's sprite with the `tszhang` slot, so its moved box never applies; the one-way door's "already shut" test
reads the class byte the floor probe leaves in `d3`; the jaguar's `call 0x415b92` never returns (the pass's
`ret` pops it); each weapon test clears only the low word of `0x466788`; a ripple that ends cancels the
display entry its twin had in the last pass; the pit wheel's slope reads raw `.det` bytes on a probe miss; the
monkey's code at `0x40e914` is dead.  Documented and not emulated: the feather tests `+0x57` of whatever
record `a5` was left at; the skull warrior's freeze path calls the weapon test with a stale `d5`; the stone
jaguars' controller, turning into the rope, clears the frame of whatever record `a5` was left at.  The boss death
(`0x415b92`) clears the long at +0x32 (the burst counts in both words).
