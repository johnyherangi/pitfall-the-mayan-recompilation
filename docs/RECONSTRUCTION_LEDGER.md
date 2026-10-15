# Reconstruction ledger

What is faithfully reconstructed, what is approximated, and what is still the
original's job.  "Verified" means checked against the shipped data or the
decompiled code by a test or a rendered image.

## Done — verified

| item | evidence |
|------|----------|
| `.PH` container, record order for all 25 level files and INIT.PH | `tests/ph_tests.cpp` consumes every record of every file with zero leftovers |
| Sprite record layout, RLE decoder, frame hot-spots | `tools/phtool.py` + `phdump sprite`; Harry's run cycle renders correctly |
| Raw image (font, score digits) layout | `phdump info` (39 / 12 cells) |
| Background record, tile-map bits, 1-based palette | full-level renders of all levels with backgrounds |
| Global palette allocator (exact / free / nearest) | port of `FUN_004451a0`, same weights and flag tests |
| 448×256 back buffer, 320×224 view at `(scrollX&7, 9)` | `FUN_0043e1c0`, `FUN_00443b90` |
| Tile pass (41×29, opaque fast path, flips, deferred hi-priority tiles) | `FUN_00436fb4` |
| Sprite blitter incl. all four flip variants and clipping rules | `FUN_00438140`; H-flip verified by image |
| Line-scrolled parallax for levels 0/5 with horizontal wrap | `FUN_00436534` first branch |
| Text / glyph mapping | `FUN_00442600`, `FUN_004387c0` |
| HUD layout (score digits, lives, weapon, ammo positions) | `FUN_00442fa0` |
| Joypad bit layout, default key bindings, joystick thresholds | `FUN_00444940`, data at `0x46d08c` |
| Option names and defaults | `FUN_004400f0` |
| Sound bank (138 ids), CD track tables | `FUN_0043ce10`, `0x46e130`, `0x470470` |
| Attract-mode demo input scripts (3 recordings) | `0x46fbc0`, `0x46fc80`, `0x46fec8` |
| Level names, string resources, original icon | PE resources |
| Per-level parallax camera ratios | `FUN_00443370` switch |
| Title → demo → intro sequencing, Start handling | `FUN_00443c90` tail |
| `.det`/`.dt2`/`.als`/`.trg` column-list layout, collision shape table | parsed for all levels; the `^` start trigger lands Harry on a `.det` surface |
| Object template table (als id → sprite, offsets, snap) | `0x4678c4`; pouches/items render on the ground in levels 0/1/3 |
| Harry run / jump / fall constants and probe offsets | `FUN_00428dfa`, `FUN_00425455`; scripted run clears Ceiba Jungle end to end |
| Exit / plane / water trigger ids | `FUN_00430229`, disassembly at `0x428890` |

## Approximated

| item | what differs |
|------|--------------|
| Tile-based parallax for non-forest levels | generic wrapping tile draw at `(parX, parY)`; the original has per-level special cases (waterfall line animation `DAT_00450028..`, level 6/18 water reflection `FUN_00437ba0`, title-screen double layer) |
| Forest leaf line-scroll band | 32-entry wave table reproduced by shape, not copied from `DAT_00455010` |
| Title screen animation | final composition + blink only; the seven logo pieces drop in on frames 0xd2–0x163 (`FUN_004426e0`) |
| Palette fades (`FUN_00441c50`) and `AnimatePalette` effects | not ported |
| Audio | software mixer instead of Miles; CD music/ambience tracks are listed but not played; the original's two sample channels with their priorities (`FUN_004262d9` / `FUN_004261e2`: a sound cuts the one before it, a lower-priority one is dropped while a higher one plays) are not modelled, every sound mixes |
| Options UI | the original property sheet (`PropertySheetA`, "System / Sound / Difficulty / Joystick / Keyboard / Language") is replaced by a menu; settings persist in `pitfall.ini` instead of the registry |
| Full-screen modes | DISPDIB 320×200 mode replaced by a stretched borderless window |

## Playable layer (`src/game/play_logic.cpp`) — reconstructed, simplified

