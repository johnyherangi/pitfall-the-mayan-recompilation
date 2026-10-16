// Harry's state machine: FUN_00426810 and the state code it runs through DAT_00465b69
// (0x426810 .. 0x431c99 of PITFALL.EXE).
//
// Every function here is named after the address it was read from (hXXXXXX), states being
// the continuation targets stored in harryCont.  The original's `jmp` from one state into
// another is `return hXXXXXX();`, and a state that sets harryCont and falls into the new
// state's code does the same.  Memory is addressed as in the listing (Harry's record is at
// 0x465b0f: +0x38 is 0x465b47, and so on).  See harry.h for the conventions.
#include "game/play_logic.h"

#include <cstdio>

namespace game {

namespace {
// 16.16 add to a position word pair (add.l to x.frac / y.frac)
inline bool neg16(int v) { return int16_t(v) < 0; }   // the sign test of a 16-bit cmp / sub (js / jns)
inline bool neg8(int v) { return int8_t(v) < 0; }     // ... of a byte cmp / sub
}

void PlayLogic::runState() {
    // `call eax` with eax = DAT_00465b69
    const uint32_t c = h_.cont();
    switch (c) {
#include "game/harry_dispatch.inc"
    default: break;
    }
    notPorted(c);
}

// ---------------------------------------------------------------- FUN_00426810

void PlayLogic::harryUpdate() {
    if (game_->state().joypad & 0x100) {
        // Ctrl (Debug_AllowCheat): fly 7 px per tick, inside the level.
        if (h_.b(0x466767) && uint16_t(h_.x()) > 7) h_.x() -= 7;
        if (h_.b(0x466765) && uint16_t(h_.y()) > 7) h_.y() -= 7;
        if (h_.b(0x466768) && uint16_t(h_.x()) < uint16_t(h_.w(0x465bae) - 7)) h_.x() += 7;
        if (h_.b(0x466766) && uint16_t(h_.y()) < uint16_t(h_.w(0x465bb0) - 7)) h_.y() += 7;
        return;
    }
    if (level_ == 7 && !neg16(h_.y() - 0x2ca)) h_.b(0x465b72) = 0xfa;   // the mine-cart ride: fell below the track
    // Trigger id 3 is a wall for Harry only while he is on the ground, id 2 never (the object
    // pass sets it to 3 for every handler).
    h_.b(0x4658c3) = h_.onGround() ? 1 : 0;
    h_.b(0x4658c2) = 0;
    h_.w(0x465aeb) = 0;                                    // the whip box (FUN_0042b2bc sets it again)
    if (neg8(h_.b(0x466795) - 2)) {
        if (h_.cont() == 0) return;
        runState();
    }
    if (h_.b(0x465b71) == 0 && !neg16(h_.w(0x4658a6) - 1)) {
        // the look up / down offset of the camera returns to 0
        h_.w(0x4658a6) -= 3;
        if (!neg16(h_.w(0x4658a6))) h_.w(0x4658a6) = 0;
    }
    if (h_.cont() == 0x42756d) return;
    if (h_.onGround()) {
        h_.w(0x465b9e) = 0;
        if (!feet()) { h42ccd7(); return; }               // walked off the floor
    }
    h_.b(0x465b3a) &= 0xc;
    if (h_.cont() != 0x428e50) h_.b(0x465b3a) &= 3;
    // The camera locks (DAT_00465b09 / DAT_00465b0b) hold Harry inside the arena.
    int16_t d0 = int16_t(h_.w(0x465b09) + 0x12);
    if (!neg16(d0 - h_.x())) { h_.b(0x465b3a) += 1; h_.x() = d0; }
    d0 = int16_t(h_.w(0x465b0b) + 0x10e);
    if (neg16(d0 - h_.x())) { h_.x() = int16_t(d0 + 1); h_.b(0x465b3a) += 2; }
    if (level_ == 9) {
        if (h_.cont() == 0x430e3a) { h_.flags() = 0xe000; return; }
        if (!neg16(h_.y() - 0x2d0) && h_.cont() != 0x426eaa) { h_.y() = 0x2d0; h426e87(); return; }
    }
    d0 = int16_t((h_.w(0x4658ae) << 4) + 0x40);
    if (neg16(d0 - h_.y()) && h_.cont() != 0x427528) {
        // fell out of the level
        if (level_ == 4 || level_ == 1) sound1(0xc8, 0x12);
        h_.b(0x465b72) = 0x80;
    }
    if (h_.cont() == 0x430e3a) { h_.flags() = 0xe000; return; }
    h430229();                                             // the triggers at Harry's position
    h_.flags() = uint16_t(h_.w(0x465b92) + 0x6000);
    if (!h_.onGround() && h_.b(0x465b95) == 0) {
        // In the air: a vine (0x1d) or a ledge (0xc) above him.
        Trg t = trg(h_.x() - 0x10, h_.y() - 0x40);
        if (t.id == 0x1d) { h42bd09(); return; }
        if (t.id == 0xc) { h42abee(); return; }
        t = trg(h_.x() - 0x20, h_.y() - 0x28);
        if (t.id == 0x1d) { h42bcfa(); return; }
        if (t.id == 0xc) { h42abdf(); return; }
    }
    if (h_.vy() < 0) return;
    const Trg t = trg(h_.x() - 0x10, h_.y() + 2);
    if (t.id == 0xd0 && h_.cont() != 0x427528) {
        // Water: Harry sinks out of sight (state 0x427528) and a splash rises.
        bool splash = true;
        if (h_.cont() == 0x429fe2) splash = h_.b(0x465b46) == 0;   // on a bungee vine: one splash
        else {
            h_.b(0x465b41) = 0;
            h_.setCont(0x427528);
            h_.frame() = 0xff;
            h_.w(0x465b35) = 0;                            // FUN_004263c4
        }
        if (splash) {
            h_.b(0x465b46) = 0x41;
            sound1(0xc8, 0x46);
            if (Obj* a3 = newObjAtHarry(1)) {
                a3->type = 0x7a;
                a3->sprite = game_->findSprite("fxisplsh");
                a3->flags = 0xa000;
                a3->x = h_.x(); a3->y = int16_t(h_.y() - 0x10);
                a3->behind = 0xff;
                if (Obj* a5 = newObjAtHarry(9)) {
                    a3->l(0x50) = int32_t(addrOf(a5));     // (written twice)
                    std::memcpy(a3->s + 0x12, a5->s + 0x12, 2);
                    a3->w(0x26) = 0x6663;
                    a5->sprite = game_->findSprite("fxisplsh");
                    a5->flags = 0xa000;
                    a5->l(0x50) = int32_t(addrOf(a3));      // (the twin has no type: drawn, never run)
                    a5->x = h_.x(); a5->y = int16_t(h_.y() - 0x10);
                    a5->facing = 0xff;
                    a5->behind = 0xff;
                }
            }
        }
    }
    if (t.id == 6 && h_.cont() != 0x427368) {
        // An icy slope: slide (state 0x427368).
        h_.w(0x465b47) = 0;
        h_.setCont(0x427368);
        sound0(0x1a, 0xa);
        h_.l(0x465b47) = 0;
    }
    if (t.id == 0x1f) {
        // Spikes: hurt and thrown up.
        h_.onGround() = 0;
        hurtHarry(0xf);
        h_.vy() = int32_t(0xfff84000);
        h_.setCont(0x42c518);
    }
    if (t.id == 0xe) {
        if (h_.cont() == 0x428aaa) return;
        h428a56();                                         // quicksand
    }
    if (t.id == 0xb && h_.cont() != 0x426fee && h_.cont() != 0x42a36f && h_.cont() != 0x430e3a) {
        snapFeet(false);
        snapFeet(true);
        h42a325();                                         // a tar pit
    }
    if (h_.b(0x465b71) != 0 || !h_.onGround()) h_.b(0x465b06) = 0;
    // The left-right waggle (15 changes spin Harry round, 0x42e138).  The masks were written
    // for the Genesis 0xff pad bytes; with the PC's raw bits (left 4, right 8) d0 is always 0.
    const uint8_t w = uint8_t((h_.b(0x466767) & 1) + (h_.b(0x466768) & 2));
    if (w == 0 || w == h_.b(0x465b07)) return;
    h_.b(0x465b07) = w;
    h_.b(0x465b08) = 0x14;
    h_.b(0x465b06) += 1;
    if (h_.b(0x465b06) == 0xf) h42e138();
}

// ---------------------------------------------------------------- sinking in a pit (level 9)

void PlayLogic::h426e87() {
    h_.setCont(0x426eaa);
    h_.w(0x465b47) = 0;
    h_.w(0x465b55) = 0;
    h_.onGround() = 0;
    h426eaa();
}

void PlayLogic::h426eaa() {
    addX(0xd000);                                          // drifts right
    h_.setSprite("hyisink");
    if (hAnim(0x465b47, 0x461875, 4)) {
        h_.b(0x465b72) = 0x80;
        h_.frame() = 0xff;
        return;
    }
    h_.w(0x465b55) -= 1;
    if (!neg16(h_.w(0x465b55))) return;
    h_.w(0x465b55) = int16_t((random() & 7) + 8);
    if (h_.frame() == 0xff || channel0Busy()) return;
    const int id = int(random() & 1) + 0x14;
    sound0(id, 0xa);
}

// ---------------------------------------------------------------- FUN_00426f67 (main loop)

void PlayLogic::leanShadow() {
    // While Harry leans on a wall (hyilean) a second object draws the sprite again.
    if (h_.spriteIs("hyilean")) {
        if (h_.ul(0x465898)) return;
        Obj* a5 = newObjAtHarry(3);
        if (!a5) { h428dfa(); return; }                    // (sic: je 0x428dfa)
        a5->sprite = game_->findSprite("hyilean");
        a5->frame = 0;
        a5->facing = h_.facing();
        a5->flags = h_.flags();
        h_.setUl(0x465898, addrOf(a5));
        return;
    }
    if (h_.ul(0x465898)) {
        if (Obj* o = objAt(h_.ul(0x465898))) objects_.free(*o);
        h_.setUl(0x465898, 0);
    }
}

// ---------------------------------------------------------------- 0x426fee: crawling into a hole

void PlayLogic::h426fee() {
    h_.setSprite("hyicrcyc");
    h_.onGround() = 0;
    hAnim(0x465b43, 0x4615e7, 5);
    addY(0xc000);
    h_.w(0x465b41) += 1;
    if (h_.w(0x465b41) != 0x3c) return;
    // ... and out again, somersaulting.
    h_.onGround() = 0;
    h_.setSprite("hyisomr2");
    h_.frame() = 0;
    h_.w(0x465b47) = 0;
    h_.vy() = int32_t(0xfff19000);
    h_.setCont(0x42cdb6);
    h_.b(0x465b01) = 0x28;
    sound1(0x45, 0xa);
}

// ---------------------------------------------------------------- 0x42709d: carried by an object

void PlayLogic::h42709d() {
    if (h_.b(0x465b72)) return;
    Obj* a0 = objAt(h_.ul(0x465b45));
    if (!a0) return;
    h_.x() = int16_t(a0->x + 0x22);
    h_.y() = int16_t(a0->y + 0x1a);
    if (h_.w(0x465b43) != 0) {
        h_.w(0x465b43) -= 1;
        h_.w(0x465b4b) = 0;
        return;
    }
    h_.setSprite("hyihwall");
    if (hAnim(0x465b4b, 0x427139, 4)) {
        h_.frame() = 7;
        h430eb6();
    }
}

// ---------------------------------------------------------------- knocked back

void PlayLogic::h427148() { h_.l(0x465b59) = 0x3e00; h_.b(0x465b5f) = 0xff; h427178(); }
void PlayLogic::h42715b() { h_.l(0x465b59) = 0xc00; h_.b(0x465b5f) = 0; h427178(); }
void PlayLogic::h427167() { h_.l(0x465b59) = 0x3e00; h_.b(0x465b5f) = 0; h427178(); }

void PlayLogic::h427178() {
    // b(0x465b55): pushed to the right; DAT_00465b59 the deceleration; b(0x465b5f) set: no
    // invulnerability afterwards.
    h_.setCont(0x427207);
    h_.vy() = 0;
    h_.facing() &= 1;
    h_.w(0x465b4b) = 0;
    h_.l(0x465b51) = 0;
    h_.setUl(0x465b47, 0x461985);
    if ((h_.b(0x465b55) & 1) != h_.facing()) h_.setUl(0x465b47, 0x4619a8);
    h_.vx() = int32_t(0xfff90000);
    if (h_.l(0x465b59) == 0xc00) h_.vx() = int32_t(0xfffe0000);
    if (h_.b(0x465b55) != 0) h_.vx() = -h_.vx();
    h427207();
}

void PlayLogic::h427207() {
    h_.onGround() = 0xff;
    feet();
    if (h_.b(0x465b5f) == 0) h_.invulnerable() = 0x50;
    if (!neg16(h_.w(0x465baa) - 6)) h_.vy() = 0x60000;
    h_.vy() += 0x5000;
    addY(h_.vy());
    if (feet()) h_.vy() = 0;
    h_.onGround() = 0;
    if (hAnim2(0x465b47, 4)) { h428dfa(); return; }
    addX(h_.vx());
    if (h_.vx() == 0) return;
    if (h_.vx() < 0) {
        h_.vx() += h_.l(0x465b59);
        if (h_.vx() < 0) { h42e81d(); return; }
    } else {
        h_.vx() -= h_.l(0x465b59);
        if (h_.vx() >= 0) { h42e81d(); return; }
    }
    h_.vx() = 0;
}

// ---------------------------------------------------------------- 0x427316: dazed

void PlayLogic::h427316() {
    h_.w(0x465b47) -= 1;
    if (h_.w(0x465b47) == 0) { h428dfa(); return; }
    h428d36();
    h_.setSprite("hyidazed");
    hAnim(0x465b45, 0x42735c, 4);
}

// ---------------------------------------------------------------- 0x427368: sliding down an icy slope

void PlayLogic::h427368() {
    h_.setSprite("hyiskte1");
    hAnim(0x465b47, 0x461770, 4);
    h_.b(0x465bb2) = 4;
    const ph::FloorHit f = ph::probeNear(game_->level().collision, h_.x() - 0x10, h_.y() + 0xd);
    if (f.hit) h_.y() = int16_t(f.surfaceY - 0xd);
    if (!f.hit) {
        h_.vy() = 0x10000;
        h_.setCont(0x42c518);
        h_.w(0x465b47) = 0;
        h_.b(0x465b7b) = 0;
        h_.onGround() = 0;
        h42c518();
        return;
    }
    // The slope's direction: the shape's height at its left column minus its right column.
    const int d0 = ph::kCollisionHeights[f.shape * 16] - ph::kCollisionHeights[f.shape * 16 + 15];
    if (!neg16(d0)) {
        h_.facing() = 0xff;
        if (h_.vx() >= int32_t(0xfffe1000)) h_.vx() = int32_t(0xfffe0000);
        if (h_.w(0x465ba6) != -5) {
            if (h_.vx() >= 0) h_.vx() = 0;
            h_.vx() -= 0x5000;
        }
    } else {
        h_.facing() = 0;
        if (h_.vx() < 0x1f000) h_.vx() = 0x20000;
        if (h_.w(0x465ba6) != 5) {
            if (h_.vx() < 0) h_.vx() = 0;
            h_.vx() += 0x5000;
        }
    }
    addX(h_.vx());
}

// ---------------------------------------------------------------- 0x427528: drowning

void PlayLogic::h427528() {
    h_.frame() = 0xff;
    h_.b(0x465b46) -= 1;
    if (h_.b(0x465b46) == 0) { h_.b(0x465b72) = 0x80; return; }
    h_.vy() += 0x4000;
    addY(h_.vy());
}

// ---------------------------------------------------------------- 0x42756d: the Loltun vault (Simon game)

void PlayLogic::h42756d() {
    // A Loltun vault.  The keys 1..3, queued at 0x4661a8 on the player's turn, send Harry to a
    // lever (x 0x54 / 0x84 / 0xb4, facing the right one) and DAT_00465b45 tells the levers he is
    // there (they start the pull, 0x427d92).  He walks under a falling reward (DAT_00465b59) and
    // back.  Once the combination is done (trg+0x215) he waits 80 ticks and walks out right
    // ("*7").  The ceiling (trg+0x1f4, 16.16, the vault's parallax layer) keeps still for 128
    // ticks, creeps down while a round is played (trg+0x1fc) and falls on a wrong lever
    // (trg+0x214) or once below 0x8a: it squashes Harry (hyihwall), a message comes up, and
    // after 84 ticks he is out too.
    if (h_.b(0x465b59) == 0x28) {                          // a reward has been falling for 40 ticks
        h_.w(0x465b41) = 0x44;
        h_.w(0x465b5b) = h_.x();
        h_.facing() = 0xff;
        h_.b(0x465b43) = 0xff;
        h_.b(0x465b59) = 0x29;
    }
    if (h_.b(0x465b59) == 0x4e) {
        if (h_.w(0x465b41) == 0) {                         // under it: back to where he was
            h_.facing() = 0;
            h_.b(0x465b43) = 0;
            h_.w(0x465b41) = h_.w(0x465b5b);
            h_.b(0x465b59) = 0;
        }
    } else if (h_.b(0x465b59) != 0) {
        h_.b(0x465b59) += 1;
    }
    if (tb(0x215) != 0) {                                  // the combination is done
        h_.facing() = 0;
        h_.b(0x465b43) = 0;
        if (tw(0x21c) != 0x50) {
            setTw(0x21c, uint16_t(tw(0x21c) + 1));
            h_.setSprite("hyiready");
            hAnimAt(&tb(0x21e), 0x4297d2, 4);
        } else {
            h_.setSprite("hyirun");
            hAnim(0x4661ca, 0x46166b, 4);
            h_.x() += 3;
            if (!neg16(h_.x() - 0x1b5)) h_.b(0x465b72) = 0x7e;
        }
        if (neg16(int16_t(tw(0x1f4)) - 0xf0)) {            // the ceiling goes back up
            setTl(0x1f4, tl(0x1f4) + 0x10000);
            if (!channel1Busy()) sound1(0xd1, 3);
        } else if (ch1Id_ == 0xd1) {
            sound1Now(0x89);
            ch1Id_ = 0;
        }
        return;
    }
    h_.w(0x4661ca) = 0;
    bool falls = true;
    if (tb(0x214) == 0) {
        if (tw(0x1fe) != 0x80) {                           // the first 128 ticks
            setTw(0x1fe, uint16_t(tw(0x1fe) + 1));
            falls = false;
        } else if (!neg16(int16_t(tw(0x1f4)) - 0x8a)) {    // still high (0x4279b2)
            falls = false;
            if (tw(0x1fc) == 0) {
                if (ch1Id_ == 0xd1) { sound1Now(0x89); ch1Id_ = 0; }
            } else {                                       // a round is on: it creeps down
                setTl(0x1f4, tl(0x1f4) - 0xc00);
                if (!channel1Busy()) sound1(0xd1, 3);
            }
        }
    }
    if (falls) {
        if (!channel1Busy()) sound1(0xd1, 3);
        if (tw(0x1f8) != 6) setTl(0x1f8, tl(0x1f8) + 0x2000);   // faster, up to 6 px a tick
        setTl(0x1f4, tl(0x1f4) - tl(0x1f8));
        if (neg16(int16_t(tw(0x1f4)) - 0x86)) {
            if (tb(0x2ee) == 0) { tb(0x2ee) = 0xff; setTln(0x2f0, 0x427d82); }   // Harry squashed
            if (neg16(int16_t(tw(0x1f4)) - 0x44)) {
                setTw(0x1f4, 0x44);                        // down
                if (tw(0x21c) != 0) {
                    h_.w(0x465b02) = 0;
                    setTw(0x21c, uint16_t(tw(0x21c) - 1));
                    if (tw(0x21c) == 1) h_.b(0x465b72) = 0x7c;   // out ("*7")
                    else if (!neg16(int16_t(tw(0x21c)) - 0x74)) h_.w(0x465b02) = int16_t((random() & 7) - 3);
                    else if (tw(0x21c) == 0x42 && tb(0x239) == 0) { sound1(0x4e, 0x60); tb(0x239) = 0xff; }
                } else {
                    // it has just come down: a word of comfort (0x427d40 ..)
                    static const uint32_t kMsg[4] = {0x427d40, 0x427d56, 0x427d6a, 0x427d71};
                    const uint32_t m = kMsg[random() & 3];
                    spawnText(reinterpret_cast<const char*>(harryMem(m)), 0, 0x17, 0x6c);
                    sound1Now(0x89);
                    sound0(0x4c, 0x11);                    // FUN_0042633d(0x4c, 0x60)
                    setTw(0x21c, 0x54);
                    tb(0x239) = 0;
                }
            }
        }
    }
    // 0x427a1e: the keys, on the player's turn
    if (h_.b(0x46617f) == 0) {
        static const struct { uint32_t edge; uint8_t key; } kKeys[3] = {{0x466769, 1}, {0x46676a, 2}, {0x46676b, 3}};
        for (const auto& k : kKeys) {
            if (h_.b(k.edge) != 1) continue;
            h_.b(k.edge) = 0x80;
            const uint32_t a0 = h_.ul(0x4661a4);
            if (a0 + 1 >= Harry::kBase + Harry::kSize) continue;   // (the original writes on into its memory)
            h_.b(a0) = k.key;
            h_.b(a0 + 1) = 0;
            h_.setUl(0x4661a4, a0 + 1);
        }
    }
    if (h_.b(0x465b53) != 0) { h427d92(); return; }       // pulling a lever
    if (h_.w(0x465b41) != 0) {                             // walking to a place
        h_.setSprite("hyirun");
        hAnim(0x4661cc, 0x46166b, 4);
        const int16_t d0 = h_.w(0x465b41);
        if (!neg16(d0 - h_.x())) { h_.x() += 8; h_.facing() = 0; }
        else { h_.x() -= 8; h_.facing() = 0xff; }
        if (h_.x() == d0) {
            h_.facing() = h_.b(0x465b43);
            h_.w(0x465b41) = 0;
            h_.b(0x465b45) = 0xff;
        }
        return;
    }
    if (tln(0x2f0) != 0) {                                 // squashed
        h_.setSprite("hyihwall");
        hAnim2At(&tb(0x2f0), 1);
        return;
    }
    h_.setSprite("hyiready");
    hAnim(0x4661ce, 0x4297d2, 4);
    if (h_.b(0x465b45) != 0 || h_.b(0x46617f) != 0) return;
    const uint32_t rp = h_.ul(0x4661a0);
    if (rp >= Harry::kBase + Harry::kSize) return;
    const uint8_t key = h_.b(rp);
    if (key == 0) return;
    h_.setUl(0x4661a0, rp + 1);
    const int16_t x = h_.x();
    auto at = [&](uint8_t facing) { h_.facing() = facing; h_.b(0x465b45) = 0xff; };
    auto go = [&](int16_t to, uint8_t facing) { h_.w(0x465b41) = to; h_.b(0x465b43) = facing; };
    switch (key) {
    case 1:                                                // the left lever, between 0x54 and 0x84
        if (x == 0x54) at(0);
        else if (x == 0x84) at(0xff);
        else if (neg16(x - 0x70)) go(0x54, 0);
        else go(0x84, 0xff);
        break;
    case 2:                                                // the middle one, between 0x84 and 0xb4
        if (x == 0xb4) at(0xff);
        else if (x == 0x84) at(0);
        else if (neg16(x - 0x9c)) go(0x84, 0);
        else go(0xb4, 0xff);
        break;
    case 3:                                                // the right one, right of 0xb4
        if (x == 0xb4) at(0);
        else go(0xb4, 0);
        break;
    default: break;
    }
}

void PlayLogic::h427d92() {
    // Harry pulls a vault lever (DAT_00465b4f): hyipull with the script 0x427e1b, the lever's
    // frame from the table 0x427e26.
    h_.setSprite("hyipull");
    const bool done = hAnim(0x465b4b, 0x427e1b, 1);
    Obj* lever = objAt(h_.ul(0x465b4f));
    if (lever) lever->frame = mb(0x427e26 + h_.b(0x465b4b));
    if (done) {
        if (lever) lever->frame = 4;
        h_.b(0x4667ae) = 0xff;
        h_.b(0x465b53) = 0;
    }
}

// ---------------------------------------------------------------- FUN_00427e30: Harry at the level start

void PlayLogic::harryLevelStart() {
    h_.onGround() = 0xff;
    h_.b(0x465e64) = 0;
    h_.w(0x465b09) = 2;                                    // camera left lock
    h_.w(0x465b0b) = int16_t((h_.w(0x4658ac) - 0x14) << 4); // right lock
    h_.w(0x465b0d) = int16_t((h_.w(0x4658ae) - 0xe) << 4);  // bottom lock
    if (level_ == 0) h_.w(0x465b0d) = 0x32e;
    if (level_ == 6) h_.w(0x465b0b) = 0x13c3;
    h4281b5();
    if (neg16(h_.x() - 0x10)) h_.x() = 0x10;
    h_.y() += 0x38;
    for (int d7 = 0x26; d7 >= 0; --d7) {                   // walk up to the floor
        h_.y() -= 2;
        feet();
    }
    h_.w(0x4658ba) = int16_t(0xffbc);
    if (level_ == 9) {
        const int16_t x = h_.x();
        if ((!neg16(x - 0x4c0) && neg16(x - 0x500)) || (!neg16(x - 0x6b0) && neg16(x - 0x6f0))) {
            h_.facing() = 0xff;
            if (Obj* a5 = spawnTemplateAtHarry(0x4680f4)) {
                a5->x = int16_t(h_.x() - 0xbe);
                a5->y = int16_t(h_.y() - 0xb4);
            }
        }
    }
    if (h_.facing()) {
        h_.w(0x4658a6) = 0;
        h_.w(0x4658b4) = int16_t(h_.x() - 0xc5);
        h_.w(0x4658ba) = 0x44;
    } else {
        h_.w(0x4658b4) = int16_t(h_.x() - 0x50);
    }
    if (neg16(h_.w(0x4658b4))) h_.w(0x4658b4) = 2;
    else {
        const int16_t d0 = int16_t((h_.w(0x4658ac) - 0x16) << 4);
        if (neg16(d0 - h_.w(0x4658b4))) h_.w(0x4658b4) = d0;
    }
    h_.w(0x4658b6) = int16_t(h_.y() - 0x7e);
    const int16_t d1 = int16_t((h_.w(0x4658ae) - 0x10) << 4);
    if (neg16(d1 - h_.w(0x4658b6))) h_.w(0x4658b6) = d1;
    if (neg16(h_.w(0x4658b6))) h_.w(0x4658b6) = 1;
    if (level_ == 0xe) { h_.w(0x4658b4) = 0x59; h_.w(0x4658b6) = 0x15; return; }
    h_.w(0x465b0d) &= int16_t(0xfff0);
    if (neg16(h_.w(0x4658b4) - h_.w(0x465b09))) h_.w(0x4658b4) = h_.w(0x465b09);
    if (neg16(h_.w(0x465b0b) - h_.w(0x4658b4))) h_.w(0x4658b4) = int16_t(h_.w(0x465b0b) + 1);
    if (neg16(h_.w(0x465b0d) - h_.w(0x4658b6))) h_.w(0x4658b6) = int16_t(h_.w(0x465b0d) + 1);
    if (neg16(h_.w(0x4658b6))) h_.w(0x4658b6) = 0;
}

void PlayLogic::h4281b5() {
    // FUN_004281b5: start positions where Harry faces left (table 0x428145: level, x).
    for (uint32_t a0 = 0x428145; ml(a0) != 0xffffffffu; a0 += 6) {
        if (ml(a0) != uint32_t(level_)) continue;
        const int16_t ex = int16_t(mw(a0 + 4));
        if (neg16(h_.x() - 0x28 - ex) && !neg16(h_.x() + 0x28 - ex)) { h_.facing() = 0xff; return; }
    }
}

// ---------------------------------------------------------------- 0x428505: flinching

void PlayLogic::h428505() {
    addX(h_.vx());
    h_.setSprite("hyirn2sn");
    if (hAnim(0x465b47, 0x4615e0, 4)) {
        if (h_.w(0x465ba6) == 0) { h428dfa(); return; }
        if (h_.w(0x465ba6) < 0) { h42e25e(); return; }
        h42ea52();
        return;
    }
    if (h_.vx() == 0) return;
    if (h_.vx() > 0) { h42ec44(); return; }
    h42e450();
}

// ---------------------------------------------------------------- 0x428588: hit while crouching

void PlayLogic::h428588() {
    h_.setSprite("hyist2cr");
    h42fa4b();
    h_.b(0x465b71) = 0x16;
    if (hAnim(0x465b47, 0x4285d4, 4)) h4285c5();
}

void PlayLogic::h4285c5() { h_.setCont(0x42f2f9); h42f2f9(); }

// ---------------------------------------------------------------- held by a bat

void PlayLogic::h4285d7(Obj& bat) {
    // FUN_004285d7 (a5 = the bat): dazed in its grip; 12 changes of the input break free.
    game_->regA5() = addrOf(&bat);                         // (0x40a771: the bat's handler puts it in a5)
    h_.setCont(0x428625);
    h_.setSprite("hyidazed");
    h_.b(0x465b47) = 0xc;
    h_.setUl(0x465b4f, addrOf(&bat));
    h_.l(0x465b49) = 0;
    h_.b(0x465b4d) = 0;
    h_.w(0x465b45) = int16_t(random() & 7);
    h428625();
}

void PlayLogic::h428625() {
    h428d36();
    const uint32_t a = h_.ul(0x465b4f);
    if (a == 0) { h428dfa(); return; }
    game_->regA5() = a;                                    // 0x42863c
    if (a & 1) { h428dfa(); return; }
    Obj* bat = objAt(a);
    if (!bat || bat->type != 0x12 || bat->w(0x4a) == 0) { h428dfa(); return; }
    h_.w(0x465b45) -= 1;
    if (neg16(h_.w(0x465b45))) {
        h_.w(0x465b45) = int16_t((random() & 0xf) + 0xa);
        sound0(2, 6);                                      // a nip
        h_.damage() += 1;
    }
    hAnim(0x465b4b, 0x4619c1, 6);
    uint8_t d0 = uint8_t(h_.b(0x466761) + h_.b(0x466762) + h_.b(0x466763));
    if (d0) d0 = 1;
    if (d0 != h_.b(0x465b4d)) {
        h_.b(0x465b4d) = d0;
        h_.b(0x465b47) -= 1;
        if (h_.b(0x465b47) == 0) h428dfa();
        return;
    }
    const uint16_t dirs = h_.uw(0x466767);
    if (dirs == 0 || dirs == h_.uw(0x465b49)) return;
    h_.uw(0x465b49) = dirs;
    h_.b(0x465b47) -= 1;
    if (h_.b(0x465b47) == 0) h428dfa();
}

// ---------------------------------------------------------------- quicksand (trigger 0xe)

void PlayLogic::h428a56() {
    h_.setCont(0x428aaa);
    h_.setSprite("hyiready");
    h_.frame() = 0;
    h_.l(0x465b41) = 0x90000;                              // depth 9.0 (16.16, the word at 0x465b43)
    h_.b(0x465b71) = 0;
    h_.l(0x465b45) = 0;
    h_.onGround() = 0;
    h_.vx() = 0;
    h_.w(0x465b55) = 5;
    h428aaa();
}

void PlayLogic::h428aaa() {
    if (h_.w(0x465b45) != 0) {
        // struggling: rises for a few ticks
        h_.w(0x465b45) -= 1;
        if (h_.w(0x465b43) != 0) h_.l(0x465b41) -= 0xf800;
    } else {
        h_.frame() = 0;
        h_.setSprite("hyiready");
        if (h_.w(0x465b43) == 0x2c) { h_.b(0x465b72) = 0x80; h_.frame() = 0xff; }
        h_.l(0x465b41) += 0x7c00;
        h_.w(0x465b55) -= 1;
        if (neg16(h_.w(0x465b55))) {
            h_.w(0x465b55) = int16_t((random() & 7) + 8);
            sound0(2, 3);
        }
    }
    h_.y() -= h_.w(0x465b43);
    snapFeet(false);
    snapFeet(true);
    h_.w(0x465b4b) = h_.y();
    h_.y() += h_.w(0x465b43);
    [&] {
        // wade left or right while the sand goes on
        if (h_.b(0x466767)) {
            h_.facing() = 0xff;
            addX(-0xc000);
            if (trg(h_.x() - 0x20, h_.w(0x465b4b) + 0x12).id == 0xe) return;
            addX(0xc000);
        }
        if (h_.b(0x466768)) {
            h_.facing() = 0;
            addX(0xc000);
            if (trg(h_.x(), h_.w(0x465b4b) + 0x12).id == 0xe) return;
            addX(-0xc000);
        }
    }();
    if (h_.b(0x46676a) != 1) return;
    if (neg16(h_.w(0x465b43) - 3)) { h42c490(); return; }  // shallow enough: jump out
    h_.w(0x465b55) = 0x10;
    sound0(2, 3);
    h_.w(0x465b45) = 6;
    h_.b(0x46676a) = 0x80;
    h_.frame() = 4;
}

// ---------------------------------------------------------------- swiped by the jaguar boss

void PlayLogic::h428cc5() {
    h_.setCont(0x428cdb);
    h_.w(0x465b45) = 0;
    h428cdb();
}

void PlayLogic::h428cdb() {
    h_.setSprite("hyidazed");
    hAnim(0x465b45, 0x42735c, 4);
    h428d36();
    Obj* a0 = objAt(h_.ul(0x465b55));
    if (a0 && a0->type == 0x9e) {
        if (!(a0->sprite && a0->sprite->name() == "jm0swipe" && a0->frame == 3)) return;
    }
    h428d7b();
}

void PlayLogic::h428d36() {
    // FUN_00428d36: gravity, held on the floor.
    h_.vy() += 0x6000;
    addY(h_.vy());
    h_.onGround() = 0xff;
    if (feet()) h_.vy() = 0;
}

void PlayLogic::h428d7b() {
    h_.setCont(0x428dce);
    h_.setUl(0x465b4b, 0x46195e);
    h_.w(0x465b4f) = 0;
    h_.facing() = 0;
    if (Obj* a0 = objAt(h_.ul(0x465b55))) if (!neg16(h_.x() - a0->x)) h_.facing() = 0xff;
    h428dce();
}

void PlayLogic::h428dce() {
    h428d36();
    if (hAnim2(0x465b4b, 4)) h_.b(0x465b72) = 0xff;
}

// ---------------------------------------------------------------- standing (FUN_00428dfa / 0x428e50)

void PlayLogic::h428dfa() {
    h_.b(0x465b97) = 0;
    h428e01();
}

void PlayLogic::h428e01() {
    h_.w(0x465b47) = 0;
    h_.setCont(0x428e50);
    h429498();
    h_.b(0x465b71) = 0;
    h_.vx() = 0;
    h_.vy() = 0;
    h_.onGround() = 0xff;
    h_.setSprite("hyiready");
    h_.frame() = 0;
    h428e50();
}

void PlayLogic::h428e50() {
    h_.w(0x465b9a) = 0;
    // Trigger 9 / 8: a wall to lean on at the right / left (once, until he leaves it).
    Trg t = trg(h_.x() - 0x10, h_.y() + 0x14);
    if (t.id == 9) {
        if (h_.b(0x465d4a) == 0 && h_.facing() == 0) { h429416(); return; }
    } else if (t.id == 8) {
        if (h_.b(0x465d4a) == 0 && h_.facing() != 0) { h429416(); return; }
    } else {
        h_.b(0x465d4a) = 0;
    }
    t = trg(h_.x() - 0x10, h_.y() + 4);
    if (t.id == 9) { if (h_.facing() == 0) { h429416(); return; } }
    else if (t.id == 8) { if (h_.facing() != 0) { h429416(); return; } }
    if (!(h_.b(0x465ae2) != 0 && h_.spriteIs("hyilooka"))) {
        bool idle = false;
        if (h_.b(0x465d49) == 0) {
            h_.w(0x465b41) -= 1;
            if (h_.w(0x465b41) == 0) {
                // the idle animation (FUN_00429498 picked it)
                h_.w(0x465b41) = 1;
                if (h_.ul(0x465b4b) == 0x4296c0) h_.facing() = h_.b(0x465b51);
                if (h_.ul(0x465b4b) == 0x4618b1) { h429e2f(); return; }
                idle = true;
                if (hAnim2(0x465b4b, 4)) {
                    h429498();
                    h_.w(0x465b47) = 0;
                    idle = false;
                }
            }
        }
        if (!idle) {
            h_.setSprite("hyiready");
            hAnim(0x465b47, 0x4297d2, 6);
        }
    }
    if (h_.b(0x466766)) { h42f2d2(); return; }            // down: crouch
    if (h_.w(0x466767) == 0) {
        if (h_.b(0x466769) == 1) {
            // Shoot: the selected weapon if there is ammunition, else the sling stones.
            const int wpn = weaponSel();
            auto stones = [&]() -> bool {
                if (ammo(0) == 0) return false;
                weaponSel() = 0;
                h_.b(0x4667af) = 0;
                h42b6c1();
                return true;
            };
            if (wpn == 0) { if (stones()) return; }
            else if (wpn == 1) {
                if (ammo(1) == 0) { if (stones()) return; }
                else if (h_.ul(0x46681c) == 0) { h42b99b(); return; }   // no boomerang in flight
            } else {
                if (ammo(2) == 0) { if (stones()) return; }
                else if (h_.b(0x465b3d) == 0) { h_.b(0x4667af) = 0xff; h42b6c1(); return; }
            }
        }
        if (h_.b(0x46676b) == 1) { h42b19d(); return; }   // whip
    }
    const int16_t d0 = int16_t(h_.facing() ? h_.x() - 0x20 : h_.x());
    if (trg(d0, h_.y()).id == 9) { h429416(); return; }
    if (h_.w(0x465b9e) != 0) {
        // just landed from a fall of DAT_00465b9e pixels
        if (neg16(h_.w(0x465b9e) - 0x60)) { h42a92e(); return; }
        if (neg16(h_.w(0x465b9e) - 0xb0)) { h42a896(); return; }
        h42a7c7();
        return;
    }
    h429127();
    h4290d7();
}

void PlayLogic::h4290d7() {
    if (h_.b(0x466767)) { if ((h_.b(0x465b3a) & 5) == 0) { h42e25e(); return; } }
    else if (h_.b(0x466768)) { if ((h_.b(0x465b3a) & 0xa) == 0) { h42ea52(); return; } }
    if (h_.b(0x46676a) == 1) h42c38c();                   // jump
}

void PlayLogic::h429127() {
    // FUN_00429127: holding up for a while looks up (hyilooka) and pans the camera.
    if (h_.cont() == 0x428e50) {
        const uint8_t d0 = h_.b(0x465ae2);
        if (!neg16(d0 - 0x16)) {
            h_.frame() = mb(0x4314eb + (uint16_t(d0 - 0x15) >> 3));
            h_.setSprite("hyilooka");
        }
    }
    if (h_.b(0x466765) && h_.b(0x466762) == 0 && h_.w(0x466767) == 0) {
        if (h_.b(0x465ae2) == 0x2d) {
            if (!neg16(h_.w(0x4658a6) - int16_t(0xffb8))) h_.w(0x4658a6) -= 3;
            return;
        }
        h_.b(0x465ae2) += 1;
        return;
    }
    if (h_.b(0x465ae2) == 0) return;
    h_.b(0x465ae2) = 0;
    if (h_.cont() != 0x428e50) return;
    h_.setSprite("hyiready");
    h_.frame() = 0;
    h429498();
}

// ---------------------------------------------------------------- 0x429203: looking up

void PlayLogic::h429203() {
    h_.setCont(0x42922b);
    h_.setSprite("hyilooka");
    h_.w(0x465b4d) = 0;
    h_.b(0x465b41) = 0;
    h42922b();
}

void PlayLogic::h42922b() {
    if (h_.b(0x466762) || h_.b(0x466763) || h_.w(0x466767)) { h428dfa(); return; }
    bool frames = true;                                    // 0x429284
    if (h_.b(0x466765)) {
        if (h_.w(0x4658a6) != int16_t(0xffb8)) h_.w(0x4658a6) -= 3;
        if (h_.w(0x465b4d) == 9) frames = false;
        else h_.w(0x465b4d) += 1;
    } else {
        if (h_.w(0x4658a6) == 0) { h428dfa(); return; }
        h_.w(0x4658a6) += 3;
        if (h_.w(0x465b4d) == 0) frames = false;
        else h_.w(0x465b4d) -= 1;
    }
    if (frames) {
        if (h_.b(0x465b41) != 0) { h429369(); return; }
        h_.frame() = mb(0x4314e7 + (h_.uw(0x465b4d) >> 2));
    }
    if (h_.b(0x465b41) != 0) { h429369(); return; }
    if (h_.b(0x466769) == 1 && ammo(0) != 0 && weaponSel() == 0) {
        // shoot a stone straight up (or up and ahead)
        h_.b(0x466769) = 0x80;
        if (h42b51c()) return;
        h_.w(0x465b4b) = 0;
        h_.b(0x465b41) = 0xff;
        h_.setSprite("hyishoot");
        h_.setUl(0x465b47, h_.b(0x466765) ? 0x4617a9 : 0x46178e);
        h429369();
    }
}

void PlayLogic::h429369() {
    if (!hAnim2(0x465b47, 5)) return;
    h_.b(0x465b41) = 0;
    h_.setSprite("hyilooka");
}

// ---------------------------------------------------------------- 0x42939b: hanging below an object

void PlayLogic::h42939b() {
    h_.setSprite("hyifall");
    hAnim(0x465b47, 0x461746, 4);
    h_.vy() = 0x50000;
    game_->regA5() = h_.ul(0x465b4b);                      // 0x4293d8
    Obj* a5 = objAt(h_.ul(0x465b4b));
    if (a5 && a5->type == 0x54) {
        h_.x() = int16_t(a5->x - 7);
        h_.y() = int16_t(a5->y - 0x10);
        return;
    }
    h_.y() += 7;
}

// ---------------------------------------------------------------- 0x429416: leaning on a wall

void PlayLogic::h429416() {
    h_.setCont(0x42943d);
    h_.setUl(0x465b47, 0x429468);
    h_.w(0x465b4b) = 0;
    h_.vx() = 0;
    h42943d();
}

void PlayLogic::h42943d() {
    h_.b(0x465d4a) = 0x80;
    if (hAnim2(0x465b47, 3)) { h428dfa(); return; }
    h4290d7();
}

void PlayLogic::h429498() {
    // FUN_00429498: pick the next idle animation (after DAT_00465b41 ticks of standing).
    h_.b(0x465b97) += 1;
    h_.w(0x465b47) = 0;
    h_.w(0x465b41) = int16_t((random() & 0x1ff) + 0x80);
    auto special = [&] {
        game_->specialIdle() = false;                      // DAT_0046d04c (F10 / Alt+Y)
        h_.setUl(0x465b4b, 0x429702);
        h_.w(0x465b4f) = 0;
    };
    if (game_->specialIdle() || h_.b(0x465b97) == 0x64) { special(); return; }
    auto common = [&] {
        const uint32_t i = random() & 3;
        h_.setUl(0x465b4b, ml(0x4296b0 + i * 4));
        h_.w(0x465b4f) = 0;
    };
    if (neg8(h_.b(0x465b97) - 4) || (random() & 1) != 0) { common(); return; }
    // A wall near Harry (class bit 0 from x - 0x40 to x + 0x20 at y - 0x20): lean on it.
    int16_t d0 = int16_t(h_.x() - 0x40);
    const int16_t d1 = int16_t(h_.y() - 0x20);
    for (int d3 = 6; d3 >= 0; --d3, d0 += 0x10) {
        if (trg(d0, d1).cls & 1) {
            h_.setUl(0x465b4b, 0x4296c0);
            h_.w(0x465b4f) = 0;
            h_.b(0x465b51) = 0xff;
            if (d3 >= 2) h_.b(0x465b51) = 0;
            return;
        }
    }
    // A ceiling over him: the common idles.
    int16_t y = int16_t(h_.y() - 0x20);
    for (int d3 = 3; d3 >= 0; --d3, y -= 0x10)
        if (trg(h_.x(), y).cls & 1) { common(); return; }
    h_.setUl(0x465b4b, 0x4618b1);                          // open sky: the whistling / skipping idles
    h_.w(0x465b4f) = 0;
    if (random() & 1) h_.setUl(0x465b4b, 0x42976e);
}

// ---------------------------------------------------------------- the continue screen (FUN_0042989c)

void PlayLogic::h42989c() {
    h_.setCont(0x4299ad);
    h_.frame() = 0xff;
    Obj* a3 = newObjAtHarry(5, true);                      // the lit floor, two halves
    if (a3) { a3->flags = 0x6000; a3->sprite = game_->findSprite("fxifloor"); }
    h_.setUl(0x465b55, addrOf(a3));
    Obj* a5 = newObjAtHarry(0, true);
    if (a5) {
        a5->flags = 0x6000; a5->sprite = game_->findSprite("fxifloor");
        a5->link = a3;
        a5->w(0x26) = 0x6663;
        a5->facing = 0xff;
    }
    h_.setUl(0x465b5f, addrOf(a5));
    a5 = newObjAtHarry(2, true);                           // the torch
    if (a5) {
        a5->flags = 0x6000; a5->sprite = game_->findSprite("fxitrch");
        a5->behind = 0xff;
        a5->x -= 2;
    }
    h_.setUl(0x465b59, addrOf(a5));
    h_.setSprite("hyimatch");
    h_.frame() = 0xff;
    h_.w(0x465b49) = 0;
    h_.w(0x465b53) = 0;
    h_.w(0x465b51) = 0;
    h_.b(0x465b51) = 0xff;                                 // "yes" selected
    h4299ad();
}

void PlayLogic::h4299ad() {
    if (h_.w(0x465b47) != 0x14) {
        h_.w(0x465b47) += 1;
        if (h_.w(0x465b47) != 0x14) return;
        h_.l(0x465b4b) = 0;
        h_.w(0x465b41) = 0;
        h_.b(0x465b41) = 2;
        h_.w(0x465b49) = 0;
        h_.setUl(0x46655e, 0x462614);                      // fade the palette in
        h_.setUl(0x466562, 0x42981c);
        h_.w(0x466566) = 0x3f;
        h_.w(0x466568) = 0xf;
        h_.b(0x465d49) = 1;
        sound0(0xc0, 8);                                   // the match strikes
    }
    if (h_.w(0x466765) != 0) h_.uw(0x465b51) = h_.uw(0x466765);   // up = yes, down = no
    if (h_.b(0x466761) || h_.b(0x466762) || h_.b(0x466763) || h_.b(0x466764)) {
        if (neg16(h_.w(0x465b49) - 0xa)) h_.b(0x465b72) = 0x7f;
        h_.b(0x465b53) = 0xff;
    }
    h_.setSprite("hyimatch");
    for (int guard = 0; guard < 16; ++guard) {
        if (hAnim(0x465b49, 0x461902, 4)) {
            // The match has burnt down: a continue is used (yes) or not.
            h_.setUl(0x46655e, 0x462614);
            h_.setUl(0x466562, 0x4621ae);
            h_.w(0x466566) = 0x3f;
            h_.w(0x466568) = 0xf;
            if (Obj* o = objAt(h_.ul(0x465b55))) objects_.free(*o);
            if (Obj* o = objAt(h_.ul(0x465b59))) objects_.free(*o);
            if (h_.b(0x465b51) != 0) {
                const int c = std::min(game_->stats().continues, 9) + 0x1a;
                for (Obj* p : objects_.list())
                    if (p->sprite && p->sprite->name() == "ltr_sm" && p->frame == c) { p->frame -= 1; break; }
            }
            h428dfa();
            return;
        }
        const int16_t f = h_.w(0x465b49);
        if (f == 0xb) h_.b(0x465b53) = 0;
        if (f == 0x18 && h_.b(0x465b53) == 0) { h_.w(0x465b49) = 0xd; continue; }
        if (f == 0x16) {
            h_.w(0x465b49) = 0x13;
            if (h_.b(0x465b53) == 0) {
                if (h_.b(0x465b51) == 0) continue;
                if (!channel1Busy()) sound1(0x28, 0x12);
                h_.w(0x465b49) = 0x16;
                continue;
            }
            h_.w(0x465b49) = 0x16;
            h_.uw(0x465c58) = h_.uw(0x465b51);
            continue;
        }
        if (f == 0x10) {
            h_.w(0x465b49) = 0xd;
            if (h_.b(0x465b53) != 0) {
                h_.w(0x465b49) = 0x18;
                h_.uw(0x465c58) = h_.uw(0x465b51);
                continue;
            }
            if (h_.b(0x465b52) == 0) continue;
            if (!channel1Busy()) sound1(0x28, 0x12);
            h_.w(0x465b49) = 0x10;
        }
        break;
    }
    // the torch and the lit floor follow the flame
    Obj* torch = objAt(h_.ul(0x465b59));
    Obj* f1 = objAt(h_.ul(0x465b55));
    Obj* f2 = objAt(h_.ul(0x465b5f));
    const uint8_t pos = h_.b(0x465b49);
    if (torch) { torch->y = h_.y(); torch->frame = mb(0x461921 + pos); }
    if (f1) { f1->y = h_.y(); f1->frame = mb(0x46193f + pos); }
    if (f2) {
        f2->y = h_.y(); f2->frame = mb(0x46193f + pos);
        if (f1) f2->x = f1->x;
        f2->x += int16_t(int16_t(mw(0x4314c9 + f2->frame * 2)) * 2);
    }
}

// ---------------------------------------------------------------- 0x429e2f: meditating (the zen idle)

void PlayLogic::h429e2f() {
    h_.setCont(0x429e5b);
    h_.onGround() = 0;
    h_.setSprite("hyizen");
    h_.setUl(0x465b47, 0x4618b1);
    h_.w(0x465b4b) = 0;
    h429e5b();
}

void PlayLogic::h429e5b() {
    if (h_.l(0x466765) != 0 || h_.l(0x466761) != 0) { h428dfa(); return; }
    if (!hAnim2(0x465b47, 5)) return;
    h_.setCont(0x429eb5);                                  // ... and levitates
    h_.w(0x465b4b) = 0xd0;
    h_.w(0x465b4d) = 0x200;
    h_.frame() = 4;
}

void PlayLogic::h429eb5() {
    h_.w(0x465b4d) -= 1;
    if (h_.w(0x465b4d) != 0) {
        addY(-0x2400);
        h_.w(0x465b4b) -= 1;
        if (h_.w(0x465b4b) == 0) h_.w(0x465b4b) = 0x2f;
        if (neg16(h_.w(0x465b4b) - 0x18)) addY(0x4800);
        if (h_.l(0x466761) == 0 && h_.l(0x466765) == 0) return;
    }
    h_.setCont(0x429f1c);
    h_.l(0x465b47) = 0;
}

void PlayLogic::h429f1c() {
    h_.l(0x465b47) += 0x8000;
    addY(h_.l(0x465b47));
    h_.onGround() = 0;
    if (!feet()) return;
    h_.setCont(0x429f76);
    h_.setUl(0x465b47, 0x4618ca);
    h_.w(0x465b4b) = 0;
}

void PlayLogic::h429f76() {
    if (hAnim2(0x465b47, 5)) h428e01();
}

// ---------------------------------------------------------------- 0x429f9a

void PlayLogic::h429f9a() {
    h_.w(0x465b47) = 0;
    h_.setCont(0x429fb0);
    h429fb0();
}

void PlayLogic::h429fb0() {
    if (hAnim(0x465b47, 0x429fde, 4)) h428dfa();
}

// ---------------------------------------------------------------- 0x429fe2: hanging from a bungee vine (h_bungee moves him)

void PlayLogic::h429fe2() {
    h_.w(0x465b9e) = 1;
    h_.w(0x4658a6) += 3;
    if (!neg16(h_.w(0x4658a6) - 0x14)) h_.w(0x4658a6) = 0x14;
    if (h_.b(0x465b46) != 0) h_.b(0x465b46) -= 1;
}

void PlayLogic::h42a017() {
    h_.setSprite("hyiready");
    h_.b(0x465b38) = 0xff;                                 // +0x29: drawn upside down
    h_.frame() = 0;
    if (h_.b(0x466765) == 0) return;
    h_.b(0x465b38) = 0;
    h_.y() += 0x30;
    h42c45d();
}

void PlayLogic::h42a048() {
    h_.w(0x4658a6) += 3;
    if (!neg16(h_.w(0x4658a6) - 0x30)) h_.w(0x4658a6) = 0x30;
}

// ---------------------------------------------------------------- 0x42a064: twirling

void PlayLogic::h42a064() {
    h_.setCont(0x42a08f);
    h_.setSprite("hyitwirl");
    h_.w(0x465b47) = 0;
    h_.frame() = 0xff;
    h_.b(0x465b4b) = 5;
    h42a08f();
}

void PlayLogic::h42a08f() {
    if (h_.b(0x465b4b) != 0) {
        h_.b(0x465b4b) -= 1;
        if (h_.b(0x465b4b) == 0) sound1(0xa2, 0x12);
    }
    feet();
    int16_t d0 = int16_t(h_.x() - h_.w(0x4658b4));
    if (neg16(d0 - int16_t(0xffa6)) || !neg16(d0 - 0x1a4)) return;
    d0 = int16_t(h_.y() - h_.w(0x4658b6));
    if (neg16(d0 - int16_t(0xffa6)) || !neg16(d0 - 0x154)) return;
    if (hAnim(0x465b47, 0x4314c2, 5)) h428dfa();
}

// ---------------------------------------------------------------- doors and tubes: in (0x42a142), out (FUN_0042a1a1)

void PlayLogic::h42a142() {
    h_.setCont(0x42a174);
    h_.setSprite("hyienter");
    h_.setUl(0x465b47, 0x4618ac);
    h_.w(0x465b4b) = 0;
    h_.frame() = 0xff;
    h_.onGround() = 0;
    h42a174();
}

void PlayLogic::h42a174() {
    addY(-0x5000);
    if (hAnim2(0x465b47, 4)) h42a46d();
}

void PlayLogic::h42a1a1() {
    h_.setCont(0x42a217);
    h_.setSprite("hyiemrg");
    h_.setUl(0x465b47, 0x4618a3);
    h_.frame() = 0;
    h_.w(0x465b4b) = 0;
    h_.b(0x465b01) = 0x40;
    h_.onGround() = 0xff;
    feet();
    h_.w(0x465b41) = h_.y();
    if (h_.b(0x465b43) == 0) { h_.x() -= 4; h_.y() -= 9; }
    h_.frame() = 0xff;
    h_.x() += 0x13;
    h42a217();
}

void PlayLogic::h42a217() {
    h_.onGround() = 0;
    int16_t d0 = int16_t(h_.x() - h_.w(0x4658b4));
    if (neg16(d0) || !neg16(d0 - 0x140)) return;           // waits until the camera shows him
    d0 = int16_t(h_.y() - h_.w(0x4658b6));
    if (neg16(d0) || !neg16(d0 - 0xc8)) return;
    if (h_.b(0x465b43) != 0) {
        h_.b(0x465b3c) = 0xf;                              // walks out
        h_.facing() = 0;
        h4281b5();
        h428dfa();
        return;
    }
    addY(0x8000);
    if (neg16(h_.w(0x465b41) - h_.y())) {
        h_.facing() = 0;
        h4281b5();
        h428dfa();
        return;
    }
    h_.b(0x465b01) = 0x44;
    hAnim2(0x465b47, 5);
}

// ---------------------------------------------------------------- FUN_0042a325: sinking into a tar pit

void PlayLogic::h42a325() {
    h_.setCont(0x42a36f);
    h_.setSprite("hyisink");
    h_.w(0x465b55) = 3;
    h_.onGround() = 0;
    h_.w(0x465b47) = 0;
    h_.b(0x465b49) = 0;
    if ((random() & 7) == 0) h_.b(0x465b49) = 0xff;       // the variant with the hand
    h42a36f();
}

void PlayLogic::h42a36f() {
    if (hAnim(0x465b47, h_.b(0x465b49) ? 0x461889 : 0x461875, 5)) {
        h_.b(0x465b72) = 0x80;
        h_.frame() = 0xff;
        return;
    }
    h_.w(0x465b55) -= 1;
    if (!neg16(h_.w(0x465b55))) return;
    h_.w(0x465b55) = int16_t((random() & 7) + 8);
    if (h_.frame() == 0xff || channel0Busy()) return;
    sound0(int(random() & 1) + 0x14, 0xa);                 // glub
}

// ---------------------------------------------------------------- warps: 0x42a423 (twirl), FUN_0042a46d

void PlayLogic::h42a423() {
    h_.setCont(0x42a443);
    h_.setSprite("hyitwirl");
    h_.w(0x465b47) = 0;
    h42a443();
}

void PlayLogic::h42a443() {
    if (hAnim(0x465b47, 0x4619c7, 4)) h42a46d();
}

void PlayLogic::h42a46d() {
    // FUN_0042a46d: come out at the trigger whose id is DAT_00465b45 (or at the type 0x22 object).
    h_.frame() = 0xff;
    h_.onGround() = 0xff;
    // (the trigger record in the load block; each column's word added unsigned, its pairs walked
    // two bytes at a time to the 0xff)
    const uint8_t d4 = h_.b(0x465b45);
    bool found = false;
    int16_t d7 = 0, d1 = 0;
    for (int col = 0; col < uint16_t(h_.w(0x4658ac)) && !found; ++col) {
        const uint16_t p = uint16_t(trgAt(col * 2) << 8 | trgAt(col * 2 + 1));
        if (p == 0) continue;
        for (uint16_t e = p, n = 0; trgAt(e) != 0xff && n < 0x8000; e = uint16_t(e + 2), ++n)
            if (trgAt(e + 1) == d4) { d1 = int16_t(trgAt(e) << 4); d7 = int16_t(col << 4); found = true; break; }
    }
    if (!found) {
        for (Obj* p : objects_.list())
            if (p->type == 0x22) { d7 = int16_t(p->x & 0xfff0); d1 = int16_t(p->y & 0xfff0); found = true; break; }
    }
    if (!found) { std::fprintf(stderr, "harry: warp destination %#x not found\n", d4); return; }   // (an engine error exit)
    h_.x() = int16_t(d7 - 0x10);
    h_.y() = int16_t(d1 + 0x10);
    if (neg16(h_.x() - 0x10)) h_.x() = 0x10;
    h_.y() -= 0x20;
    for (int i = 0; i <= 0x10; ++i) { h_.y() += 4; feet(); }
    h_.vx() = 0;
    h_.vy() = 0;
    h_.b(0x465b00) = 0xff;
    if (h_.b(0x465b46) == 0) { h42a1a1(); return; }
    // Into another area of the level: the objects start again (FUN_00432713).
    fadeCmd() = 0xffffff01;                                 // DAT_0046b1b0
    sound1Now(0x89);
    ch1Id_ = 0;
    h4311bf();
    h_.facing() = 0;
    h_.b(0x465b01) = 0x40;
    if (h_.b(0x465b43) == 0) {
        h42a1a1();
        h_.x() -= 0xb;
        h_.facing() = 0;
    } else {
        h_.b(0x465b3c) = 0x19;
        h_.facing() = 0;
    }
    levelRestart();                                        // FUN_00432713
}

// ---------------------------------------------------------------- 0x42a6e7: pulling a lever

void PlayLogic::h42a6e7() {
    h_.setSprite("hyipull");
    const bool done = hAnim(0x465b47, 0x427e1b, 4);
    Obj* lever = objAt(h_.ul(0x465b41));
    if (lever) lever->frame = mb(0x427e26 + h_.b(0x465b47));
    if (!done) return;
    if (lever) lever->frame = 4;
    h_.b(0x4667ae) = 0xff;
    h428dfa();
}

void PlayLogic::h42a76e() { h_.w(0x465b9e) = 0; h428dfa(); }

// ---------------------------------------------------------------- landings

void PlayLogic::h42a77c() {
    h_.setCont(0x42a792);
    h_.w(0x465b47) = 0;
    h42a792();
}

void PlayLogic::h42a792() {
    h_.setSprite("hyigland");
    if (hAnim(0x465b47, 0x461860, 3)) h428dfa();
}

void PlayLogic::h42a7c7() {
    // FUN_0042a7c7: a long fall (DAT_00465b9e >= 0xb0): knocked flat
    h_.w(0x465b9e) = 0;
    h_.setCont(0x42a817);
    h_.setUl(0x465b43, 0x461848);
    h_.w(0x465b47) = 0;
    if (!channel0Busy()) sound0(0x4a, 4);
    h42a817();
}

void PlayLogic::h42a817() {
    if (hAnim2(0x465b43, 3)) h428dfa();
}

void PlayLogic::h42a838() {
    h_.setCont(0x42a86b);
    h_.setSprite("hyisomr3");
    h_.w(0x465b9e) = 0;
    h_.vy() = 0;
    h_.w(0x465b47) = 0;
    h42a86b();
}

void PlayLogic::h42a86b() {
    if (hAnim(0x465b47, 0x4615e0, 4)) h428dfa();
}

void PlayLogic::h42a896() {
    // FUN_0042a896: a medium fall
    h_.setCont(0x42a8f9);
    h_.w(0x465b47) = 0;
    h_.w(0x465b9e) = 0;
    if (!channel0Busy() && game_->logicTicks() - game_->lastLandTick() >= 0x28) {
        sound0(0x3e, 4);
        game_->lastLandTick() = game_->logicTicks();
    }
    h42a8f9();
}

void PlayLogic::h42a8f9() {
    h_.setSprite("hyigland");
    if (hAnim(0x465b47, 0x461842, 3)) h428dfa();
}

void PlayLogic::h42a92e() {
    // a short fall
    h_.setCont(0x42a991);
    h_.w(0x465b47) = 0;
    h_.w(0x465b9e) = 0;
    if (!channel0Busy() && game_->logicTicks() - game_->lastLandTick() >= 0x28) {
        sound0(0x1e, 4);
        game_->lastLandTick() = game_->logicTicks();
    }
    h42a991();
}

void PlayLogic::h42a991() {
    if (h_.w(0x466767) || h_.b(0x466766) || h_.b(0x466761) || h_.b(0x466762) || h_.b(0x466763)) { h428dfa(); return; }
    h_.setSprite("hyigland");
    if (hAnim(0x465b47, 0x46183c, 3)) h428dfa();
}

// ---------------------------------------------------------------- 0x42aa08: the mine cart (level 7)

void PlayLogic::h42aa08() {
    h_.setCont(0x42aa3b);
    h_.l(0x465b45) = 0;
    h_.l(0x465b59) = 0;
    h_.w(0x465b5d) = 0;
    h_.w(0x465b51) = 0;
    h_.w(0x465b47) = 0;
    h42aa3b();
}

void PlayLogic::h42aa3b() {
    if (level_ == 7) {
        if (h_.l(0x465b59) != 0) {
            h_.b(0x465b71) = 0;
            if (!hAnim2(0x465b59, 2)) return;
            h_.l(0x465b59) = 0;
            h_.w(0x465b5d) = 0;
        }
        if (h_.b(0x466190) != 0) {
            if (!h_.spriteIs("hyipsbal") && !h_.spriteIs("hyilsbal") && !h_.spriteIs("hyiblrec")) {
                h_.setUl(0x465b53, 0x461806);
                h_.w(0x465b57) = 0;
            }
            if (!hAnim2(0x465b53, 3)) return;
            h_.b(0x466190) = 0;
            return;
        }
    }
    h_.setSprite("hyiskte1");
    hAnim(0x465b47, 0x4314a1, 4);
    if (neg8(h_.b(0x465b47) - 0x1a)) {
        if (h_.b(0x465b45) == 0) {
            const bool go = h_.b(0x465b46) == 0 ? h_.b(0x466768) != 0 : h_.b(0x466767) != 0;
            if (go) { h_.b(0x465b47) = 0x1a; h_.b(0x465b48) = 0; }
        }
    } else if (h_.b(0x465b45) != 0) {
        h_.w(0x465b47) = 0;
    }
    if (neg8(h_.frame() - 0x14)) return;
    if (!neg8(h_.frame() - 0x28)) { h_.frame() -= 0x28; h_.setSprite("hyiskte3"); return; }
    h_.frame() -= 0x14;
    h_.setSprite("hyiskte2");
}

void PlayLogic::h42abaa() {
    h_.setSprite("hyiskte4");
    if (hAnim(0x465b47, 0x4619cf, 3)) h42aa08();
}

// ---------------------------------------------------------------- zip lines (trigger 0xc above Harry)

void PlayLogic::h42abdf() {
    h_.y() += 0x18;
    h_.x() -= 0x10;
    h42abee();
}

void PlayLogic::h42abee() {
    h_.setCont(0x42acc2);
    h_.b(0x46676d) = 0x80;
    h_.w(0x465b47) = 0;
    h_.w(0x465b49) = 1;
    h_.vx() = 0;
    h_.y() += 8;
    h_.w(0x465b43) = 0x14;
    h_.onGround() = 0;
    h_.facing() = 0;
    // the line runs downhill to the left when its shape is higher on the right (.dt2)
    const ph::FloorHit f = ph::probeNear(dt2(), h_.x() - 0x10, h_.y() - 0x3c);
    if (f.hit && !neg16(ph::kCollisionHeights[f.shape * 16] - ph::kCollisionHeights[f.shape * 16 + 15])) h_.facing() = 0xff;
    h42acc2();
}

void PlayLogic::h42acc2() {
    h_.b(0x465b95) = 0x1e;
    bool slide = true;
    if (h_.w(0x465b49) != 0) {
        h_.setSprite("hyimzip");
        slide = hAnim(0x465b49, 0x4615e0, 3);               // reaching up first
    }
    if (slide) {
        h_.setSprite("hyizip");
        hAnim(0x465b47, 0x431490, 5);
        if (h_.facing()) {
            if (h_.w(0x465ba6) != -7) h_.vx() -= 0x2200;
        } else if (h_.w(0x465ba6) != 7) {
            h_.vx() += 0x2200;
            if (h_.w(0x465ba6) == 3 && level_ == 5) {
                const int16_t d0 = int16_t(h_.x() - 0x1374);
                if (!neg16(d0 - int16_t(0xff40)) && neg16(d0 - 0xc0)) {
                    h_.b(0x465d49) = 1;                    // the end of the Lakamul rain forest
                    h_.b(0x466507) = 0;
                }
            }
        }
    }
    addX(h_.vx());
    if (!channel1Busy()) sound1(0xc1, 1);
    if (level_ != 3) {
        h_.w(0x465b43) -= 1;
        if (h_.w(0x465b43) == 0) {
            // leaves shaken from the trees
            int16_t d0 = int16_t((random() & 3) + 4);
            if (level_ == 1 || level_ == 6) d0 += 0xe;
            h_.w(0x465b43) = d0;
            if (Obj* a5 = newObjAtHarry(1, true)) {
                a5->w(0x4e) = 0x46;
                a5->y = h_.y(); a5->x = h_.x();
                a5->w(0x58) = 0x1e;
                a5->b(0x57) = 0xff;
                a5->y -= 0x36;
                a5->type = 0x40;
                a5->flags = 0;
                const uint32_t r = random() & 0x7fff;
                a5->l(0x36) = int32_t(r);
                if (r & 1) {
                    a5->sprite = game_->findSprite("fx_leaf1");
                    a5->script = harryMem(0x4619df);
                    a5->l(0x36) += 0x6000;
                } else {
                    a5->sprite = game_->findSprite("fxzleaf2");
                    a5->script = harryMem(0x4619e8);
                    a5->l(0x36) += 0x10000;
                }
            }
        }
    }
    if (h_.b(0x46676d) != 1) {
        if (trg(h_.x() - 8, h_.y() - 0x40).id == 0xd) { h42afdf(); return; }   // the end of the line
        int y = h_.y() - 0x3e;
        const ph::FloorHit f = ph::probeNear(dt2(), h_.x() - 0x10, y);
        if (f.hit) y = f.surfaceY;
        h_.y() = int16_t(y + 0x42);
        if (f.hit) return;
    }
    // let go
    h_.vy() = 0x20000;
    h_.b(0x465bb2) = 0x14;
    h_.y() += 8;
    if (ch1Id_ == 0xc1) { sound1Now(0x89); ch1Id_ = 0; }
    h42c467();
}

void PlayLogic::h42afdf() {
    h_.x() += 8;
    h_.setCont(0x42b049);
    h_.l(0x465b43) = 0;
    h_.l(0x465b47) = 0;
    if (ch1Id_ == 0xc1) { sound1Now(0x89); ch1Id_ = 0; }
    sound0(0x4a, 4);                                       // the thump at the end of the line
    h42b049();
}

void PlayLogic::h42b049() {
    h_.onGround() = 0;
    if (h_.w(0x465b47) == 0x200) {
        if (h_.b(0x465b49) != 0x1e) { h_.b(0x465b49) += 1; return; }
        if (h_.w(0x465b45) != 8) h_.l(0x465b43) += 0x4800;
        addY(h_.l(0x465b43));
        h_.onGround() = 0;
        if (!feet()) return;
    }
    const bool done = hAnim(0x465b47, 0x4314f8, 4);
    h_.setSprite("hyihwall");
    if (!neg8(h_.b(0x465b47) - 0x14)) {
        h_.setSprite("hyisprng");
        if (level_ == 7) {
            h_.setSprite("hyihwall");
            h_.frame() = 7;
            h_.b(0x465b72) = 0x80;
            return;
        }
    }
    if (!neg8(h_.frame() - 0xf)) {
        h_.frame() -= 0x10;
        h_.setSprite("hyigland");
    }
    if (done) h428dfa();
}

// ---------------------------------------------------------------- the whip

void PlayLogic::h42b148() {
    // a wall in front: the short whip
    h_.setCont(0x42b168);
    h_.setSprite("hyiwhip");
    h_.w(0x465b47) = 0;
    h42b168();
}

void PlayLogic::h42b168() {
    h42b2bc();
    h429127();
    if (hAnim(0x465b47, 0x4617db, 3)) h428dfa();
}

void PlayLogic::h42b19d() {
    // FUN_0042b19d
    h_.setUl(0x465b4d, 0x42b4f9);
    h42b1a7();
}

void PlayLogic::h42b1a7() {
    h_.w(0x465b47) = 0;
    h_.b(0x46676b) = 0x80;
    if (h42b51c()) { h42b148(); return; }
    h_.setCont(0x42b1ea);
    h_.setSprite("hyiwhip");
    h_.b(0x465b49) = 1;
    h_.b(0x465b4a) = 0;
    h_.onGround() = 0xff;
    h42b1ea();
}

void PlayLogic::h42b1ea() {
    h42b2bc();
    h429127();
    if (hAnim(0x465b47, h_.ul(0x465b4d), 4)) { h428dfa(); return; }
    const uint8_t f = h_.frame();
    if (h_.b(0x466762) == 0 && h_.w(0x466767) == 0 && h_.b(0x466766) == 0) {
        if (f == 5 && h_.b(0x465b4a) != 0) { h42b438(); return; }   // whip held again: the second crack
    } else if (f == 5 || f == 6 || f == 7 || f == 4 || f == 3) {
        h428dfa();
        return;
    }
    h42b28b();
}

void PlayLogic::h42b28b() {
    // a fresh press of the whip button during the swing queues another one
    if (h_.b(0x465b49) == 0) {
        if (h_.b(0x466763)) h_.w(0x465b49) = 0x101;
    } else if (h_.b(0x466763) == 0) {
        h_.b(0x465b49) = 0;
    }
}

void PlayLogic::h42b2bc() {
    // FUN_0042b2bc: the whip's reach for this sprite and frame (table 0x4612e0), or none.
    uint32_t a0 = 0x4612e0;
    for (int guard = 0; guard < 64; ++guard) {
        const uint32_t slot = ml(a0);
        a0 += 4;
        if (slot == h_.spriteSlot()) {
            for (; mw(a0) != 0xffff; a0 += 0xa) {
                if (mb(a0) != h_.frame()) continue;
                const int16_t dx0 = int16_t(mw(a0 + 2)), dy0 = int16_t(mw(a0 + 4));
                const int16_t dx1 = int16_t(mw(a0 + 6)), dy1 = int16_t(mw(a0 + 8));
                if (h_.facing()) {
                    h_.w(0x465aed) = int16_t(h_.x() - dx0);
                    h_.w(0x465aeb) = int16_t(h_.x() - dx1);
                } else {
                    h_.w(0x465aeb) = int16_t(h_.x() + dx0);
                    h_.w(0x465aed) = int16_t(h_.x() + dx1);
                }
                h_.w(0x465aef) = int16_t(h_.y() + dy0);
                h_.w(0x465af1) = int16_t(h_.y() + dy1);
                return;
            }
            break;
        }
        for (;; a0 += 0xa) {
            if (mw(a0) == 0xfffe) { h_.w(0x465aeb) = 0; return; }
            if (mw(a0) == 0xffff) { a0 += 2; break; }
        }
    }
    h_.w(0x465aeb) = 0;
}

void PlayLogic::h42b438() {
    h_.w(0x465b47) = 0;
    h_.setCont(0x42b460);
    h_.setSprite("hyirwhip");
    h_.b(0x465b49) = 1;
    h_.b(0x465b4a) = 0;
    h42b460();
}

void PlayLogic::h42b460() {
    h42b2bc();
    if (hAnim(0x465b47, 0x42b4ef, 5)) {
        h_.setUl(0x465b4d, h_.b(0x465b4a) == 0 ? 0x42b518 : 0x42b509);
        h42b1a7();
        return;
    }
    if (h_.l(0x466765) != 0) return;
    h42b28b();
}

bool PlayLogic::h42b51c() {
    // FUN_0042b51c: a wall (class bit 0) 0x18..0x28 px ahead at y - 0x10; id 3 does not count.
    const uint8_t keep = h_.b(0x4658c3);
    h_.b(0x4658c3) = 0;
    bool wall = (trg(h_.x() + (h_.facing() ? -0x38 : 0x18), h_.y() - 0x10).cls & 1) != 0;
    if (!wall) wall = (trg(h_.x() + (h_.facing() ? -0x48 : 0x28), h_.y() - 0x10).cls & 1) != 0;
    h_.b(0x4658c3) = keep;
    return wall;
}

bool PlayLogic::h42b5eb() {
    // FUN_0042b5eb: the nearer probe of FUN_0042b51c alone
    const uint8_t keep = h_.b(0x4658c3);
    h_.b(0x4658c3) = 0;
    const bool wall = (trg(h_.x() + (h_.facing() ? -0x38 : 0x18), h_.y() - 0x10).cls & 1) != 0;
    h_.b(0x4658c3) = keep;
    return wall;
}

// ---------------------------------------------------------------- throwing (sling stones, the stone)

void PlayLogic::h42b66e() {
    // a wall in front: no throw
    h_.setCont(0x42b692);
    h_.setUl(0x465b47, 0x4617c4);
    h_.w(0x465b4b) = 0;
    h_.onGround() = 0xff;
    h42b692();
}

void PlayLogic::h42b692() {
    h429127();
    if (hAnim2(0x465b47, 3)) h428dfa();
}

void PlayLogic::h42b6b8() { h_.b(0x465b4c) = 0xff; h42b6cf(); }   // after spinning the sling: a power shot

void PlayLogic::h42b6c1() {
    // FUN_0042b6c1
    h_.b(0x465b4c) = 0;
    h_.onGround() = 0xff;
    h42b6cf();
}

void PlayLogic::h42b6cf() {
    h_.setUl(0x465b43, h_.b(0x466765) ? 0x4617a9 : 0x46178e);
    h_.b(0x466769) = 0x80;
    if (h42b51c()) { h42b66e(); return; }
    h_.setCont(0x42b726);
    h_.setSprite("hyishoot");
    h_.b(0x465b49) = 1;
    h_.b(0x465b4a) = 0;
    h_.w(0x465b47) = 0;
    h42b726();
}

void PlayLogic::h42b726() {
    for (int guard = 0; guard < 4; ++guard) {
        h429127();
        if (hAnim2(0x465b43, 2)) {
            if (h_.b(0x4667af) != 0 || h_.w(0x466767) != 0 || ammo(0) == 0 || weaponSel() != 0) { h428dfa(); return; }
            if (h_.b(0x466761)) { h42b875(); return; }    // still held: spin the sling
            h428dfa();
            return;
        }
        if (h_.b(0x465b4c) == 0 && h_.b(0x4667af) == 0) {
            if (h_.b(0x466762) || h_.w(0x466767) || h_.b(0x466766)) {
                const uint8_t f = h_.frame();
                if (f == 4 || f == 2 || f == 5) { h428dfa(); return; }
            } else if (h_.frame() == 4 && ammo(0) != 0 && h_.b(0x465b4a) != 0) {
                // pressed again: the next stone straight away
                h_.b(0x465b4c) = 0;
                h_.w(0x465b47) = 0;
                h_.setUl(0x465b43, h_.b(0x466765) ? 0x4617a9 : 0x46178e);
                h_.b(0x465b49) = 1;
                h_.b(0x465b4a) = 0;
                continue;
            }
        }
        break;
    }
    if (h_.b(0x465b49) == 0) {
        if (weaponSel() == 0 && h_.b(0x466761)) h_.w(0x465b49) = 0x101;
    } else if (h_.b(0x466761) == 0) {
        h_.b(0x465b49) = 0;
    }
}

void PlayLogic::h42b875() {
    h_.setCont(0x42b89b);
    h_.setSprite("hyisspi2");
    h_.w(0x465b47) = 0;
    h_.w(0x465b49) = 0xa0;
    h42b89b();
}

void PlayLogic::h42b89b() {
    h429127();
    if (h_.w(0x465b49) != 0x10) h_.w(0x465b49) -= 2;      // spins faster and faster
    hAnim(0x465b47, 0x461746, h_.uw(0x465b49) >> 4);
    if (h_.w(0x465b47) == 2) sound0(0x1b, 7);
    if (h_.b(0x466761)) return;
    if (!neg16(h_.w(0x465b49) - 0x20)) h42b6c1();
    else h42b6b8();
}

// ---------------------------------------------------------------- the boomerang

void PlayLogic::h42b939() {
    h_.setCont(0x42b94c);
    h_.w(0x465b47) = 0;
    h42b94c();
}

void PlayLogic::h42b94c() {
    h429127();
    h_.setSprite("hyibmrng");
    if (hAnim(0x465b47, 0x42b986, 3)) h428dfa();
}

void PlayLogic::h42b98f() { h_.setUl(0x465b4b, 0x461785); h42b9a5(); }
void PlayLogic::h42b99b() { h_.setUl(0x465b4b, 0x46177b); h42b9a5(); }   // FUN_0042b99b

void PlayLogic::h42b9a5() {
    h_.setCont(0x42b9d7);
    h_.w(0x465b47) = 0;
    h_.setSprite("hyibmrng");
    h_.b(0x466769) = 0x80;
    if (h42b51c()) { h42b939(); return; }
    h42b9d7();
}

void PlayLogic::h42b9d7() {
    h429127();
    if (hAnim(0x465b47, h_.ul(0x465b4b), 3)) { h428dfa(); return; }
    if (h_.l(0x466761) != 0 || h_.l(0x466765) != 0) {
        const uint8_t f = h_.frame();
        if (f == 7 || f == 8 || f == 9) { h428dfa(); return; }
    }
    if (h_.w(0x465b47) == 4) h4313aa();                    // let go of the boomerang
}

// ---------------------------------------------------------------- crouching: whip, throw

void PlayLogic::h42ba4c() {
    h_.b(0x46676b) = 0x80;
    h_.b(0x465b71) = 0x16;
    h_.setCont(0x42ba7d);
    h_.uw(0x465b45) = h_.uw(0x466767);
    h_.w(0x465b47) = 0;
    h42ba7d();
}

void PlayLogic::h42ba7d() {
    h42b2bc();
    h42fa4b();
    h_.setSprite("hyicrwhp");
    if (hAnim(0x465b47, 0x46174b, 3)) h4285c5();
}

void PlayLogic::h42babc() {
    h_.setCont(0x42bafb);
    h_.b(0x465b71) = 0x16;
    h_.uw(0x465b45) = h_.uw(0x466767);
    h_.setSprite("hyicrwhp");
    h_.setUl(0x465b4b, 0x461758);
    h_.w(0x465b4f) = 0;
    h_.b(0x466769) = 0x80;
    h42bafb();
}

void PlayLogic::h42bafb() {
    h42fa4b();
    if (hAnim2(0x465b4b, 3)) h4285c5();
}

// ---------------------------------------------------------------- running: whip, throw

void PlayLogic::h42bb21() {
    h_.b(0x46676b) = 0x80;
    h_.setCont(0x42bb48);
    h_.uw(0x465b45) = h_.uw(0x466767);
    h_.w(0x465b47) = 0;
    h42bb48();
}

void PlayLogic::h42bb48() {
    h42b2bc();
    h_.setSprite("hyirwhpr");
    const bool done = hAnim(0x465b47, 0x4601c8, 3);
    h42bc03(done);
}

void PlayLogic::h42bb80() {
    h_.setCont(0x42bbc4);
    h_.uw(0x465b45) = h_.uw(0x466767);
    h_.setUl(0x465b47, h_.b(0x466765) ? 0x42bcdf : 0x42bcc4);
    h_.w(0x465b4b) = 0;
    h_.b(0x466769) = 0x80;
    h42bbc4();
}

void PlayLogic::h42bbc4() {
    const bool done = hAnim2(0x465b47, 3);
    const uint8_t f = h_.frame();
    if ((f == 2 || f == 3 || f == 4) && h_.b(0x466762)) { h428dfa(); return; }
    h42bc03(done);
}

// ---------------------------------------------------------------- shared movement helpers

void PlayLogic::h42fa4b() {
    // FUN_0042fa4b: crouching for a while (DAT_00465b41) looks down: the camera pans down.
    if (h_.b(0x466766)) {
        if (h_.w(0x4658a6) == 0 && (h_.b(0x466761) || h_.b(0x466763))) h_.w(0x465b41) = 0;
        if (h_.w(0x465b41) == 0x2d) {
            if (h_.w(0x4658a6) != 0x69) h_.w(0x4658a6) += 3;
            return;
        }
    } else {
        if (h_.w(0x4658a6) == 0) return;
        h_.w(0x4658a6) -= 3;
        h_.w(0x465b41) = 0;
    }
    h_.w(0x465b41) += 1;
}

void PlayLogic::h4300e0(int32_t d0) {
    // FUN_004300e0: slow down by d0 towards 0
    if (h_.w(0x465ba6) == 0) { h_.vx() = 0; return; }
    if (h_.w(0x465ba6) > 0) {
        h_.vx() -= d0;
        if (h_.vx() >= 0) return;
    } else {
        h_.vx() += d0;
        if (h_.vx() < 0) return;
    }
    h_.vx() = 0;
}

void PlayLogic::h43011c(int32_t decel, int16_t max, int32_t accel) {
    // FUN_0043011c: accelerate the way the pad points, up to max px/tick, or slow down.
    if (h_.w(0x466767) == 0) { h4300e0(decel); return; }
    if (h_.b(0x466767)) {
        if (!neg16(-max - h_.w(0x465ba6))) return;
        h_.vx() -= accel;
        return;
    }
    if (neg16(max - 1 - h_.w(0x465ba6))) return;
    h_.vx() += accel;
}

void PlayLogic::h430174() {
    // FUN_00430174: count the height of a fall (DAT_00465b9e); a long one ends in 0x42c2cc.
    const int16_t d0 = h_.w(0x465baa);
    h_.w(0x465b9e) += d0;
    if (d0 <= 0) h_.w(0x465b9e) = 1;
    if (h_.cont() != 0x42c2cc && !neg16(h_.w(0x465b9e) - 0xc0)) {
        h_.w(0x465b47) = 0;
        h_.setCont(0x42c2cc);
    }
}

void PlayLogic::h4301ce(int16_t d0) {
    // FUN_004301ce with a6 = Harry: triggers 0x30 / 0x31 put him in front of / behind the scenery.
    const Trg t = trg(h_.x() + d0, h_.y());
    if (t.raw != 0xf2) return;
    if (t.id == 0x30) h_.flags() = uint16_t((h_.flags() & 0x7fff) + 0x8000);
    else if (t.id == 0x31) h_.flags() &= 0x7fff;
}

// ---------------------------------------------------------------- FUN_00430229: Harry's triggers

namespace {
// The class 0xf3 triggers 0x60..0x7f (the table at 0x430585, part of it code): the trigger id to
// come out at, whether the level's objects start again (d1), and whether up must be pressed.
struct Door { uint8_t id; uint8_t to; uint8_t d1; bool up; };
constexpr Door kDoors[] = {
    {0x60, 0x70, 0, false}, {0x61, 0x71, 1, false}, {0x62, 0x72, 0, false}, {0x63, 0x73, 0, false},
    {0x64, 0x74, 1, false}, {0x70, 0x60, 0, false}, {0x71, 0x61, 1, false},
    {0x65, 0x75, 0, true}, {0x66, 0x76, 1, true}, {0x67, 0x77, 0, true}, {0x68, 0x78, 1, true},
    {0x69, 0x79, 1, true}, {0x6a, 0x7a, 0, true}, {0x6b, 0x7b, 1, true}, {0x6c, 0x7c, 1, true},
    {0x6d, 0x7d, 0, true}, {0x6e, 0x7e, 1, true}, {0x75, 0x65, 0, true}, {0x76, 0x66, 1, true},
    {0x7e, 0x6e, 1, true},
};
}

void PlayLogic::h430229() {
    if (h_.b(0x465b72)) return;
    int16_t d1 = int16_t(h_.y() - 0x10);
    if (h_.b(0x465b38)) d1 += 0x34;
    const Trg t = trg(h_.x() - 0x10, d1);
    if (t.raw == 0xf3) { h430463(t.id); return; }
    if (t.raw != 0xf2) { h_.b(0x465b00) = 0; return; }
    if (h_.b(0x465b00) == 0 && t.id == 0x18) {
        // gravity flips: Harry walks on the ceiling (or back on the floor)
        h_.b(0x465b38) ^= 1;
        h_.b(0x465b00) = 0xff;
        const int dir = h_.b(0x465b38) ? -1 : 1;
        h_.y() += int16_t(dir * 0x20);
        for (int guard = 0; guard < 0x100; ++guard) {
            h_.y() += int16_t(dir * 0x10);
            if (feet()) break;
        }
        return;
    }
    auto plane = [&](bool two) {
        // the .dt2 plane (and the rope triggers that belong to each plane)
        h_.b(0x465bb3) = two ? 0xff : 0;
        h_.w(0x4659b0) = two ? 0x300 : 3;
        h_.b(0x4658d9) = two ? 0 : 8;
        h_.b(0x4658da) = two ? 0 : 8;
        h_.b(0x4659b9) = two ? 8 : 0;
    };
    switch (t.id) {
    case 0x5f: if (h_.b(0x466507) == 0) h430912(); return;    // the level exit
    case 0x10: plane(false); return;
    case 0x11: plane(true); return;
    case 0x20: plane(true); h_.w(0x465b92) = 0; return;
    case 0x21: plane(false); h_.w(0x465b92) = 0; return;
    case 0x22: plane(true); h_.uw(0x465b92) = 0x8000; return;
    case 0x24: plane(false); h_.uw(0x465b92) = 0x8000; return;
    case 0x30: h_.uw(0x465b92) = 0x8000; return;
    case 0x31: h_.w(0x465b92) = 0; return;
    default: return;
    }
}

void PlayLogic::h430463(uint8_t id) {
    // doors and passages
    if (h_.cont() == 0x42a443 || h_.cont() == 0x42a174) return;
    const Door* d = nullptr;
    for (const Door& e : kDoors) if (e.id == id) d = &e;
    if (!d) return;                                        // (a null entry: the original would jump to 0)
    if (!d->up) {
        if (h_.b(0x465b00) != 0) return;
        // walked into it: straight through
        h_.b(0x465b45) = d->to;
        h_.b(0x465b46) = d->d1;
        h_.b(0x465b43) = 1;
        if (level_ == 0 && !neg16(h_.x() - 0xba0)) h_.b(0x465d49) = 1;   // (and FUN_004263a2, the Genesis music)
        if (level_ == 0 || level_ == 5) sound0(0x1a, 0x14);
        h42a46d();
        return;
    }
    // a door: up while standing
    if (h_.cont() != 0x428e50 || h_.b(0x466765) == 0 || h_.b(0x466760) == 1) return;
    h_.b(0x466760) = 1;
    const int16_t x0 = int16_t(h_.x() - 0x20), y0 = int16_t(h_.y() - 0x10);
    h_.x() = int16_t((h_.x() & 0xfff0) + 2);
    if (trg(x0, y0).id == 0) h_.x() += 0x10;
    h_.facing() = 0;
    h_.b(0x465b45) = d->to;
    h_.b(0x465b46) = d->d1;
    h_.b(0x465b43) = 0;
    if ((d->id == 0x68 && level_ == 4) || (d->id == 0x6b && (level_ == 4 || level_ == 9))) h_.b(0x465b43) = 1;
    h42a142();
}

void PlayLogic::h430912() {
    // The exit of the level ("*4": the next level of the tour).
    h_.b(0x466504) = 0;
    game_->stats().secret1up = false;
    if (!exiting_) { exiting_ = true; game_->completeLevel(); }
}

void PlayLogic::h42bc03(bool done) {
    // Keep running the way he was going (the direction saved at +0x36) at 3 (5) px/tick.
    const uint16_t pad = h_.uw(0x466767);
    h_.uw(0x466767) = h_.uw(0x465b45);
    h43011c(0x3000, h_.b(0x466503) ? 5 : 3, 0x3000);
    if (done) {
        const bool left = h_.b(0x466767) != 0;
        h_.uw(0x466767) = pad;
        if (left) h42e25e(); else h42ea52();
        return;
    }
    addX(h_.vx());
    if (h_.b(0x466767)) h42e450(); else h42ec44();
}

} // namespace game

