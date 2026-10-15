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

#include <span>
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
    // mem: the logic's memory (0x465800 .. 0x4668ff), the Game's, kept from level to level;
    // places: the object records' places (ObjPlace), the Game's too, taken and given back.
    PlayLogic(Harry& mem, ObjectSystem::Places& places) : h_(mem), places_(places) { objects_.loadPlaces(places_); }
    ~PlayLogic() override { objects_.savePlaces(places_); }
    PlayLogic(const PlayLogic&) = delete;
    PlayLogic& operator=(const PlayLogic&) = delete;
    void enter(Game& g) override;               // FUN_00435fc0 / FUN_0043256f: a level starts
    void tick(Game& g) override;
    void restart(Game& g) override;             // FUN_004360e9 / FUN_00432615: "*3", again at the checkpoint
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
    bool hurtHarry(int d5);                     // FUN_00428233 (true: past its early returns)
    void grabHarry(Obj& bat) { h4285d7(bat); }  // FUN_004285d7 (a5 = the bat)
    // FUN_00425455 with DAT_00465b70 held at 0xff around it (0x4221f7): the floor under Harry's
    // feet (y snaps to it); true when there is one.
    bool harryFeetGrounded() { const uint8_t g = h_.onGround(); h_.onGround() = 0xff; const bool r = feet(); h_.onGround() = g; return r; }

    // --- services for the object handlers (game/objects.h) ---
    uint32_t random();                         // FUN_0043387e
    bool sound0(int id, int pri);              // FUN_004262d9 / FUN_0042633d: channel 0, by priority
    bool sound1(int id, int pri);              // FUN_004261e2: channel 1
    void sound1Now(int id);                    // FUN_004261b0
    uint8_t& scriptEvent() { return ch1Id_; } // DAT_004627c1: the last channel-1 sound (script op 0xfe)
    bool channel0Busy() const;                 // FUN_004262c8
    bool channel1Busy() const;                 // FUN_004261d1
    void stopChannel0() { stopSound0(); }      // FUN_0042624a
    uint8_t channel0Id() const { return ch0Id_; }   // DAT_004627bf: the last channel-0 sound
    void clearChannel0Hold() { ch0Hold_ = 0; }      // DAT_004627c2 = 0: the next channel-0 sound plays at once
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
    void setBoss(Obj* o);                       // trg+0x1f4 (big-endian) and DAT_00466507
    void endBoss() { h_.b(0x466507) = 0; }        // (trg+0x1f4 keeps pointing at the dead boss)
    Obj* bossObject() const { return boss_; }
    bool bossActive() const { return h_.b(0x466507) != 0; }
    void startBossOutro() { h_.b(0x465d49) = 1; }   // DAT_00465d49 (FUN_0043426d)
    // A handler `call`ed code that never returns (it ends with `jmp 0x405313`): the return
    // address stays on the stack, and the `ret` that ends the object pass runs the code there
    // before the pass really returns (see runStrayReturns).
    void strayReturn(uint32_t addr) { strayReturns_.push_back(addr); }
    void hudAdd(const Obj& o) { hud_.push_back(objects_.indexOf(o)); }   // 0x4150a2: FUN_0043348e places it next tick
    uint32_t& fadeCmd() { return game_->brightness(); }   // DAT_0046b1b0 (0xc0: the big stone's white flash)
    // [DAT_004504e4] + off: the Genesis variables the PC keeps in the .als buffer past the level's
    // data (+0x384: the Balankanche ride's last obstacle, the map screen's flags).  The buffer is
    // the working copy (0x800 bytes) and the level's own copy after it, the 4 KB a vortex warp
    // saves (0x4288a2).
    uint32_t alsLong(size_t off) const;
    void setAlsLong(size_t off, uint32_t v);
    void setAlsByte(size_t off, uint8_t v) { if (off < alsMem_.size()) alsMem_[off] = v; }
    // [DAT_00450510] + off: the triggers' record in the load block, where the PC keeps Genesis RAM
    // (the jaguar's distance at 0x1ea; from 0x1f4 the boss pointer, the vaults' ceiling, timers and
    // Harry's squash script), over the level's trigger lists, or past a short record over its
    // copy and the tables after it.  The 68000 code reads it big-endian; tln() is the native long
    // FUN_004237a5 reads.
    uint8_t& tb(int off) { return trgByte(off); }
    uint16_t tw(int off) const { return uint16_t(trgAt(off) << 8 | trgAt(off + 1)); }
    void setTw(int off, uint16_t v) { trgByte(off) = uint8_t(v >> 8); trgByte(off + 1) = uint8_t(v); }
    uint32_t tl(int off) const { return uint32_t(tw(off)) << 16 | tw(off + 2); }
    void setTl(int off, uint32_t v) { setTw(off, uint16_t(v >> 16)); setTw(off + 2, uint16_t(v)); }
    uint32_t tln(int off) const { return uint32_t(trgAt(off)) | uint32_t(trgAt(off + 1)) << 8 | uint32_t(trgAt(off + 2)) << 16 | uint32_t(trgAt(off + 3)) << 24; }
    void setTln(int off, uint32_t v) { for (int i = 0; i < 4; ++i) trgByte(off + i) = uint8_t(v >> (8 * i)); }
    // FUN_00434a30: spell `text` out in letter objects of `type` (ltr_lg / ltr_md / ltr_sm, one
    // object a glyph), each line centred on x + 0xa0; 0xf0 starts a line 0x19 lower, 0xf1 centres
    // the next one.
    void spawnText(const char* text, int x, int y, uint8_t type);

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
    bool hAnimAt(uint8_t* c, uint32_t script, int ticks);      // ... with the cursor anywhere
    bool hAnimPeekAt(uint8_t* c, uint32_t script);
    bool hAnim2At(uint8_t* c, int ticks);
    uint8_t textGlyph(uint8_t ch, uint32_t& sprite);           // 0x434d2d
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
    void levelStart();                          // FUN_0043256f
    void levelSetup();                          // FUN_0043305f
    void levelVars();                           // FUN_00433116
    void startPosition();                       // FUN_004331d8
    void restartAtCheckpoint();                 // FUN_00432615
    void levelRestart();                        // FUN_00432713
    void resetObjects();                        // FUN_00435274
    void restartBody();                         // 0x432723: what the three share
    void harryDeath();                          // FUN_00430a69
    void blinkTimer();                          // FUN_0042306c
    void selectWeapon();                        // FUN_00434932
    bool airAttack(uint32_t whip);              // 0x42c5c1 / 0x42ce31
    bool runAttack();                           // 0x42e333 / 0x42eb27
