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

The game logic requests a transition by setting a string `"*<digit>..."`:
`*1` game over → title, `*3` restart current level, `*4` level 10 → room 22
else map 19, `*5` next level (`*50` → 2600 level 20 / ending 24),
`*6` bonus vault return, `*7` continue, `*8` title, `*9` credits.
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
| `FUN_00403fa0` .. `FUN_004042fe` | spawn `.als` objects entering the view | `PlayLogic::spawnObjects` |
| `FUN_0040454e` | object init from template (`0x4678c4`) | `ph::kObjectTemplates` |
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
| `0x405170` / `0x4051e5` / `0x405313` | object dispatcher: walks the list from `DAT_00462848`, splits into the behind-Harry (`0x464bfc`) and in-front (`0x464d3c`) draw lists, jumps through the handler table | `ObjectSystem::tick`, `PlayLogic::emitSprites` |
| `0x462350` | 256-entry handler table indexed by `obj+8` (type) | `ObjectSystem::handler` |
| `0x462854 .. 0x464ab4` | object pool, 0x50 records of 0x6e bytes | `ObjectSystem` (`Obj`) |
| `FUN_00435905` / `FUN_0043598f` | allocate / free a record | `ObjectSystem::alloc / free` |
| `FUN_00423134` / `FUN_00423150` | animation script interpreter (frame bytes, `f0` loop, `fc/fd/ee/ef/fb/ed` sounds, `f7` flip, `f9` unflip) | `animStep / animPeek` |
| `FUN_004237a5` | pointer-based script interpreter (Harry's; objects use frames, `f0`, sounds, `f3` jump, `f1` set sprite) | `anim2Step` |
| `0x424171` | weapon hits: sling list `0x466808`, stone `0x46681c`, whip box `0x465aeb..` against the sprite box; `obj+0xa` accumulates, `d5` = hit points | `weaponHits` |
| `0x4081fc` | enemy killed: class table `0x45b6c3` (bit 1 = permanent), score table `0x461aca`, death-effect table `0x45b814`, kill list `0x465a40` | `killObject`, `Game::permanentlyGone` |
| `FUN_00424af1` | sprite box overlap test; boxes come from the `0x468824` table through the sprite header word at +8 | `ObjCtx::boxesOverlap / touchesHarry`, `ph::kHitboxes` |
| `0x407aff` | Harry contact every 6th tick, hurt by `d5` | `harryContact` |
| `FUN_00428233` | hurt Harry: `+d5` damage, cap 0xa0, 0x46 ticks invulnerable, hit sounds 0x17/0x2f/0x30/0x31 | `PlayLogic::hurt` |
| `0x4301ce` | triggers 0x30 / 0x31 toggle the in-front flag | `priorityTrigger` |
| `0x424d6a` + `0x424ec4`, `0x424fbb` + `0x425051` | handler exits: release when 120 px / one screen beyond the camera | `exitDespawnFar / exitDespawnWide` |
| `0x424cd4` | "Harry stands on this platform" box test | `harryOnBox`, `PlayLogic::standOn` |
| `0x412629` | stop-watch freeze sparkle | `freezeSparkle` |
| `0x433914` / `0x433747` / `0x433706` | one-shot effect object (type 0x68) / sparkle bursts (type 0x10) | `ObjCtx::spawnEffect / sparkleBurst` |
| `0x45a8d4` | pickup effect table (sprite, frame, routine) | `kPickupEffects` |
| `0x4612e0` | whip hit boxes per Harry sprite and frame | `PlayLogic::runObjects` |
| `0x4658b4/b6` | the game logic's camera (left/top edge) | `FrameState::scrollX/Y` |
| `0x46652f` / `0x4652b` / `0x46652d` | stop-watch timer / chili timer / invulnerability | `ObjCtx::freeze`, `Harry::chiliTimer / invulnerable` |
| `0x466527` / `0x465ae3` / `0x466526` | lives / continues / treasure count | `PlayerStats` |
| `0x4285d7` / `0x428625` | Harry grabbed by a bat (hyidazed, 12 input changes to break free) | `PlayLogic::grabHarry / tickGrabbed` |
| `0x465b7e` / `0x465b4d` / `0x429fe2` | Harry on a bungee vine: the vine moves him; `0x46676a` (jump edge) is consumed by the handler | `Harry::bungee`, `PlayLogic::grabBungee`, `ObjCtx::padPressed` |
| `0x424ec4` | "far from the camera" test on its own | `farFromCamera` |
| `0x415028` | pouch respawner (type 74): template 7 (`mi_pouc2`) spawned off-screen while `DAT_00466528` (sling stones) < 10 | `h_pouchRespawner` |
| `FUN_0042e25e` / `FUN_0042ea52` (`0x42e49b..`, `0x42ec8f..`) | invisible walls: `.trg` cells whose class byte has bit 0, probed 8 / 24 px ahead of Harry at y - 4 and 24 px ahead at y - 0x1c; he snaps to `(x & ~15) + 7` (left) / `+ 9` (right) | `PlayLogic::stepHorizontal` |
| `FUN_00430229` + `FUN_004251f9` | Harry's trigger cell is `((x + 0x10) >> 4, max(4, y - 0x10) >> 4)`; `.trg` range entries `{0xfe, from, to, id}` cover `from <= row < to` | `PlayLogic::checkTriggers`, `ph::ColumnTable` |
| `0x46679e..a4` | checkpoint (fx_mls marker) restored by `FUN_00430a69` | `Game::checkpoint` |
| `0x466507` / `trg+0x1f4` | boss fight on / the boss record (heart beat script, exit trigger ignored, meter source) | `PlayLogic::setBoss / bossObject`, `ObjCtx::boss` |
| `0x465b09` | camera left lock (the jaguar arena: `0x9fa`) read by `0x404e17` | `PlayLogic::lockCameraLeft` |
| `0x465d49` + `FUN_0043426d` | boss outro: controls dropped for 0x50 ticks, then `DAT_00466767` (left) held until the exit | `PlayLogic::startBossOutro`, `outroTicks_` |
| `0x427167` / `0x427207` | Harry shoved by a boss: 7 px/tick (2 with `DAT_00465b59 = 0xc00`), slowing by `DAT_00465b59` per tick | `PlayLogic::knockBack`, `Harry::knockDecel` |
| `0x41018c` + `0x4335ce` | boss health meter: three `scoline` digits + `mi_prcnt` at `camX + 0x82`, `camY + 0xde + slide`, `(hp - hits) * 100 / hp` through `FUN_00431a3f` | `h_bossMeter` (type 0x30) |
| `0x415b92` / `0x415d15` | boss death: score, `fx_deat2` cloud with the pointer script `0x45b323`, then type 0x51 bursts 22 `fx_stone` sparkles | `jaguar::bossDeath`, `h_bossDeathBurst` |

| `0x45b1fc` | level script table, 12-byte entries `{leverTrgId, doorTrgId, flagAddr, w6, w8, param}`; the lever handler (`FUN_0041804b`) finds the entry by the trigger under the lever and sets its flag, the door / chute handlers look their own trigger up to see whether the flag is set | `kLevelScript[16]`, `scriptByLever / scriptByDoor`, `levelScriptPull` |
| `DAT_00466510..` | 32 level-script flags (lever pulled, one-way door shut); kept across deaths, cleared with the level (`FUN_00430eb6`) | `Game::levelFlags_[32]` (index = address - 0x466510) |
| `DAT_004658c0` | runtime copy of the trigger class table, rebuilt from `0x461f8e` at level start and restart (`FUN_00432713`): doors of pulled levers cleared, flag 0x15 clears id 0x5d, one-way doors 0x90..0xaf with flag 3 become class 3 | `PlayLogic::trgClass_`, `triggerClassOf / clearTriggerClass / setTriggerClass` |
| `DAT_0046650d` + `0x45b9c0 / 0x45b9d0` | temple piston cycle (0x50 steps, one every 5 ticks), phase offsets by trigger id 0xa0..0xa7 and the frame table | `PlayLogic::pistonPhase_ / pistonTick_`, `kPistonPhase / kPistonFrames` |
| `FUN_00447190` | binds the Genesis sprite slots to the level's sprites by name (slot `obzpit2a` -> `obrpit2a` in level 3, `obtpit2a` in level 4, `obfpit2a` in levels 5 / 8, `fxleaf*2` leaves, the level-8 `obftung2 / fxpltof2 / ob_door2`, ...) | `ph::kSlotBindings` (generated `slot_bindings_data.cpp`), aliased in `ph::loadLevel` |
| `DAT_00465b41` / `0x42a6e7`, `0x41aaa0` / `0x46168e`, `0x417c93` / `0x42cdb6`, `DAT_00465b9a` | Harry pulling a lever (`hyipull`, 11 steps x 4 ticks, movement frozen), tripping over a skull (`hyitrip1`, 8 frames x 5 ticks), somersaulting off a tongue plant (`hyisomr2`), ticks spent running (>= 0x14 = running) | `Harry::lever / leverTicks / tripTicks / somersault / runTicks`, `PlayLogic::pullLever / trip / somersault / running` |

### Handlers ported (type: address -> function in `handlers.cpp`)

2 item `0x405328`, 3 checkpoint marker `0x405aae`, 8 web `0x406bbc`, 10 condor `0x406f62`, 16 sparkle `0x40a2c8`,
18 bat `0x40a4be`, 23 rat `0x40b5d2`, 26 lever `0x40c2d6` (levels 11..13's vault levers are not ported), 27 bungee vine `0x40c5fe` (Harry state `0x429fe2`; his hanging animation is
a triple-format script set at `0x45aaa4`, tables `0x45aa54` / `0x45aa88`; releases through `0x42c3bc` / `0x42c2cc`),
29 thrown cactus / bone / stone / spike `0x40d3b0`, 30 temple piston `0x40d77a` (shared cycle `DAT_0046650d`, phase
triggers 0xa0..0xa7), 31 door `0x40d9e6`, 32 sticky web `0x40dc2e`, 33 chute `0x40de86`,
34 TNT `0x40e18d` (+ 50 explosion `0x41052a`), 35 monkey `0x40e394`, 38 clay pot `0x40ed62` (contents by the trigger
under it, ids 0x34..), 40 tar pit `0x40f620`, 43 sleeping bat
`0x40f9dc`, 46 wall torch `0x4100a0`, 48 boss meter `0x41018c`, 55 jaguar boss `0x410ce6` (states INTRO `0x411391`, GO `0x41124d`, POUNCE
`0x411054`, REVERSE `0x41190f`, RUN `0x411651`, CROUCH `0x410f1f`, ROAR `0x41119c`, STOP `0x41156b`; speed
`0x411a3e / 0x411a7a`), 51 floor spikes `0x41061a`, 59 pit creature `0x412444`, 60 tree-snake body
`0x412710`, 61 gargoyle `0x41271a` (+ 62 thrown axe `0x412b2c`), 63 leaf spawner `0x412c52`, 64 feather / leaf `0x412d6a`, 70 climbing rat `0x414ab1`, 72 vine monkey
`0x414dac`, 76 crumbling rock `0x41514e`, 78 bat roost `0x4153e0`, 80 wild boar `0x415640`, 83 drip splash
`0x415f4a`, 74 pouch respawner `0x415028`, 81 dead boss `0x415d15`, 87 exploding plant `0x416be0`, 91 tongue plant `0x417a54` (somersault launch), 96 ceiling dripper `0x41836e` (+ 150 falling rock `0x41dc50`), 97 ceiling spikes `0x418474`, 100 fire jet `0x418834`, 102
stinging fly `0x418992`, 104 effect `0x418ec6`, 109 snake `0x418fd0`, 110 spider `0x419622`, 112 fast rat
`0x419ec8`, 116 floor skull `0x41a9fa` (trips a running Harry), 117 hopping skull `0x41aade`, 118 marker flame `0x41ae9a`, 123/124/125 branch `0x41b2da / 0x41b424 / 0x41b5d8`, 130 death cloud
`0x41b9d1`, 133 scorpion `0x41baa0`, 142 heart `0x41d520`, 147 coin `0x41d876`, 156 hanging pouch `0x41e132`,
160 eyes `0x41f2c2`, 161 dot `0x41f320`, 170 1-up flight `0x41fd26`, 172 tree snake `0x420090`, 173 one-way door `0x42068c`.
Everything else runs `h_default` (drawn, never acts); the biggest remaining ones are the skull warrior (66,
`0x413084`), the mine-cart ride (15, `0x408f5a`, carts 19 / 98), the stone balls (68 / 9), the turtle (11), the
vortex (71, `0x414c58`), the water snake (12, `0x407bfc`), the later jaguars (140, `0x41c4c2`; 158, `0x41e462`)
and the other bosses (the meter handler only reads the level-0 jaguar; its branches for types 0x69 / 0x8c and the
level-8 boss are not ported).