| item | status |
|------|--------|
| Harry run / decelerate / jump with variable height / fall / land | original constants; the edge-teeter, crouch, climb, hang, rope, vine, zip-line, swim and whip states are not ported |
| Floor following on slopes, walls | centre probe only; the original adds front-foot probes and per-state rules. The invisible walls of the `.trg` table (class bit 0, e.g. the jaguar arena's sides and the tree trunks) are honoured with the original probe offsets and snap |
| Camera | plain follow window; the original camera (`DAT_004658b4/b6`) is scripted per level |
| Objects | the original object system: pool, dispatcher, handler table, both animation interpreters, sprite hit boxes, weapon hits / kills, Harry contact, despawn windows (`src/game/objects.*`) |
| Enemies ported from their handlers | snakes (ground, roaming, respawning, tree snakes that spit), monkeys (+ thrown cactus) and vine monkeys, condors and hawks (+ feathers), stinging flies, bats (grab Harry) and bat roosts, rats (three kinds), spiders, scorpions, wild boars (toss Harry), crocodiles (swimmers and lurkers, Harry drowns), turtles (+ ripples), fish, skull warriors (all three variants, PC bone throw kept), temple archers (+ arrows), fire priests (+ homing fireballs), fire pits, spears, spitting heads, gargoyles (+ axes), exploding plants, tongue plants, tar pits, hopping and floor skulls, the Ceiba Jungle jaguar boss with its GO sign, health meter, death burst and walk-out; 92 behaviour types in all, listed in `docs/ENGINE_MAP.md` |
| Jaguar boss details | the whole state machine, arena clamps, 0x960-tick aggression timer, 24-hit health and the knock-back are the original's; the Genesis boss music call (`FUN_004263a2`) is a stub in the PC build, so no music change; the idle-animation suppression (`DAT_00465d49`) has nothing to suppress yet; what stops Harry at the arena's right edge in the original (the `.det` floor ends 16 px short of the level edge) is unverified — here he can fall off it |
| Props ported | pickups with the original effect table, coins, hearts, tree pouches, pouch respawners (type 74), 1-up flight, sparkles, webs (bounce) and sticky webs (shove), crumbling rocks, branches (give way), TNT crates (platform, explode when whipped), mine-ceiling rocks, checkpoint markers, leaves, eyes, temple pistons (type 30, the shared 400-tick cycle), bungee vines (catch in mid-air, stretch / rebound physics, Harry's hanging animation scripts, jump off with the stored speed), clay pots (contents from the trigger under them, or random; whip to break), wall torches, pit creatures (surface at random within 32 px, per-level sprite through `kSlotBindings`), floor and ceiling spikes (extend when Harry nears / stands over them), fire jets, tongue plants (open on approach, somersault launch at -12 px/tick), gargoyles (turn toward Harry, throw axes; 40 hp), stone balls (dropping and swinging), rolling wheels and the wheel spawner, the pit wheel arena (camera lock, screen shake), stone blocks, drips and drippers, whip levers, the hidden 1-ups (Copan Temple's long fall, the Lost City's lean lever) |
| Level script ported | levers (type 26, "up" in front of one: Harry's 44-tick `hyipull`, the lever's 11-step animation, the `0x45b1fc` entry's flag set), doors (type 31, slide open when their lever's flag is set and Harry is 0x40 px away), chutes (type 33, hatch Harry stands on, opens when the lever is pulled, he drops through and it closes behind), one-way doors (type 173, shut behind Harry 40..144 px past, class 3 wall, flag 3 kept across deaths); the flag array `DAT_00466510..` and the runtime class table `DAT_004658c0` are rebuilt at level start / restart as the original does. The vault levers of levels 11..13 (`FUN_0041804b` branches on `DAT_004658a8` 0xb..0xd) and the `FUN_0043cbc0` sound-channel tests in the lever handler are not ported; the chute's burn signal `0x46676d` is ignored |
| Bungee details | the original's release states are `0x42c3bc` (hyivjump, gravity 0x4600) and `0x42c2cc` (hyifall tumbling); here both go through `launch()`, so the fall off the vine shows the ordinary jump frames; the camera's look-down while hanging (`DAT_004658a6`) is not ported; the handler's quirk of re-entering itself at the top of a bounce until the position fraction carries is reproduced literally |
| Objects not ported yet | the mine-cart ride (15 / 19 / 98), vortex warps (71), the limp snake (88), the push lever (92), crouching into a hollow (41), the vault letters (103), the Warrior Spirit (105), type 115, the later jaguars (140 / 158) and Harry's thrown weapons (4 / 5 / 65): most of them need Harry states that are not ported yet; they are drawn in their template frame and do nothing (`h_default`) |
| Weapons | whip only (box table `0x4612e0`); sling stones and boomerang not thrown yet, so `weaponHits` sees only the whip |
| Triggers | start, exit, plane switch, water, priority 0x30/0x31, invisible walls, lever / door / chute / one-way door scripts, piston phases, pot contents; vaults and swing anchors ignored. The object template's byte 8 is a Genesis sound-channel class (`FUN_004263a6`, a stub in the PC build), not a spawn sound. Trigger id 2 is a wall for objects only (class 3 during each handler, cleared by thrown things) and id 3 a wall for Harry only while he is on the ground, as the original sets them per tick |
| Death | falling out of the level, water, or the damage meter (enemy contact); checkpoint markers restore the position; lives / continues as the original |
| Level flow | exit → next level in tour order; the map screen (level 19) is skipped |

## Not ported yet — the rest of the Genesis-derived game logic

Enemy and object behaviour, weapons, the whip/rope/vine/zip-line/swim states,
bonus vaults, map screen, boss, cutscenes and per-level scripts remain in the
machine-translated 68000 half of the binary (`0x401000–0x437fff`, 196 Ghidra
functions, ~70 k lines of pseudocode in `re/pitfall_decomp.c`).  It is
driven by:

* `FUN_0043628c(level)` — per-tick dispatcher called from `FUN_00443370`;
* the `.trg / .det / .dt2 / .als` level tables (loaded, kept opaque);
* globals such as `DAT_004601b8` (Harry object), `DAT_00465b69` (Harry's
  continuation address), `DAT_0046aa94` (display list it fills),
  `DAT_004517f8` (joypad it reads), `DAT_0046b1a8` (transition command it sets).

The port hook is `game::LevelLogic` (`src/game/game.h`): `PlayLogic` for
levels with collision data, `ExplorerLogic` (free camera) for the others.
Suggested next steps: object behaviour handlers by `type` (start with the
snake, condor and treasure chest), Harry's remaining states, the whip and
sling, then the map screen and vaults.

## Known gaps in the asset layer

* `.trg` ids other than those listed above; `kTriggerClass` semantics.
* `.als` object *type* handlers for the types listed as not ported.
* Level 14 (continue screen) has no `.bg`; the original draws it from sprites.
* Level 20 (2600 level) and 19/22/23 have special renderers in `FUN_00436fb4`.
