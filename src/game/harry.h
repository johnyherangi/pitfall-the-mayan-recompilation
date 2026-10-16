// Harry and the game-logic globals around him.
//
// The original keeps Harry in a record at 0x465b0f (a6 while his state code runs) with the
// same layout as the other objects for its first bytes (x +0, x.frac +2, y +4, y.frac +6,
// frame +9, damage +0xa, facing +0xb, flags +0xc, sprite +0xe) and many Harry-only fields
// after them, plus globals before and after it (the camera, the trigger class table at
// 0x4658c0, the joypad bytes at 0x466760, timers).  His behaviour is a state machine:
// DAT_00465b69 holds the address of the code that runs next tick (103 continuations
// between 0x426eaa and 0x4311a5; FUN_00426810 calls it).
//
// The port keeps that whole data area (0x465800 .. 0x466900) as raw bytes addressed by the
// original addresses, so the state code reads like the listing it comes from, and keeps the
// state as the original continuation address.  Script pointers stay original addresses too
// and are read through harryMem() (generated from PITFALL.EXE by tools/gen_harry_data.py).
// Object pointers stored in this memory hold the original record address of the object
// (0x462854 + slot * 0x6e, see PlayLogic::objAt).
#pragma once
#include "game/objects.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace game {

class Game;
class PlayLogic;

// The bytes of PITFALL.EXE that Harry's code reads at their original addresses (animation
// scripts, tables), or nullptr when the address was not extracted.
const uint8_t* harryMem(uint32_t va);
uint32_t harryMemAddr(const uint8_t* p);   // the address of bytes harryMem gave (0: not theirs)
struct SpriteSlot { uint32_t va; const char* name; };
extern const SpriteSlot kSpriteSlots[];   // 0x46ffc8..0x470470: slot address -> sprite name
extern const int kSpriteSlotCount;
const char* spriteSlotName(uint32_t va);  // "" when unknown
uint32_t spriteSlotOf(const char* name);  // 0 when unknown

// The sprite slots' contents: each holds the address of a sprite in the load block, as the
// loads fill them (FUN_004457d0 the INIT.PH sprites', FUN_00447190 a level's; a slot the level
// does not fill keeps what an earlier load put there), and the sprites now at those addresses.
class SpriteSlots {
public:
    SpriteSlots();
    uint32_t of(uint32_t slot) const;                  // the slot's content (0 for an unknown slot)
    uint32_t of(const char* name) const { return of(spriteSlotOf(name)); }
    const ph::Sprite* at(uint32_t addr) const;         // the sprite at that address (nullptr: none)
    // A load: each slot the finder knows a sprite for takes its address; `loaded` are the
    // sprites now in the block (INIT.PH's and the level's).
    template <class Find, class Each> void fill(Find find, Each loaded) {
        for (size_t i = 0; i < value_.size(); ++i)
            if (const ph::Sprite* s = find(kSpriteSlots[i].name)) value_[i] = s->arenaAddr();
        at_.clear();
        loaded([&](const ph::Sprite* s) { if (s->arenaAddr()) at_.emplace(s->arenaAddr(), s); });
    }

private:
    std::vector<uint32_t> value_;
    std::unordered_map<uint32_t, const ph::Sprite*> at_;
};

constexpr uint32_t kHarryAddr = 0x465b0f;     // Harry's record (a6 in his state code)
constexpr uint32_t kObjPoolAddr = 0x462854;   // the object records (0x50 x 0x6e bytes)
constexpr uint32_t kObjSize = 0x6e;

