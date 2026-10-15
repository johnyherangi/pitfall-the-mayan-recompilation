# Pitfall: The Mayan Adventure (Windows 95) — asset formats

Everything below was recovered from `PITFALL.EXE` (Kinesoft Development for
Activision, 1995; MSVC 2.x/4.x build) with REA/Ghidra and verified against
the shipped data files by `tools/phtool.py` and `tests/ph_tests.cpp`.

## 1. `.PH` container

A `.PH` file is a **sequential stream of size-prefixed records** with no
directory and no names:

```
repeat:
    uint32  size
    uint8   body[size]
```

The engine reads records in a fixed order hard-coded in the executable
(`FUN_004457d0` for `INIT.PH`, `FUN_004460e0` + `FUN_00447190` for the level
files, `FUN_0043ce10` for the sounds).  The record names that appear in the
binary (`hyirun`, `forest1.bg`, …) are only used for error messages; the
real association is positional.  `tools/manifests.json` holds those orders
and `src/ph/manifests_data.cpp` is generated from it.

The game also opens all files through a 1.5 MB arena (`GlobalAlloc(0x180000)`)
and keeps every record in memory for the lifetime of a level.

### INIT.PH (2,057,605 bytes, 258 records)

| records   | kind         | contents                                         |
|-----------|--------------|--------------------------------------------------|
| 0         | raw image    | `font` — 39 cells 8×8: A–Z, 0–9, `,` `!` `.`      |
| 1         | raw image    | `scoline` — 12 cells 8×16: digits 0–9, `x`, blank |
| 2 … 119   | sprite       | Harry (`hyi*`), items (`mi_*`), effects (`fx*`), letters, HUD icons |
| 120 … 257 | RIFF/WAVE    | 138 sound effects, 8-bit mono 11/22 kHz PCM       |

### LEVELxx.PH (x = 00 … 24)

| order | record   | present when the name table entry is non-empty  |
|-------|----------|-------------------------------------------------|
| 1     | `*.bg`   | main tile layer                                 |
| 2     | `*.trg`  | trigger table (opaque, consumed by game logic)  |
| 3     | `*.det`  | detail table (opaque)                           |
| 4     | `*.dt2`  | detail table 2 (opaque)                         |
| 5     | `*.als`  | "alien" (enemy/object) placement list (opaque)  |
| 6     | parallax `*.bg` | parallax tile layer                       |
| 7     | extra `*.bg`    | only levels 15 (`logo3.bg`) and 22        |
| 8 …   | sprites  | per-level sprites in `FUN_00447190` order       |

The per-level name table lives at `0x46f0d8` (25 entries × 0x60 bytes, six
16-byte names: bg, parallax, det, dt2, trg, als).  The `.trg` record is loaded
and then duplicated in memory; `.als` is copied to a second 2 KB block
(`FUN_004460e0`) — the copies are the pristine state restored after a death.
Their layout is described in section 8.

## 2. Sprite record (`magic 0x34561234`)

```
u32  magic            0x34561234
u32  pal_off          offset of the local palette
u16  ncolors
u16  nframes
u32  frame_off[nframes]        relative to body + 0x10
...  frames
rgb  palette[ncolors]          3 bytes each, 8-bit RGB
```

Each frame:

```
i32 w, i32 h, i32 hot_x, i32 hot_y
rle rows
```

RLE opcodes (`FUN_00438140` blitter, `FUN_004453a0` loader):

| byte      | meaning                               |
|-----------|---------------------------------------|
| `< 0x7e`  | skip *n* transparent pixels           |
| `0x7f`    | end of row                            |
| `0x7e`    | end of frame                          |
| `>= 0x80` | `n - 0x7f` literal pixels follow      |

Pixel values are indices into the record's *local* palette.  At load time the
colours are allocated into the shared 256-entry palette (section 4) and every
literal pixel is rewritten in place to its global index, so the blitter writes
bytes straight into the frame buffer.

Drawing rules (`FUN_00438140`, display-list entry `{x, y, sprite, frame, flags}`):

* `flags & 1` visible, `0x20` draw after the high-priority tiles,
  `0x40` vertical flip, `0x80` horizontal flip.
* Unflipped: top-left = `(x + hot_x, y + hot_y)`.
  H-flip: the first pixel of each row lands at `x - hot_x` and runs leftwards.
  V-flip: the first row lands at `y - hot_y` and rows run upwards.
