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
    uint8_t type = 0;              // +0x08 handler index
    uint8_t frame = 0;             // +0x09 sprite frame, 0xff = not drawn
    uint8_t hits = 0;              // +0x0a damage taken from the whip / stones
    uint8_t facing = 0;            // +0x0b 1 = faces left (drawn H-flipped)
    uint16_t flags = 0;            // +0x0c 0x8000 = drawn in front of the high-priority tiles
    const ph::Sprite* sprite = nullptr;   // +0x0e
    int col = -1, row = -1;        // +0x1c pointer to the .als entry (col, row here)
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
    bool freed = false;
    bool fresh = true;             // first tick after spawning

    FieldRef<uint8_t> b(int off) { return {s + off}; }
    FieldRef<int16_t> w(int off) { return {s + off}; }
    FieldRef<int32_t> l(int off) { return {s + off}; }
    AnimCursor& anim(int off) { return *reinterpret_cast<AnimCursor*>(s + off); }
    void setSprite(const ph::Sprite* spr) { sprite = spr; }
};

// Harry's state as the object handlers see it (continuation addresses in
// DAT_00465b69 and a few flags of the original).
enum class HarryState { Normal, Jumping, Hurt, Dead, Drowning, Swinging, Climbing, Grabbed, OnBungee };

struct ObjCtx {
    Game& g;
    PlayLogic& pl;
    const ph::LevelData& lv;
    Harry& harry;
    int levelNo = 0;
    int camX = 0, camY = 0;        // DAT_004658b4 / DAT_004658b6
    uint32_t pad = 0;              // joypad bits (engine::kJoy*), DAT_00466761..65
    uint32_t padPressed = 0;       // bits that went down this tick (DAT_0046676a == 1 for jump; a handler clears a bit it consumes)
    bool hard = false;             // DAT_00462694: hard difficulty (whip damage 9 instead of 10)
    bool boss = false;             // DAT_00466507: a boss fight is on (heart beat, boss meter)
    bool freeze = false;           // DAT_0046652f (stop-watch): enemies stand still and sparkle
    int pistonPhase = 0, pistonTick = 0;   // DAT_0046650d / DAT_0046650e: the temple pistons' shared cycle
    HarryState hstate = HarryState::Normal;
    bool harryUntouchable = false; // DAT_00465b72 (dying / leaving the level)
    const char* harrySprite = "";
    // Whip box (DAT_00465aeb..) while the whip is out, 0 width otherwise.
    int whipX0 = 0, whipX1 = 0, whipY0 = 0, whipY1 = 0;

    // helpers shared by the handlers (objects.cpp)
    uint32_t rnd();                                    // FUN_0043387e
    void sound(int id);                                // FUN_004261b0 / FUN_004262d9 / FUN_004261e2
    void soundNear(const Obj& o, int id);              // only when Harry is within 128 px
    void hurtHarry(int amount);                        // FUN_00428233
    bool touchesHarry(Obj& o, bool evenHidden = false);   // FUN_00424af1 against Harry (frame 0xff objects skipped unless asked)
    bool boxesOverlap(const Obj& a, const Obj& b);     // FUN_00424af1
    const ph::ColumnTable& det() const;                // .det collision plane
    const ph::ColumnTable& dt2() const;                // .dt2
    // FUN_00425d6f: floor in the cell of (x, y) or one above / below; returns the surface y or y.
    bool floorSnap(int x, int& y, bool plane2 = false) const;
    // FUN_004251f9: trigger at (x + 0x20, y): class byte (kTriggerClass) and id.
    uint8_t triggerClass(int x, int y, uint8_t* id = nullptr) const;
    Obj* spawn();                                      // FUN_00435905 (nullptr when the pool is full)
    void release(Obj& o);                              // FUN_0043598f
    Obj* spawnEffect(Obj& at, const ph::Sprite* spr, const uint8_t* script, int ticks);   // 0x433914
    void sparkleBurst(Obj& at, const int16_t* offsets);                                    // 0x433747
    const ph::Sprite* spr(const char* name);
};