namespace game {

// ---------------------------------------------------------------- the weapons Harry throws

Obj* PlayLogic::h43124c() {
    // FUN_0043124c: a sling stone (type 5) or the big stone (type 4), if one of the five
    // in-flight slots (DAT_00466808..) is free.
    uint32_t slot = 0;
    for (uint32_t a = 0x466808; a <= 0x466818; a += 4) if (h_.ul(a) == 0) { slot = a; break; }
    if (!slot) return nullptr;
    const bool big = h_.b(0x4667af) != 0;
    Obj* a5 = newObjAtHarry(big ? 1 : 0);
    if (!a5) return nullptr;
    if (!big) {
        ammo(0) -= 1;
        a5->sprite = game_->findSprite("obrston0");
        a5->type = 5;
    } else {
        ammo(2) -= 1;
        if (ammo(2) == 0) weaponSel() = 0;
        a5->sprite = game_->findSprite("mi_stone");
        a5->type = 4;
        h_.b(0x465b3d) = 0x4c;
        a5->behind = 0xff;
        a5->w(0x46) = 0;
    }
    a5->frame = 0;
    a5->flags = uint16_t(0x6000 + h_.uw(0x465b92));
    const uint32_t r = random();
    a5->facing = uint8_t(r & 1);
    a5->vflip = uint8_t(r & 2);
    h_.setUl(slot, addrOf(a5));
    a5->l(0x3a) = int32_t(slot);
    return a5;
}

void PlayLogic::h4313aa() {
    // FUN_004313aa: the boomerang (type 6)
    Obj* a5 = newObjAtHarry(2, true);
    if (!a5) return;
    a5->b(0x6a) = 3;
    ammo(1) -= 1;
    if (ammo(1) == 0) weaponSel() = 0;
    h_.setUl(0x46681c, addrOf(a5));
    sound0(0x59, 0xc);
    a5->sprite = game_->findSprite("fxrbmrng");
    a5->x = h_.x();
    a5->y = int16_t(h_.y() - 0x40);
    a5->flags = uint16_t(0x6000 + h_.uw(0x465b92));
    a5->b(0x6d) = 0;
    a5->behind = 0xff;
    a5->type = 6;
    a5->l(0x3c) = 0x461a90;                                // the flight path (h_boomerang)
    a5->l(0x4a) = 0;
    a5->l(0x46) = 0xc0000;
    a5->x += 0xc;
    a5->facing = h_.facing();
    if (h_.facing()) { a5->l(0x46) = int32_t(0xfff40000); a5->x -= 0x18; }
    a5->w(0x50) = a5->x;
    a5->w(0x52) = a5->y;
}

} // namespace game

