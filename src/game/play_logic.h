// Playable level logic: the main loop of the game logic (FUN_0043628c) with Harry's state
// machine (game/harry*.cpp), the level objects (game/objects.h) and the triggers.
//
// Harry and the globals of the Genesis-derived half live in PlayLogic::h_ (game/harry.h) at
// their original addresses; his state code is ported function by function from
// 0x426810 .. 0x431c99 (see harry_states.cpp).  Objects live in game/objects.h.
#pragma once
#include "game/game.h"
#include "game/harry.h"
#include "game/objects.h"

#include <vector>

namespace game {

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
    Harry& harry() { return h_; }
    const Harry& harry() const { return h_; }
    ObjectSystem& objectSystem() { return objects_; }
    const ObjectSystem& objectSystem() const { return objects_; }

    // Object records by original address (0x462854 + slot * 0x6e) and back; Harry's
    // record (0x465b0f) is not an Obj.
    Obj* objAt(uint32_t addr);
    uint32_t addrOf(const Obj* o) const;

    // --- the entry points of Harry's state code that the object handlers call ---
    // `call 0xXXXXXX` from a handler (a6 = Harry): the knock-backs 0x427148 / 0x42715b /
    // 0x427167, standing 0x428dfa, FUN_00428233 (hurt, d5 = amount) and the others listed in
    // harryCall (harry_states.cpp).
    void harryCall(uint32_t addr);
    void hurtHarry(int d5);                     // FUN_00428233
    void grabHarry(Obj& bat) { h4285d7(bat); }  // FUN_004285d7 (a5 = the bat)

    // --- services for the object handlers (game/objects.h) ---
    uint32_t random();                         // FUN_0043387e
    bool sound0(int id, int pri);              // FUN_004262d9 / FUN_0042633d: channel 0, by priority
    bool sound1(int id, int pri);              // FUN_004261e2: channel 1
    void sound1Now(int id);                    // FUN_004261b0
    uint8_t& scriptEvent() { return ch1Id_; } // DAT_004627c1: the last channel-1 sound (script op 0xfe)
    bool channel0Busy() const;                 // FUN_004262c8
    bool channel1Busy() const;                 // FUN_004261d1
    void healHarry() { h_.damage() = 0; }      // fx_hrtbt
    void startChili() { h_.w(0x46652b) = 0x380; }   // mi_chili (0x405a5b)
    void markCollected(const Obj& o);          // the .als entry stays cleared (it comes back after a death unless special)
    void markKilled(const Obj& o);             // 0x4081fc: the 0x465a40 list brings the entry back after 15 s
    void restoreEntry(const Obj& o);           // put the .als id back: the entry respawns when it scrolls back in
    bool freezeSparkleBlocked() const { return h_.b(0x465d4b) != 0; }   // DAT_00465d4b
    bool harryBusy() const { return h_.ul(0x465b7e) != 0; }             // DAT_00465b7e: on a bungee vine
    bool harryOnPlatform() const { return h_.b(0x465b73) != 0; }        // DAT_00465b73: standing on an object
    void standOn(int y, bool flagFirst = false);   // a platform holds Harry up (see play_logic.cpp)
    void carryHarry(int32_t vx16);             // add.l to Harry's x.frac (a moving platform)
    void pullLever(Obj& lever);                // 0x40c42d: Harry pulls (state 0x42a6e7)
    Trg trg(int x, int y) const;               // FUN_004251f9: the trigger at (x + 0x20, y)
    uint8_t triggerClassOf(uint8_t id) const { return h_.trgClass()[id]; }
    void clearTriggerClass(uint8_t id) { h_.trgClass()[id] = 0; }
    void setTriggerClass(uint8_t id, uint8_t cls) { h_.trgClass()[id] = cls; }
    Obj* harryBungee() { return objAt(h_.ul(0x465b7e)); }   // DAT_00465b7e: the bungee vine Harry hangs on
    void setShake(int v) { h_.w(0x465b02) = int16_t(v); }   // DAT_00465b02: vertical screen shake
    int shake() const { return h_.w(0x465b02); }
    void lockCameraLeft(int x) { h_.w(0x465b09) = int16_t(x); }    // DAT_00465b09
    int cameraLeftLock() const { return h_.w(0x465b09); }
    void lockCameraRight(int x) { h_.w(0x465b0b) = int16_t(x); }   // DAT_00465b0b
    int cameraRightLock() const { return h_.w(0x465b0b); }
    void setBoss(Obj* o) { boss_ = o; h_.b(0x466507) = o ? 0xff : 0; }   // DAT_00466507
    Obj* bossObject() const { return boss_; }
    bool bossActive() const { return h_.b(0x466507) != 0; }
    void startBossOutro() { h_.b(0x465d49) = 1; }   // DAT_00465d49 (FUN_0043426d)

private:
    friend struct HarryAccess;
    // ---- harry.cpp: the support code ----
    uint8_t mb(uint32_t va) const;              // the bytes of PITFALL.EXE at their original addresses
    uint16_t mw(uint32_t va) const;
    uint32_t ml(uint32_t va) const;
    void addX(int32_t v);                       // 16.16 add to x.frac / x
    void addY(int32_t v);
    void stopSound0();                          // FUN_0042624a
    void readPad();                             // FUN_004263f3
    void scriptedPad();                         // FUN_004346a7
    void padOverrides();                        // FUN_0043426d
    const ph::ColumnTable& det() const;
    const ph::ColumnTable& dt2() const;
    bool floorAt(bool plane2, bool ground, int x, int& y) const;
    bool feet();                                // FUN_00425455
    bool feetAt(int d0, int d1);                // FUN_0042547d
    bool snapFeet(bool plane2);                 // FUN_0042562b / FUN_004256a5
    bool climbableHere();                       // FUN_00422ee0
    bool ropeHere();                            // FUN_00422f93
    bool grabClimbable();                       // FUN_004253a8
    bool hAnim(uint32_t cursor, uint32_t script, int ticks);   // FUN_00423134 on Harry
    bool hAnimPeek(uint32_t cursor, uint32_t script);          // FUN_00423150
    bool hAnim2(uint32_t cursor, int ticks);                   // FUN_004237a5 on Harry
    uint8_t padBits() const;                    // FUN_00423f46
    Obj* newObjAtHarry(int d0, bool tail = false);   // FUN_004356bc (head of the list) / FUN_0043546e (tail)
    Obj* spawnTemplateAtHarry(uint32_t tmpl);   // FUN_004351c5 with a6 = Harry
    void notPorted(uint32_t addr);
    int& weaponSel() { return game_->stats().weapon; }          // DAT_0046650b
    int& ammo(int i) { return game_->stats().ammoCount[i]; }    // DAT_00466528 + i

