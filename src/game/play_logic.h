// Playable level logic: Harry's movement on the real collision data and the
// level objects.
//
// This is a reconstruction of the core of Harry's state machine from the
// Genesis-derived half of PITFALL.EXE (Harry object at 0x465b0f, state code
// FUN_00426810 / FUN_00428dfa / FUN_0042babc and the collision probes
// FUN_00425455 / FUN_0042571f / FUN_00425d6f).  Constants are the original
// 16.16 fixed-point values; see docs/RECONSTRUCTION_LEDGER.md for what is
// still simplified.  Objects live in game/objects.h.
#pragma once
#include "game/game.h"
#include "game/objects.h"

#include <vector>

namespace game {

// Harry object (original layout: x @+0, x.frac @+2, y @+4, frame @+9,
// facing @+0xb, sprite @+0xe, onGround @+0x61, vx @+0x95, vy @+0x99).
struct Harry {
    int32_t x = 0, y = 0;          // integer pixel position (DAT_00465b0f / DAT_00465b13)
    uint16_t xFrac = 0, yFrac = 0; // fractional accumulators
    int32_t vx = 0, vy = 0;        // 16.16 velocities (DAT_00465ba4 / DAT_00465ba8)
    bool facingLeft = false;       // DAT_00465b1a
    bool onGround = true;          // DAT_00465b70
    bool usePlane2 = false;        // DAT_00465bb3: probe .dt2 instead of .det
    int jumpHold = 0;              // DAT_00465aff: frames of gravity suspension left
    int frame = 0;                 // DAT_00465b18
    int animTick = 0;              // DAT_00465b71
    int scriptPos = 0;
    const char* anim = "hyiready";
    int chiliTimer = 0;            // DAT_0046652b (chili pepper, 0x380 ticks): faster run, higher jump
    bool powerJump() const { return chiliTimer > 0; }
    bool dead = false;
    bool drowning = false;         // state 0x427528: sinking in the water (the crocodiles leave)
    int damage = 0;                // DAT_00465b19: 0 = healthy, >= 0x96 (100 on hard) = dead
    int invulnerable = 0;          // DAT_0046652d: ticks of invulnerability after a hit
    int hurtTimer = 0;             // DAT_00465b77: knock-back / hit-reaction ticks
    int32_t knockDecel = 0;        // DAT_00465b59: while set, the knock-back slides to a stop on its own
    int whipTimer = 0;             // ticks left in the whip swing (hyiwhip, 9 frames)
    // Grabbed by a bat (0x428625): hyidazed, struggle free with 12 input changes.
    Obj* grabbedBy = nullptr;      // DAT_00465b4f
    int struggleLeft = 0;          // obj+0x38 of Harry's record
    int nipTimer = 0;              // obj+0x36: ticks to the next +1 damage
    uint8_t lastButtons = 0;       // obj+0x3e
    uint16_t lastDirs = 0;         // obj+0x3a
    AnimCursor dazedAnim;          // obj+0x3c
    int platformTicks = 0;         // > 0 while an object platform holds Harry up
    int platformY = 0;
    Obj* bungee = nullptr;         // DAT_00465b7e: the bungee vine Harry hangs from (state 0x429fe2)
    int runTicks = 0;              // DAT_00465b9a: ticks spent running on the ground
    int tripTicks = 0;             // hyitrip1 (script 0x46168e) after stumbling over a skull
    int somersault = 0;            // state 0x42cdb6 (hyisomr2) after a tongue-plant bounce: ticks in the air so far
    Obj* lever = nullptr;          // DAT_00465b41 while pulling a lever (state 0x42a6e7, hyipull)
    int leverTicks = 0;
};

// The level-script table at 0x45b1fc: a lever's trigger id, the door / chute trigger id it
// opens, the flag it sets (index into Game::levelFlags) and the door parameter (64 = a
// sliding door, -1 = vanish, -2 = a chute, -3 / -4 = other script hooks).
struct LevelScriptEntry { uint8_t lever, door; uint8_t flag; int16_t param; };
constexpr LevelScriptEntry kLevelScript[] = {
    {0x52, 0x53, 0x04, 64}, {0x54, 0x55, 0x05, 64}, {0x56, 0x57, 0x0a, 64}, {0x58, 0x59, 0x0b, 64},
    {0x5a, 0x5b, 0x0c, 64}, {0x40, 0x45, 0x06, 64}, {0x41, 0x46, 0x07, 64}, {0x42, 0x47, 0x08, 64},
    {0x43, 0x48, 0x09, 64}, {0x4a, 0x4d, 0x12, -3}, {0x4b, 0xae, 0x13, -4}, {0xb0, 0xb1, 0x0d, -2},
    {0xb2, 0xb3, 0x0e, -2}, {0xb4, 0xb5, 0x0f, -2}, {0xb7, 0xb8, 0x10, -2}, {0xb9, 0xba, 0x11, -2},
};

class PlayLogic : public LevelLogic {
public:
    void enter(Game& g) override;
    void tick(Game& g) override;
    const Harry& harry() const { return h_; }
    ObjectSystem& objectSystem() { return objects_; }
    const ObjectSystem& objectSystem() const { return objects_; }