* `frame` wraps modulo `nframes`.
* Horizontal clipping rejects whole sprites only; the 448-pixel-wide back
  buffer absorbs the overhang.  Vertical clipping skips RLE rows.

## 3. Raw image record (font, score digits)

```
u32 magic ; u32 pal_off ; u16 ncolors ; u16 cell_bytes ; u16 cell_w ; u16 cell_h
u8  pixels[pal_off - 0x10]     cells back to back, row-major
rgb palette[ncolors]
```

Colour 0 is transparent when cells are drawn (`FUN_004387c0`).  Text glyph
mapping (`FUN_00442600`): `A–Z → 0–25`, `0–9 → 26–35`, `, → 36`, `! → 37`,
`. → 38`.

## 4. Background record (tile layer)

```
u32 w_tiles ; u32 h_tiles ; u32 tiles_off ; u32 pal_off ; u32 ncolors-1
u16 map[w_tiles * h_tiles]
u8  tiles[(pal_off - tiles_off) / 64][64]      8×8 pixels each
rgb palette[ncolors-1]
```

Tile-map entry bits:

| bits    | meaning                                                |
|---------|--------------------------------------------------------|
| 0–11    | tile index                                             |
| 0x1000  | opaque tile — copied with a fast 8×8 copy              |
| 0x2000  | vertical flip                                          |
| 0x4000  | horizontal flip                                        |
| 0x8000  | high priority — deferred and drawn **after** sprites    |

Local colour index 0 is transparent (the parallax layer shows through);
local index *i ≥ 1* maps to `palette[i-1]`.  The camera limits are
`(w_tiles/2 - 20) * 16` and `(h_tiles/2 - 14) * 16` pixels.

For levels 0 and 5 the parallax tile map is expanded into a linear bitmap at
load time because their parallax renderer scrolls individual lines
(`FUN_004460e0` tail, `FUN_00436534`).

## 5. Global palette (`FUN_004451a0`)

The hardware palette has 256 entries; Windows reserved 10 static colours at
each end, so the game allocates within `[10, 246)`.  Each entry carries flags
(`0x453080`): `1` allocated by the current level, `4` allocated by `INIT.PH`
(persistent), `0x80` system.  Allocation order for every local colour:

1. identical RGB already present in the free range → reuse,
2. first unused entry → set colour, mark allocated,
3. otherwise the nearest colour by `(ΔR²·32) + (3·ΔG² + ΔB²)·17`.

Level loads clear the per-level bit first (`flags &= 0xfc`).

## 6. Sounds

Records 120–257 of `INIT.PH` are complete RIFF/WAVE files.  The table at
`0x46e130` (138 × `{char* name, u8 id}`) gives each one its *sound id* used by
the game logic (`FUN_0043cc10(id, priority)`), e.g. `SOUND01.WAV → 1`,
`SOUNDF0.WAV → 0xf0`.  Music and ambience were CD-audio tracks (MCI
`cdaudio`); the per-level track numbers are in `src/engine/audio.cpp`.

## 7. Frame buffer and presentation

`PITFALL.EXE` renders into a 448×256 top-down 8-bpp DIB section.  The visible
320×224 window is at `(scrollX & 7, 9)`; the tile pass draws whole 8×8 tiles
(41 × 29) starting at `row 9 - (scrollY & 7)` and the blit applies the fine
horizontal scroll.  `BitBlt` for the 1× window, `StretchBlt` for 640×448 and
full screen (`FUN_00443b90`).

## 8. Level tables: `.det`, `.dt2`, `.als`, `.trg`

All four are *column lists* in the big-endian layout of the Genesis
original (the game logic is translated 68000 code and reads them as-is):

```
u16be offset[columns]        one per 16-pixel column, columns = w_tiles / 2
                             (the files carry 8 extra padding columns); 0 = empty
lists: (u8 row, u8 id) ...   sorted by 16-pixel row, terminated by row 0xff
.trg only: (0xfe, rowFrom, rowTo, id)   a vertical range entry
```

`.det`, `.dt2` and `.als` have a 4-byte header before the offset table;
`.trg` starts directly with the offsets (`FUN_0042571f`, `FUN_00433d0b`
vs. `FUN_004331d8`, `FUN_004251f9`).

### Collision (`.det` main plane, `.dt2` second plane)