    // ---- harry_states.cpp ----
    void harryUpdate();                         // FUN_00426810
    void runState();                            // call harryCont
    void leanShadow();                          // FUN_00426f67
    void harryLevelStart();                     // FUN_00427e30
    void levelRestart();                        // FUN_00432713
    void harryDeath();                          // FUN_00430a69
    void blinkTimer();                          // FUN_0042306c
    void selectWeapon();                        // FUN_00434932
    bool airAttack(uint32_t whip);              // 0x42c5c1 / 0x42ce31
    bool runAttack();                           // 0x42e333 / 0x42eb27
#include "game/harry_decls.inc"

    // ---- play_logic.cpp ----
    void updateCamera(Game& g);
    void spawnObjects(Game& g);
    void runObjects(Game& g);
    void timers();                              // FUN_00433a09
    void displayList(Game& g);                  // FUN_00434f6c

    Game* game_ = nullptr;
    int level_ = 0;                  // DAT_00466535
    Harry h_;                        // 0x465800 .. 0x4668ff
    ObjectSystem objects_;
    std::vector<uint8_t> als_;       // the working copy of the .als ids (0 = alive, collected or killed)
    struct Killed { uint8_t count = 0, id = 0; int als = -1; };
    Killed killed_[19];              // 0x465a40 .. 0x465ad8
    bool exiting_ = false;
    int deathTimer_ = 0;
    Obj* boss_ = nullptr;            // trg+0x1f4 (the boss object)
    // Sound channel state (FUN_004262d9 / FUN_004261e2).
    uint8_t ch0Pri_ = 0, ch0Id_ = 0, ch0Hold_ = 0;   // DAT_004627be / DAT_004627bf / DAT_004627c2
    uint8_t ch1Pri_ = 0, ch1Id_ = 0;                 // DAT_004627c0 / DAT_004627c1
    uint32_t tickCount_ = 0;         // DAT_004601d9 (incremented every tick of the main loop)
    uint32_t lastLandSound_ = 0;     // DAT_004601d5
    int altIdle_ = 0;                // DAT_0046d04c: F10 was pressed (the special idle animation)
    uint32_t fadeCmd_ = 0;           // DAT_0046b1b0
};

// Item handler (type 2, 0x405328) effect table at 0x45a8d4: sprite -> effect.

} // namespace game
