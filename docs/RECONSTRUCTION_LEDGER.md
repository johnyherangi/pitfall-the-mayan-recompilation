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
| HUD (`FUN_00442fa0`, `FUN_00435d45`): the "%06d" score, the crocodile's place, jaws and dead pose, `mi_life`'s animations (the meter going down, a 1-up arriving, the blink and flash near the end), lives, treasures, weapon, ammo, the levels it is drawn on, shown in the demos with "DEMONSTRATION"; the slide in from the edges after a start or restart (`DAT_00451940` / `DAT_004667a8`, put back after the pre-ticks) | rendered frames of level 0 and demo 0; `tests/ph_tests.cpp` checks the slide (0x2d at the start, 0 after 15 ticks) and the tick count |
| The logic's end `0x4363bd` on every path (twice at a start or restart), the logic ticks a level load and `*3` run before the first frame (`0x46ffa0`), the hard flag set each tick (normal in a demo), the tick counts that are never reset (`DAT_004601d9`, `DAT_004601d5`) | `PlayLogic::epilogue`, `Game::preTicks`, `Game::hardMode` |
| Tick pacing: the 17 ms timer (58.8 Hz), the game-over wait of 3000 ms, the blink of "PAUSED" on the timer's count | `FUN_004448c0`, `0x44423d` |
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
| Engine exits inside the logic tick | `engineExit` unwinds the tick at once; the port runs the tick out and skips only its end (`0x4363bd`) |
| "PAUSED" | drawn with `TextOutA` in the original, in the 8×8 font here |
| Audio | software mixer instead of Miles; CD music/ambience tracks are listed but not played. The game logic's two sample channels and their priorities (`FUN_004262d9` / `FUN_004261e2`) are modelled |
| Options UI | the original property sheet (`PropertySheetA`, "System / Sound / Difficulty / Joystick / Keyboard / Language") is replaced by a menu; settings persist in `pitfall.ini` instead of the registry |
| Full-screen modes | DISPDIB 320×200 mode replaced by a stretched borderless window |

## Playable layer (`src/game/play_logic.cpp`) — reconstructed, simplified