struct Harry {
    // 0x465800 .. 0x4668ff by original address (little-endian, as the PC build lays it out).
    static constexpr uint32_t kBase = 0x465800, kSize = 0x1100;
    uint8_t mem[kSize] = {};
    uint8_t* at(uint32_t a) { return mem + (a - kBase); }
    const uint8_t* at(uint32_t a) const { return mem + (a - kBase); }
    FieldRef<uint8_t> b(uint32_t a) { return {at(a)}; }
    FieldRef<int16_t> w(uint32_t a) { return {at(a)}; }
    FieldRef<uint16_t> uw(uint32_t a) { return {at(a)}; }
    FieldRef<int32_t> l(uint32_t a) { return {at(a)}; }
    uint8_t b(uint32_t a) const { return *at(a); }
    int16_t w(uint32_t a) const { int16_t v; std::memcpy(&v, at(a), 2); return v; }
    uint16_t uw(uint32_t a) const { uint16_t v; std::memcpy(&v, at(a), 2); return v; }
    int32_t l(uint32_t a) const { int32_t v; std::memcpy(&v, at(a), 4); return v; }
    uint32_t ul(uint32_t a) const { uint32_t v; std::memcpy(&v, at(a), 4); return v; }
    void setUl(uint32_t a, uint32_t v) { std::memcpy(at(a), &v, 4); }
    int8_t sb(uint32_t a) const { return int8_t(*at(a)); }

    // --- Harry's record, by name ---
    FieldRef<int16_t> x() { return w(0x465b0f); }
    FieldRef<uint16_t> xFrac() { return uw(0x465b11); }
    FieldRef<int16_t> y() { return w(0x465b13); }
    FieldRef<uint16_t> yFrac() { return uw(0x465b15); }
    FieldRef<uint8_t> frame() { return b(0x465b18); }
    FieldRef<uint8_t> damage() { return b(0x465b19); }      // 0 healthy .. 0x96 (100 on hard) dead
    FieldRef<uint8_t> facing() { return b(0x465b1a); }      // 0 right, nonzero left
    FieldRef<uint16_t> flags() { return uw(0x465b1b); }
    FieldRef<int32_t> vx() { return l(0x465ba4); }          // 16.16
    FieldRef<int32_t> vy() { return l(0x465ba8); }
    FieldRef<uint8_t> onGround() { return b(0x465b70); }    // 0xff on the ground
    FieldRef<int16_t> invulnerable() { return w(0x46652d); }   // DAT_0046652d: ticks left after a hit
    int16_t x() const { return w(0x465b0f); }
    int16_t y() const { return w(0x465b13); }
    uint16_t xFrac() const { return uw(0x465b11); }
    uint16_t yFrac() const { return uw(0x465b15); }
    uint8_t frame() const { return b(0x465b18); }
    uint8_t damage() const { return b(0x465b19); }
    uint8_t facing() const { return b(0x465b1a); }
    bool facingLeft() const { return b(0x465b1a) != 0; }
    uint16_t flags() const { return uw(0x465b1b); }
    int32_t vx() const { return l(0x465ba4); }
    int32_t vy() const { return l(0x465ba8); }
    bool onGround() const { return b(0x465b70) != 0; }
    int16_t invulnerable() const { return w(0x46652d); }
    uint32_t cont() const { return ul(0x465b69); }          // DAT_00465b69: the state
    void setCont(uint32_t a) { setUl(0x465b69, a); }
    bool gone() const { return b(0x465b72) != 0; }          // DAT_00465b72: dying / leaving (0x80 = restart)

    // The sprite (+0xe, DAT_00465b1d): the sprite's address in the load block, a slot's content
    // (without the slots, as in a test of the memory alone, the slot's own address).
    const SpriteSlots* slots = nullptr;
    uint32_t slotContent(uint32_t slot) const { return slots ? slots->of(slot) : slot; }
    const char* sprite() const;                        // its name ("" when none)
    const ph::Sprite* spriteRecord() const { return slots ? slots->at(ul(0x465b1d)) : nullptr; }
    void setSprite(const char* name) { setUl(0x465b1d, slotContent(spriteSlotOf(name))); }
    void setSpriteSlot(uint32_t slot) { setUl(0x465b1d, slotContent(slot)); }
    bool spriteIs(const char* name) const { return ul(0x465b1d) == slotContent(spriteSlotOf(name)); }
    bool spriteSlotIs(uint32_t slot) const { return ul(0x465b1d) == slotContent(slot); }

    // The runtime trigger class table (DAT_004658c0, 256 bytes).
    uint8_t* trgClass() { return at(0x4658c0); }
    const uint8_t* trgClass() const { return at(0x4658c0); }
};


} // namespace game
