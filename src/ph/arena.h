// The load block: the 1.5 MB the original loads INIT.PH and each level into.
//
// WinMain takes it with GlobalAlloc(2, 0x180000) (DAT_004504dc) and FUN_004457d0 fills it with
// INIT.PH's records one after the other; FUN_004460e0 puts each level's from the next 32-byte
// boundary after them (DAT_004504e0): the background, the triggers and a copy of them, the two
// collision planes, 4 KB for the objects (the working copy, the level's own copy at +0x800), the
// parallax map from the next 32 bytes, level 15's or 22's extra map, levels 0 / 5's parallax
// expanded to a bitmap, and the sprites.  Each record stays as loaded: its tile or sprite pixels
// in the palette's colours, a sprite's +4 and +8 replaced (ArenaSprite), a raw image's or a
// sprite's palette overwritten by the next record.
//
// The game logic reads past its tables into whatever follows them (the collision probes at a
// negative x read 8 KB on, FUN_0042571f) and keeps Genesis RAM inside the triggers' record
// ([DAT_00450510] + 0x1ea / 0x1f4 ..): the port keeps the block as the original does, so a load
// leaves the bytes it does not write as the last loads left them.  (Where the block itself sits
// is the allocator's: it is taken as 32-byte aligned; nothing reads before or past it here.)
#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace ph {

// A sprite record's header as FUN_004453a0 leaves it: +4 the address of its name, +8 the word
// FUN_004361ee gives for its slot (4 + 12 * its entry in the box table at 0x468824, or 0xffff).
struct ArenaSprite { uint32_t name; uint16_t box; const char* text; };
std::span<const ArenaSprite> initArenaSprites();             // FUN_004457d0's order
std::span<const ArenaSprite> levelArenaSprites(int level);   // FUN_00447190's

// Where a level's records went (offsets in the block).
struct ArenaLayout {
    uint32_t base = 0;                 // DAT_004504e0 (the background, DAT_00450fc4)
    uint32_t trg = 0, trgSize = 0;     // DAT_00450510; the copy at trg + trgSize (DAT_0045052c)
    uint32_t det = 0;                  // DAT_00450fc0
    uint32_t dt2 = 0;                  // DAT_00450518
    uint32_t als = 0;                  // DAT_004504e4 (0x1000 bytes)
    uint32_t parallax = 0;             // DAT_00450f90 as the load first sets it
    uint32_t end = 0;                  // DAT_00450fa0 after the sprites
};

class Arena {
public:
    static constexpr uint32_t kSize = 0x180000;
    Arena() : mem_(kSize, 0) {}
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    uint8_t* data() { return mem_.data(); }
    const uint8_t* data() const { return mem_.data(); }
    static constexpr uint32_t size() { return kSize; }
    bool contains(int64_t off) const { return off >= 0 && off < int64_t(kSize); }
    uint8_t at(int64_t off) const { return contains(off) ? mem_[size_t(off)] : 0; }
    // the big-endian word the 68000 code reads
    uint16_t be16(int64_t off) const { return uint16_t(at(off) << 8 | at(off + 1)); }
    // copy into the block, clipped to it
    void put(int64_t off, const uint8_t* src, size_t n) {
        for (size_t i = 0; i < n; ++i) if (contains(off + int64_t(i))) mem_[size_t(off + int64_t(i))] = src[i];
    }
    void fill(int64_t off, uint8_t v, size_t n) {
        for (size_t i = 0; i < n; ++i) if (contains(off + int64_t(i))) mem_[size_t(off + int64_t(i))] = v;
    }

    uint32_t initEnd = 0;              // DAT_00450fa0 after INIT.PH
    uint32_t levelBase() const { return (initEnd & ~0x1fu) + 0x20; }   // DAT_004504e0

private:
    std::vector<uint8_t> mem_;
};

} // namespace ph
