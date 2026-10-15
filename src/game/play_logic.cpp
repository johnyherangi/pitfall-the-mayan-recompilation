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
    g_debugLogic = this;
    game_ = &g;
    level_ = g.levelNumber();
    const ph::LevelData& lv = g.level();
    h_ = Harry{};
    alsMem_ = lv.als;                                          // [DAT_004504e4]
    for (const auto& cr : g.permanentlyGone()) {               // collected for good (FUN_00430eb6 keeps them)
        const uint16_t off = alsWord(4 + size_t(cr.first) * 2);
        if (off == 0) continue;
        for (size_t e = off; alsByte(e) != 0xff; e += 2)
            if (alsByte(e) == cr.second) alsMem_[e + 1] = 0;
    }
    for (Killed& k : killed_) k = Killed{};
    exiting_ = false;
    deathTimer_ = 0;
    boss_ = nullptr;
    ch0Pri_ = ch0Id_ = ch0Hold_ = ch1Pri_ = ch1Id_ = 0;
    objects_.reset();
    hud_.clear();                                                           // 0x4331c4
    h_.w(0x4658ac) = int16_t(lv.columns);                                   // the level size in cells
    h_.w(0x4658ae) = int16_t(lv.hasBg ? lv.bg.hTiles / 2 : 14);
    h_.w(0x465bae) = int16_t(lv.hasBg ? lv.bg.wTiles * 8 : 320);           // and in pixels
    h_.w(0x465bb0) = int16_t(lv.hasBg ? lv.bg.hTiles * 8 : 224);
    h_.l(0x466535) = level_;

    // FUN_004331d8: the '^' trigger gives the start position.
    bool found = false;
    for (int c = 0; c < lv.triggers.columns() && !found; ++c)
        for (const ph::ColumnEntry& e : lv.triggers.column(c))
            if (e.id == ph::kTrgStart) { h_.x() = int16_t(c * 16 - 8); h_.y() = int16_t((e.row + 2) * 16); found = true; break; }
    if (!found) { h_.x() = 160; h_.y() = 160; }
    if (g.checkpoint().valid) {               // FUN_00430a69 restart: DAT_0046679e.. after a death
        h_.x() = int16_t(g.checkpoint().x); h_.y() = int16_t(g.checkpoint().y);
    }
    levelRestart();
    if (level_ >= 0 && level_ < 19 && !g.checkpoint().valid && ph::kStartFacing[level_]) h_.facing() = 0xff;
    if (g.checkpoint().valid) h_.facing() = g.checkpoint().facingLeft ? 0xff : 0;
    FrameState& fs = g.state();
    fs.scrollX = h_.w(0x4658b4);
    fs.scrollY = h_.w(0x4658b6);
    displayList(g);
}

void PlayLogic::levelRestart() {
    // FUN_00432713 (the parts every level shares; the vaults' and the map screen's set-up
    // that follows in the original is not ported yet).
    h_.b(0x4667a8) = 0;
    h_.facing() = 0;
    h_.b(0x466555) = 0xff;
    h_.b(0x465b38) = 0;
    h_.b(0x465bb3) = 0;
    h_.onGround() = 0xff;
    // FUN_00435274: the object pool and Harry's state fields
    h_.b(0x465ae4) = 0; h_.l(0x465ae7) = 0;
    h_.frame() = 0xff;
    h_.setUl(0x465898, 0);
    h_.b(0x465d49) = 0;
    h_.b(0x465b35) = 0;
    h_.w(0x465b35) = 0x6663;                               // FUN_004263a6 on Harry
    for (uint32_t a = 0x465b41; a < 0x465b69; a += 4) h_.l(a) = 0;
    for (uint32_t a = 0x466808; a < 0x46681c; a += 4) h_.l(a) = 0;
    h_.l(0x46681c) = 0;
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
    h_.b(0x465b39) = 0;
    h_.b(0x46650c) = 0;
    h_.l(0x466796) = 0;
    h_.b(0x466552) = 0;
    h_.b(0x466553) = 0;
}

// ---------------------------------------------------------------- the main loop (FUN_0043628c)