| item | status |
|------|--------|
| Harry | the state machine of `0x426810 .. 0x431c99` ported continuation by continuation (`harry_states.cpp`, dispatched by the original addresses), the vortex warp `0x428780` with its save `FUN_004288ec` included |
| Floor following on slopes, walls | centre probe only; the original adds front-foot probes and per-state rules. The invisible walls of the `.trg` table (class bit 0, e.g. the jaguar arena's sides and the tree trunks) are honoured with the original probe offsets and snap |
| Camera | the original `FUN_00404714` (lead, look offset, locks, boss / swing / ride branches) with the edge-cell spawner it drives (`game/scroll.cpp`); objects appear when their column or row scrolls into view, as in the original (the Ceiba Jungle jaguar wakes only when Harry comes down into its arena at the right edge, the one time its entry is met again; the Lost City's stone jaguars only when the camera comes left over them with Harry beside them) |
| Objects | the original object system: pool, dispatcher, handler table, both animation interpreters, sprite hit boxes, weapon hits / kills, Harry contact, despawn windows (`src/game/objects.*`) |
| Enemies ported from their handlers | snakes (ground, roaming, respawning, tree snakes that spit), monkeys (+ thrown cactus) and vine monkeys, condors and hawks (+ feathers), stinging flies, bats (grab Harry) and bat roosts, rats (three kinds), spiders, scorpions, wild boars (toss Harry), crocodiles (swimmers and lurkers, Harry drowns), turtles (+ ripples), fish, skull warriors (all three variants, PC bone throw kept), temple archers (+ arrows), fire priests (+ homing fireballs), fire pits, spears, spitting heads, gargoyles (+ axes), exploding plants, tongue plants, tar pits, hopping and floor skulls, the Ceiba Jungle jaguar boss with its GO sign, health meter, death burst and walk-out, the Lost City's two stone jaguars (statues on crumbling bases, the fight's controller taking them in turns, two health meters, the leap onto the ledge, the rope out of the arena), the jaguar man of level 8 (its man form's swipe and claw, the charge, Harry dazed and finished off), the Warrior Spirit (16 body parts posed from the original tables, its fire, slam, pound, suction and throw, the leap off the screen and the rain of its parts, the pouches of stones, its death and the last letter that ends the game); 140 behaviour types in all, listed in `docs/ENGINE_MAP.md` |
| Jaguar boss details | the whole state machine, arena clamps, 0x960-tick aggression timer, 24-hit health and the knock-back are the original's; the Genesis boss music call (`FUN_004263a2`) is a stub in the PC build, so no music change; the idle-animation suppression (`DAT_00465d49`) has nothing to suppress yet; what stops Harry at the arena's right edge in the original (the `.det` floor ends 16 px short of the level edge) is unverified — here he can fall off it |
| Props ported | pickups with the original effect table, coins, hearts, tree pouches, pouch respawners (type 74), 1-up flight, sparkles, webs (bounce) and sticky webs (shove), crumbling rocks, branches (give way), TNT crates (platform, explode when whipped), mine-ceiling rocks, checkpoint markers, leaves, eyes, temple pistons (type 30, the shared 400-tick cycle), bungee vines (catch in mid-air, stretch / rebound physics, Harry's hanging animation scripts, jump off with the stored speed), clay pots (contents from the trigger under them, or random; whip to break), wall torches, pit creatures (surface at random within 32 px, per-level sprite through `kSlotBindings`), floor and ceiling spikes (extend when Harry nears / stands over them), fire jets, tongue plants (open on approach, somersault launch at -12 px/tick), gargoyles (turn toward Harry, throw axes; 40 hp), stone balls (dropping and swinging), rolling wheels and the wheel spawner, the pit wheel arena (camera lock, screen shake), stone blocks, drips and drippers, whip levers, the hidden 1-ups (Copan Temple's long fall, the Lost City's lean lever) |
| Level script ported | levers (type 26, "up" in front of one: Harry's 44-tick `hyipull`, the lever's 11-step animation, the `0x45b1fc` entry's flag set), doors (type 31, slide open when their lever's flag is set and Harry is 0x40 px away), chutes (type 33, hatch Harry stands on, opens when the lever is pulled, he drops through and it closes behind), one-way doors (type 173, shut behind Harry 40..144 px past, class 3 wall, flag 3 kept across deaths); the flag array `DAT_00466510..` and the runtime class table `DAT_004658c0` are rebuilt at level start / restart as the original does. The `FUN_0043cbc0` sound-channel tests in the lever handler are not ported; the chute's burn signal `0x46676d` is ignored |
| Bungee details | the original's release states are `0x42c3bc` (hyivjump, gravity 0x4600) and `0x42c2cc` (hyifall tumbling); here both go through `launch()`, so the fall off the vine shows the ordinary jump frames; the camera's look-down while hanging (`DAT_004658a6`) is not ported; the handler's quirk of re-entering itself at the top of a bounce until the position fraction carries is reproduced literally |
| Objects not ported yet | only the types whose templates no level places (58, 77, 94): drawn in their template frame, they do nothing (`h_default`) |
| Loltun vaults | levels 11..13 as the original: the vault set-up at the end of the restart, Harry's state `0x42756d`, the levers' Simon game, the letters, doors, rewards, the falling ceiling and its messages (`FUN_00434a30`), the exits (`*7`), and the vault render order (room in the parallax layer, ceiling in the main layer).  The levers' palette flash (`0x40b960` into the Genesis colours) is not shown |
| Mine carts and the ride | the ridden cart (15: track of both planes, slope frames, brake lever and sparks, wall crash, thrown out of a fast drop), the pushed cart (19), the runaway carts (98 / 0x63); level 7's scripted intro (`FUN_0043426d`) and the Balankanche ride (115 with its lights, runaway carts, coins, vapours and the 0x600-px loop) |
| Weapons | the whip (box table `0x4612e0`), sling stones (type 5) with the power shot's sparkle trail, the big stone (type 4) and its screen-clearing blast (types 0x89 / 0x8a), and the boomerang (types 6 / 0x87: flight path, homing, bounce cells, the return, riding it), each from its handler. The blast's white flash (`DAT_0046b1b0`) is kept but not drawn, as the palette fades are not ported |
| Triggers | start, exit, plane switch, water, priority 0x30/0x31, invisible walls, lever / door / chute / one-way door scripts, piston phases, pot contents; vaults and swing anchors ignored. The object template's byte 8 is a Genesis sound-channel class (`FUN_004263a6`, a stub in the PC build), not a spawn sound. Trigger id 2 is a wall for objects only (class 3 during each handler, cleared by thrown things) and id 3 a wall for Harry only while he is on the ground, as the original sets them per tick |
| Death | falling out of the level, water, or the damage meter (enemy contact).  `FUN_00430eb6` as the original: every object goes, the `.als` working copy takes the level's entries back but for the treasures and letters (ids 0x58..0x5c, 0xf0..0xf6), the lever flags 0x466510..21 are cleared, and `*3` starts the logic again in place at the checkpoint (`FUN_00432615`, the level is not loaded again); with the lives out `*0` the continue screen (level 14: the sign, "continues N", the letters found, Harry's match, yes / no) and a continue loads the level again from its start (`*1`), or `*2` the game-over screen for 3 s and the title |
| Level flow | exit (`0x430912`: the level kept in `DAT_0046b1ac`, `*4`) → the map screen (level 19) → the next level (`*1`), in the original's order 0, 1, 2, 3, 4, 5, 6, 16, 17, 7, 18, 8, 9, 10, and after level 10 the reunion room (22); the vortices lead to the vaults and back (`*51`..`*53`, `*7`) with the level saved and restored as `FUN_004288ec` / `FUN_0043256f` do, and on level 3 to the 2600 level (`*50`, `*6`) |
| Map screen | level 19 as the original: the set-up at the end of the restart (`0x431c99`: the path by the level left, the pegs of up to three paths before it, the next level), the camera's pan along the path a peg every 16 ticks with its plinks, the jungle sounds (`FUN_00431a67`), the next level's name in `ltr_lg` letters rising at the end, the buttons (after 30 ticks) or 180 ticks going on (`*1`), and its map drawn wrapping both ways (`0x43718c`) |
| Atari 2600 level | level 20 as the original: its start, screens and their 255 descriptors, the object pass (crocodiles, rolling logs, the scorpion, the vine, pits, swamps, fires, cobras, the 32 treasures), Harry's running, jumping, ladders, walls, holes and the vine, the deaths and the pause after them (`*3`), the clock, the game over and `*6`, the HUD and the copyright line in `chrs2600`, and the screen drawn from the two strips of the map (`0x437078`).  `DAT_0046fb88` (the engine's own start key, which also leaves the game over) is not kept; the pits' palette flags (0x2000 / 0x4000) select Genesis palette lines, which the PC ignores as well |

## Not ported yet — the rest of the Genesis-derived game logic

The machine-translated 68000 half of the binary (`0x401000–0x437fff`) is ported routine by routine: Harry's
states, the object system and 140 of its behaviour types, the camera and spawner, the triggers and level
scripts, the map screen, the 2600 level and the continue screen.  What is left: the palette fades
(`FUN_00441c50`, `DAT_0046b1b0`, the fade out / in around `*1` .. `*4` with their 750 ms waits), CD music, the
intro, title, credits, ending and reunion room logic (levels 15, 21 .. 24), the typed cheat codes (`0x439868`,
e.g. the invincibility of `DAT_0046d054`), the per-level water effects of `FUN_00443370` / `FUN_00437ba0`, and
the MSVC build.

For the fades: the 1-up's arrival (`0x41ff34` / `0x41ff56`) writes the word 1 at `a0 + 0x5a` and `a0 + 0x6a`,
a0 being left in the sparkle table by the same tick's sparkle (the arrival always falls on one): two words of
the death fade's target palette at `0x46212e` (0x462142 + 4r and 0x462152 + 4r, r the sparkle's corner, 4 bytes
lower when the sparkle found no free record), which the PC keeps in writable data.

## Known gaps in the asset layer

* `.trg` ids other than those listed above; `kTriggerClass` semantics.
* `.als` object *type* handlers for the types listed as not ported.
* Levels 22/23 have special renderers in `FUN_00436fb4` (levels 19 and 20's are ported: the wrapped map and
  `Game::renderAtari2600`).