    // --- services for the object handlers (game/objects.h) ---
    uint32_t random();                         // FUN_0043387e
    void hurt(Game& g, int amount);            // FUN_00428233
    void healHarry() { h_.damage = 0; }        // fx_hrtbt
    void startChili() { h_.chiliTimer = physics_kChiliTicks(); }
    void markCollected(const Obj& o);          // the .als entry is gone for good
    void markKilled(const Obj& o);             // permanent kill (0x465a40 list)
    void restoreEntry(const Obj& o);           // put the .als id back: the entry respawns when it scrolls back in
    bool freezeSparkleBlocked() const { return false; }   // DAT_00465d4b
    bool harryBusy() const { return h_.grabbedBy != nullptr || h_.bungee != nullptr; }   // DAT_00465b73
    bool harryOnPlatform() const { return h_.platformTicks > 0; }  // DAT_00465b73 (platform half)
    void standOn(int y);                       // a platform object carries Harry (0x415230)
    void carryHarry(int32_t vx16) { int32_t x = h_.x; uint16_t f = h_.xFrac; int64_t v = (int64_t(x) << 16 | f) + vx16; h_.x = int32_t(v >> 16); h_.xFrac = uint16_t(v); }   // add.l to Harry's x.frac (a moving platform)
    void bounce(int y, int32_t vx, int32_t vy, bool faceRight);   // web bounce (0x406d17)
    void launch(int32_t vx, int32_t vy);        // thrown into the air (boar toss, 0x415a41)
    void knockBack(bool toLeft, bool strong);   // shoved back (state 0x427207: 7 or 2 px/tick)
    void sink(int y);                           // swallowed by a pit (FUN_0042a325, hyisink)
    void grabHarry(Obj& bat);                  // FUN_004285d7
    void releaseHarry();
    void trip() { if (h_.tripTicks == 0) h_.tripTicks = 40; }   // 0x41aaa0: stumble (hyitrip1, 8 frames x 5 ticks)
    void somersault(int32_t vy) { launch(0, vy); h_.somersault = 1; }   // 0x417c93: thrown up tumbling (hyisomr2)
    bool running() const { return h_.onGround && h_.vx != 0 && h_.runTicks >= 0x14; }   // DAT_00465ba0 == hyirun && DAT_00465b9a >= 0x14
    void pullLever(Obj& lever);                // 0x40c42d: Harry pulls (hyipull, 11 steps x 4 ticks)
    // The runtime copy of the trigger class table (DAT_004658c0): the level script clears
    // the class of a door's trigger ids when its lever is pulled (FUN_0041804b).
    uint8_t triggerClassOf(uint8_t id) const { return trgClass_[id]; }
    void clearTriggerClass(uint8_t id) { trgClass_[id] = 0; }
    void setTriggerClass(uint8_t id, uint8_t cls) { trgClass_[id] = cls; }
    void grabBungee(Obj& vine);                // 0x40c784: Harry hangs from the vine, which moves him
    void releaseBungee() { h_.bungee = nullptr; }
    bool onBungee(const Obj& vine) const { return h_.bungee == &vine; }   // 0x40d2ef
    // Boss fights: the camera cannot scroll left of `x` (DAT_00465b09), DAT_00466507
    // marks the fight, and the outro (DAT_00465d49, FUN_0043426d) freezes the controls
    // for 0x50 ticks and then walks Harry left to the exit.
    void setShake(int v) { shake_ = v; }       // DAT_00465b02: vertical screen shake (copied to FrameState::bob)
    int shake() const { return shake_; }
    void lockCameraLeft(int x) { camMinX_ = x; }
    void lockCameraRight(int x) { camMaxX_ = x; }   // DAT_00465b0b (the level's right limit until a handler narrows it)
    int cameraRightLock() const { return camMaxX_; }
    void setBoss(Obj* o) { boss_ = o; }        // trg+0x1f4; nullptr clears DAT_00466507
    Obj* bossObject() const { return boss_; }
    bool bossActive() const { return boss_ != nullptr; }
    void startBossOutro() { outroTicks_ = 1; }

private:
    static int physics_kChiliTicks();
    void stepHorizontal(Game& g, uint32_t pad);
    void stepVertical(Game& g, uint32_t pad);
    void animate(Game& g);
    void updateCamera(Game& g);
    void emitSprites(Game& g);
    void spawnObjects(Game& g);
    void runObjects(Game& g);
    void checkTriggers(Game& g);
    void whip(Game& g);
    void die(Game& g);
    void tickGrabbed(Game& g, uint32_t pad);
    bool isGone(int col, int row, const Game& g) const;
    ph::FloorHit probeFeet(const ph::LevelData& lv, int x, int y) const;