`id` selects one of 84 *shapes* in a 16-byte-per-shape height table in the
executable (`0x4668c4`, `ph::kCollisionHeights`).  For a probe at pixel
`(x, y)`: column `x >> 4`, row `y >> 4`, byte `h = heights[id][x & 15]`;
`h == 0` means no surface, otherwise the surface pixel row is
`row * 16 + h - 1`.  Shapes 1–16 are flat floors at every height, 22/23 are
45° slopes, 32–83 gentle slopes in both directions.  Trigger ids `0x10` /
`0x11` switch Harry between the two planes (`DAT_00465bb3`).

Harry's probes (`FUN_00425455`): centre of the feet at `(x - 16, y + 14)`,
front foot at `x` (facing right) or `x - 32` (facing left); `FUN_00425d6f`
also looks one row above and below to follow slopes.  His sprite is drawn
at `(x + 16, y + 16)` (`FUN_00434eef`); trigger lookups use `(x + 32, y)`.

### Objects (`.als`)

`id` indexes the 256-entry template table at `0x4678c4`
(`ph::kObjectTemplates`, `tools/object_templates.json`):

```
u8  type         behaviour class (per-type handler in the Genesis code)
u8  sub          initial frame, 0xff = shown by the handler
u16 flags
u32 sprite       pointer to the sprite slot (-> name)
u8  soundSlot    Genesis sound-channel class (FUN_004263a6 is a stub in the PC build: it only marks obj+0x26 = 0x6663)
i16 xOff, yOff   object x = col*16 + 0x18 + xOff, y = row*16 - 0x10 + yOff
i16 snap         != 0: snap to the floor probed at y + snap
```

`.trg` columns hold 2-byte `{row, id}` entries sorted by row, with `{0xfe, from, to, id}`
covering `from <= row < to`; the lookup column for a level x is `(x + 0x20) >> 4`
(`FUN_004251f9`), i.e. the table is offset two columns from `.det` / `.als`.  The class
table at `0x461f8e` (`ph::kTriggerClass`) sorts the ids: bit 0 = solid wall (Harry and
the enemies), `0xf2` = level-script triggers (start, exit, planes, water, ...).

Objects are instantiated when their cell comes within 64 px of the camera
(`FUN_00403fa0` and three siblings for the four edges) and released when
they leave; a picked-up or killed entry has its id zeroed in the working
copy of `.als` and is restored from the backup copy after a death unless
its state byte says it is gone for good (`FUN_00430a69`).

### Triggers (`.trg`)

Looked up at `(x + 0x20, y)` every tick.  Known ids: `0x5e` `^` start
position (`x = col*16 - 8`, `y = (row + 2) * 16`, facing from the table at
`0x433334`), `0x5f` `_` level complete (`"*4"` → map screen), `0x10`/`0x11`
collision plane, `0x18` gravity flip section, `0x1d` sling swing anchor,
`0x89` boss exit (`"*50"`), `0x8a`–`0x8c` Loltun vault entrances
(`"*51"`–`"*53"`), `0xd0` water.  `kTriggerClass[id]` (`0x461f8e`) gives
each id a class byte the handlers test.

### Harry physics (16.16 fixed point, per 17 ms tick)

| quantity | value | source |
|----------|-------|--------|
| run acceleration | 0x3000 | `FUN_00428dfa` |
| run speed limit | 0x30000 (3 px), 0x40000 with the chili power-up | |
| deceleration | 0x4000 | `FUN_004300e0` |
| gravity | 0x5000, suspended up to 9 ticks while jump is held | |
| terminal velocity | 12 px/tick | |
| jump impulse | −0x4a000 (−0x70000 powered) | |
| run cycle | frames 8,9,0,1,2,3,4,5,6,7 (script at `0x46166b`) | `FUN_004237a5` |
| jump frame | by integer vy, table at `0x461480` | |

## 9. Object behaviour data

### Animation scripts (`FUN_00423134`)

Byte scripts in the data section drive the object animations
(`tools/gen_anim_scripts.py` extracts the ones used into `src/game/anim_scripts_data.cpp`):

| byte | meaning |
|------|---------|
| `< 0x80` | show this frame (a frame lasts *ticks + 1* calls) |
| `f0` | end: restart from the top and report "looped" |
| `fc id pri` / `fd id pri` | play sound `id` (`ee` / `ef`: only when Harry is within 128 px) |
| `fb id pri m a p` | sound `id` with a random pitch (`ed`: near Harry only) |
| `fe ev` | fire level-script event `ev` |
| `f7` / `f9` | toggle the object's facing / clear its vertical flip |
| other `>= 0x80` | shown as the frame number, i.e. `ff` hides the sprite |

