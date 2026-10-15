// Level objects ("aliens" in the .als table): the object pool, the per-type
// behaviour handlers and the helpers they share.
//
// This mirrors the Genesis-derived object system of PITFALL.EXE:
//   * objects are 0x6e-byte records in a pool (FUN_00435905 alloc / FUN_0043598f
//     free) linked from DAT_00462848; the dispatcher at 0x405170 walks the list
//     and jumps through the 256-entry handler table at 0x462350 on the type byte;
//   * handlers keep their state in the record at fixed offsets (0x30..0x6d),
//     so the ports below address that state by the original offset (`o.w(0x66)`)
//     and quote the handler address they were read from;
//   * animation runs through two small script interpreters (FUN_00423134 and
//     FUN_004237a5) over byte scripts in the executable's data section, which
//     tools/gen_anim_scripts.py extracts into anim_scripts_data.cpp.
#pragma once
#include "ph/level.h"
#include "ph/tables.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace game {

class Game;
class PlayLogic;
struct Harry;

// Byte / word / long references into an object's state area.
template <typename T>
struct FieldRef {
    uint8_t* p;
    operator T() const { T v; std::memcpy(&v, p, sizeof v); return v; }
    FieldRef& operator=(T v) { std::memcpy(p, &v, sizeof v); return *this; }
    FieldRef& operator=(const FieldRef& other) { return *this = T(other); }   // copies the value, not the binding
    FieldRef& operator+=(T v) { return *this = T(T(*this) + v); }
    FieldRef& operator-=(T v) { return *this = T(T(*this) - v); }
    FieldRef& operator^=(T v) { return *this = T(T(*this) ^ v); }
    FieldRef& operator|=(T v) { return *this = T(T(*this) | v); }
    FieldRef& operator&=(T v) { return *this = T(T(*this) & v); }
};

// Animation cursor of the FUN_00423134 interpreter: script position and tick.
struct AnimCursor { uint8_t pos = 0, tick = 0; };

struct Obj {
    // --- fields with a fixed meaning (original offset in the comment) ---
    int16_t x = 0;                 // +0x00 position (the sprite is drawn at x + 0x10, y + 0x10)
    uint16_t xFrac = 0;            // +0x02
    int16_t y = 0;                 // +0x04
    uint16_t yFrac = 0;            // +0x06
    uint8_t type = 0;              // +0x08 handler index (0 = a free record)
    uint8_t frame = 0;             // +0x09 sprite frame, 0xff = not drawn
    uint8_t hits = 0;              // +0x0a damage taken from the whip / stones
    uint8_t facing = 0;            // +0x0b 1 = faces left (drawn H-flipped)
    uint16_t flags = 0;            // +0x0c 0x8000 = drawn in front of the high-priority tiles
    const ph::Sprite* sprite = nullptr;   // +0x0e
    int prev = -1, next = -1;      // +0x14 / +0x18 the object list (record indices, -1 = none)
    int col = -1, row = -1;        // +0x1c pointer to the .als entry (col, row here)
    int als = -1;                  // ... and its index in the level's .als table (-1: the dummy entry 0x466770)
    uint8_t id = 0;                // +0x20 the .als id to restore when the object leaves the view
    uint8_t behind = 0;            // +0x28 drawn behind Harry
    uint8_t vflip = 0;             // +0x29 drawn V-flipped
    uint8_t whipCool = 0;          // +0x2d ticks before the whip can hit again
    uint8_t touchTick = 0;         // +0x2e Harry contact is tested every 6th tick
    // --- per-type state (+0x30 .. +0x6d), addressed by the original offsets ---
    uint8_t s[0x70] = {};
    // Pointers that the original kept inside the state area.
    Obj* link = nullptr;           // +0x50 / +0x5c: a related object
    Obj* link2 = nullptr;          // +0x40 in the bat roost: a second related object
    Obj* link3 = nullptr;          // +0x60 in the boss meter: the % sign
    std::vector<Obj*> parts;       // +0x32.. in the bungee body: pool slots of the rope segments
    const uint8_t* script = nullptr;   // +0x3c / +0x3e / +0x64: an animation script
    const ph::SpriteHitbox* rawBox = nullptr;   // +0xe pointing at a box record instead of a sprite (FUN_00424af1)
    bool freed = true;             // on the free stack (FUN_0043598f), not in the list

    FieldRef<uint8_t> b(int off) { return {s + off}; }
    FieldRef<int16_t> w(int off) { return {s + off}; }
    FieldRef<int32_t> l(int off) { return {s + off}; }
    AnimCursor& anim(int off) { return *reinterpret_cast<AnimCursor*>(s + off); }
    void setSprite(const ph::Sprite* spr) { sprite = spr; rawBox = nullptr; }
};

