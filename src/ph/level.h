// Level data records (LEVELxx.PH) and the INIT.PH asset bank.
//
// Background record body (FUN_004460e0, renderer FUN_00436fb4):
//   u32 w_tiles ; u32 h_tiles ; u32 tiles_off ; u32 pal_off ; u32 ncolors-1
//   u16 map[w*h]        bits 0..11 tile index, 0x1000 opaque tile (fast copy),
//                       0x2000 vertical flip, 0x4000 horizontal flip,
//                       0x8000 high priority (drawn after sprites)
//   u8  tiles[n][64]    8x8 pixels, local palette indices, 0 = transparent
//   rgb24 palette[ncolors-1]   local index i (>=1) -> palette[i-1]
#pragma once
#include "ph/manifests.h"
#include "ph/palette.h"
#include "ph/sprite.h"
#include "ph/tables.h"

#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ph {

struct Background {
    std::string name;
    int wTiles = 0, hTiles = 0;
    std::vector<uint16_t> tilemap;      // wTiles * hTiles
    std::vector<uint8_t> tiles;         // 64 bytes per tile (already remapped to global palette)
    int tileCount() const { return int(tiles.size() / 64); }
    int colourCount = 0;
    std::vector<uint8_t> localPalette;  // rgb24 * (colourCount-1)

    static Background parse(const std::string& name, std::span<const uint8_t> body);
    // Allocate colours into `pal` and remap tile pixels (local 0 stays 0).
    void bindPalette(Palette& pal);
};

// Levels 0 and 5 pre-expand their parallax tilemap into a linear 8bpp bitmap
// (FUN_004460e0 tail) because their parallax renderer does per-line scrolling.
struct LinearBitmap {
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;
    static LinearBitmap expand(const Background& bg);
};

struct LevelData {
    int level = -1;
    Background bg;                       // main tile layer (may be absent: wTiles == 0)
    bool hasBg = false;
    std::vector<uint8_t> trg, det, dt2, als;   // raw game-logic tables (Genesis big-endian data)
    ColumnTable collision;               // .det  - main collision layer
    ColumnTable collision2;              // .dt2  - secondary collision layer (DAT_00465bb3 selects it)
    ColumnTable objects;                 // .als  - object / enemy placement
    ColumnTable triggers;                // .trg  - triggers (start position, scripts)
    int columns = 0;                     // level width in 16-pixel columns (DAT_004658ac)
    Background parallax;                 // parallax tile layer
    bool hasParallax = false;
    LinearBitmap parallaxLinear;         // only for levels 0 / 5
    Background extra;                    // levels 15 / 22
    bool hasExtra = false;
    std::vector<std::unique_ptr<Sprite>> sprites;   // per-level sprites, manifest order
    std::map<std::string, Sprite*> spriteByName;

    Sprite* sprite(const std::string& name) const {
        auto it = spriteByName.find(name);
        return it == spriteByName.end() ? nullptr : it->second;
    }
};

// Loads "LEVEL%02d.PH" exactly the way FUN_004460e0 does.
LevelData loadLevel(int level, const std::string& dataDir, Palette& pal);

// INIT.PH: global font, score digits, Harry + common sprites, sound effects.
struct SoundSample {
    std::string name;
    int id = 0;
    std::vector<uint8_t> riff;           // complete RIFF/WAVE file image
};

struct AssetBank {
    RawImage font;                       // 39 cells of 8x8 (A-Z, 0-9, punctuation)
    RawImage scoline;                    // score digits 8x16
    std::vector<std::unique_ptr<Sprite>> sprites;
    std::map<std::string, Sprite*> spriteByName;
    std::vector<SoundSample> sounds;     // 138 samples, by manifest order
    std::map<int, const SoundSample*> soundById;

    Sprite* sprite(const std::string& name) const {
        auto it = spriteByName.find(name);
        return it == spriteByName.end() ? nullptr : it->second;
    }
    const SoundSample* sound(int id) const {
        auto it = soundById.find(id);
        return it == soundById.end() ? nullptr : it->second;
    }
};

// Loads INIT.PH the way FUN_004457d0 + FUN_0043ce10 do.
AssetBank loadInit(const std::string& dataDir, Palette& pal);

} // namespace ph
