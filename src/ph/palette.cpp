#include "ph/palette.h"

namespace ph {

Palette::Palette() { setFreeRange(10, 246); }

void Palette::setFreeRange(int first, int end) {
    first_ = first; end_ = end;
    // FUN_004457d0: everything outside the free range is marked as system (0x80).
    for (int i = 0; i < 256; ++i) flags_[i] = (i < first_ || i >= end_) ? kSystem : 0;
    // FUN_0043e1c0: free entries start out black.
    for (int i = first_; i < end_; ++i) rgb_[i] = Rgb{0, 0, 0};
    dirty_ = true;
}

void Palette::setColour(int i, Rgb c) {
    rgb_[i & 0xff] = c;
    dirty_ = true;
}

void Palette::allocate(const uint8_t* rgb, int count, int local) {
    // Port of FUN_004451a0.  `rgb` points at the triplet for `local`.
    for (; local != count; ++local, rgb += 3) {
        const uint8_t r = rgb[0], g = rgb[1], b = rgb[2];
        remap_[local] = 0xff;

        // 1. identical colour already allocated (and not locked)
        for (int i = first_; i < end_; ++i) {
            if (rgb_[i].r == r && rgb_[i].g == g && rgb_[i].b == b &&
                (flags_[i] & (kSystem | kInitAlloc | kLevelAlloc)) != 0 &&
                (flags_[i] & (kLevelLock | kInitLock)) == 0) {
                remap_[local] = uint8_t(i);
                break;
            }
        }
        if (remap_[local] != 0xff) continue;

        // 2. first free entry
        for (int i = first_; i < end_; ++i) {
            if ((flags_[i] & (kSystem | kInitAlloc | kLevelAlloc)) == 0) {
                setColour(i, Rgb{r, g, b});
                flags_[i] |= kLevelAlloc;
                remap_[local] = uint8_t(i);
                break;
            }
        }
        if (remap_[local] != 0xff) continue;

        // 3. nearest colour (weights from the original: R*0x20, (3*G + B)*0x11)
        uint32_t best = 0xfd02ff;
        int bestIdx = first_;
        for (int i = first_; i < end_; ++i) {
            const int dr = int(r) - rgb_[i].r, dg = int(g) - rgb_[i].g, db = int(b) - rgb_[i].b;
            const uint32_t dist = uint32_t((dg * dg * 3 + db * db) * 0x11 + dr * dr * 0x20);
            if (dist < best && (flags_[i] & (kLevelLock | kInitLock)) == 0) {
                best = dist; bestIdx = i;
            }
        }
        remap_[local] = uint8_t(bestIdx);
    }
}

void Palette::commitInitAllocations() {
    // FUN_004457d0 tail: bits 0-1 -> bits 2-3, keep 0x80.
    for (auto& f : flags_) {
        const uint8_t low = f & 3;
        if (low) f = uint8_t((f & kSystem) | (low << 2));
    }
}

void Palette::releaseLevelAllocations() {
    for (auto& f : flags_) f &= 0xfc;      // FUN_004460e0
}

} // namespace ph