namespace game {

namespace {
inline bool neg16b(int v) { return int16_t(v) < 0; }
}

// ---------------------------------------------------------------- falls and jumps

void PlayLogic::h42c1ea() {
    // off a vine
    h_.w(0x465b47) = 0;
    h_.setCont(0x42c20a);
    h_.setSprite("hyidvine");
    h42c20a();
}

void PlayLogic::h42c20a() {
    if (hAnim(0x465b47, 0x461746, 2)) { h_.setSprite("hyivjump"); h42c467(); return; }
    h42c7b5();
}

void PlayLogic::h42c244() {
    // carried down (DAT_00466182 px per tick to the left)
    h_.setSprite("hyifall");
    hAnim(0x465b47, 0x461746, 6);
    h_.vx() = 0;
    h_.x() -= h_.w(0x466182);
    if (neg16b(h_.w(0x465baa) - 8)) h_.vy() += 0x5000;
    addY(h_.vy());
}

void PlayLogic::h42c2cc() {
    // a long fall (FUN_00430174)
    h_.setSprite("hyifall");
    hAnim(0x465b47, 0x461746, 0xa);
    h42c643();
}

void PlayLogic::h42c2ff() {
    h429127();
    h_.setSprite("hyifall");
    hAnim(0x465b47, 0x461746, 6);
}

void PlayLogic::h42c333() {
    // dropping from a rope
    h_.setSprite("hyiroped");
    if (hAnim(0x465b47, 0x42c387, 5)) { h_.setSprite("hyivjump"); h42c467(); return; }
    if (h_.frame() != 0) h42c643();
}

void PlayLogic::h42c38c() {
    // the jump button on the ground: a short crouch first
    h_.setCont(0x42c3ae);
    h_.setSprite("hyivjump");
    h_.frame() = 0;
    h_.b(0x465b47) = 4;
    h42c3ae();
}

void PlayLogic::h42c3ae() {
    h_.b(0x465b47) -= 1;
    if (h_.b(0x465b47) == 0) h42c490();
}

void PlayLogic::h42c3bc() {
    // thrown up by a bungee vine
    h_.b(0x46676a) = 0x80;
    h_.setSprite("hyivjump");
    h_.frame() = mb(uint32_t(int32_t(0x46148c) + h_.w(0x465baa)));
    h_.vy() += 0x4600;
    addY(h_.vy());
    if (h_.w(0x465baa) == 1) h_.setCont(0x42c518);
    h42c836();
}

void PlayLogic::h42c45d() { h_.vy() = 0; h42c467(); }

void PlayLogic::h42c467() {
    // FUN_0042c467: fall
    h_.setCont(0x42c518);
    h_.w(0x465b47) = 0;
    h_.b(0x465b7b) = 0;
    h_.onGround() = 0;
    h42c518();
}

void PlayLogic::h42c490() {
    // take off
    h_.b(0x46676a) = 0x80;
    h_.setCont(0x42c518);
    h_.w(0x465b47) = 0;
    h_.b(0x465aff) = 9;                                    // the jump holds for up to 9 ticks
    h_.b(0x465b73) = 0;
    h_.b(0x465b74) = 0;
    h_.b(0x465b7b) = 0xc;
    sound0((random() & 0x80) ? 0x8a : 0x88, 5);
    h_.vy() = int32_t(0xfffb6000);
    if (h_.w(0x46652b) != 0) h_.vy() = int32_t(0xfff90000);   // the chili jump
    h_.onGround() = 0;
    h42c518();
}

bool PlayLogic::airAttack(uint32_t whip) {
    // 0x42c5c1 / 0x42ce31: shoot or whip in the air
    if (h_.b(0x466769) == 1) {
        const int wpn = weaponSel();
        bool stones = false;
        if (wpn == 2) {
            if (ammo(2) == 0) stones = true;
            else if (h_.b(0x465b3d) == 0) { h_.b(0x4667af) = 0xff; h42f69a(); return true; }
        } else if (wpn == 1) {
            if (ammo(1) == 0) stones = true;
            else if (h_.ul(0x46681c) == 0) { h42f735(); return true; }
        } else if (wpn == 0) {
            stones = true;
        }
        if (stones && ammo(0) != 0) {
            weaponSel() = 0;
            h_.b(0x4667af) = 0;
            h42f69a();
            return true;
        }
    }
    if (h_.b(0x46676b) == 1) { if (whip == 0x42f600) h42f600(); else h42f7f2(); return true; }
    return false;
}

void PlayLogic::h42c518() {
    // in the air (a standing jump, a fall)
    h_.b(0x465b71) = 0;
    h_.setSprite("hyivjump");
    if (!neg16b(h_.w(0x465baa) - 6)) {
        // falling fast: the two flailing frames
        h_.b(0x465b52) += 1;
        h_.b(0x465b52) &= 7;
        if (h_.b(0x465b52) == 0) h_.b(0x465b51) += 1;
        h_.b(0x465b51) &= 1;
        h_.frame() = uint8_t(h_.b(0x465b51) + 9);
    } else {
        h_.uw(0x465b51) = 0x700;
        h_.frame() = mb(uint32_t(int32_t(0x46148c) + h_.w(0x465baa)));
    }
    if (h_.b(0x465c56) == 0 && airAttack(0x42f600)) return;
    h42c643();
}

void PlayLogic::h42c643() {
    if (grabClimbable()) { h42cc5d(); return; }
    if (h_.w(0x465baa) < 0) {
        // rising: a ceiling (class bit 1)?
        Trg t = trg(h_.x() - 0x18, h_.y() - 0x36);
        if (!(t.cls & 2)) {
            t = trg(h_.x() - 8, h_.y() - 0x36);
            if (!(t.cls & 2)) { h42c7b5(); return; }
        }
        if (t.id == 0x1f) {
            h_.onGround() = 0;
            hurtHarry(0xf);
            h_.vy() = 0;
            return;
        }
        if (!neg16b(h_.w(0x465baa) - int16_t(0xfffc))) { h_.vy() = 0; h42c7b5(); return; }
        h_.vy() = 0;
        h_.y() = int16_t((h_.y() & 0xfff0) + 5);
        h42f1c9();                                         // a hard knock on the head
        return;
    }
    if (h_.b(0x465c56) != 0 || !feet()) { h42c7b5(); return; }
    // landed
    h_.vx() = 0;
    if (h_.spriteIs("hyisomr1")) { h42a838(); return; }
    if (h_.cont() == 0x42f6c8) {
        if (!neg8(h_.frame() - 2)) { h428dfa(); return; }
        h42b6c1();
        return;
    }
    if (h_.cont() == 0x42f759) { h428dfa(); return; }
    if (!h_.spriteIs("hyivwhip")) { h428dfa(); return; }
    if (!neg8(h_.frame() - 3)) { h428dfa(); return; }
    h42b19d();
}

void PlayLogic::h42c7b5() {
    // FUN_0042c7b5: gravity (held jump: 9 ticks without it), the fall, steering
    if (neg16b(h_.w(0x465baa) - 6)) {
        bool gravity = true;
        if (h_.b(0x465aff) != 0) {
            h_.b(0x465aff) -= 1;
            if (h_.b(0x466762)) gravity = false;
            else h_.b(0x465aff) = 0;
        }
        if (gravity && neg16b(h_.w(0x465baa) - 0xc)) h_.vy() += 0x5000;
    }
    int32_t d0 = h_.vy();
    if (h_.b(0x465b38)) d0 = -d0;
    addY(d0);
    h430174();
    if (h_.b(0x465c56) != 0) { h42c8cf(); return; }
    h42c836();
}

void PlayLogic::h42c836() {
    if (h_.b(0x465bb2) != 0) h43011c(0x200, 3, 0x1400);
    else h43011c(0x3800, h_.b(0x466503) ? 5 : 3, 0x7800);
    addX(h_.vx());
    if (h_.w(0x465ba6) != 0) h_.facing() = h_.w(0x465ba6) < 0 ? 1 : 0;
    h42c8cf();
}

void PlayLogic::h42c8cf() {
    // walls while in the air (class bit 0 at y - 0x20 / - 0x10 / 0, 0x18 and 0x28 px out)
    const bool left = h_.vx() == 0 ? h_.facing() != 0 : h_.vx() < 0;
    auto wall = [&](int dx) {
        return (trg(h_.x() + dx, h_.y() - 0x20).cls & 1) || (trg(h_.x() + dx, h_.y() - 0x10).cls & 1) ||
               (trg(h_.x() + dx, h_.y()).cls & 1);
    };
    const int nearDx = left ? -0x28 : 8, farDx = left ? -0x38 : 0x18;
    if (wall(nearDx)) {
        h_.vx() = 0;
        h_.x() += int16_t(left ? 1 : -1);
        return;
    }
    if (!wall(farDx)) return;
    h_.x() = int16_t((h_.x() & 0xfff0) + 7);
    h_.vx() = 0;
    // a running jump into a wall while holding up and towards it: grab the top (0x42e59a)
    if (h_.cont() == 0x42cdb6 && neg16b(h_.w(0x465baa) - 1) && h_.b(0x466765) && !neg16b(h_.w(0x465b9a) - 0xa) &&
        h_.b(left ? 0x466767 : 0x466768))
        h42e59a();
}

void PlayLogic::h42cc5d() {
    // caught something to climb
    h_.setCont(0x42cc7a);
    h_.setUl(0x465b43, 0x46171e);
    h_.w(0x465b47) = 0;
    h42cc7a();
}

void PlayLogic::h42cc7a() {
    if (hAnim2(0x465b43, 2)) { h42d741(); return; }
    if (h_.b(0x465b47) == 0 && h_.spriteIs("hyimrop2") && h_.frame() == 3 && h_.b(0x466765)) {
        h_.facing() = uint8_t((h_.facing() & 1) ^ 1);
        h42d741();
    }
}

void PlayLogic::h42ccd7() {
    // walked off the floor
    h_.vy() = 0;
    h_.setSprite("hyihjump");
    h_.setCont(0x42cdb6);
    h_.w(0x465b47) = 0;
    h_.b(0x465b7b) = 0;
    h_.onGround() = 0;
    h42cdb6();
}

void PlayLogic::h42cd1e() {
    // a running jump
    h_.b(0x46676a) = 0x80;
    h_.b(0x465aff) = 9;
    h_.setCont(0x42cdb6);
    h_.b(0x465b7b) = 0xc;
    sound0((random() & 0x80) ? 0x8a : 0x88, 5);
    h_.vy() = int32_t(0xfffb6000);
    if (h_.w(0x46652b) != 0) h_.vy() = int32_t(0xfff90000);
    h_.onGround() = 0;
    h_.w(0x465b47) = 0;
    h_.setSprite("hyihjump");
    if (h_.w(0x46652b) != 0) h_.setSprite("hyisomr2");    // the chili somersault
    h42cdb6();
}

void PlayLogic::h42cdb6() {
    h_.b(0x465b71) = 0;
    if (neg16b(h_.w(0x465baa) - 6)) {
        bool gravity = true;
        if (h_.b(0x465aff) != 0) {
            h_.b(0x465aff) -= 1;
            if (h_.b(0x466762)) gravity = false;
            else h_.b(0x465aff) = 0;
        }
        if (gravity && neg16b(h_.w(0x465baa) - 0xc)) h_.vy() += 0x5000;
    }
    int32_t d0 = h_.vy();
    if (h_.b(0x465b38)) d0 = -d0;
    addY(d0);
    h430174();
    if (airAttack(0x42f7f2)) return;
    if (grabClimbable()) { h42cc5d(); return; }
    if (h_.spriteIs("hyisomr2")) {
        if (neg8(h_.frame() - 4)) {
            h_.b(0x465b55) += 1;
            h_.b(0x465b55) &= 7;
            if (h_.b(0x465b55) == 0) sound0(0x8a, 4);
        }
        if (hAnim(0x465b47, 0x42d23b, 2)) {
            h_.w(0x465b47) = 0;
            h_.setCont(0x42c518);
            h42c518();
            return;
        }
    } else {
        hAnim(0x465b47, 0x42d24d, 3);
    }
    if (h_.w(0x465baa) < 0) {
        // rising: a ceiling?
        Trg t = trg(h_.x() - 8, h_.y() - 0x28);
        if (!(t.cls & 2)) t = trg(h_.x() - 0x18, h_.y() - 0x28);
        if (t.cls & 2) {
            if (t.id == 0x1f) {
                h_.onGround() = 0;
                hurtHarry(0xf);
                h_.vy() = 0x10000;
                return;
            }
            h_.vy() = 0;
        }
        h42d19f();
        return;
    }
    bool land = feet();
    if (!land) {
        // the end of the jump just short of a ledge: nudged onto it
        if (!h_.spriteIs("hyihjump") || neg8(h_.frame() - 9) || h42b5eb() || neg16b(h_.x() - 0x50)) { h42d19f(); return; }
        const int16_t ahead = int16_t(h_.x() + (h_.facing() ? -0x20 : 0));
        if (!feetAt(ahead, h_.y() + 0xe)) { h42d19f(); return; }
        const int32_t keep = h_.l(0x465b13);
        const bool under = feetAt(h_.x() - 0x10, h_.y() + 0x14);
        h_.l(0x465b13) = keep;
        if (under) { h42d19f(); return; }
        const int16_t d6 = h_.facing() ? -2 : 2;
        for (int guard = 0; guard < 0x40; ++guard) {
            h_.x() += d6;
            if (feet()) break;
        }
    }
    // landed
    if (game_->logicTicks() - game_->lastLandTick() >= 0x28 && !channel0Busy()) {
        sound0(0x1e, 4);
        game_->lastLandTick() = game_->logicTicks();
    }
    if (h_.w(0x465ba6) != 0) {
        if (h_.w(0x465ba6) < 0) { if (h_.b(0x466767)) { h42e25e(); return; } }
        else if (h_.b(0x466768)) { h42ea52(); return; }
    }
    h_.vx() = 0;
    if (h_.spriteIs("hyisomr2")) { h42a838(); return; }
    h428dfa();
}

void PlayLogic::h42d19f() {
    if (h_.b(0x465bb2) != 0) h43011c(0x200, 3, 0x1400);
    else h43011c(0x4000, h_.b(0x466503) ? 5 : 3, 0x8000);
    addX(h_.vx());
    if (h_.vx() != 0) h_.facing() = h_.vx() < 0 ? 0xff : 0;
    h42c8cf();
}

void PlayLogic::h42d27f() {
    h_.setCont(0x42d29f);
    h_.w(0x465b47) = 0;
    h_.setSprite("hyispin");
    h42d29f();
}

void PlayLogic::h42d29f() {
    if (hAnim(0x465b47, 0x46149a, 3)) h428dfa();
}

void PlayLogic::h42d2ca() {
    // hanging (hyihang); falls into FUN_0042d2ec
    h_.setSprite("hyihang");
    hAnim2(0x465b4d, 7);
    h42d2ec();
}

} // namespace game