void PlayLogic::tick(Game& g) {
    game_ = &g;
    level_ = g.levelNumber();
    FrameState& fs = g.state();
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
    fs.scrollX = h_.w(0x4658b4);
    fs.scrollY = h_.w(0x4658b6);
    fs.bob = h_.w(0x465b02);
    // FUN_00435c4e (the damage meter, drawn every frame): a full meter kills
    const int limit = g.stats().difficulty ? 100 : 0x96;
    if (h_.damage() >= limit && h_.cont() != 0x428dce && h_.cont() != 0x428cdb) h_.b(0x465b72) = 0xff;
    g.stats().damage = h_.damage();
    g.stats().harryDead = h_.b(0x465b72) != 0;
    // 0x436435: the stats for the HUD: 50 treasures make a continue (max 9), ammo stops at 99
    PlayerStats& st = g.stats();
    while ((st.treasures & 0xff) >= 0x32) {
        st.treasures = uint8_t(st.treasures - 0x32);
        st.continues += 1;
        if (st.continues >= 0xa) st.continues = 9;
        sound1Now(0x22);
    }
    if (st.lives > 0xa) st.lives = 0xa;                    // (the original also turns 0 into 10; 0 is handled at the death)
    for (int& a : st.ammoCount) if ((a & 0xff) >= 0x64) a = 0x63;
    ++tickCount_;                                          // DAT_004601d9
}

void PlayLogic::timers() {
    // FUN_00433a09
    if (fadeCmd_ != 0) {
        if (int32_t(fadeCmd_) > 0) fadeCmd_ = fadeCmd_ >= 0x10 ? fadeCmd_ - 0x10 : 0;
        else fadeCmd_ = fadeCmd_ + 0x10 < fadeCmd_ ? 0 : fadeCmd_ + 0x10;
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
    if (b72 == 0xfa || b72 == 0x80) { h430eb6(); return; }
    if (h_.spriteIs("hyislice")) { h43118d(); return; }
    if (level_ == 0xe || b72 == 0x7f || b72 == 0x7e || b72 == 0x7c) {
        notPorted(0x430ca8);                                // the continue screen, the vaults' exits
        h430eb6();
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
    if (!h_.onGround() && h_.ul(0x465b47) >= 0x4615b7) fadeCmd_ -= 0x13;
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
    // FUN_00430eb6: a life is lost (the level data restored, "*3" restarts the level).
    if (deathTimer_) return;
    deathTimer_ = 1;
    if (level_ == 7) game_->stats().treasures = 0;
    h_.damage() = 0;
    ammo(0) = 0x14;
    if (ammo(1) < 2) ammo(1) = 2;
    if (ammo(2) == 0) ammo(2) = 1;
    game_->loseLife();
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
    if (!game_->stats().difficulty && o.type == 0x23) return;
    for (Killed& k : killed_)
        if (k.count == 0) { k.count = 0xf; k.id = o.id; k.als = o.als; return; }
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
    ctx.hard = g.stats().difficulty != 0;
    objects_.tick(ctx);
}

void PlayLogic::displayList(Game& g) {
    // FUN_00434f6c: the display entries (FUN_00434eef) of the objects listed behind Harry by the
    // last object pass, then Harry (flickering while invulnerable), then the ones in front.
    FrameState& fs = g.state();
    fs.displayList.clear();
    auto entry = [&](int16_t x, int16_t y, uint8_t frame, uint8_t facing, uint8_t vflip, uint16_t flags, const ph::Sprite* spr) {
        if (int8_t(frame) < 0 || !spr || fs.displayList.size() >= 0x50) return;
        engine::SpriteInstance s;
        s.x = x + 0x10; s.y = y + 0x10 - h_.w(0x465b02); s.sprite = spr;
        s.frame = uint32_t(frame % std::max(1, spr->frameCount()));
        s.flags = engine::kSprVisible | (facing ? engine::kSprHFlip : 0u) | (vflip ? engine::kSprVFlip : 0u)
                | ((flags & 0x8000) ? engine::kSprFront : 0u);
        fs.displayList.push_back(s);
    };
    auto objs = [&](size_t from) {
        // the records as they are now (an object freed since keeps its fields), up to the -1;
        // -2 is a cancelled entry
        const auto& d = objects_.displayArray();
        for (size_t k = from; k < d.size() && d[k] != -1; ++k) {
            if (d[k] < 0) continue;
            const Obj& o = objects_.record(d[k]);
            entry(o.x, o.y, o.frame, o.facing, o.vflip, o.flags, o.sprite);
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
        entry(x, y, h_.frame(), h_.facing(), h_.b(0x465b38), h_.flags(), g.findSprite(h_.sprite()));
    }
    objs(ObjectSystem::kFront);
}

} // namespace game
