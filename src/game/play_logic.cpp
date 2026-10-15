#include "game/play_logic.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace game {

PlayLogic* g_debugLogic = nullptr;   // the most recent PlayLogic, for the debugging tools

namespace {
inline bool neg(int v) { return int16_t(v) < 0; }     // the sign test of a 16-bit cmp (js)
}

// ---------------------------------------------------------------- object records by address

Obj* PlayLogic::objAt(uint32_t addr) {
    // An object pointer kept in Harry's memory (0 / -1 / -2: none).  Like the original's, it
    // leads to the record even when that has been freed (or handed out again) since.
    if (addr < kObjPoolAddr) return nullptr;
    const uint32_t i = (addr - kObjPoolAddr) / kObjSize;
    if ((addr - kObjPoolAddr) % kObjSize != 0 || i >= uint32_t(ObjectSystem::kPoolSize)) return nullptr;
    return &objects_.record(int(i));
}

uint32_t PlayLogic::addrOf(const Obj* o) const {
    if (!o) return 0;
    return kObjPoolAddr + uint32_t(objects_.indexOf(*o)) * kObjSize;
}

// ---------------------------------------------------------------- level start

void PlayLogic::enter(Game& g) {
    // FUN_00435fc0, the engine's bridge into the game logic, with FUN_0043256f: a level starts.
    // An engine exit in its start returns from it (engineExit 0x4361d6); its tick is a bridge of
    // its own (FUN_0043628c), after which the end runs once more.
    g_debugLogic = this;
    game_ = &g;
    level_ = g.levelNumber();
    g.bridge([&] {
        // (the logic's memory keeps what the last level, or the last game, left in it)
        // [DAT_004504e4]: the .als working copy (0x800 bytes, zero past the data) and the level's
        // own copy after it ([DAT_004504c4], what a lost life restores from), as the level's load
        // set them up in the load block.
        alsMem_ = std::span<uint8_t>(g.arena().data() + g.arenaLayout().als, 0x1000);
        exiting_ = false;
        deathTimer_ = 0;
        boss_ = nullptr;
        h_.l(0x466535) = level_;
        endTimer_ = 0;                                             // DAT_0046b1b4
        g.state().displayList.clear();                             // DAT_0046aa90 = 0x46aa94
        if (level_ >= kIntro && level_ <= kCredits) { cutFrame(g, 2); return; }   // FUN_00435fc0: 0x401071 / 0x401176 / 0x401215
        if (level_ == kAtari2600Level) { a26Frame(g, 2); return; }   // FUN_00435fc0: 0x401e00
        if (g.warp().fromAtari) h_.b(0x465b39) = 0xff;            // (before the level start: the checkpoint stays)
        levelStart();                                              // FUN_0043256f
        g.warp().fromAtari = g.warp().fromVault = false;           // DAT_004601c4 / DAT_004504cc
        g.state().hudSlide = h_.uw(0x4667a8);                      // 0x4360d3: DAT_00451940 for the tick
        tick(g);                                                   // 0x4360df: a logic tick
        epilogue(g);                                               // 0x4360e4: and its end once more
    });
}

void PlayLogic::restart(Game& g) {
    // FUN_004360e9 ("*3"): the level logic starts again at the checkpoint; the level is not
    // loaded again, so the .als working copy keeps what FUN_00430eb6 left in it.  A bridge as
    // FUN_00435fc0.
    game_ = &g;
    g.bridge([&] {
        endTimer_ = 0;                                             // DAT_0046b1b4
        g.state().displayList.clear();                             // DAT_0046aa90 = 0x46aa94
        if (g.levelNumber() >= kIntro && g.levelNumber() <= kCredits) { cutFrame(g, 1); return; }
        if (g.levelNumber() == kAtari2600Level) { a26Frame(g, 1); return; }   // 0x401f3c
        deathTimer_ = 0;
        restartAtCheckpoint();                                     // FUN_00432615
        g.warp().fromAtari = g.warp().fromVault = false;
        g.state().hudSlide = h_.uw(0x4667a8);                      // 0x4361c0
        tick(g);                                                   // 0x4361cc: a logic tick
        epilogue(g);                                               // 0x4361d1
    });
}

void PlayLogic::levelStart() {
    // FUN_0043256f
    h_.b(0x465adf) = 0;
    levelSetup();                                              // FUN_0043305f
    std::memset(game_->levelFlags(), 0, 0x14);                 // DAT_00466510 .. 23 (24 / 25 last the game)
    h_.b(0x465b00) = 0;
    h_.w(0x465afb) = 0;                                        // the vortex Harry went through
    h_.b(0x465b3c) = 0;
    restartAtCheckpoint();                                     // (runs on into FUN_00432615)
}

void PlayLogic::levelSetup() {
    // FUN_0043305f: the level's size, FUN_00433116, the start position, Harry's record.
    const ph::LevelData& lv = game_->level();
    h_.w(0x4658ac) = int16_t(lv.hasBg ? lv.bg.wTiles / 2 : 0x28);    // DAT_00450fb4: the size in cells
    h_.w(0x4658ae) = int16_t(lv.hasBg ? lv.bg.hTiles / 2 : 0xe);     // DAT_00450fb0
    h_.w(0x465bae) = int16_t((h_.w(0x4658ac) << 4) - 0x20);         // and in pixels
    h_.w(0x465bb0) = int16_t((h_.w(0x4658ae) << 4) - 0x20);
    h_.w(0x466539) = 0xfa;
    levelVars();                                               // FUN_00433116
    startPosition();                                           // FUN_004331d8
    h_.uw(0x465b1b) = 0x6000;
    h_.setSprite("hyirun");
    h_.frame() = 1;
    h_.w(0x465b35) = 0;
    h_.b(0x465b17) = 0xff;
    resetObjects();                                            // FUN_00435274
    h_.b(0x466555) = 0;
}