// FUN_004251f9: the trigger under (x, y).  cls = the class byte (0 when >= 0xf0, d2),
// id = the trigger id (d3), raw = the class byte itself (d6); found is false when the
// cell has no trigger (the routine then returns with Z set, as it does for a class >= 0xf0).
struct Trg { uint8_t cls = 0, id = 0, raw = 0; bool found = false; bool z() const { return !found || raw >= 0xf0; } };

struct ObjCtx {
    Game& g;
    PlayLogic& pl;
    const ph::LevelData& lv;
    Harry& harry;
    int levelNo = 0;
    int camX = 0, camY = 0;        // DAT_004658b4 / DAT_004658b6
    bool hard = false;             // DAT_00462694: hard difficulty (whip damage 9 instead of 10)
    // DAT_0046652f (the stop-watch's count): enemies stand still and sparkle.  Read as it is
    // now: a watch picked up earlier in the pass freezes the objects after it.
    bool frozen() const;

    // helpers shared by the handlers (objects.cpp)
    uint32_t rnd();                                    // FUN_0043387e
    bool sound0(int id, int pri);                      // FUN_004262d9 / FUN_0042633d: channel 0 by priority
    bool sound1(int id, int pri);                      // FUN_004261e2: channel 1 by priority
    void soundNow(int id);                             // FUN_004261b0: channel 1, no priority test
    bool channel0Busy();                               // FUN_004262c8
    bool channel1Busy();                               // FUN_004261d1
    bool hurtHarry(int amount);                        // FUN_00428233 (true: it got past its early returns)
    // FUN_00424af1 with a5 = Harry; `d1`, when given, receives what the test leaves in d1 (the
    // object's box edge it stopped at; unchanged when either box is missing).
    bool touchesHarry(Obj& o, int16_t* d1 = nullptr);
    bool boxesOverlap(const Obj& a, const Obj& b);     // FUN_00424af1
    const ph::ColumnTable& det() const;                // .det collision plane
    const ph::ColumnTable& dt2() const;                // .dt2
    // FUN_00425d6f: floor in the cell of (x, y) or one above / below; returns the surface y or y.
    bool floorSnap(int x, int& y, bool plane2 = false) const;
    // FUN_004251f9: trigger at (x + 0x20, y): class byte (the runtime table, 0 for >= 0xf0) and id.
    uint8_t triggerClass(int x, int y, uint8_t* id = nullptr) const;
    Trg trigger(int x, int y) const;                   // FUN_004251f9 with all its results
    Obj* spawn(bool tail = false);                     // FUN_00435905 / FUN_00435780 (nullptr when the pool is full)
    // FUN_004356bc / FUN_0043546e: at the parent's x / y.  With d0.b != 0 (nearly every call
    // site) the routine also calls the VRAM-slot stub FUN_004263a6, which sets w26 = 0x6663.
    Obj* spawnFrom(Obj& parent, bool tail = false, int d0 = 1);
    void release(Obj& o);                              // FUN_0043598f
    Obj* spawnEffect(Obj& at, const ph::Sprite* spr, const uint8_t* script, int ticks);   // 0x433914
    void sparkleBurst(Obj& at, const int16_t* offsets);                                    // 0x433747
    const ph::Sprite* spr(const char* name);
};

using Handler = void (*)(ObjCtx&, Obj&);

// The object records and their list (FUN_00435274 / FUN_00435905 / FUN_00435780 /
// FUN_0043598f): 0x50 records of 0x6e bytes at 0x462854, a stack of free records
// (0x464ab4.., top at DAT_00462850; it counts as empty with 8 records left) and a doubly
// linked list (DAT_00462848 head, DAT_0046284c tail) that the object pass walks.  A freed
// record keeps its fields (only the type is cleared) and its list pointers, as the
// original's does; a new record keeps x / y and their fractions from its last use.
class ObjectSystem {
public:
    static constexpr int kPoolSize = 0x50;
    static constexpr int kReserve = 8;       // 0x464ad4 - 0x464ab4 = 8 entries never handed out

