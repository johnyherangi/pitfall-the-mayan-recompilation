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

    // FUN_00436534 (levels 0/5 branch): line-scrolled, horizontally wrapping bitmap.
    // lineOffsets (optional) holds one signed byte per screen line (DAT_00450fd0).
    void drawParallaxLinear(const ph::LinearBitmap& bmp, int scrollX, int scrollY,
                            const int8_t* lineOffsets, int lineCount);
    // Tile-based parallax layer with horizontal wrap (other levels).
    // mainFineX = main layer scrollX & 7 (the fine scroll applied at blit time).
    void drawParallaxTiles(const ph::Background& bg, int scrollX, int scrollY, int mainFineX);

    // FUN_00438140: RLE sprite blit with the original flip/clip semantics.
    void drawSprite(const SpriteInstance& s, const View& view);

    // FUN_004387c0: one cell of a raw image at view-space (x, y); colour 0 is transparent.
    void drawCell(const ph::RawImage& img, int cell, int x, int y, int scrollX);
    // FUN_00442600: text in the 8x8 font (A-Z, 0-9, ',', '!', '.').
    void drawText(const ph::RawImage& font, int x, int y, const char* text, int scrollX);

    // Fill the view with colour 0 (level 22 / blank backgrounds).
    void clearView(int scrollX);

private:
    void drawTile(uint8_t* dst, const uint8_t* tile, uint16_t entry);
    Framebuffer& fb_;
};

} // namespace engine