namespace game {

// ---------------------------------------------------------------- ropes

void PlayLogic::h42d2ec() {
    // FUN_0042d2ec (main loop): while Harry is on a rope, a chain of 7 objects draws it along
    // the rope's trigger column (ids 0x19 / 0x1a / 0xf9) from above his head down; the chain
    // hangs at 0x465aa1 + 0x46 = DAT_00465ae7, each link's +0x46 the next one.
    auto next = [&](uint32_t a) -> uint32_t {
        if (a == 0x465aa1) return h_.ul(0x465ae7);
        Obj* o = objAt(a);
        return o ? uint32_t(o->l(0x46)) : 0;
    };
    auto freeChain = [&] {
        h_.b(0x465ae4) = 0;
        for (uint32_t a = h_.ul(0x465ae7); a;) {
            Obj* o = objAt(a);
            if (!o) break;
            a = uint32_t(o->l(0x46));
            objects_.free(*o);
        }
        h_.setUl(0x465ae7, 0);
    };
    const uint32_t c = h_.cont();
    const bool onRope = c == 0x42d781 || c == 0x42cc7a || c == 0x42de4f || c == 0x42dd7e || c == 0x42dea6 || c == 0x42df5b;
    if (!onRope) {
        if (h_.ul(0x465ae7) == 0) { h_.setUl(0x465ae7, 0); return; }
        if (!h_.onGround()) {
            h_.b(0x465ae4) += 1;
            if (h_.b(0x465ae4) != 0x30) goto update;
        }
        freeChain();
        return;
    }
    if (h_.ul(0x465ae7) == 0) {
        h_.w(0x465ae5) = h_.x();
        h_.l(0x465aa1) = 0;
        uint32_t a4 = 0x465aa1;
        const char* spr = spriteSlotName(ml(0x431516 + uint32_t(game_->levelNumber()) * 4));
        for (int d5 = 6; d5 >= 0; --d5) {
            Obj* a5 = objects_.allocTail();                  // FUN_00435780
            if (!a5) { freeChain(); return; }
            a5->sprite = game_->findSprite(spr);
            if (a4 == 0x465aa1) h_.setUl(0x465ae7, addrOf(a5));
            else if (Obj* p = objAt(a4)) p->l(0x46) = int32_t(addrOf(a5));
            a4 = addrOf(a5);
        }
    }
    h_.b(0x465ae4) = 0;
    h_.w(0x465ae5) = h_.x();
update:
    {
        int16_t d5 = int16_t(h_.y() - 0x40);
        if (neg16(d5 - 4)) d5 = 4;
        d5 &= int16_t(0xfff0);
        uint8_t d4 = 0xff;                                 // drawn behind Harry
        if (h_.spriteIs("hyidrope")) {
            const uint8_t f = h_.frame();
            if (f != 3 && f != 4 && f != 5 && f != 6) d4 = 0;
        }
        h_.l(0x466788) = 0;
        // (the trigger record in the load block: the column's word added signed)
        const int colOff = (uint16_t(h_.w(0x465ae5) + 0x10) >> 3) & 0xfffe;
        const uint16_t ptr = uint16_t(trgAt(colOff) << 8 | trgAt(colOff + 1));
        if (ptr == 0) { freeChain(); return; }
        int a0 = int16_t(ptr);
        uint16_t row = uint16_t(d5 >> 4) & 0xff;
        uint32_t a4 = 0x465aa1;
        auto byteAt = [&](int o) -> uint8_t { return trgAt(o); };
        for (int guard = 0; guard < 64; ++guard) {
            const uint8_t d0 = byteAt(a0);
            int id = -1;
            if (d0 == 0xfe) {
                if (!(row < byteAt(a0 + 2))) { a0 += 4; continue; }
                if (row >= byteAt(a0 + 1)) id = byteAt(a0 + 3);
            } else if (row == d0) {
                id = byteAt(a0 + 1);
                a0 += 2;
            } else if (row > d0) {
                a0 += 2;
                continue;
            }
            const uint32_t n = next(a4);
            if (n == 0) break;
            a4 = n;
            Obj* o = objAt(a4);
            if (!o) break;
            if (id < 0) {                                  // a gap in the rope
                o->frame = 0xff;
                d5 += 0x10;
                row += 1;
                continue;
            }
            o->x = int16_t(h_.w(0x465ae5) - 2);
            o->y = int16_t(d5 - 0x10);
            d5 += 0x10;
            row += 1;
            o->behind = d4;
            o->frame = 0;
            if (id == 0x19 || id == 0xf9) { h_.setUl(0x466788, a4); continue; }
            o->frame = 2;
            if (id == 0x1a) { h_.setUl(0x466788, a4); continue; }
            o->frame = 0xff;
        }
        if (Obj* last = objAt(h_.ul(0x466788))) {
            const uint8_t id = trg(last->x - 8, last->y + 0x20).id;
            if (id != 0x19 && id != 0xf9 && id != 0x1a) last->frame = 1;   // the end of the rope
        }
    }
}

void PlayLogic::h42d741() {
    // FUN_0042d741: climbing a rope
    h_.setCont(0x42d781);
    h_.w(0x465b43) = 0;
    h_.w(0x465b45) = 0;
    h_.w(0x465b47) = 0;
    h_.vx() = 0;
    h_.setUl(0x465b4d, 0x461735);
    h_.w(0x465b51) = 0;
    h_.b(0x465b41) = 0;
    h42d781();
}

void PlayLogic::h42d781() {
    h_.setSprite("hyiurope");
    h_.b(0x465b75) = 0;
    if (h_.y() < 0) h_.y() = 0;
    if (h_.b(0x46676a) == 1) {
        // jump off
        h_.b(0x46676a) = 0x80;
        sound0(0x8a, 5);
        h_.b(0x465aff) = 0;
        h_.b(0x465b77) = 0x28;
        h_.w(0x4658bc) = h_.x();
        h_.vy() = int32_t(0xfffa0000);
        h_.b(0x465b76) = 1;
        h_.setCont(0x42c333);
        h_.w(0x465b47) = 0;
        h_.y() -= 4;
        h_.b(0x465b7b) = 0;
        h_.onGround() = 0;
        h_.vx() = 0;
        if (h_.w(0x466767) != 0) h_.vx() = h_.b(0x466767) ? int32_t(0xfffcc000) : 0x34000;
        h42c333();
        return;
    }
    bool d7 = false;
    if (h_.b(0x465b77) == 0) {
        if (h_.w(0x466765) == 0) { h42dc0e(); return; }
        if (!ropeHere() && h_.b(0x466765)) {
            // the top of the rope
            h_.w(0x465b45) = 0;
            h_.b(0x466765) = 0;
            d7 = true;
            if (h42d938(d7)) return;
            h42dab9();
            return;
        }
        h_.uw(0x465b45) = h_.uw(0x466765);
        h_.b(0x465b77) = 5;
        if (h_.b(0x466766) == 0) {
            h_.b(0x465b77) = 0x20;
            h_.b(0x465b76) = h_.facing() ? 0xa : 2;
        }
        h_.w(0x465b51) = 0;
    }
    h_.b(0x465b77) -= 1;
    h_.uw(0x466765) = h_.uw(0x465b45);
    h_.setUl(0x465b4d, 0x461735);
    if (h_.b(0x466765)) {
        if (ropeHere()) { h_.y() -= 2; h42dab9(); return; }
        if (h42d938(false)) return;
        h42dab9();
        return;
    }
    if (h_.b(0x466766)) {
        h_.y() += 2;
        if (!ropeHere()) {
            if (feet()) { h42d27f(); return; }
            h_.vy() = 0x10000;
            h_.b(0x465b77) = 5;
            h42c467();
            return;
        }
    }
    h42dab9();
}

bool PlayLogic::h42d938(bool d7) {
    // 0x42d938: at the top: onto the ledge (FUN_0042de7b / FUN_0042df0b), or hang there
    if ((trg(h_.x() - 0x10, h_.y() - 0x20).cls & 1) == 0 && (trg(h_.x() - 0x10, h_.y() - 0x2c).cls & 1) == 0) {
        if (ph::probeCell(det(), h_.x(), h_.y() - 0x38).hit) { h42de7b(); return true; }
        if (ph::probeCell(dt2(), h_.x(), h_.y() - 0x38).hit) { h42df0b(); return true; }
        if (ph::probeCell(det(), h_.x(), h_.y() - 0x2e).hit) { h42de7b(); return true; }
        if (ph::probeCell(dt2(), h_.x(), h_.y() - 0x2e).hit) { h42df0b(); return true; }
    }
    if (d7) { h42dc0e(); return true; }
    return false;
}

void PlayLogic::h42dab9() {
    h_.b(0x465b47) -= 1;
    if (neg8(h_.b(0x465b47))) {
        h_.b(0x465b47) = 3;
        if (h_.b(0x466765)) {
            h_.b(0x465b76) += 1;
            if (h_.b(0x465b76) == 0x11) h_.b(0x465b76) = 1;
        }
    }
    if (h_.b(0x466766)) {
        // sliding down
        h_.setSprite("hyidrope");
        if (hAnim(0x465b43, 0x4614a4, 2)) {
            h_.w(0x465b43) = 2;
            h_.frame() = 1;
            h_.b(0x465b43) = 1;
            return;
        }
        if (h_.w(0x465b43) == 3) sound0(0xb7, 5);
        return;
    }
    const uint32_t t = 0x4616fc + uint32_t(h_.b(0x465b76)) * 2;
    h_.w(0x465b43) = 0;
    h_.frame() = mb(t);
    h_.facing() = mb(t + 1);
    if (h_.b(0x465b47) == 3 && h_.frame() == 4) sound0(0xb8, 5);
}

void PlayLogic::h42dc0e() {
    // hanging on the rope: turn, whip, shoot
    h_.setSprite("hyihang");
    hAnim2(0x465b4d, 9);
    if (h_.b(0x466767)) h_.facing() = 0xff;
    if (h_.b(0x466768)) h_.facing() = 0;
    if (h_.b(0x46676b) == 1) { h42dd5a(); return; }
    if (h_.b(0x466769) != 1) return;
    const int wpn = weaponSel();
    if (wpn != 0) {
        if (wpn != 2) return;
        if (ammo(2) != 0) {
            if (h_.b(0x465b3d) != 0) return;
            h_.b(0x4667af) = 0xff;
            h42de0c();
            return;
        }
    }
    if (ammo(0) == 0) return;
    weaponSel() = 0;
    h_.b(0x4667af) = 0;
    h42de0c();
}

void PlayLogic::h42dcbc() {
    // shooting while hanging from a vine
    h_.b(0x466769) = 0x80;
    h_.setCont(0x42dd10);
    h_.setSprite("hyihshot");
    h_.w(0x465b4b) = 0;
    h_.setUl(0x465b47, 0x4616d1);
    if (h_.b(0x466766)) h_.setUl(0x465b47, 0x4616bb);
    else if (h_.b(0x466765)) h_.setUl(0x465b47, 0x4616e8);
    h42dd10();
}

void PlayLogic::h42dd10() {
    h_.b(0x465b95) = 0x1e;
    h_.y() += 0x1b;
    const bool done = hAnim2(0x465b47, 4);
    h_.y() -= 0x1b;
    if (done) h42bd68();
}

void PlayLogic::h42dd5a() {
    // the whip while hanging from a rope
    h_.b(0x46676b) = 0x80;
    h_.setCont(0x42dd7e);
    h_.setUl(0x465b47, 0x4614ad);
    h_.w(0x465b4b) = 0;
    h42dd7e();
}

void PlayLogic::h42dd7e() {
    h42b2bc();
    if (hAnim2(0x465b47, 4)) h42d741();
}

void PlayLogic::h42dda4() {
    // the whip while hanging from a vine
    h_.b(0x46676b) = 0x80;
    h_.setCont(0x42ddc8);
    h_.setUl(0x465b47, 0x4614ad);
    h_.w(0x465b4b) = 0;
    h42ddc8();
}

void PlayLogic::h42ddc8() {
    h42b2bc();
    h_.b(0x465b95) = 0x1e;
    h_.y() += 0x1b;
    const bool done = hAnim2(0x465b47, 4);
    h_.y() -= 0x1b;
    if (done) h42bd68();
}

void PlayLogic::h42de0c() {
    // FUN_0042de0c: a stone thrown from the rope
    h_.b(0x466769) = 0x80;
    h_.setCont(0x42de4f);
    h_.w(0x465b4b) = 0;
    h_.setUl(0x465b47, h_.w(0x466767) ? 0x4616d1 : 0x4616bb);
    h_.setSprite("hyihshot");
    h42de4f();
}

void PlayLogic::h42de4f() {
    if (hAnim2(0x465b47, 4)) h42d741();
}

void PlayLogic::h42de7b() {
    // FUN_0042de7b: climb off the top of the rope onto the floor (.det)
    h_.setCont(0x42dea6);
    feet();
    h_.setUl(0x465b47, 0x461698);
    h_.w(0x465b4b) = 0;
    h_.w(0x465b9e) = 0;
    h42dea6();
}

void PlayLogic::h42dea6() {
    if (hAnim2(0x465b47, 5)) { h42dee8(); return; }
    if (h_.b(0x465b4b) == 0 && h_.ul(0x465b47) == 0x4616a3) {
        h_.y() -= 0x40;
        feet();
    }
}

void PlayLogic::h42dee8() {
    h_.x() += int16_t(h_.facing() ? -4 : 4);
    h428dfa();
}

void PlayLogic::h42df0b() {
    // FUN_0042df0b: ... onto a .dt2 floor (and over to that plane)
    h_.setCont(0x42df5b);
    h_.setUl(0x465b47, 0x461698);
    h_.w(0x465b4b) = 0;
    h_.b(0x465bb3) = 0xff;
    h_.w(0x4659b0) = 3;
    h_.b(0x4658d9) = 0;
    h_.b(0x4658da) = 0;
    h_.b(0x4659b9) = 8;
    feet();
    h_.w(0x465b9e) = 0;
    h42df5b();
}

void PlayLogic::h42df5b() {
    if (hAnim2(0x465b47, 5)) {
        h_.uw(0x465b92) = 0x8000;
        h42dee8();
        return;
    }
    if (!neg8(h_.frame() - 8)) h_.uw(0x465b92) = 0x8000;
    if (h_.b(0x465b4b) == 0 && h_.ul(0x465b47) == 0x4616a3) {
        h_.y() -= 0x48;
        feet();
    }
}

} // namespace game