void PlayLogic::levelVars() {
    // FUN_00433116: the sounds stop, the killed-enemy list, the objects and the HUD list start afresh.
    h_.b(0x465b72) = 0;
    h_.w(0x46652b) = 0;
    h_.w(0x46652f) = 0;
    stopSound0();                                              // FUN_0042624a
    sound1Now(0x89);                                           // FUN_004261b0
    ch1Id_ = 0;                                                // DAT_004627c1
    ch0Hold_ = 0;                                              // DAT_004627c2
    h_.b(0x465e64) = 0;
    for (Killed& k : killed_) k = Killed{};                    // 0x465a40 .. 0x465ad8
    h_.uw(0x465e6c) = 0xffff;
    h_.setUl(0x465e68, 0x465e6c);
    resetObjects();                                            // FUN_00435274
    h_.b(0x466552) = 0;
    h_.b(0x466553) = 0;
    h_.w(0x465b02) = 0;
    hud_.clear();                                              // DAT_00466870 = 0x466874
    h_.w(0x466874) = 0;
}

void PlayLogic::startPosition() {
    // FUN_004331d8: Harry at the '^' trigger (x = col * 16 - 8, y = (row + 2) * 16) and the
    // checkpoint there too, unless he comes out of a vortex (DAT_00465b39); the facing from
    // the table at 0x433334 (one byte a level, read past its end for the levels after 18).
    h_.w(0x4667a2) = 0;                                        // no marker lit
    h_.w(0x466782) = 0;
    const ph::ColumnTable& trg = game_->level().triggers;
    for (int c = 0; c < uint16_t(h_.w(0x4658ac)) && c < trg.columns(); ++c, h_.w(0x466782) += 1) {
        for (const ph::ColumnEntry& e : trg.column(c)) {
            if (e.id != ph::kTrgStart) continue;
            const int16_t x = int16_t(c * 16 - 8), y = int16_t((e.row + 2) * 16);
            if (h_.b(0x465b39) == 0) h_.w(0x46679e) = x;
            h_.x() = x;
            h_.y() = y;
            if (h_.b(0x465b39) == 0) h_.w(0x4667a0) = y;
            const uint8_t f = level_ >= 0 && level_ < 25 ? ph::kStartFacing[level_] : 0;
            h_.b(0x4667a4) = f;
            h_.facing() = f;
            return;
        }
    }
}

void PlayLogic::restartAtCheckpoint() {
    // FUN_00432615: Harry at the checkpoint (DAT_0046679e / a0 / a4), or where a vortex warp
    // brings him back to with the level as he left it (FUN_004288ec saved it).
    Game::Warp& wp = game_->warp();
    h_.x() = h_.w(0x46679e);
    h_.y() = h_.w(0x4667a0);
    h_.facing() = h_.b(0x4667a4);
    h_.b(0x465b01) = 0;
    auto restoreSaved = [&] {
        if (wp.als.size() == 0x1000) std::copy(wp.als.begin(), wp.als.end(), alsMem_.begin());   // 0x4601dd
        h_.w(0x46679e) = wp.cpX;                               // DAT_0046a860 / 62 / 66
        h_.w(0x4667a0) = wp.cpY;
        h_.w(0x4667a4) = wp.cpFacing;
    };
    if (wp.fromAtari) {                                        // DAT_004601c4: out of the 2600 level
        wp.fromAtari = false;
        h_.x() = 0x6e5;
        h_.y() = 0x587;
        h_.facing() = 0xff;
        restoreSaved();
    }
    if (wp.fromVault) {                                        // DAT_004504cc: out of a vault
        if (wp.als.size() == 0x1000) std::copy(wp.als.begin(), wp.als.end(), alsMem_.begin());
        h_.x() = wp.x;
        h_.y() = wp.y;
        h_.facing() = wp.facing;
        h_.b(0x465b39) = 0xff;                                 // he twirls out of the vortex (FUN_0042a064)
        h_.w(0x46679e) = wp.cpX;
        h_.w(0x4667a0) = wp.cpY;
        h_.w(0x4667a4) = wp.cpFacing;
    }
    h_.w(0x4667a8) = 0x30;
    restartBody();
}

void PlayLogic::levelRestart() {
    // FUN_00432713: into another area of the level, the objects start again.
    h_.w(0x4667a8) = 0;
    h_.facing() = 0;
    restartBody();
}

void PlayLogic::resetObjects() {
    // FUN_00435274: the object pool, Harry's state fields and the killed-enemy list.
    h_.b(0x465ae4) = 0; h_.l(0x465ae7) = 0;
    h_.frame() = 0xff;
    h_.setUl(0x465898, 0);
    h_.b(0x465d49) = 0;
    objects_.reset();
    h_.b(0x465b35) = 0;
    h_.w(0x465b35) = 0x6663;                               // FUN_004263a6 on Harry
    for (uint32_t a = 0x465b41; a < 0x465b69; a += 4) h_.l(a) = 0;
    for (uint32_t a = 0x466808; a < 0x46681c; a += 4) h_.l(a) = 0;
    h_.l(0x46681c) = 0;
    for (Killed& k : killed_) k = Killed{};                // 0x465a40 .. 0x465ad8
    h_.b(0x466554) = 0xff;
}

