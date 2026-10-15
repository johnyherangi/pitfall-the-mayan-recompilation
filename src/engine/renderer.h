// Software renderer: tile layers, parallax, RLE sprites, font cells.
// Each routine is a port of the corresponding PITFALL.EXE function (noted inline).
#pragma once
#include "engine/framebuffer.h"
#include "ph/level.h"
#include "ph/sprite.h"

#include <cstdint>
#include <vector>

namespace engine {

// Camera for one frame (copies of DAT_0046fba0 / DAT_0046fba4 and derived limits).
struct View {
    int scrollX = 0, scrollY = 0;
    int right() const { return scrollX + kViewW; }    // DAT_00450530
    int bottom() const { return scrollY + kViewH; }   // DAT_00450f9c
};

// One entry of the sprite display list (original 20-byte entries at 0x46aa94).
struct SpriteInstance {
    int32_t x = 0, y = 0;             // world position
    const ph::Sprite* sprite = nullptr;
    uint32_t frame = 0;               // wraps modulo frameCount (original loop)
    uint32_t flags = 0;               // see below
};
enum SpriteFlags : uint32_t {
    kSprVisible  = 0x01,
    kSprFront    = 0x20,   // drawn after the high-priority tile pass (FUN_004426e0 skips these)
    kSprHFlip    = 0x40,   // FUN_00434eef: object facing byte (+0xb) -> 0x40
    kSprVFlip    = 0x80,   // object byte +0x29 -> 0x80
};

// High-priority tiles found during the tile pass; drawn after sprites (0x46b25c list).
struct DeferredTile {
    uint8_t* dst;       // destination inside the framebuffer
    uint16_t entry;     // tilemap entry
};

class Renderer {
public:
    explicit Renderer(Framebuffer& fb) : fb_(fb) {}

    // FUN_00436fb4: draws 41x29 tiles of `bg` at the given scroll (clamped to the map).
    // High-priority tiles are appended to `deferred` instead of being drawn.
    void drawTileLayer(const ph::Background& bg, View& view, std::vector<DeferredTile>& deferred);
    // FUN_00436b2c list drain: draw the deferred tiles over the sprites.
    void drawDeferredTiles(const ph::Background& bg, const std::vector<DeferredTile>& deferred);
    // 0x4371a5 / FUN_0043731c: 41x29 tiles of `bg` from tile (x >> 3, y >> 3), wrapping both
    // ways, drawn (y & 7) px up and `shift` px left of the view origin (mainFineX: the main
    // layer's fine x the presentation applies).  With `deferred`, the high-priority entries
    // (bit 15) are kept for drawDeferredTiles instead (FUN_0043731c).
    void drawWrappedLayer(const ph::Background& bg, int x, int y, int shift, int mainFineX,
                          std::vector<DeferredTile>* deferred);

    // FUN_00437128: `rows` x `cols` tiles of `bg` from (col, row), drawn from the left edge of the
    // buffer at buffer row `bufRow` (the 2600 level's screen), every tile as it is (no list of
    // high-priority tiles).
    void drawTileStrip(const ph::Background& bg, int col, int row, int cols, int rows, int bufRow);
    // `rows` lines of the buffer from `bufRow`, 320 px wide, to colour 0.
    void clearRows(int bufRow, int rows);

    // FUN_00436534 (levels 0/5 branch): line-scrolled, horizontally wrapping bitmap.
    // lineOffsets (optional) holds one signed byte per screen line (DAT_00450fd0).
    void drawParallaxLinear(const ph::LinearBitmap& bmp, int scrollX, int scrollY,
                            const int8_t* lineOffsets, int lineCount);
    // Tile-based parallax layer with horizontal wrap (other levels).
    // mainFineX = main layer scrollX & 7 (the fine scroll applied at blit time).
    void drawParallaxTiles(const ph::Background& bg, int scrollX, int scrollY, int mainFineX);

    // FUN_00438140: RLE sprite blit with the original flip/clip semantics.
    void drawSprite(const SpriteInstance& s, const View& view);
    // FUN_00438140's "hatman" cheat (DAT_0046d058): 1 draws Harry's sprites (names "hy..") as
    // `hat`, 0x12 px higher; 2 draws every other sprite but the items ("mi..") and the pouches and
    // pits ("ob_p..", "obrp..") as `hat`; the frame is the sprite's & 3, 3 taken as 1.
    void setHatman(int mode, const ph::Sprite* hat) { hatMode_ = mode; hat_ = hat; }
    // Whether the hatman cheat draws Harry's sprite `s` 0x12 px higher (mode 1).
    bool hatmanLifts(const ph::Sprite* s) const;

    // FUN_004387c0: one cell of a raw image at view-space (x, y); colour 0 is transparent.
    void drawCell(const ph::RawImage& img, int cell, int x, int y, int scrollX);
    // FUN_00442600: text in the 8x8 font (A-Z, 0-9, ',', '!', '.').
    void drawText(const ph::RawImage& font, int x, int y, const char* text, int scrollX);

    // Fill the view with colour 0 (level 22 / blank backgrounds).
    void clearView(int scrollX);

    // FUN_00436534 at 0x4366ca (levels 1, 6, 18): the waterfalls' parallax in 21 columns of 16 px
    // from the view's origin less (parX & 0xf).  cols[0] is the map's first tile column, cols[1 ..]
    // each column's height in the map (0 .. 511; the map is 64 tiles high and wraps), drawn two
    // opaque tiles wide (FUN_00436701 / FUN_00436998: h-flip only), the map's column moving on two
    // tiles a drawn column; -1 clears the column (16 px of colour 0 down the view).
    void drawWaterfall(const ph::Background& bg, const int32_t* cols, size_t n, int parX, int scrollX);
    // FUN_00437ba0: line `row` of the view turned round by `by` px (right when positive).
    void rotateRow(int row, int by, int scrollX);
    // FUN_00438140 on levels 18 and 6 (not for the HUD, DAT_0046d040): a sprite whose x is in
    // 0x3c0 .. 0x64f or 0x870 .. 0x8e7 (level 18), 0x948 .. 0xb07 or 0xbb8 .. 0xc2f (level 6) is
    // cut off at y 0x3b8 / 0x1b8, under the water.  0: none.
    void setWaterClip(int level) { waterClip_ = level; }

private:
    void drawTile(uint8_t* dst, const uint8_t* tile, uint16_t entry);
    Framebuffer& fb_;
    int hatMode_ = 0;
    const ph::Sprite* hat_ = nullptr;
    int waterClip_ = 0;
};

} // namespace engine