namespace game {

// ---------------------------------------------------------------- running

void PlayLogic::h42dfba() {
    // turning round while running left (right pressed): skid, then run right
    h_.setCont(0x42dfe0);
    h_.setSprite("hyirtrn2");
    h_.w(0x465b49) = 0x204;
    h_.w(0x465b47) = 0;
    h42dfe0();
}

void PlayLogic::h42dfe0() {
    h_.vx() += 0x2000;
    addX(h_.vx());
    if (hAnim(0x465b47, 0x4616b3, 4)) {
        h_.facing() = 1;
        h_.w(0x465b47) = 1;
        h42ea5e();
        return;
    }
    h_.b(0x465b49) -= 1;
    if (h_.b(0x465b49) == 0) {
        h_.b(0x465b49) = 3;
        if (h_.b(0x465b4a) != 0) {
            h_.b(0x465b4a) -= 1;
            if (Obj* a5 = newObjAtHarry(1, true)) {        // a puff of dust
                a5->sprite = game_->findSprite("fxrdust2");
                a5->type = 0x41;
                a5->l(0x32) = int32_t(0xffff0000);
                a5->facing = 0xff;
                a5->x -= 0x15;
            }
        }
    }
    h42e08c();
}

void PlayLogic::h42e08c() {
    // the wall the skid slides towards
    if (h_.vx() < 0) {
        if (trg(h_.x() - 0x38, h_.y() - 0x14).cls & 1) { h_.x() = int16_t((h_.x() & 0xfff0) + 7); h_.vx() = 0; }
    } else {
        if (trg(h_.x() + 0x18, h_.y() - 0x14).cls & 1) { h_.x() = int16_t((h_.x() & 0xfff0) + 9); h_.vx() = 0; }
    }
}

void PlayLogic::h42e138() {
    // FUN_0042e138: spun round by the waggle
    h_.setCont(0x42e163);
    h_.setSprite("hyispin");
    h_.b(0x465b06) = 0;
    h_.w(0x465b47) = 0;
    h_.b(0x465b71) = 1;
    h42e163();
}

void PlayLogic::h42e163() {
    if (hAnim(0x465b47, 0x46149a, 3)) h428dfa();
}

void PlayLogic::h42e18e() {
    // turning round while running right (left pressed)
    h_.setCont(0x42e1b4);
    h_.setSprite("hyirtrn2");
    h_.w(0x465b49) = 0x204;
    h_.w(0x465b47) = 0;
    h42e1b4();
}

void PlayLogic::h42e1b4() {
    h_.vx() -= 0x2000;
    addX(h_.vx());
    if (hAnim(0x465b47, 0x4616b3, 4)) {
        h_.facing() = 0;
        h_.w(0x465b47) = 1;
        h42e26a();
        return;
    }
    h_.b(0x465b49) -= 1;
    if (h_.b(0x465b49) == 0) {
        h_.b(0x465b49) = 3;
        if (h_.b(0x465b4a) != 0) {
            h_.b(0x465b4a) -= 1;
            if (Obj* a5 = newObjAtHarry(1, true)) {
                a5->sprite = game_->findSprite("fxrdust2");
                a5->type = 0x41;
                a5->l(0x32) = 0x10000;
                a5->x += 0x18;
            }
        }
    }
    h42e08c();
}

void PlayLogic::h42e25e() {
    // FUN_0042e25e: run left
    h_.w(0x465b47) = 0xa;
    h42e26a();
}

void PlayLogic::h42e26a() {
    h_.b(0x465b3a) &= 3;
    h_.setCont(0x42e297);
    h_.facing() = 0xff;
    h_.setSprite("hyirun");
    h_.setUl(0x465ba0, 0x46166b);
    h42e297();
}

bool PlayLogic::runAttack() {
    // 0x42e333 / 0x42eb27: shoot or whip on the run
    if (h_.b(0x466769) == 1) {
        const int wpn = weaponSel();
        if (wpn == 1 && h_.ul(0x46681c) == 0) {
            if (ammo(1) != 0) { h42b98f(); return true; }
        } else if (wpn == 2) {
            if (ammo(2) != 0) {
                if (h_.b(0x465b3d) == 0) { h_.b(0x4667af) = 0xff; h42bb80(); return true; }
                goto whip;
            }
        } else if (wpn != 0) {
            goto whip;
        }
        if (ammo(0) != 0) {
            weaponSel() = 0;
            h_.b(0x4667af) = 0;
            h42bb80();
            return true;
        }
    }
whip:
    if (h_.b(0x46676b) == 1) { h42bb21(); return true; }
    return false;
}

void PlayLogic::h42e297() {
    h42e9b2();
    if (!h_.spriteIs("hyitrip1")) {
        if (neg16(h_.w(0x465ba6) - int16_t(0xfffe)) && h_.b(0x466766) && h_.b(0x466762)) { h42ee71(); return; }   // slide
        if (!(h_.vx() < int32_t(0xfffe0000)) && h_.b(0x466768)) { h42dfba(); return; }
        if (h_.b(0x46676a) == 1) { h42cd1e(); return; }
        if (h_.b(0x466767) == 0) {
            h4300e0(h_.b(0x466503) ? 0x5500 : 0x4000);
            if (h_.vx() == 0) { h428dfa(); return; }
            h42e427();
            return;
        }
    }
    if (runAttack()) return;
    h_.vx() -= 0x3000;
    int32_t max;
    if (h_.b(0x466503)) {
        h_.vx() -= 0x1500;
        max = int32_t(0xfffb0000);
    } else {
        max = h_.w(0x46652b) ? int32_t(0xfffc0000) : int32_t(0xfffd0000);
    }
    if (!(max - h_.vx() < 0)) h_.vx() = max;
    h42e427();
}

void PlayLogic::h42e427() {
    addX(h_.vx());
    h_.w(0x465b9a) += 1;
    h42e450();
}

void PlayLogic::h42e450() {
    // walls on the left
    if (h_.b(0x465b3a) & 1) { h428dfa(); return; }
    bool snap = (trg(h_.x() - 0x28, h_.y() - 4).cls & 1) || (trg(h_.x() - 0x38, h_.y() - 4).cls & 1);
    if (!snap) {
        if (h_.cont() == 0x42fadf) return;
        if (!(trg(h_.x() - 0x38, h_.y() - 0x1c).cls & 1)) return;
    } else {
        h_.x() = int16_t((h_.x() & 0xfff0) + 7);
        h_.vx() = 0;
    }
    h42e55f(true);
}

void PlayLogic::h42e55f(bool left) {
    // against a wall: a hard run bumps (0x42ed82), else he pushes (FUN_0042f0c4 / FUN_0042f123)
    if (!neg16(h_.w(0x465b9a) - 0x32)) { h42ed82(); return; }
    h_.w(0x465b9a) = 0;
    if (h_.b(0x465b73) != 0 || h_.cont() == 0x428e50 || level_ == 0xa) return;
    if (left) h42f0c4(); else h42f123();
}

void PlayLogic::h42e59a() {
    // a running jump against a high wall while holding up: a back flip off it
    const int16_t d0 = int16_t(h_.x() + (h_.facing() ? -0x40 : 0x20));
    int16_t d1 = int16_t(h_.y() - 0x18);
    for (int d5 = 3; d5 >= 0; --d5) {
        d1 -= 0x10;
        if (!(trg(d0, d1).cls & 1)) { h42e808(); return; }
    }
    d1 = int16_t(h_.y() - 0x18);
    for (int d2 = 3; d2 >= 0; --d2) {
        if (ph::probeNear(det(), h_.x() - 0x10, d1).hit) { h42e808(); return; }
        d1 -= 0x10;
    }
    h_.setCont(0x42e6c1);
    h_.setSprite("hyibflip");
    h_.setUl(0x465b47, 0x42e7f9);
    h_.w(0x465b4b) = 0;
    h_.b(0x465b4d) = 0;
    h42e6c1();
}

void PlayLogic::h42e6c1() {
    if (h_.b(0x465b4d) == 0) {
        if (!hAnim(0x465b4b, 0x42e7e7, 3)) return;
        h_.b(0x465b4d) = 0xff;
        h_.l(0x465b4f) = 0x40000;
        h_.l(0x465b53) = int32_t(0xfffc8000);
        h_.w(0x465b4b) = 0;
    }
    hAnim2(0x465b47, 4);
    h42e82b();
    if (grabClimbable()) { h42cc5d(); return; }
    addY(h_.l(0x465b53));
    if (h_.facing() == 0) addX(-h_.l(0x465b4f)); else addX(h_.l(0x465b4f));
    if (h_.w(0x465b51) != 0) h_.l(0x465b4f) -= 0x2000;
    if (h_.w(0x465b55) != 6) h_.l(0x465b53) += 0x5000;
    h_.vy() = h_.l(0x465b53);
    if (h_.frame() == 8 && feet()) h42a896();
}

void PlayLogic::h42e808() {
    h_.w(0x465b9a) = 0;
    runState();                                            // jmp harryCont: the state again
}

void PlayLogic::h42e81d() {
    if (h_.vx() < 0) h42e838(); else h42e8f5();
}

void PlayLogic::h42e82b() {
    if (h_.facing() != 0) h42e8f5(); else h42e838();
}

void PlayLogic::h42e838() {
    if ((trg(h_.x() - 0x38, h_.y() - 0x20).cls & 1) || (trg(h_.x() - 0x38, h_.y() - 0x10).cls & 1) ||
        (trg(h_.x() - 0x38, h_.y()).cls & 1)) {
        h_.x() = int16_t((h_.x() & 0xfff0) + 7);
        h_.vx() = 0;
    }
}

void PlayLogic::h42e8f5() {
    if ((trg(h_.x() + 0x18, h_.y() - 0x20).cls & 1) || (trg(h_.x() + 0x18, h_.y() - 0x10).cls & 1) ||
        (trg(h_.x() + 0x18, h_.y()).cls & 1)) {
        h_.x() = int16_t((h_.x() & 0xfff0) + 7);
        h_.vx() = 0;
    }
}

void PlayLogic::h42e9b2() {
    // FUN_0042e9b2: the run animation (DAT_00465ba0: hyirun, now and then hyirun2)
    for (int guard = 0; guard < 4; ++guard) {
        h_.onGround() = 0xff;
        const int ticks = h_.spriteIs("hyitrip1") ? 5 : 4;
        if (!hAnim(0x465b47, h_.ul(0x465ba0), ticks)) return;
        if (h_.ul(0x465ba0) != 0x46166b) {
            h_.setSprite("hyirun");
            h_.setUl(0x465ba0, 0x46166b);
            continue;
        }
        h_.b(0x465b79) -= 1;
        if (!neg8(h_.b(0x465b79))) return;
        h_.b(0x465b79) = uint8_t(random() & 3);
        h_.setUl(0x465ba0, 0x461682);
        h_.setSprite("hyirun2");
    }
}

void PlayLogic::h42ea52() {
    // FUN_0042ea52: run right
    h_.w(0x465b47) = 0xa;
    h42ea5e();
}

void PlayLogic::h42ea5e() {
    h_.b(0x465b3a) &= 3;
    h_.setCont(0x42ea8b);
    h_.facing() = 0;
    h_.setSprite("hyirun");
    h_.setUl(0x465ba0, 0x46166b);
    h42ea8b();
}

void PlayLogic::h42ea8b() {
    h42e9b2();
    if (!h_.spriteIs("hyitrip1")) {
        if (!neg16(h_.w(0x465ba6) - 2) && h_.b(0x466766) && h_.b(0x466762)) { h42ee71(); return; }
        if (!(h_.vx() < 0x20000) && h_.b(0x466767)) { h42e18e(); return; }
        if (h_.b(0x46676a) == 1) { h42cd1e(); return; }
        if (h_.b(0x466768) == 0) {
            h4300e0(h_.b(0x466503) ? 0x5500 : 0x4000);
            if (h_.vx() == 0) { h428dfa(); return; }
            h42ec1b();
            return;
        }
    }
    if (runAttack()) return;
    h_.vx() += 0x3000;
    int32_t max;
    if (h_.b(0x466503)) {
        h_.vx() += 0x1500;
        max = 0x50000;
    } else {
        max = h_.w(0x46652b) ? 0x40000 : 0x30000;
    }
    if (max - h_.vx() < 0) h_.vx() = max;
    h42ec1b();
}

void PlayLogic::h42ec1b() {
    addX(h_.vx());
    h_.w(0x465b9a) += 1;
    h42ec44();
}

void PlayLogic::h42ec44() {
    // walls on the right
    if (h_.b(0x465b3a) & 2) { h428dfa(); return; }
    bool snap = (trg(h_.x() + 8, h_.y() - 4).cls & 1) || (trg(h_.x() + 0x18, h_.y() - 4).cls & 1);
    if (!snap) {
        if (h_.cont() == 0x42fadf) return;
        if (!(trg(h_.x() + 0x18, h_.y() - 0x1c).cls & 1)) return;
    } else {
        h_.x() = int16_t((h_.x() & 0xfff0) + 9);
        h_.vx() = 0;
    }
    h42e55f(false);
}

void PlayLogic::h42ed82() {
    // ran into a wall at full speed: knocked onto his back
    h_.setCont(0x42edb1);
    h_.w(0x465b47) = 0;
    sound1(0x4a, 9);
    h42edb1();
}

void PlayLogic::h42edb1() {
    const bool done = hAnim(0x465b47, 0x4314f8, 4);
    h_.setSprite("hyihwall");
    if (!neg8(h_.b(0x465b47) - 0x14)) {
        h_.setSprite("hyisprng");
    } else if (h_.frame() != 7) {
        addX(-0x8e00);
        if (h_.facing()) addX(0x11c00);
    }
    if (!neg8(h_.frame() - 0xf)) {
        h_.frame() -= 0x10;
        h_.setSprite("hyigland");
    }
    if (!done) return;
    h_.x() += 0x17;
    if (h_.facing()) h_.x() -= 0x2e;
    h428dfa();
}

void PlayLogic::h42ee71() {
    // a running slide (down + jump)
    h_.setCont(0x42ee90);
    h_.w(0x465b47) = 0;
    h_.w(0x465b49) = 0x301;
    h42ee90();
}

void PlayLogic::h42ee90() {
    h_.onGround() = 0xff;
    bool f = feet();
    h_.onGround() = 0;
    if (!f) {
        h_.vx() = h_.facing() ? -0x28000 : 0x28000;
        h_.y() += 8;
        h42f034();
        return;
    }
    h_.setSprite("hyirn2cl");
    if (hAnim(0x465b47, 0x4615a2, 4)) { h42fabb(); return; }
    if (h_.w(0x465b47) == 3) {
        h_.onGround() = 0xff;
        f = feet();
        h_.onGround() = 0;
        if (!f) { h42f034(); return; }
    }
    if (trg(h_.x() + (h_.facing() ? -0x38 : 0x18), h_.y() - 8).cls & 1) return;
    addX(h_.vx());
    if (neg8(h_.frame() - 4)) return;
    if (h_.vx() < 0) h_.vx() += 0x2900; else h_.vx() -= 0x2900;
    h_.b(0x465b49) -= 1;
    if (h_.b(0x465b49) != 0) return;
    h_.b(0x465b49) = 3;
    if (h_.b(0x465b4a) == 0) return;
    h_.b(0x465b4a) -= 1;
    if (Obj* a5 = newObjAtHarry(1, true)) {
        a5->sprite = game_->findSprite("fxrdust2");
        a5->type = 0x41;
        a5->y += 5;
        a5->l(0x32) = int32_t(0xffff0000);
        a5->x -= 0x16;
        a5->facing = h_.facing();
        if (!h_.facing()) { a5->x += 0x30; a5->l(0x32) = -a5->l(0x32); }
    }
}

void PlayLogic::h42f034() {
    // slid off an edge: somersaulting down
    h_.setCont(0x42f06c);
    h_.setSprite("hyisomr1");
    h_.w(0x465b9e) = 0;
    h_.w(0x465b47) = 0;
    h_.vy() = 0;
    h_.onGround() = 0;
    h42f06c();
}

void PlayLogic::h42f06c() {
    if (!hAnim(0x465b47, 0x4615e0, 4)) { h42c643(); return; }
    h_.setSprite("hyisomr2");
    h_.setCont(0x42cdb6);
    h_.w(0x465b47) = 0;
    h_.w(0x465b9e) = 0;
    h42cdb6();
}

void PlayLogic::h42f0c4() {
    // FUN_0042f0c4: pushing against a wall on the left
    h_.setCont(0x42f0e1);
    h_.setUl(0x465b47, 0x461643);
    h_.w(0x465b4b) = 0;
    h42f0e1();
}

void PlayLogic::h42f0e1() {
    feet();
    h_.b(0x465b71) = 1;
    if (h_.b(0x466767) == 0) { h42f17e(); return; }
    if (hAnim2(0x465b47, 4)) {
        // (sic) the byte written over the script pointer's low byte, then FUN_00423134 on it
        const uint32_t a1 = h_.ul(0x465b47);
        h_.b(0x465b47) = 3;
        hAnim(0x465b47, a1, 1);
    }
}

void PlayLogic::h42f123() {
    // FUN_0042f123: ... on the right
    h_.setCont(0x42f140);
    h_.setUl(0x465b47, 0x461643);
    h_.w(0x465b4b) = 0;
    h42f140();
}

void PlayLogic::h42f140() {
    feet();
    h_.b(0x465b71) = 1;
    if (h_.b(0x466768) == 0) { h42f17e(); return; }
    if (hAnim2(0x465b47, 4)) {
        const uint32_t a1 = h_.ul(0x465b47);
        h_.b(0x465b47) = 3;
        hAnim(0x465b47, a1, 1);
    }
}

void PlayLogic::h42f17e() {
    h_.setCont(0x42f19e);
    h_.w(0x465b47) = 0;
    h_.setSprite("hyist2pu");
    h42f19e();
}

void PlayLogic::h42f19e() {
    if (hAnim(0x465b47, 0x46163f, 3)) h428dfa();
}

void PlayLogic::h42f1c9() {
    // FUN_0042f1c9: banged his head on the ceiling
    h_.setCont(0x42f1ee);
    h_.w(0x465b47) = 0;
    h_.y() -= 4;
    h_.b(0x465b77) = 0xf;
    h42f1ee();
}

void PlayLogic::h42f1ee() {
    h_.setSprite("hyiding");
    if (!hAnim(0x465b47, 0x461638, 3)) return;
    h_.vx() = 0;
    h_.vy() = 0x10000;
    h42c467();
}

void PlayLogic::h42f238() {
    // turning round while crouched
    h_.setCont(0x42f26e);
    h_.w(0x465b47) = 0;
    h_.setSprite("hyicrtrn");
    h_.facing() = h_.facing() ? 0 : 0xff;
    h42f26e();
}

void PlayLogic::h42f26e() {
    h42fa4b();
    if (hAnim(0x465b47, 0x42f2cb, 2)) {
        h_.b(0x465b71) = 0x16;
        h_.setCont(0x42f2f9);
        h_.facing() ^= 0xff;
        h4285c5();
        return;
    }
    if (h_.w(0x465b47) == 3) h_.facing() ^= 0xff;
}

} // namespace game