void PlayLogic::restartBody() {
    // 0x432723: the part FUN_0043256f / FUN_00432615 and FUN_00432713 share (with the map
    // screen's set-up at its end, 0x431c99).
    h_.b(0x466555) = 0xff;
    h_.b(0x465b38) = 0;
    h_.b(0x465bb3) = 0;
    h_.onGround() = 0xff;
    resetObjects();                                        // FUN_00435274
    h_.w(0x465b02) = 0;
    h_.vx() = 0; h_.vy() = 0;
    h_.setUl(0x465b7e, 0);
    h_.l(0x466796) = 0;
    h_.b(0x465c56) = 0;
    h_.w(0x4658bc) = 0;
    h_.l(0x466868) = 0;
    harryLevelStart();                                     // FUN_00427e30
    preSpawn();                                            // FUN_00433d0b
    if (h_.cont() == 0x42a217) {
        h42a1a1();
    } else {
        h_.l(0x465b45) &= 0xffff;
        h_.setCont(0x428e50);
        h429498();
        if (level_ == 0xe) h42989c();
    }
    if (h_.b(0x465b39)) h42a064();
    h_.b(0x465aff) = 0;
    h_.b(0x465b72) = 0;
    h_.uw(0x465e6c) = 0xffff;
    h_.b(0x465b79) = 5;
    h_.w(0x465b92) = 0;
    h_.b(0x466547) = 0;
    h_.l(0x465cf8) = 0;
    h_.b(0x465b76) = 0;
    h_.setUl(0x465892, 0xffffffff);
    h_.b(0x465b71) = 0;
    h_.b(0x4667ae) = 0;
    h_.w(0x465cf2) = 0x200;
    h_.l(0x46617c) = 0;
    h_.l(0x466180) = 0;
    // The trigger classes (0x461f8e), with the doors of pulled levers opened again.
    std::memcpy(h_.trgClass(), ph::kTriggerClass, 0xff);
    const uint8_t* f = game_->levelFlags();
    static const struct { uint8_t flag, id; } kOpen[] = {
        {0x12, 0x4d}, {0x13, 0x4e}, {0x14, 0x51}, {0x15, 0x5d}, {0x06, 0x45}, {0x07, 0x46}, {0x08, 0x47},
        {0x09, 0x48}, {0x04, 0x53}, {0x05, 0x55}, {0x0a, 0x57}, {0x0b, 0x59}, {0x0c, 0x5b}};
    for (const auto& e : kOpen) if (f[e.flag]) h_.trgClass()[e.id] = 0;
    for (int i = 0; i < 4; ++i) h_.trgClass()[0x90 + i] = f[3 - i];   // the long at 0x466510, byte-swapped
    h_.b(0x46650d) = 0; h_.b(0x46650e) = 0;
    h_.w(0x465b9e) = 0;
    h_.w(0x465b04) = 0x14;
    h_.w(0x4658a6) = 0;
    h_.onGround() = 0xff;
    h_.l(0x46588c) = 0;
    h_.b(0x465b97) = 0;
    h_.w(0x465b9a) = 0;
    h_.w(0x46652b) = 0; h_.w(0x46652d) = 0; h_.w(0x46652f) = 0;
    h_.b(0x466507) = 0;
    h_.b(0x46650a) = 0;
    h_.b(0x46618a) = 0;
    h_.w(0x465cf0) = 0x96;
    if (game_->warp().fromVault) {                         // DAT_004504cc: the classes as the level was left
        game_->warp().fromVault = false;
        std::memcpy(h_.trgClass(), game_->warp().classes, 0x100);   // 0x4611dd
    }
    h_.b(0x466795) = 6;
    h_.b(0x465d49) = 0;
    if (level_ == 7) h_.b(0x465d49) = 1;
    if (level_ == 4 && !neg(h_.x() - 0xbe9) && neg(h_.x() - 0xc29)) {
        for (Obj* p : objects_.list()) if (p->type == 0x20) { objects_.free(*p); break; }
    }
    if (level_ == 0 || level_ == 5) {
        if (level_ == 5) h_.b(0x466507) = 0xff;
        h_.l(0x465c6e) = int32_t((random() & 0x3f) + 0x10);
    }
    if (level_ == 6 && neg(h_.x() - 0x5b)) { h_.y() = 0xb4; h42abee(); }   // the level starts on a zip line
    if (level_ == 10) h_.b(0x465d49) = 1;
    h_.b(0x466794) = 0x3c;
    spawnAll();                                            // FUN_00405114
    if (level_ == 0xb || level_ == 0xc || level_ == 0xd) {
        // 0x432bab: a Loltun vault.  Harry's state for the room, its RAM in the trigger buffer
        // (the ceiling at 240), the key queue at 0x4661a8, the letters' count, Harry at x 0x34
        // and the type 107 object that writes the instructions.
        h_.setCont(0x42756d);
        h_.b(0x465b39) = 0;
        setTl(0x1f4, 0); setTl(0x1f8, 0); setTl(0x1fc, 0); setTl(0x212, 0);
        setTw(0x21c, 0); setTw(0x2ee, 0); setTl(0x2f0, 0);
        h_.l(0x465b41) = 0; h_.l(0x465b45) = 0; h_.l(0x465b49) = 0;
        h_.setUl(0x4661a0, 0x4661a8); h_.setUl(0x4661a4, 0x4661a8); h_.b(0x4661a8) = 0;
        h_.l(0x4661cc) = 0;
        h_.b(0x4661d6) = 0;
        h_.x() = 0x34;
        if (Obj* a5 = newObjAtHarry(1, true)) a5->type = 0x6b;   // FUN_0043546e(1)
        setTl(0x1f4, 0x00f00000);
        setTl(0x1f8, 0);
        h_.b(0x466788) = 0; h_.w(0x46678c) = 0;
        h_.b(0x465d48) = 0x14;
        setTl(0x1fc, 0);
    }
    h_.b(0x465b39) = 0;
    h_.b(0x46650c) = 0;
    h_.l(0x466796) = 0;
    h_.b(0x466552) = 0;
    h_.b(0x466553) = 0;
    if (level_ == kMapScreen) mapSetup();                  // 0x432cd6: the map screen's set-up
}

// ---------------------------------------------------------------- the main loop (FUN_0043628c)

void PlayLogic::tick(Game& g) {
    // FUN_0043628c, a bridge of its own: an engine exit anywhere in the tick returns from it at
    // once, without the rest of the tick and its end (0x4363bd).
    game_ = &g;
    level_ = g.levelNumber();
    bool usual = false;
    g.bridge([&] { logicTick(g, usual); });
    if (!usual) return;
    // What the engine reads of the logic as it draws the frame (after the tick, its end or not).
    g.state().vaultCeiling = int16_t(tw(0x1f4));
    meterLive(g);
}