    ObjectSystem() { reset(); }
    void reset();                              // FUN_00435274: every record free (0x4f on top), empty list
    Obj* allocHead();                          // FUN_00435905 (nullptr when no record is left)
    Obj* allocTail();                          // FUN_00435780
    void free(Obj& o);                         // FUN_0043598f
    // 0x435532: the object goes to the end of the list (it then runs again in this pass and
    // draws after the others).  false: there was no record to spare, the object was freed
    // (and the handler has to return).
    bool moveToTail(Obj& o);
    // One object pass (0x405170): every object in the list runs its handler.
    void tick(ObjCtx& ctx);
    // [DAT_00465b8e] = -2: the running object's display entry is cancelled (not drawn).
    void cancelCurrentDisplay() { if (hasSlot_) disp_[curSlot_] = -2; }
    // `sub.l #4, DAT_00464bf8` / `move.l #-1, (a0)`: the last entry of the front list is taken back.
    void popFrontDisplay() { --frontN_; disp_[frontN_] = -1; }
    Obj& record(int i) { return recs_[size_t(i)]; }
    const Obj& record(int i) const { return recs_[size_t(i)]; }
    int indexOf(const Obj& o) const { return int(&o - recs_.data()); }
    std::vector<Obj*> list();                  // the objects in list order (DAT_00462848 ..)
    // A walk of the list as the handlers do it (DAT_00462848, then +0x18 of each record, which a
    // freed record keeps).
    Obj* first() { return head_ < 0 ? nullptr : &recs_[size_t(head_)]; }
    Obj* nextOf(const Obj& o) { return o.next < 0 ? nullptr : &recs_[size_t(o.next)]; }
    static Handler handler(uint8_t type);

    // The display lists the pass makes, one array as in RAM: from 0x464bfc the objects behind
    // Harry, from 0x464d3c (entry kFront) the ones in front; FUN_00434f6c draws them on the
    // next tick.  Entries are record indices, -1 ends a list, -2 is a cancelled entry; the
    // entries past an end are left from earlier passes.
    static constexpr size_t kFront = 0x50;
    static constexpr uint32_t kDisplayAddr = 0x464bfc;
    const std::array<int, 0xa1>& displayArray() const { return disp_; }
    uint32_t currentDisplayAddr() const { return hasSlot_ ? kDisplayAddr + 4u * uint32_t(curSlot_) : 0; }   // DAT_00465b8e
    void cancelDisplayAt(uint32_t addr) {                // [addr] = -2
        const uint32_t i = (addr - kDisplayAddr) / 4;
        if (addr >= kDisplayAddr && (addr - kDisplayAddr) % 4 == 0 && i < disp_.size()) disp_[i] = -2;
    }

private:
    void init(Obj& o);                         // 0x43582b
    void unlink(Obj& o);
    void linkTail(Obj& o);
    std::array<Obj, kPoolSize> recs_;
    int stack_[kPoolSize] = {};
    int top_ = -1;                             // index of the top entry of the free stack
    int head_ = -1, tail_ = -1;
    std::array<int, 0xa1> disp_{};             // 0x464bfc .. 0x464e7c
    size_t behindN_ = 0, frontN_ = kFront;     // DAT_00464bf4 / DAT_00464bf8: where the next entry goes
    size_t curSlot_ = 0;                       // DAT_00465b8e: the entry of the object that ran last
    bool hasSlot_ = false;
};

// --- shared behaviour pieces (objects.cpp) ---

// FUN_00423134: advance the animation cursor over `script` at `ticksPerFrame`
// and set o.frame; returns true when the script wrapped around (0xf0).
bool animStep(ObjCtx& ctx, Obj& o, AnimCursor& cur, const uint8_t* script, int ticksPerFrame);
// FUN_00423150: set the frame for the current cursor position without advancing.
void animPeek(ObjCtx& ctx, Obj& o, AnimCursor& cur, const uint8_t* script);
// FUN_004237a5 (the pointer-based interpreter used by Harry and a few objects):
// o.script / o.l(pcOff) hold the script and the position, o.b(pcOff + 4) the tick.
bool anim2Step(ObjCtx& ctx, Obj& o, int pcOff, int ticksPerFrame);
bool anim2Step(ObjCtx& ctx, Obj& o, const uint8_t* script, int pcOff, int ticksPerFrame);   // a second script on the same object

