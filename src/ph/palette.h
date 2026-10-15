// Global 256-colour palette manager.
//
// Faithful port of the allocator in PITFALL.EXE (FUN_004451a0) and the
// bookkeeping around it (FUN_0043e1c0, FUN_004457d0, FUN_004460e0).
//
// Every sprite / background record carries its own small RGB palette.  When a
// record is loaded its colours are *allocated* into the shared 256-entry
// hardware palette and the record's pixel indices are remapped in place.  The
// allocator tries, in order:
//   1. an identical colour already present in the free range,
//   2. an unused entry in the free range,
//   3. the perceptually nearest colour already present (weighted distance).
#pragma once
#include <array>
#include <cstdint>

namespace ph {

struct Rgb { uint8_t r = 0, g = 0, b = 0; };

class Palette {
public:
    // Entry flags (original global byte array at 0x453080).
    enum : uint8_t {
        kLevelAlloc  = 0x01,   // allocated by the current level's records
        kLevelLock   = 0x02,   // treated as "not reusable" by the exact/nearest match tests
        kInitAlloc   = 0x04,   // allocated by INIT.PH (persistent across levels)
        kInitLock    = 0x08,
        kSystem      = 0x80,   // outside the free range (Windows static colours)
    };

    Palette();

    // The free range is [firstFree, endFree).  Windows reserved 10 static
    // colours at each end of the system palette, so the original game used
    // [10, 246).  The visible result is identical with any range; we keep
    // the original so palette indices match the original binary.
    void setFreeRange(int first, int end);
    int firstFree() const { return first_; }
    int endFree() const { return end_; }

    const std::array<Rgb, 256>& colours() const { return rgb_; }
    const Rgb& colour(int i) const { return rgb_[i & 0xff]; }
    void setColour(int i, Rgb c);                 // FUN_0043ebc0 (the colour and what is shown)
    // What the screen shows: the LOGPALETTE / DIB colour table (DAT_0046d41c / DAT_00452c58).
    // setColour writes it with the colour (DAT_0046d824); the fades write it alone.
    const Rgb& shown(int i) const { return shown_[i & 0xff]; }
    void show(int i, Rgb c) { shown_[i & 0xff] = c; dirty_ = true; }
    void showAll() { shown_ = rgb_; dirty_ = true; }   // FUN_0043ed00: every entry set again
    uint8_t flags(int i) const { return flags_[i & 0xff]; }

    // Remap table built by the last allocate() call (original DAT_004503c0); what a call does not
    // set stays as the calls before left it.
    const std::array<uint8_t, 256>& remap() const { return remap_; }
    void setRemap(int local, uint8_t global) { remap_[local & 0xff] = global; }

    // Allocate `count` colours starting at local index `firstLocal`.
    //   rgb      -> packed r,g,b triplets, one per local colour
    //   count    -> number of local colours (table is indexed [firstLocal, count) exactly
    //               like the original loop: `while (param_3 != param_2)`)
    // Sprites call allocate(pal, n, 0); backgrounds call allocate(pal, n+1, 1) so
    // that local index 0 stays mapped to global 0 (transparent/black).
    void allocate(const uint8_t* rgb, int count, int firstLocal);

    // FUN_004457d0 tail: promote INIT.PH allocations to persistent.
    void commitInitAllocations();
    // FUN_004460e0: forget the previous level's allocations.
    void releaseLevelAllocations();

    // Dirty flag for the presentation layer (set by setColour, cleared by caller).
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

private:
    std::array<Rgb, 256> rgb_{};
    std::array<Rgb, 256> shown_{};
    std::array<uint8_t, 256> flags_{};
    std::array<uint8_t, 256> remap_{};
    int first_ = 10, end_ = 246;
    bool dirty_ = true;
};

} // namespace ph