void PlayLogic::logicTick(Game& g, bool& usual) {
    FrameState& fs = g.state();
    h_.w(0x4667a8) = int16_t(fs.hudSlide);                 // 0x4362ad: the slide as the engine has it (the pre-ticks put it back)
    if (fs.joypad == 0xff) { g.command("*2"); return; }    // 0x4362cd: a demo ended (a key, or its script)
    if (level_ == kAtari2600Level) { a26Frame(g, 0); return; }   // 0x436224: FUN_004263f3, 0x4020d1
    if (level_ >= kIntro && level_ <= kCredits) {              // 0x436233 ..: FUN_004263f3, then the scene
        fs.displayList.clear();
        cutFrame(g, 0);
        return;
    }
    if (level_ == kEnding && endTimer_ != 0 && --endTimer_ == 0) {   // 0x436260: ew_ready was hit 200 ticks ago
        fs.displayList.clear();
        g.command("*6");
        return;
    }
    if (level_ == kMapScreen) {                            // 0x436215: FUN_004263f3, FUN_00432038
        fs.displayList.clear();
        specialExited_ = false;
        readPad();
        mapTick();
        if (!specialExited_) epilogue(g);                  // (engineExit skips it)
        return;
    }
    usual = true;
    const bool debug = (fs.joypad & 0x100) != 0;
    readPad();                                             // FUN_004263f3
    camera();                                              // FUN_00404714 (with the spawner)
    harryUpdate();                                         // FUN_00426810
    if (!debug) hudPlacement();                            // FUN_0043348e
    if (level_ != 7) displayList(g);                       // FUN_00434f6c
    runObjects(g);                                         // FUN_00405170
    timers();                                              // FUN_00433a09
    if (!debug) {
        blinkTimer();                                      // FUN_0042306c
        // FUN_004348f1: the controls are dead for the first ticks of a level
        if (h_.b(0x466795) != 0) {
            h_.b(0x466795) -= 1;
            for (uint32_t a : {0x466768u, 0x466765u, 0x466766u, 0x466761u, 0x466762u, 0x466763u, 0x466764u}) h_.b(a) = 0;
        }
        selectWeapon();                                    // FUN_00434932
        leanShadow();                                      // FUN_00426f67
        h42d2ec();                                         // FUN_0042d2ec: the rope sprites
        harryDeath();                                      // FUN_00430a69
    }
    if (level_ == 7) displayList(g);
    epilogue(g);                                           // (an engine exit never gets here)
}

void PlayLogic::epilogue(Game& g) {
    // 0x4363bd: the camera for the engine, the tick count, then the numbers the HUD shows.
    // (DAT_004517f4 = DAT_00465cf6 is never read; DAT_004517e8 = DAT_0046652f, the stop-watch,
    // stills the water of FUN_00443370 / FUN_00437ba0.)
    FrameState& fs = g.state();
    fs.scrollX = h_.w(0x4658b4);
    fs.scrollY = h_.w(0x4658b6);
    fs.camX = fs.scrollX;
    fs.camY = fs.scrollY;
    fs.bob = h_.w(0x465b02);
    fs.stopWatch = h_.w(0x46652f);                         // DAT_004517e8
    ++g.logicTicks();                                      // DAT_004601d9
    if (g.invincible()) h_.damage() = 0;                   // DAT_0046d054
    // 0x436435: 50 treasures make a continue (at most 9), the lives are 1 .. 10 (none left:
    // 10), the ammunition at most 99.  (The score is shown up to 999999, DAT_00451914.)
    PlayerStats& st = g.stats();
    while ((st.treasures & 0xff) >= 0x32) {
        st.treasures = uint8_t(st.treasures - 0x32);
        st.continues += 1;
        if (st.continues >= 0xa) st.continues = 9;
        sound1Now(0x22);
    }
    if (uint32_t(st.lives - 1) >= 0xa) st.lives = 0xa;
    fs.hudScore = std::min(uint32_t(st.score), 999999u);  // DAT_00451914
    fs.hudTreasures = st.treasures & 0xff;                 // DAT_00451a28
    fs.hudLives = st.lives - 1;                            // DAT_00451804
    fs.hudWeapon = st.weapon;                              // DAT_00451938
    fs.hudAmmo = std::min(st.ammo() & 0xff, 0x63);         // DAT_00451908
    for (int& a : st.ammoCount) if ((a & 0xff) >= 0x64) a = 0x63;
    fs.hudSlide = h_.uw(0x4667a8);                         // 0x4364f9: DAT_00451940
}

void PlayLogic::meterLive(Game& g) {
    // What the HUD reads of the logic's memory as it draws (after the tick, its end or not):
    // FUN_00435c4e kills Harry on a full meter (on the levels that show it: 0 .. 10, 16 .. 18).
    if (level_ <= kWarriorSpirit || (level_ >= kPalenqueRuins && level_ <= kJainaIslandFalls)) {
        if (h_.damage() >= (g.hardMode() ? 0x64 : 0x96) && h_.cont() != 0x428dce && h_.cont() != 0x428cdb) h_.b(0x465b72) = 0xff;
    }
    g.stats().damage = h_.damage();
    g.stats().harryDead = h_.b(0x465b72) == 0xff;
    // FUN_00436534 reads the ride's bytes as it draws: level 7's loop has no parallax (0x4366a8)
    g.state().rideLoop = level_ == kBalankancheMine && h_.b(0x465c56) != 0 && h_.b(0x4661c1) == 0;
}

void PlayLogic::timers() {
    // FUN_00433a09
    if (fadeCmd() != 0) {
        if (int32_t(fadeCmd()) > 0) fadeCmd() = fadeCmd() >= 0x10 ? fadeCmd() - 0x10 : 0;
        else fadeCmd() = fadeCmd() + 0x10 < fadeCmd() ? 0 : fadeCmd() + 0x10;
    }
    h_.b(0x465d4b) -= 1;
    if (int8_t(h_.b(0x465d4b)) < 0) h_.b(0x465d4b) = uint8_t((random() & 0xf) + 0xc);
    if (ch0Hold_) --ch0Hold_;
    h_.w(0x465b0d) &= int16_t(0xfff0);
    if (h_.b(0x465b72) == 0 && h_.w(0x4667a8) != 0) {
        if (h_.w(0x4667a8) < 0) h_.w(0x4667a8) = 0; else h_.w(0x4667a8) -= 3;
    }
    if (h_.b(0x465b95)) h_.b(0x465b95) -= 1;
    if (h_.b(0x465b3d)) h_.b(0x465b3d) -= 1;
    if (h_.b(0x465bb2)) h_.b(0x465bb2) -= 1;
    if (h_.onGround()) h_.b(0x465bb2) = 0;
    h_.b(0x465ad8) -= 1;
    if (int8_t(h_.b(0x465ad8)) < 0) {
        // once a second: killed enemies come back after 15 s
        h_.b(0x465ad8) = 0x3c;
        for (Killed& k : killed_) {
            if (k.count == 0) continue;
            k.count -= 1;
            if (k.count == 0 && k.als >= 0) alsMem_[size_t(k.als) + 1] = k.id;
        }
    }
    if (h_.b(0x465b01)) h_.b(0x465b01) -= 1;
    h_.b(0x465b08) -= 1;
    if (h_.b(0x465b08) == 0) h_.b(0x465b06) = 0;
    // FUN_0040d9bd: the temple pistons' shared cycle steps every 5 ticks, 0x50 steps
    h_.b(0x46650e) -= 1;
    if (int8_t(h_.b(0x46650e)) < 0) { h_.b(0x46650e) = 4; h_.b(0x46650d) += 1; if (h_.b(0x46650d) == 0x50) h_.b(0x46650d) = 0; }
    // the chili pepper and the stop-watch run their own music beat
    auto beat = [&](uint32_t timer) {
        h_.w(timer) -= 1;
        if (timer == 0x46652f && h_.w(timer) == 0) game_->setCdVolumeCmd(2);   // 0x433c42: the CD back up
        h_.b(0x465897) -= 1;
        if (int8_t(h_.b(0x465897)) < 0) {
            h_.b(0x466506) ^= 1;
            h_.b(0x466506) &= 1;
            sound1Now(0xef + h_.b(0x466506));
            h_.b(0x465897) = uint8_t((h_.uw(timer) >> 4) + 4);
        }
        if (h_.w(timer) != 0) return true;
        sound1Now(0x89);
        ch1Id_ = 0;
        return false;
    };
    if (h_.w(0x46652b) != 0) { if (beat(0x46652b)) return; }
    if (h_.w(0x46652f) != 0) beat(0x46652f);
}