using Handler = void (*)(ObjCtx&, Obj&);

class ObjectSystem {
public:
    static constexpr int kPoolSize = 0x50;   // (0x464ab4 - 0x462854) / 0x6e

    void reset();
    Obj* alloc();
    void free(Obj& o);
    // One tick of the dispatcher (0x405170): every live object runs its handler.
    void tick(ObjCtx& ctx);
    std::vector<std::unique_ptr<Obj>>& objects() { return pool_; }
    const std::vector<std::unique_ptr<Obj>>& objects() const { return pool_; }
    static Handler handler(uint8_t type);

private:
    std::vector<std::unique_ptr<Obj>> pool_;
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

// 0x424171: hits from the whip (and the sling, once ported) accumulate in o.hits;
// returns true when something hit this tick, *killed when hits >= hitPoints.
bool weaponHits(ObjCtx& ctx, Obj& o, int hitPoints, bool* killed);
// 0x4081ae: the score table 0x461aca by type.
int scoreForType(uint8_t type);
// 0x4081fc: an enemy dies: score, death effect, permanent-kill bookkeeping, release.
void killObject(ObjCtx& ctx, Obj& o);
// 0x407aff: every 6th tick, hurt Harry by `damage` when the boxes overlap.
bool harryContact(ObjCtx& ctx, Obj& o, int damage);
// 0x4301ce: triggers 0x30 / 0x31 at (x + dx, y) toggle the "in front" flag.
void priorityTrigger(ObjCtx& ctx, Obj& o, int dx);
// 0x424d6a / 0x424fbb: release the object when it is far from the camera
// (far: 120 px beyond the screen edges; wide: a whole screen beyond them).
void exitDespawnFar(ObjCtx& ctx, Obj& o);
void exitDespawnWide(ObjCtx& ctx, Obj& o);
bool farFromCamera(const ObjCtx& ctx, const Obj& o);   // 0x424ec4: the test alone
bool beyondWide(const ObjCtx& ctx, const Obj& o);      // 0x425051: the test alone
void exitDespawnTall(ObjCtx& ctx, Obj& o);             // 0x4250ce: far sideways, a tall window vertically
void exitDespawnVertical(ObjCtx& ctx, Obj& o);         // 0x42501f: the vertical distance alone
void exitDespawnMid(ObjCtx& ctx, Obj& o);              // 0x424d9c: 0xc8 / 0x208 px sideways, the far window vertically
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
    kAnim_hawkGlide[];
extern const uint8_t kData_bungeeScripts[];      // 0x45aaa4: Harry's bungee animation triples (see h_bungee)
extern const uint8_t kData_firePath[];           // 0x45ad94: the fire pit's 59 keyframes {frame, dx, dy, tailFrame, tailDx, tailDy} (int16)
extern const uint8_t kData_priestDeath[];        // 0x45a849: the fire priest's collapse (a byte script; its cursor jumps past the first 0xf0)
extern const uint8_t kAnim2_monkeyToss[], kAnim2_condorDive[], kAnim2_jaguarLeap[], kAnim2_jaguarCrouchA[],
    kAnim2_jaguarCrouchB[], kAnim2_jaguarCrouchRoar[], kAnim2_jaguarRoar[], kAnim2_bossCloud[],
    kAnim2_skullThrow[], kAnim2_skullChop[], kAnim2_boneTumble[], kAnim2_ballSwingA[], kAnim2_ballSwingB[], kAnim2_ballSwingC[],
    kAnim2_crocSwim[], kAnim2_crocSink[], kAnim2_priestRise[], kAnim2_priestMain[], kAnim2_fireball[],
    kAnim2_hawkDive[];
extern const char* const kAnim2Sprites[];        // names for the relocated 0xf1 opcodes
// Sparkle offset tables (handlers.cpp), 0x8000-terminated (dx, dy) pairs.
extern const int16_t kSparkleOffsetsA[], kSparkleOffsetsB[], kSparkleOffsetsC[];

} // namespace game
