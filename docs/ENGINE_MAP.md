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
| `0x437128` / `0x436b2c` | deferred tile drain                         | `Renderer::drawDeferredTiles`         |
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
title, `*9` credits.  A level load runs one logic tick in `FUN_00435fc0` and one engine tick (thirteen more on levels
0, 2, 5, 8 and 16, the table `0x46ffa0`) before its first frame; `*3` runs the one logic tick and the thirteen.
Title → attract: after 0x5a0 ticks play demo 0 (level 0), demo 1 (level 1),
demo 2 (level 20 — the Atari 2600 easter-egg level), then the intro (21).

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
16 sparkle `0x40a2c8`, 18 bat `0x40a4be`, 19 pushCart `0x40ab62`, 20 crocLurker `0x40b08a`, 23 rat `0x40b5d2`, 26
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
`0x4185ce`, 99 runawayCart `0x4186c4`, 100 fireJet `0x418834`, 101 crocSplash `0x4188e0`, 102 fly `0x418992`, 104
effect `0x418ec6`, 109 snake `0x418fd0`, 110 spider `0x419622`, 111 floatPlatform `0x419d00`, 112 rat2 `0x419ec8`, 113
hidden1up `0x41a092`, 115 rideDirector `0x41a2ae`, 116 floorSkull `0x41a9fa`, 117 hoppingSkull `0x41aade`, 118
markerFlame `0x41ae9a`, 119 whipLever `0x41aee0`, 121 rideMarker `0x41b208`, 122 harrySplash `0x41b24e`, 123 branch
`0x41b2da`, 124 branchTip `0x41b424`, 125 branchPiece `0x41b5d8`, 126 rideLaneLight `0x41b628`, 127 rideCart
`0x41b644`, 128 rideVapor `0x41b736`, 129 ripple `0x41b92e`, 130 deathCloud `0x41b9d1`, 132 / 133 scorpion `0x41baa0`,
135 boomerangBack `0x41bcb2`, 137 stonePiece `0x41bfe2`, 138 stoneSpark `0x41c420`, 142 heart `0x41d520`, 147 coin
`0x41d876`, 148 pitWheel `0x41d9e6`, 149 wheelSpawner `0x41dbf2`, 150 ceilingRock `0x41dc50`, 151 hudText `0x41dd6e`,
155 hudGo `0x41e110`, 156 hangingPouch `0x41e132`, 160 eyes `0x41f2c2`, 161 dot `0x41f320`, 164 snakeRespawner
`0x41f5e6`, 169 ropeSpawner `0x41fc1a`, 170 oneUpFly `0x41fd26`, 172 treeSnake `0x420090`, 173 oneWayDoor `0x42068c`,
174 leanLever `0x4208f8`.

Notes on some of them: the jaguar boss's states are INTRO `0x411391`, GO `0x41124d`, POUNCE `0x411054`, REVERSE
`0x41190f`, RUN `0x411651`, CROUCH `0x410f1f`, ROAR `0x41119c`, STOP `0x41156b`; the bungee vine drives Harry's state
`0x429fe2` with the triple-format scripts at `0x45aaa4` and releases through `0x42c3bc` / `0x42c2cc`; the condor and the
hawk share the stun tumble `0x4073d3` / `0x407555`, the wall turn `0x40769b` and the climb `0x40775b`; the vault levers
of levels 11..13 (the Simon game at `0x40b9e0`'s vault branch) are not ported.

The vortex (71) catches Harry standing in its box `0x45bbce` and hands him to his state `0x428780` (spins away in the
script `0x428a43`, 6 ticks a frame); the trigger under it then picks the exit: 0x89 the 2600 level (`*50`, level 3),
0x8a / 0x8b / 0x8c the vaults (`*53` from level 2, `*51` from level 4, `*52` from level 8), anything else the
checkpoint (`FUN_00432615`).

Still `h_default` (drawn in the template frame, never act): 103 vault letters `0x418e24` (with the Simon game of the
vault levers), 105 the Warrior Spirit `0x420bd0`, and the later jaguars 140 `0x41c4c2` / 158 `0x41e462`, with the
objects only they spawn.  The
boss meter's branches for types 0x69 / 0x8c and the level-8 boss are not ported.

Every ported handler has been checked against its disassembly, in address order.  Quirks of the original that
are kept: the boar's grazing test reads a `d1` the contact test may have clobbered; the fire jet's roar count
tests `o+0x65` (its `a0` still points at the animation cursor); the tree snake's contact routine compares
Harry's sprite with the `tszhang` slot, so its moved box never applies; the one-way door's "already shut" test
reads the class byte the floor probe leaves in `d3`; the jaguar's `call 0x415b92` never returns (the pass's
`ret` pops it); each weapon test clears only the low word of `0x466788`; a ripple that ends cancels the
display entry its twin had in the last pass; the pit wheel's slope reads raw `.det` bytes on a probe miss; the
monkey's code at `0x40e914` is dead.  Documented and not emulated: the feather tests `+0x57` of whatever
record `a5` was left at; the skull warrior's freeze path calls the weapon test with a stale `d5`.