void PlayLogic::blinkTimer() {
    // FUN_0042306c: DAT_00466799 toggles every 0x55 ticks
    h_.b(0x466796) += 1;
    if (h_.b(0x466796) != 0x55) return;
    h_.b(0x466796) = 0;
    const bool on = h_.b(0x466797) == 0;
    h_.b(0x466799) = on ? 1 : 0;
    h_.b(0x466797) = on ? 1 : 0;
}

void PlayLogic::selectWeapon() {
    // FUN_00434932: the select key cycles the weapon (sling, boomerang, stone)
    if (h_.b(0x466764) == 0) { h_.b(0x46650a) = 0; return; }
    if (h_.b(0x46650a) == 1) return;
    h_.b(0x46650a) = 1;
    if (h_.b(0x465b72) || h_.b(0x466795) || level_ == 0xe || h_.w(0x4667a8)) { h_.b(0x46650a) = 0; return; }
    if (level_ == 0xb || level_ == 0xc || level_ == 0xd) return;
    weaponSel() += 1;
    if (weaponSel() == 3) weaponSel() = 0;
}

void PlayLogic::harryDeath() {
    // FUN_00430a69
    const uint8_t b72 = h_.b(0x465b72);
    if (b72 == 0) return;
    if (h_.cont() == 0x4311a5) { h4311a5(); return; }     // (runs as Harry's state)
    h_.uw(0x465b92) = 0x8000;
    if (b72 == 0xfa) { h430eb6(); return; }
    if (h_.spriteIs("hyislice")) { h43118d(); return; }
    if (b72 == 0x80) { h430eb6(); return; }
    if (level_ == 0xe || b72 == 0x7f) {
        // 0x430ca8: the continue screen is over.  Off to the left ("no") the game is over; off
        // to the right (x 0xc8, or 0xc0 on) a continue is used: four lives (three on hard), no
        // score, no treasures, back to the level ("*1"); otherwise "*0" again while there are
        // continues.  (The Genesis wait loop after the "*2" is never reached on the PC.)
        if (exiting_) return;
        exiting_ = true;
        if (b72 == 0x7e) { game_->command("*2"); return; }
        h_.l(0x466535) = 0;
        PlayerStats& st = game_->stats();
        if (h_.x() == 0xc8 || !neg(h_.x() - 0xc0)) {
            st.continues = (st.continues - 1) & 0xff;
            st.lives = game_->hardMode() ? 3 : 4;   // DAT_00462694
            st.score = 0;
            st.treasures = 0;
            game_->command("*1");
            return;
        }
        game_->command(st.continues == 0 ? "*2" : "*0");
        return;
    }
    if (b72 == 0x7e || b72 == 0x7c) {
        // 0x430b19 / 0x430b00 (after 0x30 vertical blanks, nothing on the PC): out of the vault
        if (!exiting_) { exiting_ = true; game_->command("*7"); }
        return;
    }
    if (h_.cont() == 0x430e3a) return;
    if (h_.spriteIs("hyirn2cl")) { if (h_.frame() == 5) h43118d(); return; }
    if (h_.spriteIs("hyidie")) return;
    if (h_.spriteIs("hyifcrwl")) { h43118d(); return; }
    if (h_.spriteIs("hyihwall") && h_.frame() == 7) { h43118d(); return; }
    // 0x430e06: Harry dies (hyidie)
    h_.setCont(0x430e3a);
    h_.setSprite("hyidie");
    h_.setUl(0x465b47, 0x4615ab);
    h_.w(0x465b4b) = 0;
    h4310d5();
}

void PlayLogic::h430e3a() {
    h_.uw(0x465b92) = 0x8000;
    h43112b();
    h4310ca();
    if (!h_.onGround() && h_.ul(0x465b47) >= 0x4615b7) fadeCmd() -= 0x13;
    if (hAnim2(0x465b47, 4)) { h430eb6(); return; }
    // (0x430ea1 loads a sound id and pitch but plays nothing)
}

void PlayLogic::h4310ca() { h_.w(0x46652d) = 0x78; }

void PlayLogic::h4310d5() {
    // the palette fades to the death colours
    h_.setUl(0x46655e, 0x462614);
    h_.w(0x466566) = 0x3f;
    h_.setUl(0x466562, 0x46212e);
    if (h_.onGround()) {
        if (level_ == 0) h_.setUl(0x466562, 0x46224e);
        if (level_ == 5) h_.setUl(0x466562, 0x4622ce);
    }
    h_.w(0x466568) = 0xf;
}

