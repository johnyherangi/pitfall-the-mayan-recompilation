#include "engine/renderer.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>

namespace engine {

namespace {
constexpr int kTileCols = 41;   // 0x29  (320/8 + 1)
constexpr int kTileRows = 29;   // 0x1d  (224/8 + 1)
}

// ---------------------------------------------------------------- tiles ----

void Renderer::drawTile(uint8_t* dst, const uint8_t* tile, uint16_t entry) {
    const bool opaque = (entry & 0x1000) != 0;
    const bool hflip = (entry & 0x4000) != 0;
    const bool vflip = (entry & 0x2000) != 0;
    if (opaque && !hflip && !vflip) {                      // fast path in FUN_00436fb4
        for (int y = 0; y < 8; ++y) std::memcpy(dst + y * kBufW, tile + y * 8, 8);
        return;
    }
    for (int y = 0; y < 8; ++y) {
        const uint8_t* src = tile + (vflip ? (7 - y) : y) * 8;
        uint8_t* d = dst + y * kBufW;
        for (int x = 0; x < 8; ++x) {
            const uint8_t v = src[hflip ? (7 - x) : x];
            if (opaque || v != 0) d[x] = v;
        }
    }
}

void Renderer::drawTileLayer(const ph::Background& bg, View& view, std::vector<DeferredTile>& deferred) {
    if (bg.wTiles == 0 || bg.hTiles == 0) { clearView(view.scrollX); return; }
    // FUN_00436fb4: clamp the camera to the map, then draw 41x29 whole tiles.
    const int maxX = bg.wTiles * 8 - kViewW, maxY = bg.hTiles * 8 - kViewH;
    const int sx = std::clamp(view.scrollX, 0, std::max(0, maxX));
    const int sy = std::clamp(view.scrollY, 0, std::max(0, maxY));
    uint8_t* dstRow = fb_.px.data() + kViewBaseOffset - (sy & 7) * kBufW;
    int tx0 = sx >> 3, ty0 = sy >> 3;
    const int cols = std::min(kTileCols, bg.wTiles - tx0);
    const int rows = std::min(kTileRows, bg.hTiles - ty0);
    for (int r = 0; r < rows; ++r) {
        const uint16_t* src = &bg.tilemap[size_t(ty0 + r) * bg.wTiles + tx0];
        uint8_t* dst = dstRow;
        for (int c = 0; c < cols; ++c, ++src, dst += 8) {
            const uint16_t e = *src;
            if (e & 0x8000) { deferred.push_back(DeferredTile{dst, e}); continue; }
            const int t = e & 0xfff;
            if (t < bg.tileCount()) drawTile(dst, &bg.tiles[size_t(t) * 64], e);
        }
        dstRow += kBufW * 8;
    }
}

void Renderer::drawDeferredTiles(const ph::Background& bg, const std::vector<DeferredTile>& deferred) {
    for (const DeferredTile& d : deferred) {
        const int t = d.entry & 0xfff;
        if (t < bg.tileCount()) drawTile(d.dst, &bg.tiles[size_t(t) * 64], d.entry);
    }
}

void Renderer::drawWrappedLayer(const ph::Background& bg, int x, int y, int shift, int mainFineX,
                                std::vector<DeferredTile>* deferred) {
    if (bg.wTiles == 0 || bg.hTiles == 0) return;
    int tx0 = (x >> 3) % bg.wTiles; if (tx0 < 0) tx0 += bg.wTiles;
    int ty0 = (y >> 3) % bg.hTiles; if (ty0 < 0) ty0 += bg.hTiles;
    uint8_t* dstRow = fb_.px.data() + kViewBaseOffset - (y & 7) * kBufW + mainFineX - shift;
    for (int r = 0; r < kTileRows; ++r) {
        const uint16_t* row = &bg.tilemap[size_t((ty0 + r) % bg.hTiles) * bg.wTiles];
        uint8_t* dst = dstRow;
        for (int c = 0; c < kTileCols; ++c, dst += 8) {
            const uint16_t e = row[(tx0 + c) % bg.wTiles];
            if (deferred && (e & 0x8000)) { deferred->push_back(DeferredTile{dst, e}); continue; }
            const int t = e & 0xfff;
            if (t < bg.tileCount()) drawTile(dst, &bg.tiles[size_t(t) * 64], e);
        }
        dstRow += kBufW * 8;
    }
}

void Renderer::clearView(int scrollX) {
    uint8_t* dst = fb_.viewBase(scrollX);
    for (int y = 0; y < kViewH; ++y) std::memset(dst + size_t(y) * kBufW, 0, kViewW);
}

// ------------------------------------------------------------- parallax ----

void Renderer::drawParallaxLinear(const ph::LinearBitmap& bmp, int scrollX, int scrollY,
                                  const int8_t* lineOffsets, int lineCount) {
    if (bmp.w <= 0 || bmp.h <= 0) return;
    const int W = bmp.w;
    int lines = kViewH;
    if (scrollY + kViewH > bmp.h) lines = kViewH - (scrollY + kViewH - bmp.h);
    uint8_t* dst = fb_.viewBase(scrollX);
    for (int i = 0; i < lines; ++i, dst += kBufW) {
        const int srcRow = scrollY + i;
        if (srcRow < 0 || srcRow >= bmp.h) continue;
        const uint8_t* src = &bmp.pixels[size_t(srcRow) * W];
        int m = scrollX + ((lineOffsets && i < lineCount) ? lineOffsets[i] : 0);
        m %= W; if (m < 0) m += W;
        if (m + kViewW <= W) {
            std::memcpy(dst, src + m, kViewW);
        } else {                                  // wrap around the right edge
            const int first = W - m;
            std::memcpy(dst, src + m, size_t(first));
            std::memcpy(dst + first, src, size_t(kViewW - first));
        }
    }
}

void Renderer::drawParallaxTiles(const ph::Background& bg, int scrollX, int scrollY, int mainFineX) {
    if (bg.wTiles == 0 || bg.hTiles == 0) return;
    const int maxY = std::max(0, bg.hTiles * 8 - kViewH);
    const int sy = std::clamp(scrollY, 0, maxY);
    const int W = bg.wTiles * 8;
    int sx = scrollX % W; if (sx < 0) sx += W;
    // The presentation blit applies the *main* layer's fine scroll (mainFineX),
    // so the parallax grid is drawn shifted by (mainFineX - its own fine scroll).
    // A negative shift lands in the previous row's slack, which is harmless.
    uint8_t* dstRow = fb_.px.data() + kViewBaseOffset - (sy & 7) * kBufW + mainFineX - (sx & 7);
    const int ty0 = sy >> 3;
    const int rows = std::min(kTileRows, bg.hTiles - ty0);
    for (int r = 0; r < rows; ++r) {
        uint8_t* dst = dstRow;
        for (int c = 0; c < kTileCols + 1; ++c, dst += 8) {
            const int tx = ((sx >> 3) + c) % bg.wTiles;
            const uint16_t e = bg.tilemap[size_t(ty0 + r) * bg.wTiles + tx];
            const int t = e & 0xfff;
            if (t < bg.tileCount()) drawTile(dst, &bg.tiles[size_t(t) * 64], uint16_t(e | 0x1000));
        }
        dstRow += kBufW * 8;
    }
}

// -------------------------------------------------------------- sprites ----

void Renderer::drawSprite(const SpriteInstance& s, const View& view) {
    // Port of FUN_00438140.
    if (!(s.flags & kSprVisible) || !s.sprite || s.sprite->frameCount() == 0) return;
    const ph::Sprite& spr = *s.sprite;
    uint32_t frameIdx = s.frame;
    while (frameIdx >= uint32_t(spr.frameCount())) frameIdx -= uint32_t(spr.frameCount());
    const ph::SpriteFrame& f = spr.frame(int(frameIdx));
    // Flip bits as the original blitter tests them: 0x40 = mirror horizontally
    // (rows still run downwards), 0x80 = rows run upwards, 0xc0 = both.
    const bool hflip = (s.flags & kSprHFlip) != 0, vflip = (s.flags & kSprVFlip) != 0;
    const int scrollX = view.scrollX, scrollY = view.scrollY, right = view.right(), bottom = view.bottom();

    // Horizontal placement / whole-sprite rejection.
    int ax;
    if (!hflip) {
        ax = s.x + f.hotX;
        if (ax >= right || ax + f.w < scrollX) return;
    } else {
        ax = s.x - f.hotX;
        if (ax < scrollX || ax - f.w >= right) return;
    }

    // Vertical placement, row clipping.
    int startRow, rows = f.h, skipRows = 0;
    if (!vflip) {
        const int ay = s.y + f.hotY;
        if (ay >= bottom) return;
        const int end = ay + f.h;
        if (end < scrollY) return;
        if (end <= bottom) {
            const int d = ay - scrollY;
            if (d < 0) { skipRows = -d; startRow = 0; } else startRow = d;
        } else {
            rows = f.h - (end - bottom);
            const int d = ay - scrollY;
            if (d < 0) return;                      // taller than the view: original gives up
            startRow = d;
        }
    } else {
        const int ay = s.y - f.hotY;                // first RLE row lands here, rows go upward
        if (ay < scrollY) return;
        const int end = ay - f.h;
        if (end >= bottom) return;
        if (end < scrollY) {
            rows = f.h + (end - scrollY) + 1;
            startRow = ay - scrollY;
        } else {
            const int d = ay - bottom;
            if (ay < bottom || d == 0) startRow = ay - scrollY;
            else { skipRows = d; startRow = kViewH - 1; }
        }
    }
    if (rows <= 0) return;

    const uint8_t* p = spr.rle(int(frameIdx));
    const uint8_t* pend = spr.bytes().data() + spr.bytes().size();
    // Skip rows that are off-screen (the original walks the RLE stream).
    while (skipRows > 0 && p < pend) {
        const uint8_t b = *p++;
        if (b == ph::kRleEndFrame) return;
        if (b == ph::kRleEndRow) { --skipRows; continue; }
        if (b >= 0x80) p += b - 0x7f;
    }

    uint8_t* const base = fb_.viewBase(scrollX);
    const int xdir = hflip ? -1 : 1;
    const int ystep = vflip ? -kBufW : kBufW;
    uint8_t* rowStart = base + startRow * kBufW + (ax - scrollX);
    uint8_t* dst = rowStart;
    uint8_t* const bufBegin = fb_.px.data();
    uint8_t* const bufEnd = bufBegin + fb_.px.size();
    for (;;) {
        if (p >= pend) break;
        const uint8_t b = *p++;
        if (b >= 0x80) {
            int n = b - 0x7f;
            while (n-- && p < pend) {
                if (dst >= bufBegin && dst < bufEnd) *dst = *p;   // safety net; original had none
                ++p; dst += xdir;
            }
        } else if (b <= 0x7d) {
            dst += xdir * int(b);
        } else {
            if (b == ph::kRleEndFrame) break;
            if (--rows == 0) break;
            rowStart += ystep;
            dst = rowStart;
        }
    }
}

// ------------------------------------------------------------ raw cells ----

void Renderer::drawCell(const ph::RawImage& img, int cell, int x, int y, int scrollX) {
    // Port of FUN_004387c0 (view-space coordinates, vertical clip to the view).
    if (cell < 0 || cell >= img.cellCount()) return;
    const int cw = img.cellW();
    int ch = img.cellH();
    const uint8_t* src = img.cell(cell);
    if (y + ch <= 0) return;
    if (y < 0) { src += size_t(-y) * cw; ch += y; y = 0; }
    if (y >= kViewH) return;
    if (y + ch > kViewH) ch = kViewH - y;
    uint8_t* dst = fb_.viewBase(scrollX) + size_t(y) * kBufW + x;
    for (int yy = 0; yy < ch; ++yy, dst += kBufW, src += cw)
        for (int xx = 0; xx < cw; ++xx)
            if (src[xx]) dst[xx] = src[xx];
}

void Renderer::drawText(const ph::RawImage& font, int x, int y, const char* text, int scrollX) {
    // Port of FUN_00442600: glyphs 0-25 letters, 26-35 digits, 36 ',', 37 '!', 38 '.'
    for (; *text; ++text, x += 8) {
        const int c = std::toupper(static_cast<unsigned char>(*text));
        int g = -1;
        if (c >= 'A' && c <= 'Z') g = c - 'A';
        else if (c >= '0' && c <= '9') g = c - 0x16;
        else if (c == ',') g = 0x24;
        else if (c == '!') g = 0x25;
        else if (c == '.') g = 0x26;
        if (g >= 0) drawCell(font, g, x, y, scrollX);
    }
}

} // namespace engine