namespace game {

// ---------------------------------------------------------------- crouching and crawling

void PlayLogic::h42f2d2() {
    // crouch
    h_.setCont(0x42f2f9);
    h_.l(0x465b41) = 0;
    h_.l(0x465b45) = 0;
    h_.w(0x465b49) = 0;
    h42f2f9();
}

void PlayLogic::h42f2f9() {
    if (h_.b(0x465c56) == 0 && h_.b(0x466762) == 0 && h_.b(0x466766) != 0) {
        bool ok = true;                                    // not pressing away from where he faces
        if (h_.b(0x466767)) ok = h_.facing() != 0;
        else if (h_.b(0x466768)) ok = h_.facing() == 0;
        if (ok) {
            if (h_.b(0x46676b) == 1) { h42ba4c(); return; }
            if (h_.b(0x466769) == 1) {
                const int wpn = weaponSel();
                bool stones = wpn == 0;
                if (wpn == 2) {
                    if (ammo(2) == 0) stones = true;
                    else if (h_.b(0x465b3d) == 0) { h_.b(0x4667af) = 0xff; h42babc(); return; }
                }
                if (stones && ammo(0) != 0) {
                    weaponSel() = 0;
                    h_.b(0x4667af) = 0;
                    h42babc();
                    return;
                }
            }
        }
    }
    if (h_.b(0x466766) == 0) { h42f4e1(); return; }        // stands up
    h_.setSprite("hyist2cr");
    if (h_.b(0x465b71) != 0x16) {
        // going down
        h_.b(0x465b71) += 1;
        h_.frame() = mb(0x4615f0 + h_.b(0x465b71));
        return;
    }
    if (h_.b(0x465c56) == 0) {
        if (h_.b(0x466762)) { h42f8fc(); return; }        // jump while crouched: crawl
        if (h_.b(0x466767)) { if (h_.facing() == 0) { h42f238(); return; } }
        else if (h_.b(0x466768)) { if (h_.facing() != 0) { h42f238(); return; } }
    }
    h42fa4b();
    h_.setSprite("hyicrcyc");
    hAnim(0x465b43, 0x4615e7, 5);
}

void PlayLogic::h42f486() {
    // FUN_0042f486: from the crawl to standing
    h_.setCont(0x42f4ad);
    h_.setSprite("hyicl2rn");
    h_.w(0x465b47) = 0;
    h_.b(0x465b71) = 0;
    h42f4ad();
}

void PlayLogic::h42f4ad() {
    if (hAnim(0x465b47, 0x42f4db, 4)) h428dfa();
}

void PlayLogic::h42f4e1() {
    h_.setCont(0x42f508);
    h_.setSprite("hyist2cr");
    h_.w(0x465b47) = 0;
    h_.b(0x465b71) = 0;
    h42f508();
}

void PlayLogic::h42f508() {
    if (!hAnim(0x465b47, 0x42f56e, 4)) return;
    if (h_.b(0x465c56) == 0) { h428dfa(); return; }
    // (in the mine cart)
    h_.setCont(0x42aa3b);
    h_.l(0x465b45) = 0;
    h_.l(0x465b59) = 0;
    h_.w(0x465b5d) = 0;
    h_.w(0x465b51) = 0;
    h42aa3b();
}

void PlayLogic::h42f574() {
    // catching the boomerang in the air
    h_.setSprite("hyivgrab");
    if (hAnim(0x465b47, 0x4619c1, 2)) {
        h_.setCont(0x42c518);
        h_.w(0x465b47) = 0;
        h42c518();
        return;
    }
    if (h_.w(0x465b47) == 1) {
        Obj* o = objAt(h_.ul(0x465b41));
        if (o && (o->type == 6 || o->type == 0x87)) {
            objects_.free(*o);
            ammo(1) += 1;
        }
    }
    h42c643();
}

void PlayLogic::h42f600() {
    // the whip in a standing jump
    h_.setCont(0x42f627);
    h_.b(0x46676b) = 0x80;
    h_.setSprite("hyivwhip");
    h_.w(0x465b47) = 0;
    h42f627();
}

void PlayLogic::h42f627() {
    h42b2bc();
    if (hAnim(0x465b47, 0x42f68a, 3)) {
        h_.setCont(0x42c518);
        h_.w(0x465b47) = 0;
        h_.b(0x465b7b) = 0xe;
        h_.onGround() = 0;
        h_.setSprite("hyivjump");
        h42c518();
        return;
    }
    h42c643();
}

void PlayLogic::h42f69a() {
    // FUN_0042f69a: a stone thrown in the air
    h_.setCont(0x42f6c8);
    h_.b(0x466769) = 0x80;
    h_.setSprite("hyivwhip");
    h_.setUl(0x465b43, 0x42f71c);
    h_.w(0x465b47) = 0;
    h42f6c8();
}

void PlayLogic::h42f6c8() {
    if (hAnim2(0x465b43, 3)) {
        h_.setCont(0x42c518);
        h_.w(0x465b47) = 0;
        h_.b(0x465b7b) = 0xe;
        h_.onGround() = 0;
        h_.setSprite("hyivjump");
        h42c518();
        return;
    }
    h42c643();
}

void PlayLogic::h42f735() {
    // FUN_0042f735: the boomerang thrown in the air
    h_.setCont(0x42f759);
    h_.b(0x466769) = 0x80;
    h_.setUl(0x465b43, 0x42f7da);
    h_.w(0x465b47) = 0;
    h42f759();
}

void PlayLogic::h42f759() {
    if (hAnim2(0x465b43, 3)) {
        h_.setCont(0x42c518);
        h_.w(0x465b47) = 0;
        h_.b(0x465b7b) = 0xe;
        h_.onGround() = 0;
        h_.setSprite("hyivjump");
        h42c518();
        return;
    }
    if (h_.b(0x465b47) == 0 && h_.frame() == 4 && h_.spriteIs("hyibmrng")) h4313aa();
    h42c643();
}

void PlayLogic::h42f7f2() {
    // the whip in a running jump
    h_.b(0x46676b) = 0x80;
    h_.setCont(0x42f820);
    h_.setSprite("hyihwhip");
    h_.setUl(0x465b47, 0x42f879);
    h_.w(0x465b4b) = 0;
    h42f820();
}

void PlayLogic::h42f820() {
    h42b2bc();
    if (hAnim2(0x465b47, 3)) {
        h_.setCont(0x42cdb6);
        h_.w(0x465b47) = 0;
        h_.b(0x465b7b) = 0xe;
        h_.onGround() = 0;
        h_.setSprite("hyihjump");
        h42cdb6();
        return;
    }
    h42c643();
}

void PlayLogic::h42f88c() {
    // knocked out of the air by a hit
    h_.setSprite("hyijp2sn");
    h_.frame() = 0;
    if (h_.l(0x466761) == 0) {
        h_.b(0x465b48) += 1;
        if (h_.b(0x465b48) != 0xe) {
            h_.w(0x465b9e) = 0;
            h42c643();
            return;
        }
    }
    h_.setCont(0x42c518);
    h_.w(0x465b47) = 0;
    h_.b(0x465b7b) = 0xe;
    h_.onGround() = 0;
    h_.setSprite("hyivjump");
    h42c518();
}

void PlayLogic::h42f8fc() {
    // down onto all fours
    h_.setCont(0x42f939);
    h_.setSprite("hyicr2cl");
    h_.w(0x465b47) = 0;
    h_.vx() = h_.facing() ? int32_t(0xfffe0000) : 0x20000;
    h_.onGround() = 0xff;
    h42f939();
}

void PlayLogic::h42f939() {
    addX(h_.vx());
    h4300e0(0x2000);
    feet();
    h42fa4b();
    if (hAnim(0x465b47, 0x4615e4, 4)) { h42fabb(); return; }
    const int16_t d0 = int16_t(h_.vx() < 0 ? h_.x() - 0x38 : h_.x() + 0x18);
    if (trg(d0, h_.y() - 8).cls & 1) h_.vx() = 0;
}

void PlayLogic::h42f9fc() {
    // the ceiling is too low to stand up: back to crawling
    h_.setCont(0x42fa1a);
    h_.w(0x465b47) = 0;
    h_.setSprite("hyicl2rn");
    h42fa1a();
}

void PlayLogic::h42fa1a() {
    if (hAnim(0x465b47, 0x42fa44, 3)) h42fabb();
}

void PlayLogic::h42fabb() {
    // crawling
    h_.setCont(0x42fadf);
    h_.w(0x465b47) = 0;
    h_.b(0x465b4f) = 0;
    h_.onGround() = 0xff;
    h42fadf();
}

void PlayLogic::h42fadf() {
    h_.b(0x465b71) = 0x16;
    h42fa4b();
    bool crawl = h_.b(0x466766) != 0;
    bool keepCount = false;
    if (!crawl) {
        if (h_.b(0x46676d) != 1) {
            if (h_.b(0x466765) == 0) crawl = true;
            else {
                h_.b(0x465b4f) += 1;                       // up held for 6 ticks: stand up
                if (h_.b(0x465b4f) != 6) { crawl = true; keepCount = true; }
            }
        }
    }
    if (!crawl) {
        // room to stand up?
        static const int16_t kProbe[6][2] = {{-0x36, -0x18}, {-0x26, -0x18}, {-0x16, -0x18}, {-6, -0x18}, {4, -0x10}, {0x14, -0x10}};
        for (const auto& p : kProbe)
            if (trg(h_.x() + p[0], h_.y() + p[1]).cls & 1) { h42f9fc(); return; }
        if (h_.w(0x466767) == 0) h42f486(); else h430011();
        return;
    }
    if (!keepCount) h_.b(0x465b4f) = 0;
    h_.setSprite("hyifcrwl");
    if (h_.b(0x466767)) {
        h_.x() -= 1;
        if (h_.w(0x46652b)) addX(-0x8000);
        if (h_.facing() == 0) addX(0x8000);
        h_.vx() = int32_t(0xfffd8000);
        if (trg(h_.x() - 0x38, h_.y()).cls & 1) { h_.x() = int16_t((h_.x() & 0xfff0) + 7); h_.vx() = 0; return; }
    } else if (h_.b(0x466768)) {
        h_.x() += 1;
        if (h_.w(0x46652b)) addX(0x8000);
        if (h_.facing() != 0) addX(-0x8000);
        h_.vx() = 0x28000;
        if (trg(h_.x() + 0x18, h_.y()).cls & 1) { h_.x() = int16_t((h_.x() & 0xfff0) + 9); h_.vx() = 0; return; }
    } else {
        return;
    }
    h_.b(0x465b4b) = 0x28;
    if (!feet()) { h_.onGround() = 0; h42f034(); return; }
    h_.b(0x465b47) -= 1;
    if (!neg8(h_.b(0x465b47))) return;
    h_.b(0x465b47) = 4;
    if (h_.facing()) {
        h_.b(0x465b48) -= 1;
        if (h_.vx() >= 0) h_.b(0x465b48) += 2;
    } else {
        h_.b(0x465b48) += 1;
        if (h_.vx() < 0) h_.b(0x465b48) -= 2;
    }
    h_.b(0x465b48) &= 7;
    h_.frame() = mb(0x4615d6 + h_.b(0x465b48));
    if (h_.frame() == 3 || h_.frame() == 7) sound1(0xc2, 5);
}

void PlayLogic::h430011() {
    // from the crawl straight into a run
    h_.setCont(0x430044);
    h_.w(0x465b47) = 0;
    h_.uw(0x465b45) = h_.uw(0x466767);
    h_.setSprite("hyicl2rn");
    h_.vx() = 0;
    h430044();
}

void PlayLogic::h430044() {
    if (hAnim(0x465b47, 0x4300d6, 2)) {
        if (h_.facing()) h42e25e(); else h42ea52();
        return;
    }
    if (h_.frame() == 1) h_.vx() = h_.facing() ? int32_t(0xfffe2000) : 0x1e000;
    addX(h_.vx());
    if (h_.facing()) h42e450(); else h42ec44();
}

} // namespace game