void PlayLogic::h43112b() {
    // nothing is drawn behind Harry any more
    for (Obj* p : objects_.list()) {
        Obj& o = *p;
        if (o.type == 0x78 || o.type == 0x86 || o.type == 0x88) continue;
        o.behind = 0;
        if (o.flags == 0x6000) o.flags = 0x4000;
        if (o.flags == 0xe000) o.flags = 0xc000;
    }
}

void PlayLogic::h43118d() {
    h4310d5();
    h_.setCont(0x4311a5);
    h_.w(0x465b47) = 0x40;
    h4311a5();
}

void PlayLogic::h4311a5() {
    h43112b();
    h4310ca();
    h_.w(0x465b47) -= 1;
    if (h_.w(0x465b47) < 0) h430eb6();
}

void PlayLogic::h430eb6() {
    // FUN_00430eb6: a life is lost.  Every object goes (FUN_004311bf), the .als working copy
    // takes the level's own entries back but for the treasures and the letters, which stay
    // collected, the triggers' working copy is restored (it holds the boss pointer), the lever
    // flags 0x466510 .. 21 are cleared, and "*3" starts the level again at the checkpoint
    // (the engine exit ends the tick there).
    if (deathTimer_) return;
    deathTimer_ = 1;
    h_.b(0x465b39) = 0;
    if (level_ == 7) game_->stats().treasures = 0;
    h_.damage() = 0;
    h4311bf();
    ammo(0) = 0x14;
    if (ammo(1) < 2) ammo(1) = 2;
    if (ammo(2) == 0) ammo(2) = 1;
    for (size_t i = 0; i < 0x800; ++i) {                   // [DAT_004504e4] from [DAT_004504c4]
        const uint8_t d4 = alsMem_[0x800 + i];
        if (alsMem_[i] == d4) continue;
        if ((d4 >= 0x58 && d4 <= 0x5c) || (d4 >= 0xf0 && d4 <= 0xf6)) continue;   // gold, 1-up, coin, silver, ring, P-I-T-F-A-L-L
        alsMem_[i] = d4;
    }
    {
        // [DAT_0045052c] -> [DAT_00450510]: the triggers' record from its copy (the boss pointer,
        // and what else the logic keeps there, as far as the record goes)
        ph::Arena& a = game_->arena();
        const ph::ArenaLayout& lay = game_->arenaLayout();
        for (uint32_t i = 0; i < lay.trgSize; ++i) {
            const uint8_t v = a.at(int64_t(lay.trg) + lay.trgSize + i);
            a.put(int64_t(lay.trg) + i, &v, 1);
        }
        boss_ = nullptr;
    }
    std::memset(game_->levelFlags(), 0, 0x12);
    game_->loseLife();
}

// ---------------------------------------------------------------- the triggers' record

uint8_t& PlayLogic::trgByte(int off) {
    static uint8_t outside = 0;                            // (past the load block)
    const int64_t at = int64_t(game_->arenaLayout().trg) + off;
    if (!game_->arena().contains(at)) { outside = 0; return outside; }
    return game_->arena().data()[size_t(at)];
}

uint8_t PlayLogic::trgAt(int off) const { return game_->arena().at(int64_t(game_->arenaLayout().trg) + off); }

void PlayLogic::setBoss(Obj* o) {
    // [DAT_00450510] + 0x1f4 = the boss's record, as the 68000 code stores it (big-endian), and
    // DAT_00466507
    boss_ = o;
    h_.b(0x466507) = o ? 0xff : 0;
    if (o) setTl(0x1f4, addrOf(o));
}

// ---------------------------------------------------------------- the entry points the objects call

void PlayLogic::harryCall(uint32_t addr) {
    switch (addr) {
    case 0x427148: h427148(); return;
    case 0x42715b: h42715b(); return;
    case 0x427167: h427167(); return;
    case 0x428dfa: h428dfa(); return;
    case 0x428cc5: h428cc5(); return;
    case 0x428d7b: h428d7b(); return;
    case 0x42a325: h42a325(); return;
    case 0x42afe9: h_.setCont(0x42b049); h_.l(0x465b43) = 0; h_.l(0x465b47) = 0; h42b049(); return;
    case 0x42c1ea: h42c1ea(); return;
    case 0x42c490: h42c490(); return;
    case 0x42e9b2: h42e9b2(); return;
    case 0x42f0c4: h42f0c4(); return;
    case 0x42f123: h42f123(); return;
    case 0x42f2d2: h42f2d2(); return;
    case 0x430912: h430912(); return;
    default: notPorted(addr); return;
    }
}

void PlayLogic::notPorted(uint32_t addr) {
    static uint32_t last = 0;
    if (addr != last) { std::fprintf(stderr, "harry: %#x not ported\n", addr); last = addr; }
    if (addr >= 0x426eaa && addr < 0x431c99 && h_.cont() == addr) h428dfa();
}

// ---------------------------------------------------------------- services for the object handlers

void PlayLogic::standOn(int y, bool flagFirst) {
    // The landing the platforms share: Harry's y, no fall speed, the landing (FUN_00428dfa
    // with a6 = Harry) when he was in the air, and DAT_00465b73 (set before the landing at
    // some sites, e.g. 0x41b574, after it at others, e.g. 0x41527d).
    h_.y() = int16_t(y);
    h_.vy() = 0;
    if (flagFirst) h_.b(0x465b73) = 0xff;
    if (!h_.onGround()) { h_.onGround() = 0xff; h428dfa(); }
    if (!flagFirst) h_.b(0x465b73) = 0xff;
}

void PlayLogic::carryHarry(int32_t vx16) { addX(vx16); }

void PlayLogic::pullLever(Obj& lever) {
    // 0x40c45e: Harry takes the lever (a6) and pulls (state 0x42a6e7)
    h_.setUl(0x465b41, addrOf(&lever));
    h_.setCont(0x42a6e7);
    sound0(0x1d, 9);                            // FUN_0042633d(0x1d, 0x60)
    h_.w(0x465b47) = 0;
    h_.w(0x465b45) = 0;
}


void PlayLogic::markCollected(const Obj& o) { (void)o; }   // the working id simply stays 0

void PlayLogic::restoreEntry(const Obj& o) {
    // The handler exits write the saved id (+0x20) back into the working .als copy (+0x1c).
    if (o.als >= 0 && size_t(o.als) + 1 < alsMem_.size()) alsMem_[size_t(o.als) + 1] = o.id;
}