    Harry h_;
    ObjectSystem objects_;
    std::vector<std::pair<int, int>> collected_;   // (col,row) of picked-up .als entries (this life)
    std::vector<std::pair<int, int>> cleared_;     // entries whose working id is 0 (object alive or consumed)
    int deathTimer_ = 0;                            // ticks until the restart after a death
    bool exiting_ = false;
    int levelW_ = 0, levelH_ = 0;   // DAT_00465bae / DAT_00465bb0
    bool jumpWasDown_ = false;
    uint32_t rng_ = 0x2545f491;
    int shake_ = 0;                            // DAT_00465b02
    int camMinX_ = 0;                // DAT_00465b09
    int camMaxX_ = -1;               // DAT_00465b0b, -1 = the level's own limit
    Obj* boss_ = nullptr;            // DAT_00466507 (set) / trg+0x1f4 (the boss object)
    int outroTicks_ = 0;             // DAT_00465d49: 0 = off, counts up to 0x50 then auto-walks left
    int pistonPhase_ = 0, pistonTick_ = 0;   // DAT_0046650d / DAT_0046650e (0x40d9bd, every tick)
    uint8_t trgClass_[256] = {};              // DAT_004658c0
};

// Original physics constants (16.16 fixed point, per 60 Hz tick).
namespace physics {
constexpr int32_t kRunAccel      = 0x3000;    // FUN_00428dfa run state
constexpr int32_t kRunMax        = 0x30000;   // 3 px/tick
constexpr int32_t kRunMaxPower   = 0x40000;   // 4 px/tick with the chili power-up
constexpr int32_t kRunDecel      = 0x4000;    // FUN_004300e0 argument
constexpr int32_t kAirAccel      = 0x2000;
constexpr int32_t kGravity       = 0x5000;
constexpr int32_t kTerminalVy    = 12;        // integer px/tick (gravity stops at vy >= 12)
constexpr int32_t kJumpVy        = -0x4a000;
constexpr int32_t kJumpVyPower   = -0x70000;
constexpr int     kJumpHoldTicks = 9;         // DAT_00465aff = 9
constexpr int     kProbeDx       = -16;       // FUN_00425455: x - 0x10 (centre of the feet)
constexpr int     kProbeDy       = 14;        // FUN_00425455: y + 0xe
constexpr int     kDisplayOffset = 16;        // FUN_00434eef: display entry = object + 0x10
constexpr int     kTriggerDx     = 32;        // FUN_004251f9: trigger column of x + 0x20
constexpr int     kSndJump       = 0x8a;
constexpr int     kSndLand       = 0x88;
constexpr int     kDamageMax     = 0x96;      // FUN_00435c4e (100 on hard difficulty)
constexpr int     kDamageCap     = 0xa0;      // FUN_00428233
constexpr int     kInvulnTicks   = 0x46;      // DAT_0046652d after a hit
constexpr int     kHurtTicks     = 0x1e;      // DAT_00465b77
constexpr int     kWhipTicks     = 18;        // 9 frames x 2 ticks
constexpr int     kChiliTicks    = 0x380;     // item handler 0x405a5b
}

// Item handler (type 2, 0x405328) effect table at 0x45a8d4: sprite -> effect.
struct PickupEffect {
    const char* sprite;
    int score;
    int treasure;      // DAT_00466526 increment (50 = an extra continue)
    int sound;         // 0 = none
    int ammoSlot;      // -1 none, 0 sling stones, 1 boomerang, 2 stone
    int ammo;
    bool heal;         // fx_hrtbt: damage = 0
    bool chili;        // mi_chili: DAT_0046652b = 0x380
    bool life;         // mi_1up (awarded by the fly-to-HUD object, type 0xaa)
    bool letter;       // mi_ptfal: DAT_00465896 |= 1 << frame
};
constexpr PickupEffect kPickupEffects[] = {
    {"mi_stone", 600,   0, 75,  2, 2,  false, false, false, false},   // 0x40598d
    {"fxrbmrot", 500,   0, 0,   1, 1,  false, false, false, false},   // 0x4059b8
    {"mi_pouc2", 50,    0, 252, 0, 10, false, false, false, false},   // 0x405a22
    {"mi_chili", 600,   0, 254, -1, 0, false, true,  false, false},   // 0x405a5b
    {"mi_time",  750,   0, 0,   -1, 0, false, false, false, false},   // 0x405a88
    {"mi_1up",   2000,  0, 0x33, -1, 0, false, false, true, false},   // 0x4059ed
    {"mi_gold",  500,   5, 254, -1, 0, false, false, false, false},   // 0x4058b7
    {"mi_silvr", 200,   2, 254, -1, 0, false, false, false, false},   // 0x4058e2
    {"mi_ring",  1000, 10, 254, -1, 0, false, false, false, false},   // 0x40588c
    {"mi_coin",  100,   1, 253, -1, 0, false, false, false, false},   // 0x40590d
    {"fx_hrtbt", 500,   0, 251, -1, 0, true,  false, false, false},   // 0x405935
    {"mi_ptfal", 10000, 0, 36,  -1, 0, false, false, false, true},    // 0x405838
};

// Object behaviour classes (obj+8) with a known meaning; see docs/ENGINE_MAP.md.
enum ObjectType : uint8_t {
    kTypeItem        = 2,     // 0x405328: pickups (mi_*, fx_hrtbt heart, mi_stone ammo)
    kTypeWeb         = 8,     // 0x406bbc: spider webs (bouncy platforms)
    kTypeHawk        = 10,    // 0x406f62: condor / hawk
    kTypeSparkle     = 16,    // 0x40a2c8: fx_sprk1 / fx_sprk2
    kTypeBat         = 18,    // 0x40a4be
    kTypeRat         = 23,    // 0x40b5d2
    kTypeProjectile  = 29,    // 0x40d3b0: thrown cactus / bone
    kTypeMonkey      = 35,    // 0x40e394: monkey
    kTypeBossMeter   = 48,    // 0x41018c: boss health digits (spawned by the boss)
    kTypeJaguarBoss  = 55,    // 0x410ce6: the Ceiba Jungle jaguar
    kTypeBossDead    = 81,    // 0x415d15: a dead boss bursting into sparkles
    kTypeFallingRock = 76,    // 0x41514e
    kTypeWasp        = 102,   // 0x418992: stinging fly
    kTypeEffect      = 104,   // 0x418ec6: one-shot animation
    kTypeSnake       = 109,   // 0x418fd0
    kTypeSpider      = 110,   // 0x419622
    kTypeSkeleton    = 117,   // 0x41aade
    kTypeDeathCloud  = 130,   // 0x41b9d1
    kTypeScorpion    = 133,   // 0x41baa0
    kTypeJaguar      = 140,   // 0x41c4c2
    kTypeHeart       = 142,   // 0x41d520
    kTypeCoin        = 147,   // 0x41d876: mi_coin treasure
    kTypePouchTree   = 156,   // 0x41e132: pouch hanging in a tree
    kTypeOneUpFly    = 170,   // 0x41fd26
};

} // namespace game