namespace game {

// ---------------------------------------------------------------- hanging from vines (trigger 0x1d)

void PlayLogic::h42bcfa() {
    h_.y() += 0x18;
    h_.x() -= 0x10;
    h42bd09();
}

void PlayLogic::h42bd09() {
    h_.y() = int16_t((h_.y() & 0xfff0) - 8);
    h_.x() = int16_t((h_.x() & 0xfff0) + 8);
    h_.vx() = 0;
    h_.l(0x465b53) = 0;
    if (Obj* a5 = newObjAtHarry(1, true)) {               // the vine in his hands
        a5->sprite = game_->findSprite("ob_sling");
        a5->frame = 0xff;
        a5->flags = 0x6000;
        h_.setUl(0x465b53, addrOf(a5));
    }
    h42bd68();
}

void PlayLogic::h42bd68() {
    h_.l(0x465b45) = 0;
    h_.setSprite("hyisswng");
    h_.frame() = 2;
    h_.setCont(0x42bda7);
    h_.setUl(0x465b4d, 0x46146b);
    h_.w(0x465b51) = 0;
    h_.w(0x465b4b) = 0;
    h_.w(0x465b47) = 1;
    h42bda7();
}

void PlayLogic::h42bda7() {
    h_.b(0x465b95) = 0x1e;
    if (h_.w(0x466767) != 0) h_.uw(0x465b4b) = h_.uw(0x466767);
    auto dropOff = [&] {
        if (Obj* o = objAt(h_.ul(0x465b53))) objects_.free(*o);
        h_.b(0x46676a) = 0x80;
        h_.y() += 0x20;
        if (h_.w(0x465b4b) != 0) {
            h_.facing() = 0;
            if (h_.b(0x465b4b) != 0) h_.facing() = 0xff;
        }
        h_.b(0x465b7b) = 0xc;
        h_.vy() = int32_t(0xfffd6000);
        h_.vx() = 0;
        h_.onGround() = 0;
        h42c1ea();
    };
    if (h_.b(0x46676d) == 1) { dropOff(); return; }
    if (h_.w(0x465b45) != 0) {
        // a swing hand over hand along the vine
        h_.uw(0x466767) = h_.uw(0x465b45);
        const bool done = hAnim(0x465b47, 0x46145e, 4);
        if (h_.b(0x465b48) == 0) {
            const uint8_t f = h_.frame();
            if (f == 5 || f == 0xb) sound0(0x1b, 9);
            if (f == 6 || f == 0) {
                sound1(0xcd, 9);
                if (level_ == 6 || level_ == 1) {
                    if (Obj* a5 = newObjAtHarry(0, true)) {   // a leaf shaken loose
                        a5->x += int16_t(h_.facing() ? -0x40 : 0x40);
                        a5->y -= 0x32;
                        a5->w(0x4e) = 0x220;
                        a5->w(0x58) = 0x70;
                        a5->w(0x64) = 0x50;
                        a5->flags = 0;
                        a5->type = 0x40;
                        const uint32_t r = random() & 0x7fff;
                        a5->l(0x36) = int32_t(r);
                        if (r & 1) {
                            a5->sprite = game_->findSprite("fx_leaf1");
                            a5->script = harryMem(0x4619df);
                            a5->l(0x36) += 0x6000;
                        } else {
                            a5->sprite = game_->findSprite("fxzleaf2");
                            a5->script = harryMem(0x4619e8);
                            a5->l(0x36) += 0x10000;
                        }
                    }
                }
            }
        }
        if (done) { h_.w(0x465b45) = 0; return; }
        h_.setSprite("hyisswng");
        const int16_t p = h_.w(0x465b47);
        if (p == 7) { h_.w(0x465b45) = 0; return; }
        if (p == 0xa || p == 4) {
            h_.x() += 0x40;
            if (h_.facing()) h_.x() -= 0x80;
        }
        return;
    }
    h_.uw(0x465b45) = h_.uw(0x466767);
    if (h_.w(0x465b45) != 0) {
        game_->regA5() = h_.ul(0x465b53);                   // 0x42be7c
        if (Obj* a5 = objAt(h_.ul(0x465b53))) a5->frame = 0xff;
        if (h_.b(0x466767) == 0) {
            if (trg(h_.x() + 0x30, h_.y() - 0x38).id != 0x1d) { dropOff(); return; }
            h_.facing() = 0;
        } else {
            if (trg(h_.x() - 0x50, h_.y() - 0x38).id != 0x1d) { dropOff(); return; }
            h_.facing() = 1;
        }
        h_.setUl(0x465b4d, 0x46146b);
        h_.w(0x465b51) = 0;
        return;
    }
    // hanging still
    game_->regA5() = h_.ul(0x465b53);                       // 0x42bf53
    if (Obj* a5 = objAt(h_.ul(0x465b53))) {
        a5->x = h_.x();
        a5->y = int16_t(h_.y() - 0x17);
        a5->frame = 0;
    }
    h_.w(0x465b45) = 0;
    h_.setSprite("hyihang");
    hAnim2(0x465b4d, 9);
    h_.w(0x465b47) = h_.facing() ? 8 : 1;
    if (h_.b(0x46676b) == 1) { h42dda4(); return; }
    if (h_.b(0x466769) != 1) return;
    const int wpn = weaponSel();
    if (wpn == 1) return;
    if (wpn == 2) {
        if (ammo(2) != 0) {
            if (h_.b(0x465b3d) != 0) return;
            h_.b(0x4667af) = 0xff;
            h42dcbc();
            return;
        }
    } else if (wpn != 0) {
        return;
    }
    if (ammo(0) == 0) return;
    weaponSel() = 0;
    h_.b(0x4667af) = 0;
    h42dcbc();
}

// ---------------------------------------------------------------- 0x428780: through a vortex

void PlayLogic::h428780() {
    // Harry spins away in a vortex (the script 0x428a43, 6 ticks a frame).  Then every object
    // goes, and the trigger under the vortex says where to: 0x89 the 2600 level ("*50", with
    // the level kept as it is), 0x8a / 0x8b / 0x8c a vault ("*53" / "*51" / "*52", with the
    // level and the way back saved by FUN_004288ec; the vortex stays shut from then on).
    // Anything else puts him back at the checkpoint (FUN_00432615).
    h_.onGround() = 0;
    if (!hAnim2(0x465b4b, 6)) return;
    h4311bf();                                             // (every register saved around it)
    game_->regA5() = h_.ul(0x465b43);                      // 0x428862
    Obj* a5 = objAt(h_.ul(0x465b43));                      // (the record is free by now; x / y stay)
    const uint8_t id = a5 ? trg(a5->x - 0x18, a5->y + 0x18).id : 0;
    Game::Warp& wp = game_->warp();
    const char* cmd = nullptr;
    switch (id) {
    case 0x89:                                             // 0x4288a2
        wp.als.assign(alsMem_.begin(), alsMem_.end());
        wp.cpX = h_.w(0x46679e); wp.cpY = h_.w(0x4667a0); wp.cpFacing = h_.w(0x4667a4);   // -> 0x46a860..
        cmd = "*50";
        break;
    case 0x8a: h4288ec(); wp.used[0] = h_.x(); cmd = "*53"; break;
    case 0x8b: h4288ec(); wp.used[1] = h_.x(); cmd = "*51"; break;
    case 0x8c: h4288ec(); wp.used[2] = h_.x(); cmd = "*52"; break;
    default: break;
    }
    if (cmd) {
        if (!exiting_) { exiting_ = true; game_->command(cmd); }
        return;
    }
    for (Obj* p : objects_.list()) { restoreEntry(*p); objects_.free(*p); }   // the stones in flight
    restartAtCheckpoint();                                 // jmp FUN_00432615
}

void PlayLogic::h4288ec() {
    // FUN_004288ec: the level as Harry leaves it for a vault, and where he comes back.
    Game::Warp& wp = game_->warp();
    wp.als.assign(alsMem_.begin(), alsMem_.end());         // [DAT_004504e4] -> 0x4601dd (4 KB)
    std::memcpy(wp.classes, h_.trgClass(), 0x100);        // 0x4658c0 -> 0x4611dd
    wp.level = level_;                                     // DAT_00465af3
    wp.x = int16_t(h_.x() + 4);                            // DAT_00465af7
    wp.y = h_.y();                                         // DAT_00465af9
    wp.facing = h_.facing();                               // DAT_00465afd
    wp.cpX = h_.w(0x46679e); wp.cpY = h_.w(0x4667a0); wp.cpFacing = h_.w(0x4667a4);   // -> 0x46a860..
}

// ---------------------------------------------------------------- FUN_004311bf: the objects start again

void PlayLogic::h4311bf() {
    // The enemies killed in the last 15 s come back, then every object puts its .als entry back
    // and goes, but the sling stones in flight (type 5).  (The stone / boomerang slots keep
    // pointing at their freed records, as in the original.)
    for (uint32_t a = kKillList; a < kKillListEnd; a += 8)
        if (h_.b(a) != 0) putEntryId(h_.ul(a + 2), h_.b(a + 1));
    for (Obj* p : objects_.list()) {
        Obj& o = *p;
        restoreEntry(o);
        if (o.type != 5) objects_.free(o);
    }
}

} // namespace game
