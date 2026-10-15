// The software back buffer.
//
// PITFALL.EXE renders into a 448x256 top-down 8bpp DIB section
// (BITMAPINFOHEADER at 0x452c30: biWidth=0x1c0, biHeight=-0x100, 8 bits).
// The visible 320x224 window sits at (scrollX & 7, 9) inside it and is copied
// to the screen with BitBlt/StretchBlt (FUN_00443b90).  The slack around the
// view lets the tile pass draw whole 8x8 tiles and lets sprites overhang the
// left/right edges without per-pixel clipping, exactly as the original did.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace engine {

constexpr int kBufW = 448;          // 0x1c0
constexpr int kBufH = 256;
constexpr int kViewW = 320;         // 0x140
constexpr int kViewH = 224;         // 0xe0
constexpr int kViewTop = 9;         // first visible row (0xfc0 / 0x1c0 == 9)
constexpr int kViewBaseOffset = kViewTop * kBufW;   // 0xfc0

struct Framebuffer {
    std::array<uint8_t, size_t(kBufW) * kBufH> px{};

    // Pointer to view-space (0,0) for a given horizontal scroll (DAT_004504f4).
    uint8_t* viewBase(int scrollX) { return px.data() + kViewBaseOffset + (scrollX & 7); }
    const uint8_t* viewBase(int scrollX) const { return px.data() + kViewBaseOffset + (scrollX & 7); }
    uint8_t* row(int y) { return px.data() + size_t(y) * kBufW; }

    void clear(uint8_t v = 0) { std::memset(px.data(), v, px.size()); }

    // Copy the visible window out as a tightly packed 320x224 index bitmap.
    void copyView(int scrollX, uint8_t* dst) const {
        const uint8_t* src = viewBase(scrollX);
        for (int y = 0; y < kViewH; ++y) std::memcpy(dst + size_t(y) * kViewW, src + size_t(y) * kBufW, kViewW);
    }
};

} // namespace engine