// 0x424171: hits from Harry's stones, the boomerang or the whip accumulate in o.hits.
// d1: something hit this tick; d0: hits + 1 > hitPoints; z: the flag the callers that branch
// straight after the call test (z == !d0, but when the big stone passes through: d0 = 1, z set).
struct WeaponHit { bool d0 = false, d1 = false, z = true; };
WeaponHit weaponHit(ObjCtx& ctx, Obj& o, int hitPoints);
// The callers that test d1 (the return) and d0 (*killed).
inline bool weaponHits(ObjCtx& ctx, Obj& o, int hitPoints, bool* killed) {
    const WeaponHit r = weaponHit(ctx, o, hitPoints);
    if (killed) *killed = r.d0;
    return r.d1;
}
// 0x42401c: Harry's sling stones bounce off the object (the bosses while they cannot be hurt).
bool deflectStones(ObjCtx& ctx, Obj& o);
// 0x424733: the webs' reaction to the whip and to stones (see h_web / h_verticalWeb).
bool webHit(ObjCtx& ctx, Obj& o, bool* cut);
// 0x433725: a sparkle burst in one of two patterns (chosen at random).
void debrisPuff(ObjCtx& ctx, Obj& at);
// 0x4081ae: the score table 0x461aca by type.
int scoreForType(uint8_t type);
// 0x45b6c3: the class bits by type (1: killed by the big stone's blast, 2: comes back after a
// kill, 4: the boomerang homes in on it).
uint8_t typeClass(uint8_t type);
// 0x40855e: the death-effect table moves y by type; returns its kind (1 for unlisted types).
uint8_t deathFxAdjust(Obj& o);
int deathFxYOff(uint8_t type);                  // the same table's y offset (0 for unlisted types)
// 0x424f40: more than 0x20 px outside the screen.
bool offScreen(const ObjCtx& ctx, const Obj& o);
// 0x4081fc: an enemy dies: score, death effect, permanent-kill bookkeeping, release.
void killObject(ObjCtx& ctx, Obj& o);
// 0x407aff: every 6th call, harryContactNow.  `d1` as for ObjCtx::touchesHarry (0x60 after a
// hurt that went through).
bool harryContact(ObjCtx& ctx, Obj& o, int damage, int16_t* d1 = nullptr);
// 0x407b0f: hurt Harry by `damage` when the boxes overlap (on a vine his box swings with him).
bool harryContactNow(ObjCtx& ctx, Obj& o, int damage, int16_t* d1 = nullptr);
// 0x4301ce: triggers 0x30 / 0x31 at (x + dx, y) toggle the "in front" flag; returns the lookup
// (FUN_004251f9's d2 / d3, which some callers test next).
Trg priorityTrigger(ObjCtx& ctx, Obj& o, int dx);
// What FUN_00425d6f (the .det probe) leaves in d3, for the handlers that go on to use it:
// shape * 16 + (x & 15) on a hit; on a miss the d3 it was given (y < 0x12), 0 (an empty
// column) or the row byte of the column entry that ended the scan.
uint32_t floorProbeD3(const ObjCtx& ctx, int x, int y, uint32_t d3);
// 0x424d6a / 0x424fbb: release the object when it is far from the camera
// (far: 120 px beyond the screen edges; wide: a whole screen beyond them).
void exitDespawnFar(ObjCtx& ctx, Obj& o);
void exitDespawnWide(ObjCtx& ctx, Obj& o);
bool farFromCamera(const ObjCtx& ctx, const Obj& o);   // 0x424ec4: the test alone
bool beyondWide(const ObjCtx& ctx, const Obj& o);      // 0x425051: the test alone
void exitDespawnTall(ObjCtx& ctx, Obj& o);             // 0x4250ce: far sideways, a tall window vertically
void exitDespawnVertical(ObjCtx& ctx, Obj& o);         // 0x42501f: the vertical distance alone
void exitDespawnMid(ObjCtx& ctx, Obj& o);              // 0x424d9c: 0xc8 / 0x208 px sideways, the far window vertically
// 0x4057b0: +0x2b counts the ticks; every 30th sets +0x2a.
inline void halfMinuteTick(Obj& o) {
    o.s[0x2b] = uint8_t(o.s[0x2b] + 1);
    if (o.s[0x2b] == 0x1e) { o.s[0x2b] = 0; o.s[0x2a] = 0xff; }
}
// 0x412629: stop-watch freeze: spawn a sparkle instead of acting.
void freezeSparkle(ObjCtx& ctx, Obj& o);