#include "game/harry_decls.inc"

    // ---- scroll.cpp: the camera and the object spawner ----
    void camera();                              // FUN_00404714
    void cameraRideVertical();                  // 0x404cf1
    void scrollLeft(int16_t d0);                // 0x404dcb
    void scrollRight(int16_t d0);               // 0x404ea2
    void scrollUp(int16_t d0);                  // 0x404f75
    void scrollDown(int16_t d0);                // 0x40503e
    void spawnLeftColumn();                     // FUN_00403fa0
    void spawnRightColumn();                    // FUN_00404016
    void spawnTopRow();                         // FUN_00404269
    void spawnBottomRow();                      // FUN_004042fe
    void spawnColumn(uint16_t col);             // 0x40407f
    void spawnRow(int16_t row);                 // 0x40438e
    void spawnEntry(Obj& o, size_t entry, const ph::ObjectTemplate& t);   // FUN_0040454e
    void spawnAll();                            // FUN_00405114
    void preSpawn();                            // FUN_00433d0b
    uint16_t alsWord(size_t off) const;         // big-endian, 0 past the data
    uint8_t alsByte(size_t off) const;          // 0xff past the data

    // ---- play_logic.cpp ----
    void hudPlacement();                        // FUN_0043348e
    void runObjects(Game& g);
    void runStrayReturns();                     // the code at the return addresses left by strayReturn
    void timers();                              // FUN_00433a09
    void displayList(Game& g);                  // FUN_00434f6c
    // FUN_00434eef: a display entry at (x + 0x10, y + 0x10 - DAT_00465b02); none for a frame of
    // 0x80 or more, without a sprite, or with the 80 entries of the list taken.
    void displayEntry(int16_t x, int16_t y, uint8_t frame, uint8_t facing, uint8_t vflip, uint16_t flags, const ph::Sprite* spr);
    void displayEntry(const Obj& o) { displayEntry(o.x, o.y, o.frame, o.facing, o.vflip, o.flags, o.sprite); }
    void digits(int32_t v);                     // FUN_00431a3f: v in decimal, a digit a byte at 0x466556 ..

    // ---- atari2600.cpp: level 20, the Atari 2600 Pitfall! (0x401e00 .. 0x403f92) ----
    void a26Start();                            // FUN_00401e00, on into 0x401f3c
    void a26Screen();                           // 0x401f3c: a screen starts, on into 0x4020d1
    void a26Tick();                             // 0x4020d1
    void a26Deferred(uint32_t addr);            // `jmp [DAT_00457408]`
    void a26Show();                             // FUN_004024ee, FUN_0040254e and FUN_00435174
    void a26Copyright();                        // FUN_0040218b
    void a26Hud();                              // FUN_0040254e
    void a26Glyph(uint32_t frame, int d6, int d7);   // FUN_00402526 (FUN_00434e8a)
    void a26Objects();                          // FUN_0040291c
    void a26Populate();                         // FUN_004031f7
    bool a26Harry();                            // FUN_004036fc
    bool a26Move();                             // FUN_00403b0d
    void a26Trip();                             // FUN_00403f68
    void a26Exit(const char* cmd);              // engineExit (0x4361d6)
    const uint8_t* a26Room() const;             // [DAT_00465e9e]: the screen's descriptor
    void a26ClearButtons();
    void a26Frame(Game& g, int entry);          // level 20 in FUN_0043628c (0) / FUN_004360e9 (1) / FUN_00435fc0 (2)
    void logicTick(Game& g, bool& usual);       // FUN_0043628c's body (usual: past the special levels' paths)
    void epilogue(Game& g);                     // 0x4363bd: the end every path of FUN_0043628c shares
    void meterLive(Game& g);                    // FUN_00435c4e's reads of the logic at the HUD's draw

    // ---- cutscene.cpp: levels 21 .. 23, the intro, the reunion and the end (0x401000 .. 0x401dff) ----
    void cutFrame(Game& g, int entry);          // levels 21 .. 23 in FUN_0043628c (0) / FUN_004360e9 (1) / FUN_00435fc0 (2)
    void cutTick();                             // FUN_00401071 / FUN_00401176 / FUN_00401215
    void cutStart(uint32_t script);             // what the three set-ups share
    bool cutSkip();                             // FUN_00401000: a key skips the scene
    void cutScript();                           // the script's opcode, and those it goes on into
    void cutRoutine(uint32_t a);                // the routine of the actor at a ([a + 0x1c])
    void cutAnim(uint32_t a);                   // 0x401dbc: the actor's frame script
    void cutReady(uint32_t a);                  // the runner stands (hyiready), flag 0 up
    uint32_t cutClear(uint32_t i);              // 0x4015b6: actor i cleared, its address
    void cutClearAll();
    uint32_t cutLen(uint32_t va);               // strlen
    bool afterDemos() const;                    // DAT_0046fb84
    // The scenes' data at its original addresses (0x456000 .. 0x4569df, 0x4699a4 .. 0x46a859),
    // little-endian; Game::cutsceneData keeps it.
    uint8_t& cb(uint32_t va);
    uint32_t cl(uint32_t va);
    void setCl(uint32_t va, uint32_t v);
    uint16_t cw(uint32_t va);
    void setCw(uint32_t va, uint16_t v);

    // ---- map_screen.cpp: level 19 (0x431c99, FUN_00432038) ----
    void mapSetup();                            // 0x431c99, the end of the restart on level 19
    void mapTick();                             // FUN_00432038
    void mapArrived();                          // 0x4323b4
    void mapAmbient();                          // FUN_00431a67
    void mapPeg(Obj& o, uint32_t va);           // a peg of the path at va
    void mapText(const char* s, int y);         // FUN_00431ad5
    void mapExit();                             // "*1"
    void listAll();                             // FUN_00435174: Harry, then the objects from the tail
    uint32_t fixed(uint32_t a) const;           // a 16.16 value kept int word first
    void setFixed(uint32_t a, uint32_t v);

    Game* game_ = nullptr;
    int level_ = 0;                  // DAT_00466535
    Harry& h_;                       // 0x465800 .. 0x4668ff (Game::logicMemory)
    ObjectSystem::Places& places_;   // the records' places between levels (Game::objectPlaces)
    ObjectSystem objects_;
    std::vector<int> hud_;           // 0x466874: the records 0x4150a2 listed in the last object pass
    std::span<uint8_t> alsMem_;      // [DAT_004504e4], 4 KB of the load block: the level's .als data as the spawner works on it (an id is 0 while its object lives, or once collected or killed), then at +0x800 the level's own copy ([DAT_004504c4])
    struct Killed { uint8_t count = 0, id = 0; int als = -1; };   // als: the entry's offset in alsMem_
    Killed killed_[19];              // 0x465a40 .. 0x465ad8
    bool exiting_ = false;
    int deathTimer_ = 0;
    Obj* boss_ = nullptr;            // trg+0x1f4 (the boss object)
    // Sound channel state (FUN_004262d9 / FUN_004261e2).
    uint8_t ch0Pri_ = 0, ch0Id_ = 0, ch0Hold_ = 0;   // DAT_004627be / DAT_004627bf / DAT_004627c2
    uint8_t ch1Pri_ = 0, ch1Id_ = 0;                 // DAT_004627c0 / DAT_004627c1
    uint8_t& trgByte(int off);                  // [DAT_00450510] + off in the load block
    uint8_t trgAt(int off) const;
    std::vector<uint32_t> strayReturns_;   // return addresses left on the stack during the object pass
    // The 2600 level's own globals outside Harry's memory.
    uint32_t a26Deferred_ = 0;       // DAT_00457408: 1 = waiting for the first input, else the code to run when ...
    int32_t a26Wait_ = 0;            // DAT_0045740c: ... this countdown runs out
    int32_t a26End_ = 0;             // DAT_00457410: the game is over; the ticks before a button can leave
    bool specialExited_ = false;     // engineExit was taken this tick (the 2600 level, the map screen)
    int32_t mapEnd_ = 0;             // DAT_004601c0: the ticks the map waits at the end of its path
    uint32_t mapNamed_ = 0;          // DAT_0046a87c: the next level's name is up
    uint32_t mapKeysOff_ = 0;        // DAT_0046a88a: the ticks before the buttons count (the map, the cutscenes)
    bool cutExited_ = false;         // a cutscene's actor 9 took engineExit this tick
    uint32_t endTimer_ = 0;          // DAT_0046b1b4: level 24's countdown to "*6" (ew_ready was hit)
public:
    void startEnding() { endTimer_ = 200; }   // 0x40e1d7
};

// Item handler (type 2, 0x405328) effect table at 0x45a8d4: sprite -> effect.

} // namespace game