void PlayLogic::markKilled(const Obj& o) {
    // 0x4081fc: kept in the 0x465a40 list (if there is room) to come back after 15 seconds;
    // on the easy setting the monkeys are not.
    if (o.als < 0) return;
    if (!game_->hardMode() && o.type == 0x23) return;
    for (Killed& k : killed_)
        if (k.count == 0) { k.count = 0xf; k.id = o.id; k.als = o.als; return; }
}

// ---------------------------------------------------------------- FUN_00434a30: text in letter objects

uint8_t PlayLogic::textGlyph(uint8_t d0, uint32_t& d3) {
    // 0x434d2d: a character's glyph and its font, d3 the sprite slot and DAT_00466787 0 for the
    // large font (capitals, a space after them), 0x80 for the medium one (texts of type 0x11),
    // 0xff for the small one; the punctuation keeps the font before it.  0xff: a 6 px space.
    switch (d0) {
    case '.': return 0x24;
    case ',': return 0x25;
    case '@': return 0x26;
    case '!': return 0x27;
    case '\'': return 0x28;
    case ' ':
        if (h_.b(0x466787) != 0) return 0xff;
        d0 = uint8_t((random() & 1) + 0x2c);               // one of two large spaces
        d3 = 0x470238; h_.b(0x466787) = 0xff;
        return d0;
    case '[': d3 = 0x470238; h_.b(0x466787) = 0xff; return 0x2b;
    case ']': d3 = 0x470238; h_.b(0x466787) = 0xff; return 0x2e;
    default: break;
    }
    if (h_.b(0x465c58) == 0x11) { h_.b(0x466787) = 0x80; d3 = 0x47023c; return uint8_t(d0 - 0x41); }
    if (int8_t(d0 - 0x3a) < 0) { d3 = 0x470240; h_.b(0x466787) = 0xff; return uint8_t(d0 - 0x16); }   // digits
    if (int8_t(d0 - 0x5f) >= 0) { d3 = 0x470240; h_.b(0x466787) = 0xff; return uint8_t(d0 - 0x61); }  // small letters
    d3 = 0x470238; h_.b(0x466787) = 0;
    return uint8_t(d0 - 0x41);
}

void PlayLogic::spawnText(const char* text, int x, int y, uint8_t type) {
    // FUN_00434a30 (DAT_00466782 / 84 the position, DAT_00465c58 the type: the callers set it
    // before the call, which keeps it across the body, so it stays the type afterwards)
    h_.b(0x465c58) = type;
    h_.w(0x466782) = int16_t(x);
    h_.w(0x466784) = int16_t(y);
    h_.b(0x466786) = 0;
    h_.w(0x466788) = h_.w(0x466782);
    h_.w(0x46678c) = h_.w(0x466784);
    const uint8_t* a0 = reinterpret_cast<const uint8_t*>(text);
    uint32_t d3 = 0x470238;                                // (whatever the caller left in d3)
    auto centre = [&](bool fresh) {                        // 0x434aac: the line's width, then x + 0xa0 - w / 2
        if (fresh) h_.b(0x466787) = 0;
        uint16_t d1 = 0;
        for (const uint8_t* p = a0; *p != 0 && *p != 0xf0; ++p) {
            uint8_t d0 = *p;
            if (int8_t(d0 - 0x5f) >= 0) h_.b(0x466787) = 0xff;
            d0 = textGlyph(d0, d3);
            if (d0 == 0x2d || d0 == 0x2e || d0 == 0xff) { d1 = uint16_t(d1 + 6); continue; }
            const int i = d0 * 3 + (h_.b(0x466787) == 0x80 ? 1 : h_.b(0x466787) != 0 ? 2 : 0);
            d1 = uint16_t(d1 + kData_glyphWidths[i]);
        }
        d1 = uint16_t(d1 >> 1);
        if (h_.b(0x466787) != 0) h_.w(0x466782) -= 8;
        h_.w(0x466782) += int16_t(0xa0 - d1);
    };
    centre(true);
    for (int guard = 0; guard < 0x400; ++guard) {
        const uint8_t c = *a0++;
        if (c == 0) break;
        if (c == 0xf1) { centre(true); continue; }
        if (c == 0xf0) {                                   // a new line
            h_.w(0x46678c) += 0x19;
            h_.w(0x466782) = h_.w(0x466788);
            h_.w(0x466784) = h_.w(0x46678c);
            if (h_.b(0x466786) != 0) centre(false);
            continue;
        }
        const uint8_t g = textGlyph(c, d3);
        if (g == 0xff) { h_.w(0x466782) += 6; continue; }
        // FUN_00435905.  (The original then sets [DAT_004504e4] + 0x384 to the letter when the long
        // 0x384 bytes past the new record is 0: that is the list link of the record 8 slots on,
        // never 0, so it never does.)
        Obj* a5 = objects_.allocHead();
        if (!a5) continue;
        a5->flags = 0xe000;
        a5->x = h_.w(0x466782);
        a5->y = h_.w(0x466784);
        a5->frame = g;
        a5->setSprite(game_->findSprite(spriteSlotName(d3)));
        a5->type = h_.b(0x465c58);
        int i = g * 3;
        if (d3 == 0x47023c) { a5->flags = 0xe000; i += 1; }
        else if (d3 == 0x470240) { a5->flags = 0xc000; i += 2; }
        h_.w(0x466782) += kData_glyphWidths[i];
    }
}

// ---------------------------------------------------------------- camera, objects, display