Harry and a few objects (monkey throw, condor dive) use the second interpreter
`FUN_004237a5`, whose scripts carry 32-bit pointers: `f3 addr` jumps, `f1 slot`
switches the sprite.  The generator relocates those to offsets / name indices.

### Sprite collision boxes (`0x468824`)

`{u32 sprite_slot; i16 dx, w, dy, h}` records terminated by `-1`; the loader
stores each sprite's record offset in the sprite header word at +8 and
`FUN_00424af1` tests two objects by overlapping `(x + dx, y + dy, w, h)`.
Harry's box is mirrored when he faces left.  `ph::kHitboxes` / `ph::hitbox()`.

### Whip boxes (`0x4612e0`)

Per Harry whip sprite: `{frame, x0, y0, x1, y1}` records (`-1` ends a sprite,
`-2` the table).  Standing whip `hyiwhip`: frame 2 reaches x 6..76, y −61..−31;
frame 3 x 7..45, y −39..−22 (mirrored when facing left).  A hit adds 10 (9 on
the harder setting) to the target's hit counter; the sling stone adds 0x15.

### Pickup effects (`0x45a8d4`)

`{sprite_slot, frame or 0xff, routine}`: see `kPickupEffects` in
`src/game/play_logic.h` (scores, treasure counts, ammo, chili 0x380 ticks, full
heal, P-I-T-F-A-L-L letter bits).  Fifty treasures award a continue
(`0x465ae3`), the lives are `0x466527` and an `mi_1up` adds one after flying to
the HUD.

### Enemy tables

* hit points are the `d5` argument of each handler's `0x424171` call (snake 10,
  monkey 10, condor 20, fly 9, bat 9, rat 9, spider 9, scorpion 2);
* contact damage is the `d5` of the `0x407aff` call (snake 10, monkey 10,
  condor 15, fly 12, bat 12, rat 5, spider 12, scorpion 30, cactus 14);
* `0x461aca` score per type, `0x45b6c3` class bits (2 = stays dead after a
  death), `0x45b814` death effect kind (1 debris puff, 2 `fx_deat2`, 3
  `fxzdeath` with the body under the cloud).

## 10. The load block

WinMain takes 1.5 MB with `GlobalAlloc(2, 0x180000)` (`DAT_004504dc`) and every record the game
loads goes into it, one after the other, as `ph::Arena` keeps it:

```
INIT.PH (FUN_004457d0)      font, scoline (raw images), the 118 sprites: each record up to its
                            palette, where the next one goes
DAT_004504e0                the next 32-byte boundary: each level from here (FUN_004460e0)
  background                as read, its tiles in the palette's colours
  .trg, its copy            DAT_00450510, DAT_0045052c = .trg + its size
  .det                      DAT_00450fc0 (each record at the last one's end rounded up to 16
  .dt2                      DAT_00450518  bytes; a record that is not there leaves the last
  .als area                 DAT_004504e4  size in force), 0x1000 bytes cleared: the working
                            copy, the level's own copy at +0x800 (DAT_004504c4)
  parallax map              DAT_00450f90: past the .als area at the next 32 bytes (32 on from a
                            boundary), its tiles in the palette's colours
  extra map                 levels 15 / 22 (DAT_004504f0)
  expanded parallax         levels 0 / 5: w*8 x h*8 bytes
  sprites (FUN_00447190)    each up to its palette, +4 its name's address, +8 FUN_004361ee's
                            word for its slot (4 + 12 * its entry at 0x468824, or 0xffff)
```

What a load does not write keeps the last loads' bytes.  The game logic reads past its tables
into it: the collision probes (`FUN_0042571f` and its siblings) take the column word at
`.det + ((x >> 3) & 0xfffe) + 4` in 16 bits, so at a negative x they read 8 KB past the table's
start (on level 1 the parallax map, whose bytes then make a "floor" that breaks sling stones
thrown off the left edge), and follow the word as a signed offset wherever it points; the
trigger lookup reads past its offsets the same way for an x beyond the level.  The PC also keeps
Genesis RAM inside the trigger record: the jaguar's pounce distance at `+0x1ea`, the boss
pointer (big-endian) at `+0x1f4`, the vaults' ceiling, timers and Harry's squash script from
`+0x1f4` to `+0x3f3` — over the trigger lists of most levels, past the short records of the vaults
in the `.als` area's unused part — and a lost life copies the record back from its copy
(`FUN_00430eb6`).  The tools generate the sprites' header words from `PITFALL.EXE`
(`tools/gen_arena_data.py`).