// Animation scripts (anim_scripts_data.cpp, generated).
extern const uint8_t kAnim_snakeEmerge[], kAnim_snakeMiss[], kAnim_snakeLunge[], kAnim_snakeHit[],
    kAnim_turn7[], kAnim_turn4[], kAnim_run10[], kAnim_sparkle1[], kAnim_sparkle2[], kAnim_death14[],
    kAnim_boomerangSpin[], kAnim_heart[], kAnim_heartBoss[], kAnim_splash[], kAnim_flyWings[],
    kAnim_condorTurn[], kAnim_condorStun[], kAnim_feather[], kAnim_ratTurn[], kAnim_ratFall[], kAnim_ratRun[],
    kAnim_batDrop[], kAnim_batFly[], kAnim_batBite[], kAnim_spiderThread[], kAnim_webBounceA[], kAnim_webBounceH[],
    kAnim_eyesBlink[], kAnim_marker[], kAnim_markerFlame[], kAnim_vineMonkey[], kAnim_treeSnakeSpit[],
    kAnim_treeSnakeUp[], kAnim_treeSnakeHang[], kAnim_plantHit[], kAnim_plantBreathe[], kAnim_plantOpen[],
    kAnim_boarGraze[], kAnim_boarToss[], kAnim_boarRun[], kAnim_rat2Run[], kAnim_rockShatter[], kAnim_webSway[], kAnim_pit[],
    kAnim_run8[], kAnim_jaguarIntro[], kAnim_jaguarReverse[], kAnim_bungeeStub[],
    kAnim_skullChatter[], kAnim_skullQuiet[], kAnim_skullRoll[], kAnim_boneIdle[], kAnim_potShards[], kAnim_fireJet[], kAnim_tongueBounce[], kAnim_tongueClose[], kAnim_gargoyle[], kAnim_axeSpin[],
    kAnim_skullMorph[], kAnim_skullWalk[], kAnim_skullDie[], kAnim_ballDrop[], kAnim_splashRing[], kAnim_rippleRing[], kAnim_spearThrust[],
    kAnim_snakeLunge2[], kAnim_spitFly[], kAnim_spitSplat[], kAnim_crocBite[], kAnim_crocDive[], kAnim_priestTurn[], kAnim_fireballDie[],
    kAnim_guardShotLow[], kAnim_guardShotHigh[], kAnim_guardTurn[], kAnim_guardPeel[], kAnim_guardWalk[],
    kAnim_hawkGlide[], kAnim_crocLeap[], kAnim_splashRing2[], kAnim_treeSnakeLimp[], kAnim_cartSpark[], kAnim_floatBob[], kAnim_harrySplash[], kAnim_vaporSpin[],
    kAnim_leverBack[], kAnim_stoneJagWake[], kAnim_stoneJagReverse[], kAnim_jagManSwipeFx[];
extern const uint8_t kData_bungeeScripts[];      // 0x45aaa4: Harry's bungee animation triples (see h_bungee)
extern const uint8_t kData_firePath[];           // 0x45ad94: the fire pit's 59 keyframes {frame, dx, dy, tailFrame, tailDx, tailDy} (int16)
extern const uint8_t kData_priestDeath[];        // 0x45a849: the fire priest's collapse (a byte script; its cursor jumps past the first 0xf0)
// 0x45dfd0 / 0x45bfde: the swinging rope's positions by swing phase (DAT_00466796), without and
// with Harry on it: 0x55 word offsets, then per phase {dx, dy, frame} words for the rope and its
// 13 segments (a frame of 0x15 or more is Harry's entry, after the segment he holds), -1 ending.
extern const uint8_t kData_ropeSwing[], kData_ropeSwingHarry[];
// 0x45fdc4: the Balankanche ride's script, {distance in 0xa0 px, lane, kind} words (kind 9 adds the
// target speed as a fourth word; kind 10 ends it).
extern const uint8_t kData_rideScript[];
// 0x46b0d8: the widths of the letter glyphs, {ltr_lg, ltr_md, ltr_sm} a glyph (FUN_00434a30).
extern const uint8_t kData_glyphWidths[];
extern const uint8_t kAnim2_deathFade[], kAnim2_monkeyToss[], kAnim2_condorDive[], kAnim2_jaguarLeap[], kAnim2_jaguarCrouchA[],
    kAnim2_jaguarCrouchB[], kAnim2_jaguarCrouchRoar[], kAnim2_jaguarRoar[], kAnim2_bossCloud[],
    kAnim2_skullThrow[], kAnim2_skullChop[], kAnim2_boneTumble[], kAnim2_ballSwingA[], kAnim2_ballSwingB[], kAnim2_ballSwingC[],
    kAnim2_crocSwim[], kAnim2_crocSink[], kAnim2_priestRise[], kAnim2_priestMain[], kAnim2_fireball[],
    kAnim2_hawkDive[], kAnim2_stoneJagCrouch[], kAnim2_stoneJagLeap[], kAnim2_stoneJagBigLeap[], kAnim2_stoneJagLand[],
    kAnim2_stoneJagHop[], kAnim2_jagManCrouch[], kAnim2_jagManReady[], kAnim2_jagManSwipe[], kAnim2_jagManMorph[];
extern const char* const kAnim2Sprites[];        // names for the relocated 0xf1 opcodes
// Sparkle offset tables (handlers.cpp), 0x8000-terminated (dx, dy) pairs.
extern const int16_t kSparkleOffsetsA[], kSparkleOffsetsB[], kSparkleOffsetsC[];

} // namespace game