void PlayLogic::hudPlacement() {
    // FUN_0043348e: the objects 0x4150a2 listed in the last object pass follow the camera.
    const int16_t cx = h_.w(0x4658b4), cy = h_.w(0x4658b6);
    for (int i : hud_) {
        Obj& o = objects_.record(i);
        switch (o.type) {
        case 0x9b: o.x = int16_t(cx + 0x83); o.y = int16_t(cy + 0x54); break;
        case 0x97:
            if (o.w(0x46) != 0) o.w(0x46) -= 1;
            else { o.w(0x32) += 5; if (o.w(0x32) == 0xeb) o.w(0x46) = 0x90; }
            o.x = int16_t(cx - 0x5d + o.w(0x32)); o.y = int16_t(cy + 0x5e);
            break;
        case 7: o.x = cx; break;
        case 0x7e: o.x = int16_t(cx - o.w(0x32)); break;
        case 0x4b: o.x = int16_t(cx - 0x10); o.y = int16_t(cy + 0x50); break;
        case 0x30: {                                                        // 0x4335ce: the boss meter
            Obj* d2 = o.link; Obj* d3 = o.link2; Obj* pct = o.link3;      // l32 / l36 / l60
            int16_t x = int16_t(cx + 0x82);
            if (o.b(0x5a) == 1) x = int16_t(x - 0x1c);
            if (o.b(0x5a) == 2) x = int16_t(x + 0x14);
            o.x = x;
            x = int16_t(x + 8); if (d2) d2->x = x;
            x = int16_t(x + 8); if (d3) d3->x = x;
            if (pct) pct->x = int16_t(x + 9);
            const int16_t y = int16_t(cy + h_.w(0x4667a8) + 0xde + o.w(0x3c));
            o.y = y; if (d2) d2->y = y; if (d3) d3->y = y;
            if (pct) pct->y = int16_t(y + 0xe);
            break;
        }
        default: break;
        }
    }
}

void PlayLogic::runObjects(Game& g) {
    strayReturns_.clear();     // (an engine exit in the last pass left its stack behind)
    h_.w(0x4658a0) = 0;
    h_.w(0x465bac) = 0;
    h_.b(0x465b74) = 0;
    h_.w(0x466508) = 0;
    h_.b(0x465b73) = 0;        // 0x4051c4: DAT_00465b73 is cleared before every object pass; a platform sets it again
    h_.b(0x4658c3) = 0;        // 0x4051ab: trigger id 3 (a wall for Harry on the ground) never stops objects
    hud_.clear();              // 0x405170: the list 0x4150a2 fills starts again
    ObjCtx ctx{g, *this, g.level(), h_};
    ctx.levelNo = level_;
    ctx.camX = h_.w(0x4658b4); ctx.camY = h_.w(0x4658b6);   // DAT_004658b4 / b6
    ctx.hard = g.hardMode();                                 // DAT_00462694
    objects_.tick(ctx);
    runStrayReturns();
}

void PlayLogic::runStrayReturns() {
    // The pass's `ret` (0x4052f8) pops the addresses last-pushed first; the code at each ends
    // with `jmp 0x405313`, which finds the list done and comes back to the `ret`.
    while (!strayReturns_.empty()) {
        const uint32_t a = strayReturns_.back();
        strayReturns_.pop_back();
        switch (a) {
        case 0x410e6a:   // the Ceiba Jungle jaguar (type 55) died: the walk-out (DAT_00465d49)
            startBossOutro();
            break;
        case 0x41e62c:   // the level-8 jaguar (type 158) died: Harry, dazed by its swipe, stands up
            if (h_.spriteIs("hyidazed") || h_.cont() == 0x428dce || h_.cont() == 0x428cdb) h428dfa();
            break;
        default: break;
        }
    }
}

void PlayLogic::displayList(Game& g) {
    // FUN_00434f6c: the display entries (FUN_00434eef) of the objects listed behind Harry by the
    // last object pass, then Harry (flickering while invulnerable), then the ones in front.
    FrameState& fs = g.state();
    fs.displayList.clear();
    auto objs = [&](size_t from) {
        // the records as they are now (an object freed since keeps its fields), up to the -1;
        // -2 is a cancelled entry
        const auto& d = objects_.displayArray();
        for (size_t k = from; k < d.size() && d[k] != -1; ++k) {
            if (d[k] < 0) continue;
            displayEntry(objects_.record(d[k]));
        }
    };
    objs(0);
    bool draw = true;
    if (h_.w(0x46652d) != 0) {
        h_.w(0x46652d) -= 1;
        draw = (h_.w(0x46652d) & 1) != 0;
    }
    if (draw) {
        int16_t x = h_.x(), y = h_.y();
        const uint32_t c = h_.cont();
        if (c == 0x42edb1) { x += 0x18; if (h_.facing()) x -= 0x30; }
        if (c == 0x42dd10 || c == 0x42bda7 || c == 0x42ddc8) {
            y += 0x1b;
            h_.w(0x4658a6) += 2;
            if (!neg(h_.w(0x4658a6) - 0x40)) h_.w(0x4658a6) = 0x40;
        }
        if ((h_.spriteIs("hyihang") || c == 0x42dd10 || c == 0x42ddc8) && h_.facing()) x += 3;
        displayEntry(x, y, h_.frame(), h_.facing(), h_.b(0x465b38), h_.flags(), g.findSprite(h_.sprite()));
    }
    objs(ObjectSystem::kFront);
}

void PlayLogic::displayEntry(int16_t x, int16_t y, uint8_t frame, uint8_t facing, uint8_t vflip, uint16_t flags, const ph::Sprite* spr) {
    // FUN_00434eef.  (The entry also carries the type byte, which only the engine's error
    // message for a bad sprite reads.)
    FrameState& fs = game_->state();
    if (int8_t(frame) < 0 || !spr || fs.displayList.size() >= 0x50) return;
    engine::SpriteInstance s;
    s.x = x + 0x10; s.y = y + 0x10 - h_.w(0x465b02); s.sprite = spr;
    s.frame = uint32_t(frame % std::max(1, spr->frameCount()));
    s.flags = engine::kSprVisible | (facing ? engine::kSprHFlip : 0u) | (vflip ? engine::kSprVFlip : 0u)
            | ((flags & 0x8000) ? engine::kSprFront : 0u);
    fs.displayList.push_back(s);
}

void PlayLogic::digits(int32_t v) {
    // FUN_00431a3f: each power of ten of the table at 0x46aa18 (100000 .. 1, 0 ending it) is
    // taken away until the value goes negative; a negative value gives zeros throughout.
    uint32_t a = 0x466556;
    for (const uint8_t* p = kData_a26Powers; ; p += 4, ++a) {
        int32_t pw; std::memcpy(&pw, p, 4);
        if (pw == 0) break;
        uint8_t n = 0;
        do { ++n; v -= pw; } while (v >= 0);
        h_.b(a) = uint8_t(n - 1);
        v += pw;
    }
}

} // namespace game
