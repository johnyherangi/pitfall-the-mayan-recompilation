// Per-type object behaviour, ported from the handler table at 0x462350.
// Each handler quotes the address it was read from; state offsets are the
// original ones (see objects.h).  Helper names follow objects.h.
#include "game/objects.h"

#include "game/game.h"
#include "game/play_logic.h"

#include <algorithm>
#include <cstring>

namespace game {

// (dx, dy) tables used by the sparkle spawners, 0x8000-terminated.
const int16_t kSparkleOffsetsA[] = {9, 8, -7, 11, -4, -10, -3, -3, -10, -4, 9, 4, -4, -10, int16_t(0x8000)};   // 0x4620f0
const int16_t kSparkleOffsetsB[] = {-9, -8, 10, 0, -1, 9, -7, 3, int16_t(0x8000)};                               // 0x46210e
const int16_t kSparkleOffsetsC[] = {-9, -8, 10, 0, -1, 9, int16_t(0x8000)};                                      // 0x462120 (level 7)
namespace {
const int16_t kItemSparkle[][2] = {{4, -10}, {0, -5}, {-2, -16}, {1, -6}};        // 0x4620d4 coin, 0x4620d8 gold, 0x4620dc ring, 0x4620e0 silver
const int16_t k1upSparkle[][2] = {{10, -25}, {-5, -15}, {11, -1}};               // 0x4620e4
}

namespace {

// Signed 16.16 add into (x, xFrac): the translated `add.l d0, 2(a6)` pairs.
inline void addFixed(int16_t& whole, uint16_t& frac, int32_t v) {
    const int32_t sum = int32_t(frac) + (v & 0xffff);
    frac = uint16_t(sum);
    whole = int16_t(whole + (v >> 16) + (sum >> 16));
}
inline void moveX(Obj& o, int32_t v) { addFixed(o.x, o.xFrac, v); }
inline void moveY(Obj& o, int32_t v) { addFixed(o.y, o.yFrac, v); }

inline bool between(int v, int lo, int hi) { return v >= lo && v < hi; }   // [lo, hi)

// ---------------------------------------------------------------- generic effects

// 0x40a2c8: type 0x10 sparkle (fx_sprk1 / fx_sprk2), optional drift and delay.
void h_sparkle(ObjCtx& ctx, Obj& o) {
    if (o.b(0x57)) {
        uint8_t id = 0;
        if (ctx.triggerClass(o.x, o.y, &id) & 1) { ctx.release(o); return; }
    }
    if (o.w(0x60)) { o.w(0x60) -= 1; return; }
    if (o.link && o.link->freed) { ctx.release(o); return; }       // follows its owner (+0x5c)
    moveX(o, o.l(0x32));
    moveY(o, o.l(0x36));
    if (animStep(ctx, o, o.anim(0x3c), o.b(0x64) ? kAnim_sparkle2 : kAnim_sparkle1, 3)) ctx.release(o);
}

// 0x418ec6: type 0x68 one-shot animation (o.script at b(0x46) ticks).
void h_effect(ObjCtx& ctx, Obj& o) {
    if (!o.script || animStep(ctx, o, o.anim(0x32), o.script, o.b(0x46))) ctx.release(o);
}

// 0x41b9d1: type 0x82 death cloud; a linked second object shows the body.
void h_deathCloud(ObjCtx& ctx, Obj& o) {
    // 0x41b9d1: type 0x82, an enemy's death cloud (killObject).
    if (o.script) {                            // a pointer script at +0x64 (fx_deat2, the boss deaths)
        if (anim2Step(ctx, o, 0x64, 4)) ctx.release(o);
        return;
    }
    if (animStep(ctx, o, o.anim(0x32), kAnim_death14, 4)) {   // 0x461866
        ctx.release(o);
        if (o.link) ctx.release(*o.link);
        return;
    }
    // the mirrored half (+0x50) shows frames 1 and 2 only
    if (o.link) o.link->frame = (o.frame == 1 || o.frame == 2) ? o.frame : 0xff;
}

// ---------------------------------------------------------------- items

// 0x4057c9: the effect of a pickup, by its sprite (and for some its frame 0) through the table
// at 0x45a8d4.
void applyPickup(ObjCtx& ctx, Obj& o) {
    PlayerStats& st = ctx.g.stats();
    if (!o.sprite) return;
    const std::string& n = o.sprite->name();
    auto treasure = [&](int score, int add, int snd) {   // 0x40588c ..: DAT_00466526 counts towards a continue
        st.score += score;
        st.treasures = uint8_t(st.treasures + add);
        ctx.sound1(snd, 9);
    };
    if (n == "mi_stone") {                              // 0x40598d
        st.ammoCount[2] = uint8_t(st.ammoCount[2] + 2); st.score += 600; ctx.sound1(0x4b, 9);
    } else if (n == "fxrbmrot") {                       // 0x4059b8
        st.ammoCount[1] = uint8_t(st.ammoCount[1] + 1); st.score += 500; ctx.sound0(0x35, 7);
    } else if (n == "mi_pouc2") {                       // 0x405a22
        ctx.sound1(0xfc, 0xa); st.score += 50;
        st.ammoCount[0] = uint8_t(st.ammoCount[0] + (st.difficulty == 1 ? 8 : 10));
    } else if (n == "fx_hrtbt") {                       // 0x405935: heals 0x28
        ctx.sound1(0xfb, 9); st.score += 500;
        Harry& h = ctx.harry;
        if (h.damage() != 0) h.damage() = int16_t(h.damage() - 0x28) < 0 ? 0 : uint8_t(h.damage() - 0x28);
    } else if (n == "mi_ptfal") {                       // 0x405838: a letter of P-I-T-F-A-L-L
        st.score += 10000;
        int16_t bit = 1;
        while (true) { o.frame = uint8_t(o.frame - 1); if (int8_t(o.frame) < 0) break; bit = int16_t(bit + bit); }
        st.letters = uint8_t(st.letters + uint8_t(bit));
        ctx.sound1(0x24, 9);
    } else if (o.frame == 0) {
        if (n == "mi_chili") { ctx.harry.w(0x46652b) = 0x380; st.score += 600; ctx.sound1(0xfe, 0xc); }   // 0x405a5b
        else if (n == "mi_time") {                      // 0x405a88: the stop-watch
            ctx.harry.w(0x46652f) = 0x300; st.score += 750; ctx.harry.b(0x465897) = 0;   // (and DAT_00450500 = 1)
        } else if (n == "mi_1up") { ctx.sound0(0x33, 0x14); st.lives += 1; st.score += 2000; }   // 0x4059ed
        else if (n == "mi_gold") treasure(500, 5, 0xfe);
        else if (n == "mi_silvr") treasure(200, 2, 0xfe);
        else if (n == "mi_ring") treasure(1000, 10, 0xfe);
        else if (n == "mi_coin") treasure(100, 1, 0xfd);
    }
}

void spawnItemSparkle(ObjCtx& ctx, Obj& o, const int16_t* off, bool big) {
    // 0x405442 / 0x41d8d6: an fx_sprk2 at the item's sparkle point (FUN_004356bc(1)).
    if (Obj* e = ctx.spawnFrom(o)) {
        e->w(0x26) = 0x6663;
        e->x = int16_t(e->x + off[0]); e->y = int16_t(e->y + off[1]);
        e->type = 0x10; e->flags = 0x6000; if (big) e->b(0x64) = 0xff;
        e->sprite = ctx.spr("fx_sprk2");
    }
}

void h_oneUpFly(ObjCtx& ctx, Obj& o);

// 0x405328: type 2, the pickups (treasure, hearts, ammo, chili, letters, 1-ups).
void h_item(ObjCtx& ctx, Obj& o) {
    const std::string n = o.sprite ? o.sprite->name() : std::string();
    // Occasional sparkle (every 0x20..0x9f ticks, not on the first timeout).
    o.w(0x5e) -= 1;
    if (o.w(0x5e) < 0) {
        o.w(0x5e) = int16_t((ctx.rnd() & 0x7f) + 0x20);
        if (o.b(0x60) == 0) o.b(0x60) = 0xff;
        else {
            const int16_t* off = nullptr;
            if (n == "mi_coin") off = kItemSparkle[0];
            else if (n == "mi_gold") off = kItemSparkle[1];
            else if (n == "mi_silvr") off = kItemSparkle[3];
            else if (n == "mi_ring") off = kItemSparkle[2];
            else if (n == "mi_1up") {
                o.w(0x5e) = int16_t((ctx.rnd() & 0x3f) + 8);
                uint32_t r; do { r = ctx.rnd() & 3; } while (r == 3);
                off = k1upSparkle[r];
            }
            if (off) spawnItemSparkle(ctx, o, off, true);
        }
    }
    if (n == "fxrbmrot") {
        if (animStep(ctx, o, o.anim(0x42), kAnim_boomerangSpin, 6)) o.facing ^= 1;
    } else if (n == "mi_stone") {
        animStep(ctx, o, o.anim(0x42), kAnim_turn4, 4);
    } else if (n == "fx_hrtbt") {
        animStep(ctx, o, o.anim(0x42), ctx.boss ? kAnim_heartBoss : kAnim_heart, 6);   // 0x405589: faster beat during a boss fight
    } else {
        halfMinuteTick(o);                              // 0x4057b0
        if (o.b(0x50) == 0) {
            o.b(0x50) = 0xff;
            if (n == "mi_ptfal") {
                int16_t bit = 1;
                for (int8_t f = int8_t(o.frame); --f >= 0;) bit = int16_t(bit + bit);
                if (ctx.g.stats().letters & uint8_t(bit)) { ctx.release(o); return; }   // that letter is had
            }
            if (n != "mi_ring") {
                // Settle on the floor of either collision plane.
                int y = o.y + 0x10;
                ctx.floorSnap(o.x, y);
                o.y = int16_t(y - 0x10);
                y = o.y + 0x10;
                ctx.floorSnap(o.x, y, true);
                o.y = int16_t(y - 0x10);
            }
        }
    }
    if (o.w(0x68)) {
        o.w(0x68) -= 1;                               // pickup delay after dropping from a pouch
    } else if (ctx.touchesHarry(o)) {
        if (n == "mi_1up") {
            // The icon flies to the HUD (type 0xaa) and awards the life on arrival.
            o.type = 0xaa; o.b(0x32) = 0;
            ctx.g.stats().score += 2000;
            ctx.sound0(0x33, 0xa);                    // FUN_0042633d(0x33, 0x60)
            o.flags = uint16_t((o.flags & 0x7000) + 0x8000);
            o.behind = 0xff;
            h_oneUpFly(ctx, o);                       // 0x41fd26 in the same tick
            return;
        }
        applyPickup(ctx, o);
        ctx.pl.markCollected(o);
        o.y -= 3;
        ctx.sparkleBurst(o, ctx.levelNo == 7 ? kSparkleOffsetsC : kSparkleOffsetsB);   // 0x433706
        ctx.release(o);                               // (its display entry was cancelled first, 0x40572f)
        return;
    }
    if (o.b(0x6a) == 0) {
        if (ctx.harry.b(0x465c56)) exitDespawnWide(ctx, o);
        else exitDespawnFar(ctx, o);
    } else {
        // Dropped by a pouch: only vanish far away (0x42517c), then put the pouch's entry back.
        const int16_t dx = int16_t(o.x - ctx.camX), dy = int16_t(o.y - ctx.camY);
        if (int16_t(dx + 0x1ea) < 0 || int16_t(dx - 0x32a) >= 0 || int16_t(dy + 0x1ae) < 0 || int16_t(dy - 0x28a) >= 0) {
            ctx.pl.restoreEntry(o); ctx.release(o);
        }
    }
}

// FUN_004351c5: a new object from a 9-byte template {type, frame, flags, sprite slot, class}
// at the parent's position (FUN_0043546e copies x and y).
Obj* spawnFromTemplate(ObjCtx& ctx, Obj& parent, uint8_t type, uint8_t frame, uint16_t flags, const char* sprite) {
    Obj* n = ctx.spawn();
    if (!n) return nullptr;
    n->x = parent.x; n->y = parent.y;
    n->type = type; n->frame = frame; n->flags = flags;
    n->sprite = ctx.spr(sprite);
    return n;
}

// 0x405322: type 1, scenery: only the despawn test.
void h_static(ObjCtx& ctx, Obj& o) { exitDespawnFar(ctx, o); }

// 0x41a092: type 113, Copan Temple's hidden 1-up.  It turns into an ordinary pickup (sound
// 0xed, a sparkle burst) once Harry has been off the ground for 600 ticks in a row - the long
// fall down the temple shaft - unless this level's hidden 1-up has already been revealed.
void h_hidden1up(ObjCtx& ctx, Obj& o) {
    if (!ctx.g.stats().secret1up) {
        if (ctx.harry.onGround()) { o.w(0x32) = 0; exitDespawnWide(ctx, o); return; }
        o.w(0x32) += 1;
        if (o.w(0x32) >= 0x258) {
            o.w(0x32) = 0;
            o.type = 2; o.b(0x6a) = 0xff; o.frame = 0;   // kept far from the view, like a pouch's drop
            ctx.sound1(0xed, 0x14);                        // FUN_004261e2
            o.y -= 10; ctx.sparkleBurst(o, kSparkleOffsetsA); o.y += 10;   // 0x4336fa
            ctx.g.stats().secret1up = true;
        }
    }
    exitDespawnWide(ctx, o);
}

// 0x4208f8: type 174, the lean lever of the Lost City of Copan.  When Harry's idle animation
// leans him back against it (hyilean, from its right) it tips over in three steps and a
// hidden 1-up appears 0x58 px to the right (template 0x467f34).
void h_leanLever(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff; o.facing = 0xff; o.x -= 0x10;
        if (ctx.g.stats().secret1up) { o.frame = 3; o.type = 1; exitDespawnFar(ctx, o); return; }
    }
    if (o.frame != 0) {
        o.b(0x33) += 1;
        if (o.b(0x33) != 4) { exitDespawnFar(ctx, o); return; }
        o.b(0x33) = 0;
        o.frame += 1;
        if (o.frame != 3) { exitDespawnFar(ctx, o); return; }
        o.type = 1;
        ctx.g.stats().secret1up = true;
        Obj* u = spawnFromTemplate(ctx, o, 2, 0, 0x6000, "mi_1up");
        if (!u) { exitDespawnFar(ctx, o); return; }
        u->x += 0x58; u->y += 0xb;
        u->y -= 10; ctx.sparkleBurst(*u, kSparkleOffsetsA); u->y += 10;
        ctx.sound1(0xed, 0x14);                            // FUN_004261e2
        return;
    }
    if (ctx.harry.spriteIs("hyilean") && int16_t(ctx.harry.x() - o.x) >= 0) {
        const int dy = ctx.harry.y() - o.y;
        if (int16_t(dy - 0x40) < 0 && int16_t(dy + 0x30) >= 0) o.frame = 1;
    }
    exitDespawnFar(ctx, o);
}

// 0x41d876: type 147, the plain coin (always on the floor, no settle step).
void h_coin(ObjCtx& ctx, Obj& o) {
    if (o.w(0x5e) == 0) o.w(0x5e) = int16_t(ctx.rnd() & 0x3f);
    o.w(0x5e) -= 1;
    if (o.w(0x5e) == 0) {
        o.w(0x5e) = int16_t((ctx.rnd() & 0x7f) + 0x20);
        spawnItemSparkle(ctx, o, kItemSparkle[0], true);
    }
    if (ctx.touchesHarry(o)) {
        PlayerStats& st = ctx.g.stats();
        st.score += 100;
        st.treasures = uint8_t(st.treasures + 1);     // (50 make a continue at the end of the tick)
        ctx.sound1(0xfd, 9);
        ctx.pl.markCollected(o);
        o.y -= 3;
        ctx.sparkleBurst(o, ctx.levelNo == 7 ? kSparkleOffsetsC : kSparkleOffsetsB);
        ctx.release(o);
        return;
    }
    exitDespawnFar(ctx, o);
}

// 0x41d520: type 142, the beating heart that stands in the open (full heal).
void h_heart(ObjCtx& ctx, Obj& o) {
    animStep(ctx, o, o.anim(0x42), kAnim_heart, 6);
    o.w(0x5a) -= 1;
    if (o.w(0x5a) < 0) {
        o.w(0x5a) = 6;
        uint32_t r; do { r = ctx.rnd() & 7; } while (r == 7);
        if (Obj* e = ctx.spawn()) {
            e->x = int16_t(o.x + kSparkleOffsetsA[r * 2]); e->y = int16_t(o.y + kSparkleOffsetsA[r * 2 + 1] - 3);
            e->type = 0x10; e->flags = 0x6000; e->b(0x64) = 0xff;
            e->sprite = ctx.spr("fx_sprk2");
        }
    }
    if (ctx.touchesHarry(o)) {
        ctx.sound0(0xfb, 0xe);                        // FUN_0042633d(0xfb, 0x40)
        ctx.g.stats().score += 500;
        ctx.pl.healHarry();
        ctx.pl.markCollected(o);
        ctx.release(o);
        return;
    }
    exitDespawnFar(ctx, o);
}

// 0x41e132: type 156, a pouch hanging in the trees: whip it down.
void h_hangingPouch(ObjCtx& ctx, Obj& o) {
    o.b(0x2b) += 1;
    if (o.b(0x2b) == 0x1e) { o.b(0x2b) = 0; o.b(0x2a) = 0xff; }
    if (o.l(0x32) != 0) {
        // Falling: gravity 0x3800 up to 6 px/tick, then land on either plane.
        if (o.w(0x34) != 6) o.l(0x32) += 0x3800;
        moveY(o, o.l(0x32));
        ph::FloorHit f = ph::probeCell(ctx.det(), o.x, o.y + 0x12);
        if (!f.hit) f = ph::probeCell(ctx.dt2(), o.x, o.y + 0x12);
        if (!f.hit) return;
        if (o.sprite && o.sprite->name() == "mi_pouc2") o.frame = 1;
        o.l(0x32) = 0;
        o.type = 2;                           // becomes an ordinary pickup
        o.w(0x68) = 8;
        o.b(0x6a) = 0xff;
        ctx.sound1(0x5b, 9);
        return;
    }
    if (o.b(0x6c) == 0) {
        bool killed = false;
        if (!weaponHits(ctx, o, 0, &killed)) return;
    }
    o.l(0x32) = 1;                            // start falling
}

// 0x41fd26: type 0xaa, the collected 1-up flying to the lives counter.
void h_oneUpFly(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        const int tx = ctx.camX + 0xeb - o.x, ty = ctx.camY + 0x16 - o.y;
        o.l(0x3c) = (int32_t(tx) << 16) >> 5;        // 32 ticks of travel, 16.16 per tick
        o.l(0x40) = (int32_t(ty) << 16) >> 5;
        o.w(0x48) = int16_t(o.x - ctx.camX); o.w(0x46) = 0;    // screen-relative position (16.16)
        o.w(0x4c) = int16_t(o.y - ctx.camY); o.w(0x4a) = -1;
        o.w(0x50) = 0x20;
        o.w(0x5e) = 2;
    }
    o.w(0x5e) -= 1;
    if (o.w(0x5e) < 0) {
        o.w(0x5e) = 5;
        uint32_t r; do { r = ctx.rnd() & 3; } while (r == 3);
        if (Obj* e = ctx.spawn()) {
            e->x = int16_t(o.x + k1upSparkle[r][0]); e->y = int16_t(o.y + k1upSparkle[r][1]);
            e->type = 0x10; e->flags = 0x6000; e->sprite = ctx.spr("fx_sprk1");
        }
    }
    o.w(0x50) -= 1;
    if (o.w(0x50) < 0) {
        PlayerStats& st = ctx.g.stats();
        st.lives += 1;                                // (capped at 10 at the end of the tick)
        st.score += 5000;
        ctx.pl.markCollected(o);
        ctx.release(o);
        return;
    }
    o.l(0x46) += o.l(0x3c);
    o.l(0x4a) += o.l(0x40);
    o.x = int16_t(o.w(0x48) + ctx.camX);
    o.y = int16_t(o.w(0x4c) + ctx.camY);
}

// ---------------------------------------------------------------- snake (type 109, 0x418fd0)

// 0x419595: is Harry 0x28..0x38 px in front of the snake's head and level with it?
bool snakeBiteRange(ObjCtx& ctx, Obj& o) {
    const int dx = o.x - ctx.harry.x();
    if (o.facing) { if (dx < 0x28 || dx >= 0x38) return false; }
    else          { if (dx >= -0x28 || dx < -0x38) return false; }
    const int dy = ctx.harry.y() - o.y;
    return dy >= -0x20 && dy < 0x14;
}

void h_snake(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        o.b(0x69) = 2;
        o.l(0x4a) = -0x1e000;                 // slither speed, 16.16 (negative = left)
        o.facing = 1;
        o.b(0x32) = 0xff;
    }
    priorityTrigger(ctx, o, o.facing ? -0x20 : 0);
    bool killed = false;
    if (weaponHits(ctx, o, 10, &killed) && killed) { killObject(ctx, o); return; }
    if (ctx.freeze) { if (o.frame != 0xff) freezeSparkle(ctx, o); return; }
    if (!o.sprite || o.sprite->name() != "snimiss") harryContact(ctx, o, 10);
    if (o.b(0x33) == 0) {
        // Still in the hole: emerge when Harry comes within 0xc0 x / 0xa0 y.
        if (o.w(0x34) == 0) {
            const int dx = o.x - ctx.harry.x(), dy = ctx.harry.y() - o.y;
            if (!between(dx, -0xc0, 0xc0) || !between(dy, -0xa0, 0xa0)) { exitDespawnFar(ctx, o); return; }
        }
        if (!animStep(ctx, o, o.anim(0x34), kAnim_snakeEmerge, 3)) { exitDespawnFar(ctx, o); return; }
        o.b(0x33) = 0xff; o.frame = 4; o.b(0x34) = 2;
        exitDespawnFar(ctx, o);
        return;
    }
    for (;;) {                                // 0x419148: the strike state machine
        const uint8_t st = o.b(0x38);
        if (st == 0xfd) {                     // missed: recover
            o.sprite = ctx.spr("snimiss");
            if (!animStep(ctx, o, o.anim(0x50), kAnim_snakeMiss, 3)) { exitDespawnWide(ctx, o); return; }
            o.frame = 0; o.sprite = ctx.spr("snislthr"); o.b(0x38) = 0;
            break;
        }
        if (st == 0xfc) {                     // bit Harry: pull back
            o.sprite = ctx.spr("snuhit");
            if (!animStep(ctx, o, o.anim(0x50), kAnim_snakeHit, 3)) { exitDespawnWide(ctx, o); return; }
            o.frame = 0; o.sprite = ctx.spr("snislthr"); o.b(0x38) = 0;
            break;
        }
        if (st == 0xfe) {                     // lunge
            o.sprite = ctx.spr("snilunge");
            if (!animStep(ctx, o, o.anim(0x50), kAnim_snakeLunge, 3)) { exitDespawnWide(ctx, o); return; }
            if (snakeBiteRange(ctx, o)) { ctx.hurtHarry(10); o.b(0x38) = 0xfc; }
            else o.b(0x38) = 0xfd;
            continue;
        }
        break;
    }
    // 0x41925f: turning round
    if (o.b(0x40)) {
        o.sprite = ctx.spr("sniturn");
        if (!animStep(ctx, o, o.anim(0x42), kAnim_turn7, 3)) { exitDespawnWide(ctx, o); return; }
        o.frame = 0; o.b(0x40) = 0;
        o.l(0x4a) = -o.l(0x4a);
        o.facing ^= 1;
        o.x += 8;
        if (o.facing == 0) o.x -= 0x10;
    }
    // 0x4192ba: slither
    o.sprite = ctx.spr("snislthr");
    o.b(0x34) -= 1;
    if (int8_t(o.b(0x34)) < 0) {
        o.b(0x34) = o.w(0x66) ? 1 : 2;
        o.frame = uint8_t(o.frame + 1);
        if (o.frame == 6) o.frame = 0;
    }
    moveX(o, o.l(0x4a));
    if (o.w(0x66)) { o.w(0x66) -= 1; moveX(o, o.l(0x4a)); }   // hurries while Harry is close
    {
        const int dx = ctx.harry.x() - o.x;
        if (dx < 0x88 && dx >= -0x88) o.w(0x66) = 0x32;
    }
    { int y = o.y + 0xe; if (ctx.floorSnap(o.x, y)) o.y = int16_t(y - 0xe); }
    bool turn = false;
    if (ctx.levelNo == 3 || ctx.levelNo == 8 || ctx.levelNo == 1 || ctx.levelNo == 6) {
        // Look ahead for the edge of the ledge.
        int y = o.y + 0xe;
        if (!ctx.floorSnap(o.x + (o.facing ? -0x28 : 0x28), y)) turn = true;
    }
    if (!turn && (ctx.triggerClass(o.x + (o.facing ? -0x10 : 0xc), o.y - 0x10) & 1)) turn = true;
    if (!turn && ctx.harry.cont() != 0x429fe2) {                 // 0x419470: not while Harry hangs from a bungee vine
        if (o.w(0x6a)) o.w(0x6a) -= 1;
        else {
            const int dx = ctx.harry.x() - o.x;
            if (dx < -0x50) { if (o.w(0x4c) >= 0) turn = true; }          // Harry left, moving right
            else if (dx >= 0x50) { if (o.w(0x4c) < 0) turn = true; }      // Harry right, moving left
        }
    }
    if (turn) {
        // 0x4194dd
        o.b(0x40) = 0xff;
        moveX(o, -o.l(0x4a));
        o.w(0x42) = 0;
        o.sprite = ctx.spr("sniturn"); o.frame = 0;
        o.w(0x6a) = 0;
        o.b(0x69) -= 1;
        if (int8_t(o.b(0x69)) < 0) {
            o.b(0x69) = 2;
            if (ctx.rnd() & 1) o.b(0x69) = 0;
            o.w(0x6a) = 0x28;
        }
    }
    // 0x419546: strike when Harry is in reach
    if (o.w(0x64)) { o.w(0x64) -= 1; exitDespawnWide(ctx, o); return; }
    if (snakeBiteRange(ctx, o)) {
        o.frame = 0; o.w(0x50) = 0;
        o.w(0x3a) = o.y; o.w(0x3c) = 0;
        o.b(0x38) = 0xfe;
        o.w(0x64) = 0x28;
    }
    exitDespawnWide(ctx, o);
}

// ---------------------------------------------------------------- monkey (type 35, 0x40e394)

// 0x40b323: random monkey chatter.
void monkeyChatter(ObjCtx& ctx, Obj& o) {
    if (o.w(0x5a) == 0) o.w(0x5a) = int16_t((ctx.rnd() & 0x7f) + 0xa0);
    o.w(0x5a) -= 1;
    if (o.w(0x5a) == 0) ctx.sound1((ctx.rnd() & 0x80) ? 0xbd : 0x43, 4);
}

void monkeyThrowCactus(ObjCtx& ctx, Obj& o) {
    // 0x40e568: the cactus (type 0x1d) leaves the hand on frame 5 of mkztoss.
    Obj* c = ctx.spawn();
    if (!c) return;
    c->w(0x6c) = 0x14;
    c->x = int16_t(o.x + 0x10); c->y = int16_t(o.y - 8);
    c->sprite = ctx.spr("obrcactu");
    c->behind = 0xff; c->frame = 0;
    c->b(0x5a) = 0xc; c->type = 0x1d; c->b(0x46) = 0xff;
    c->l(0x32) = 0x24000; c->l(0x36) = 0x1c000; c->l(0x3a) = -0x100; c->l(0x3e) = 0x4100;
    if (o.b(0x5e) == 0) { c->l(0x32) = 0x68000; c->l(0x36) = -0x30000; c->l(0x3e) = 0x3800; }
    c->flags = uint16_t((o.flags & 0x8000) + 0x6000);
    if (o.facing == 0) { c->l(0x32) = -c->l(0x32); c->l(0x3a) = -c->l(0x3a); c->x -= 0x20; }
}

void h_monkey(ObjCtx& ctx, Obj& o) {
    monkeyChatter(ctx, o);
    bool killed = false;
    if (weaponHits(ctx, o, 10, &killed)) { o.w(0x64) = 0; if (killed) { killObject(ctx, o); return; } }
    if (o.w(0x64)) {                          // pausing (after a throw, or stunned by a hit)
        o.w(0x64) -= 1;
        o.frame = 0; o.sprite = ctx.spr("mkzrun");
        exitDespawnFar(ctx, o);
        return;
    }
    priorityTrigger(ctx, o, o.facing ? -0x20 : 0);
    if (o.frame == 0xff) {                    // first tick (template sub = 0xff)
        o.frame = 0;
        o.w(0x32) = -1;
        o.y += 0x10;
        o.b(0x46) = uint8_t((ctx.rnd() & 0xf) + 7);
    }
    { int y = o.y + 0x19; if (ctx.floorSnap(o.x, y)) o.y = int16_t(y - 0x19); }
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    if (o.w(0x4a)) {
        // Hop (turn-around bounce, hurts when it was aimed at Harry).
        o.sprite = ctx.spr("mkzbounc");
        if (!animStep(ctx, o, o.anim(0x4a), kAnim_turn7, 3)) {
            if (o.w(0x4a) == 2 && o.b(0x68)) ctx.hurtHarry(10);
            exitDespawnWide(ctx, o);
            return;
        }
        o.facing ^= 1; o.w(0x36) = 2;
    }
    harryContact(ctx, o, 10);
    if (o.script) {
        // Throwing (mkztoss through the pointer-based interpreter, 4 ticks).
        const bool done = anim2Step(ctx, o, 0x3e, 4);
        if (o.b(0x42) == 0 && o.l(0x3e) == 0x10) monkeyThrowCactus(ctx, o);
        if (!done) { exitDespawnWide(ctx, o); return; }
        o.script = nullptr; o.l(0x3e) = 0;
        o.w(0x46) = 0x40;
        o.w(0x64) = int16_t((ctx.rnd() & 0x3f) + 8);
        o.w(0x36) = 2; o.frame = 0; o.sprite = ctx.spr("mkzrun");
        return;
    }
    bool edgeCheck = true;
    if (o.w(0x3c)) {
        // Turning round.
        o.sprite = ctx.spr("mkzturn");
        if (!animStep(ctx, o, o.anim(0x3c), kAnim_turn4, 3)) { exitDespawnWide(ctx, o); return; }
        o.facing ^= 1; o.w(0x36) = 2;
    } else {
        // Running; faster when Harry is just ahead.
        int32_t speed = 0x12000;
        const int dy = ctx.harry.y() - o.y, dx = ctx.harry.x() - o.x;
        if (between(dy, -0x18, 0x38)) {
            if (o.facing ? between(dx, -0xa0, 0) : between(dx, 0, 0x60)) speed = 0x21000;
        }
        if (o.w(0x32) < 0) {
            moveX(o, -speed);
            if (o.x < 0) { o.w(0x32) = -o.w(0x32); o.facing ^= 1; }
        } else {
            moveX(o, speed);
        }
        // 0x40e7e6: Harry standing near the monkey: it hops at him.
        if (ctx.harry.cont() == 0x428e50 && between(dy, -0x18, 0x38)) {
            const int d = ctx.harry.x() - o.x;
            bool hop = false;
            if (d < 0) { if (o.facing == 0 && between(d, -0x2c, -0x28)) hop = true; }
            else if (o.facing && between(d, 0x28, 0x2c)) hop = true;
            if (hop) { o.w(0x32) = -o.w(0x32); o.w(0x4a) = 1; o.b(0x68) = 0xff; }
        }
        animStep(ctx, o, o.anim(0x36), kAnim_run10, 2);
        o.sprite = ctx.spr("mkzrun");
        // A wall marker ahead turns it round with a hop.
        if (ctx.triggerClass(o.x + (o.facing ? 5 : -0x23), o.y - 0x10) & 1) {
            o.w(0x32) = -o.w(0x32); o.w(0x4a) = 1; o.b(0x68) = 0;
        }
    }
    if (edgeCheck) {
        // 0x40e954: between strides, stop at the edge of the platform.
        if (o.frame >= 2 && o.frame < 6) { exitDespawnWide(ctx, o); return; }
        int y = o.y + 0x19;
        if (!ctx.floorSnap(o.x + (o.facing ? 8 : -0x20), y)) { o.w(0x32) = -o.w(0x32); o.w(0x3c) = 1; o.b(0x68) = 0; }
    }
    if (ctx.harry.cont() == 0x429fe2) { exitDespawnWide(ctx, o); return; }   // 0x40e9ce: Harry on a bungee vine
    {
        const int dy = ctx.harry.y() - o.y, dx = ctx.harry.x() - o.x;
        if (between(dy, -0x18, 0x38) && between(dx, -0xa0, 0x60)) { exitDespawnWide(ctx, o); return; }
    }
    o.b(0x46) -= 1;
    if (int8_t(o.b(0x46)) >= 0) { exitDespawnWide(ctx, o); return; }
    o.b(0x46) = uint8_t((ctx.rnd() & 7) + 0x16);
    {
        const int dx = ctx.harry.x() - o.x, dy = ctx.harry.y() - o.y;
        if (!between(dx, -0xc0, 0xc0) || !between(dy, -0x18, 0x70)) { exitDespawnWide(ctx, o); return; }
        if (ctx.harry.x() < o.x ? o.facing != 0 : o.facing == 0) { exitDespawnWide(ctx, o); return; }   // must face Harry (facing 0 = left)
        if (!between(dy, -0x40, 0xa0)) { exitDespawnWide(ctx, o); return; }
        // Wind up a throw.
        o.w(0x64) = int16_t((ctx.rnd() & 0x3f) + 8);
        o.script = kAnim2_monkeyToss; o.l(0x3e) = 0; o.b(0x42) = 0;
        o.b(0x5e) = 0;
        if (dy >= 0x48) { o.b(0x5e) = 0xff; ctx.sound1((ctx.rnd() & 0x80) ? 0x12 : 0x2b, 4); }
    }
    exitDespawnWide(ctx, o);
}


// ---------------------------------------------------------------- stinging fly (type 102, 0x418992)

void h_fly(ObjCtx& ctx, Obj& o) {
    o.behind = 0xff;
    if (o.b(0x32) == 0) {
        o.w(0x6c) = 4; o.b(0x32) = 0xff;
        o.b(0x36) = 1; o.b(0x37) = 1;
        o.w(0x50) = 0x190;                    // ticks of chasing before it gives up
        o.w(0x64) = o.x;                      // home x
    }
    bool killed = false;
    if (weaponHits(ctx, o, 9, &killed) && killed) { killObject(ctx, o); return; }
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    harryContact(ctx, o, 0xc);
    if (o.b(0x46) == 0) {
        // Resting until Harry comes close.
        const int dx = ctx.harry.x() - o.x, dy = ctx.harry.y() - 0x20 - o.y;
        if (between(dx, -0x80, 0x80) && between(dy, -0x50, 0x50)) {
            o.frame = 1; o.b(0x33) = 0xa; o.b(0x46) = 0xff; o.w(0x34) = 0;
        } else {
            o.b(0x60) -= 1;
            if (int8_t(o.b(0x60)) < 0) {
                o.b(0x60) = 4; o.frame = 0;
                o.w(0x34) -= 1;
                if (o.w(0x34) < 0) { o.w(0x34) = int16_t((ctx.rnd() & 0xf) + 1); o.frame = 1; }
            }
            exitDespawnFar(ctx, o);
            return;
        }
    }
    animStep(ctx, o, o.anim(0x34), kAnim_flyWings, 3);
    const bool steer = ctx.harry.cont() != 0x429fe2 && !(ctx.levelNo == 6 && ctx.harry.y() >= 0x1ca);   // 0x418b5b / 0x418c47
    o.b(0x36) -= 1;
    if (o.b(0x36) == 0) {
        o.b(0x36) = uint8_t((ctx.rnd() & 3) + 8);
        int32_t v = int32_t(ctx.rnd() & 0x7fff) + 0xc000;
        if (v & 1) v = -v;
        if (steer) {
            if (o.x - ctx.harry.x() >= 0) { o.facing = 0xff; v -= 0x12000; }
            else { v += 0x12000; o.facing = 0; }
        }
        o.l(0x38) = v;
    }
    moveX(o, o.l(0x38));
    o.b(0x37) -= 1;
    if (o.b(0x37) == 0) {
        o.b(0x37) = uint8_t((ctx.rnd() & 3) + 5);
        int32_t v = int32_t(ctx.rnd() & 0x7fff) + 0x8000;
        if (v & 1) v = -v;
        if (steer) {
            if (o.w(0x62)) {
                // Diving at Harry.
                o.w(0x62) -= 1;
                if (o.w(0x62) != 0) {
                    v += 0x30000;
                    if (ctx.harry.y() - o.y < 0x30) { o.w(0x62) = 0; o.w(0x50) = 0; }
                } else {
                    o.w(0x50) = 0;
                }
            } else {
                const int dx = ctx.harry.x() - o.x;
                if (dx < 0x10 && dx >= -0x10) {
                    o.w(0x6c) -= 1;
                    if (o.w(0x6c) == 0) { o.w(0x62) = 0xf; o.w(0x6c) = 0x14; }
                }
                if (o.y + 0x54 - ctx.harry.y() >= 0) v -= 0x10000; else v += 0x10000;   // hover 0x54 above Harry
            }
        }
        o.l(0x3c) = v;
    }
    moveY(o, o.l(0x3c));
    bool leaving = true;
    if (o.w(0x50)) {
        const int d = o.x - o.w(0x64);
        if (d >= 0x140 || d < -0x140) o.w(0x50) = 0;
        else { o.w(0x50) -= 1; leaving = false; }
    }
    if (leaving) {
        o.l(0x52) -= 0x4000;                  // climbs away ever faster
        o.w(0x6c) = 0; o.w(0x62) = 0;
        moveY(o, o.l(0x52));
    }
    if (ctx.levelNo == 6 && o.y >= 0x1a0) o.y = 0x1a0;
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- condor (type 10, 0x406f62), hawk (type 85, 0x4165f2)

// The handlers keep their state as a code pointer at +0x38; these are the targets.  The hawk
// has its own glide and dive and shares the condor's stun tumble, wall turn and climb away.
enum CondorState : int32_t { kCdFlyIn = 0x4070c7, kCdTurn = 0x4071c0, kCdDive = 0x407272, kCdStun = 0x407555,
                             kCdTurnWall = 0x40769b, kCdFlyAway = 0x40775b,
                             kHkGlide = 0x416709, kHkDive = 0x4167f8 };

// 0x40762b: a wall marker 0x70 px ahead turns the bird round.
void condorWallCheck(ObjCtx& ctx, Obj& o) {
    if (ctx.triggerClass(o.x + (o.facing ? 0x70 : -0x70), o.y + 8) & 1) {
        o.sprite = ctx.spr("hkiturn"); o.frame = 0;
        o.l(0x38) = kCdTurnWall; o.w(0x3c) = 0;
    }
}

// Horizontal speed eases towards +-4 px/tick in the direction faced (several states).
void condorEaseX(Obj& o, bool towardsFacing, int32_t step) {
    const bool left = towardsFacing ? o.facing != 0 : o.facing == 0;
    if (left) { if (o.w(0x36) != -4) o.l(0x34) -= step; }
    else      { if (o.w(0x36) != 4) o.l(0x34) += step; }
}

void condorFlyAway(ObjCtx& ctx, Obj& o) {
    o.l(0x38) = kCdFlyAway; o.w(0x3c) = 0;
    o.sprite = ctx.spr("hkifly"); o.frame = 0;   // (the hawk and the end of the stun keep the frame: the climb's first step sets it anyway)
}

// 0x4073d3: hit but not killed.  The state is saved at +0x64 (and the sprite at +0x68, never
// read back), four feathers drift down and the tumble starts on this tick.
void condorStun(ObjCtx& ctx, Obj& o) {
    o.l(0x64) = o.l(0x38);
    o.l(0x38) = kCdStun;
    o.l(0x5e) = -0x20000;
    o.l(0x34) = ctx.harry.facingLeft() ? -0x30000 : 0x30000;
    o.w(0x62) = 0;
    for (int d7 = 3; d7 >= 0; --d7) {
        Obj* f = ctx.spawn();                 // FUN_0043546e
        if (!f) break;
        f->w(0x4e) = 0x46;
        f->x = o.x; f->y = o.y;
        int dx = int(ctx.rnd() & 0x1f); if (d7 & 1) dx = -dx;
        f->x += dx;
        f->w(0x3a) = int16_t(ctx.rnd() & 7);
        f->y += int(ctx.rnd() & 0x1f) - 0x10;
        f->w(0x58) = 0x40;
        o.w(0x58) += int16_t(ctx.rnd() & 0x1f);   // sic (ROM too): the random part of the lifetime goes to the bird
        f->type = 0x40; f->flags = 0x6000;
        f->l(0x36) = int32_t(ctx.rnd() & 0x7fff) + 0x6000;
        f->sprite = ctx.spr("fxifeath");
        f->script = kAnim_feather;
    }
}

// The state code after the per-tick x move.  A transition the original makes with a jump
// runs the new state on the same tick (continue).
void condorStates(ObjCtx& ctx, Obj& o) {
    for (int guard = 0; guard < 4; ++guard) {
        switch (o.l(0x38)) {
        case kCdFlyIn: {
            animStep(ctx, o, o.anim(0x3c), kAnim_run10, 4);
            condorWallCheck(ctx, o);
            if (o.y - o.w(0x3e) < 0) o.l(0x40) += 0x2000; else o.l(0x40) -= 0x2000;
            moveY(o, o.l(0x40));
            const int d = ctx.harry.x() + (o.facing ? 0x50 : -0x50) - o.x;
            if (between(d, -0x20, 0x20)) {
                o.sprite = ctx.spr("hkiturn"); o.frame = 0;
                o.l(0x38) = kCdTurn; o.w(0x3c) = 0;
                continue;
            }
            harryContact(ctx, o, 0xf);
            exitDespawnWide(ctx, o);
            return;
        }
        case kCdTurn:
            if (animStep(ctx, o, o.anim(0x3c), kAnim_condorTurn, 5)) {
                o.l(0x38) = kCdDive;
                o.w(0x6a) = o.y;
                o.script = kAnim2_condorDive; o.l(0x4c) = 0;
                o.w(0x3c) = 0;
                o.sprite = ctx.spr("hkidive"); o.frame = 0;
                o.facing ^= 1;
                continue;
            }
            condorEaseX(o, true, 0x2500);
            harryContact(ctx, o, 0xf);
            exitDespawnWide(ctx, o);
            return;
        case kCdDive:
        case kHkDive: {
            const bool hawk = o.l(0x38) == kHkDive;
            moveY(o, o.l(0x46));
            condorWallCheck(ctx, o);
            if (o.b(0x52) == 0 && harryContact(ctx, o, 0xf)) {
                o.b(0x52) = 0xff; o.b(0x4a) = 0xff;
                condorFlyAway(ctx, o);
                continue;
            }
            anim2Step(ctx, o, 0x4c, 4);
            if (o.b(0x4a)) {
                if (o.w(0x48) != -2) o.l(0x46) -= 0x4800;
                exitDespawnFar(ctx, o);
                return;
            }
            if (o.w(0x48) != 4) o.l(0x46) += 0x2200;
            // Pulls out after 0x68 px, or once level with Harry (0x40 / 0x30 above his feet).
            if (o.y - o.w(0x6a) >= 0x68 || ctx.harry.y() - (hawk ? 0x30 : 0x40) - o.y < 0) {
                o.b(0x4a) = 0xff;
                condorFlyAway(ctx, o);
                continue;
            }
            if (!hawk) condorEaseX(o, false, 0x2500);
            exitDespawnWide(ctx, o);
            return;
        }
        case kCdStun:
            o.sprite = ctx.spr("hkistun");
            moveY(o, o.l(0x5e));
            if (o.w(0x60) < 1) o.l(0x5e) += 0x2000;
            if (o.w(0x36) == 0) o.l(0x34) = 0;
            else if (o.w(0x36) < 0) o.l(0x34) += 0x2000;
            else o.l(0x34) -= 0x2000;
            moveX(o, o.l(0x34));
            if (animStep(ctx, o, o.anim(0x62), kAnim_condorStun, 2)) { condorFlyAway(ctx, o); continue; }
            exitDespawnFar(ctx, o);
            return;
        case kCdTurnWall:
            if (animStep(ctx, o, o.anim(0x3c), kAnim_condorTurn, 3)) {
                condorFlyAway(ctx, o);
                o.facing ^= 1;
                continue;
            }
            if (o.w(0x42) >= -2) o.l(0x40) -= 0x5000;
            moveY(o, o.l(0x40));
            condorEaseX(o, true, 0x4000);
            harryContact(ctx, o, 0xf);
            exitDespawnWide(ctx, o);
            return;
        case kHkGlide: {
            // 0x416709: glides level (bobbing about 0xc px below where it appeared) until it is
            // 0x70 px short of Harry, then dives.  No contact damage while gliding.
            animStep(ctx, o, o.anim(0x3c), kAnim_hawkGlide, 4);
            condorWallCheck(ctx, o);
            if (o.y - o.w(0x3e) < 0) o.l(0x40) += 0x1000; else o.l(0x40) -= 0x1000;
            moveY(o, o.l(0x40));
            const int d = ctx.harry.x() + (o.facing ? -0x70 : 0x70) - o.x;
            if (between(d, -0x10, 0x10)) {
                o.w(0x6a) = o.y;
                o.sprite = ctx.spr("hkidive");
                o.script = kAnim2_hawkDive; o.l(0x4c) = 0;
                o.l(0x38) = kHkDive;
                continue;
            }
            exitDespawnFar(ctx, o);
            return;
        }
        case kCdFlyAway:
        default:
            animStep(ctx, o, o.anim(0x3c), kAnim_run10, 4);
            condorWallCheck(ctx, o);
            if (o.w(0x42) >= -2) o.l(0x40) -= 0x5000;
            moveY(o, o.l(0x40));
            if (o.facing) { if (o.l(0x5a) < 0x30000) o.l(0x5a) += 0x8000; }
            else          { if (o.l(0x5a) >= -0x30000) o.l(0x5a) -= 0x8000; }
            moveX(o, o.l(0x5a));
            harryContact(ctx, o, 0xf);
            exitDespawnWide(ctx, o);
            return;
        }
    }
}

// The common tick (0x40704b / 0x41668d): the whip kills or stuns, the stop-watch freezes,
// else the bird moves on by +0x34 and runs its state.
void condorTick(ObjCtx& ctx, Obj& o) {
    bool killed = false;
    if (weaponHits(ctx, o, 0x14, &killed)) {
        if (ctx.freeze || killed) { killObject(ctx, o); return; }
        condorStun(ctx, o);
        condorStates(ctx, o);
        return;
    }
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    moveX(o, o.l(0x34));
    condorWallCheck(ctx, o);
    condorStates(ctx, o);
}

void h_condor(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        // Enters from the side Harry is not looking at, above him.
        if (o.y - ctx.harry.y() >= 0) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        if (ctx.harry.facingLeft()) {
            if (ctx.camX + 0x15e - o.x >= 0) { exitDespawnWide(ctx, o); return; }
            o.l(0x34) = -0x40000;
        } else {
            if (o.x - ctx.camX + 0x32 >= 0) { exitDespawnWide(ctx, o); return; }
            o.l(0x34) = 0x40000; o.facing = 1;
        }
        o.frame = 0;
        o.w(0x3e) = int16_t(o.y + 0x10);        // cruising altitude
        o.l(0x38) = kCdFlyIn;
        o.b(0x32) = 0xff;
    }
    condorTick(ctx, o);
}

// 0x4165f2: type 85, the hawk.  Glides in towards Harry from where it was placed (only if
// that is above him), dives at him from 0x70 px out and climbs away.
void h_hawk(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        if (o.y - ctx.harry.y() >= 0) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        o.b(0x32) = 0xff;
        o.frame = 0;
        o.w(0x3e) = int16_t(o.y + 0xc);
        o.l(0x38) = kHkGlide;
        if (ctx.harry.x() - o.x < 0) o.l(0x34) = -0x30000;
        else { o.l(0x34) = 0x30000; o.facing = 1; }
    }
    condorTick(ctx, o);
}

// 0x412d6a: type 0x40, a drifting feather / leaf that Harry's run can push.
void h_feather(ObjCtx& ctx, Obj& o) {
    if (o.w(0x58)) { o.w(0x58) -= 1; if (o.w(0x58) == 0) { ctx.release(o); return; } }
    if (o.w(0x4e)) o.w(0x4e) -= 1;
    else {
        o.b(0x6d) -= 1;
        if (int8_t(o.b(0x6d)) < 0) {
            o.b(0x6d) = 3;
            int y = o.y;
            if (ctx.floorSnap(o.x, y)) { ctx.release(o); return; }
        }
    }
    if (ctx.touchesHarry(o)) o.l(0x50) += ctx.harry.vx() >> 5;
    o.l(0x50) = std::clamp(int32_t(o.l(0x50)), int32_t(-0x15000), int32_t(0x15000));
    moveX(o, o.l(0x50));
    if (o.l(0x50) < 0) o.l(0x50) += 0x400; else o.l(0x50) -= 0x400;
    moveY(o, o.l(0x50) >= 0 ? -o.l(0x50) : o.l(0x50));
    moveY(o, o.l(0x36));
    if (o.script) animStep(ctx, o, o.anim(0x3a), o.script, 7);
    if (o.w(0x64)) { o.w(0x64) -= 1; return; }
    if (o.w(0x46)) { o.w(0x46) -= 1; if (o.w(0x46) == 0) { ctx.release(o); return; } }
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- rat (type 23, 0x40b5d2)

void h_rat(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xff) {
        o.frame = 0;
        o.l(0x32) = -0x20000; o.facing = 1;
        { int y = o.y + 0x19; if (ctx.floorSnap(o.x - 0xc, y)) o.y = int16_t(y - 0x19); }
        if (ctx.harry.x() - o.x >= 0) { o.facing = 0; o.l(0x32) = 0x20000; }
    }
    bool killed = false;
    if (weaponHits(ctx, o, 9, &killed) && killed) { killObject(ctx, o); return; }
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    harryContact(ctx, o, 5);                  // 0x40b84f (the trip states are Harry's)
    if (o.b(0x38)) {
        o.sprite = ctx.spr("rtzturn");
        if (!animStep(ctx, o, o.anim(0x36), kAnim_ratTurn, 3)) { exitDespawnWide(ctx, o); return; }
        o.facing ^= 1; o.w(0x36) = 0; o.b(0x38) = 0;
    }
    moveX(o, o.l(0x32));
    int y = o.y + 0x19;
    const bool floor = ctx.floorSnap(o.x - 0xc, y);
    if (floor) o.y = int16_t(y - 0x19);
    if (!floor) {
        o.sprite = ctx.spr("rtzfall");
        if (animStep(ctx, o, o.anim(0x3a), kAnim_ratFall, 2)) o.w(0x36) = 3;
        if (o.w(0x3e) != 8) o.l(0x3c) += 0x4000;
        moveY(o, o.l(0x3c));
        exitDespawnFar(ctx, o);
        return;
    }
    o.sprite = ctx.spr("rtzrun");
    o.w(0x3a) = 0; o.l(0x3c) = 0;
    if (ctx.triggerClass(o.x + (o.l(0x32) < 0 ? -0x2c : 0x16), o.y + 4) & 1) {
        o.w(0x36) = 0; o.b(0x38) = 0xff; o.l(0x32) = -o.l(0x32);
    }
    animStep(ctx, o, o.anim(0x36), kAnim_ratRun, 2);
    exitDespawnWide(ctx, o);
}

// ---------------------------------------------------------------- bat (type 18, 0x40a4be)

void h_bat(ObjCtx& ctx, Obj& o) {
    if (o.w(0x64) == 0) o.w(0x64) = o.x;      // roost x
    if (o.w(0x42) < 0x15e) {
        const int d = o.x - o.w(0x64);
        if (d >= 0x140 || d < -0x140) o.w(0x42) = 0x168;   // strayed too far: go and roost
    }
    bool killed = false;
    if (weaponHits(ctx, o, 9, &killed) && killed) { killObject(ctx, o); return; }
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    if (o.w(0x5a)) { o.w(0x5a) -= 1; return; }          // settling on the roost
    o.behind = 0xff;
    if (o.b(0x32) == 0) {
        // Hanging: drop when Harry passes 20..200 px below.
        const int dx = o.x - ctx.harry.x();
        if (dx < -0x96) { o.facing = 0xff; exitDespawnFar(ctx, o); return; }
        if (dx >= 0x96) { o.facing = 0; exitDespawnFar(ctx, o); return; }
        const int dy = o.y - ctx.harry.y();
        if (dy < -0xc8 || dy >= -0x14) { exitDespawnFar(ctx, o); return; }
        o.b(0x32) = 1;
    }
    if (o.b(0x32) == 1) {
        o.sprite = ctx.spr("btfhang");
        const bool done = animStep(ctx, o, o.anim(0x34), kAnim_batDrop, 3);
        moveY(o, 0x3800);
        moveX(o, o.facing == 0 ? -0x5000 : 0x5000);
        if (!done) { exitDespawnFar(ctx, o); return; }
        o.l(0x3a) = -0x10000; o.l(0x36) = 0; o.b(0x32) = 2; o.b(0x3e) = 0x19; o.l(0x34) = 0;
    }
    bool held = false;
    [&] {
        if (o.w(0x4a) == 0) {
            if (o.w(0x42) >= 0x15e) return;
            harryContact(ctx, o, 0xc);
            // 0x40a6b2: latch onto the head of Harry standing below it.
            if (ctx.pl.harryOnPlatform() || ctx.harry.cont() != 0x428e50) return;
            const int dx = int16_t(ctx.harry.x() - o.x), dy = int16_t(ctx.harry.y() - 0x20 - o.y);
            if (!between(dx, -0x14, 0x14) || !between(dy, -0x16, 0x16)) return;
            o.w(0x4a) = 0x64; o.w(0x4c) = int16_t(0xff00);
            o.x = int16_t(ctx.harry.x()); o.y = int16_t(ctx.harry.y() - 0x30);
            ctx.pl.grabHarry(o);                           // FUN_004285d7
        }
        // 0x40a791
        if (ctx.harry.cont() == 0x428625) {
            o.w(0x4a) -= 1;
            if (o.w(0x4a) != 0) {
                o.sprite = ctx.spr("btzface");
                animStep(ctx, o, o.anim(0x46), kAnim_batBite, 4);
                held = true;
                return;
            }
            ctx.hurtHarry(0xc);
        }
        o.w(0x4a) = 0; o.w(0x42) = 0x168;
        ctx.pl.harryCall(0x428dfa);
    }();
    if (held) { exitDespawnFar(ctx, o); return; }
    // 0x40a827: flying
    o.sprite = ctx.spr("btzfly");
    animStep(ctx, o, o.anim(0x34), kAnim_batFly, 4);
    const int jitter = int(ctx.rnd() & 0x3f) - 0x20;
    const bool swinging = ctx.harry.cont() == 0x42aa3b;   // 0x40a8a7: faster beside the mine cart
    if (o.x - ctx.harry.x() + jitter - 0x14 < 0) {
        if (o.w(0x38) >= (swinging ? -4 : -2)) o.l(0x36) -= 0x3000; else o.l(0x36) += 0x3000;
    } else {
        if (o.w(0x38) >= (swinging ? 4 : 2)) o.l(0x36) -= 0x3000; else o.l(0x36) += 0x3000;
    }
    o.w(0x42) += 1;
    if (o.w(0x42) >= 0x15e) {
        if (o.w(0x3c) != 4) o.l(0x3a) += 0x4000;           // climbs back to the ceiling
    } else if (o.y - ctx.harry.y() + jitter + 0x20 < 0) {
        if (o.w(0x3c) != -2) o.l(0x3a) -= 0x3000;
    } else {
        if (o.w(0x3c) != 1) o.l(0x3a) += 0x3000;
    }
    o.facing = o.l(0x36) < 0 ? 0xff : 0;
    moveX(o, -o.l(0x36));
    if (ctx.triggerClass(o.x + (o.l(0x36) < 0 ? 0x10 : -0x20), o.y) & 1) {
        if (o.l(0x36) < 0) { o.l(0x36) = 0x30000; o.x -= 3; }
        else { o.l(0x36) = -0x30000; o.x += 3; }
    }
    moveY(o, -o.l(0x3a));
    if (o.l(0x3a) < 0) {
        if (ctx.triggerClass(o.x - 8, o.y + 0x1e) & 1) { o.l(0x3a) = 0x30000; o.y -= 3; }
    } else if (ctx.triggerClass(o.x - 8, o.y + 2) & 1) {
        if (o.w(0x42) >= 0x15e) {
            // Back on the ceiling: roost again.
            o.w(0x64) = 0; o.b(0x32) = 0;
            o.sprite = ctx.spr("btfhang"); o.w(0x34) = 0;
            o.w(0x5a) = int16_t(0x5a + (ctx.rnd() & 0x1f));
            o.y += 9; o.frame = 0;
            o.l(0x42) = 0; o.l(0x36) = 0; o.l(0x3a) = 0;
            return;
        }
        o.l(0x3a) = -0x30000; o.y += 3;
    }
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- spider (type 110, 0x419622)

void h_spider(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        o.w(0x34) = o.x; o.w(0x44) = o.y;      // home
        o.l(0x36) = 0x16000;
        o.w(0x54) = 0xa;
        o.b(0x3e) = 2;                         // floor walker ...
        if (o.frame != 0xff) { o.b(0x3e) = 0; o.vflip = 1; }   // ... or a ceiling walker (template sub 0)
        else {
            o.frame = 0;
            int y = o.y + 0x11; if (ctx.floorSnap(o.x, y)) o.y = int16_t(y - 0x11);
        }
    }
    bool killed = false;
    if (weaponHits(ctx, o, 9, &killed) && killed) { killObject(ctx, o); return; }
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    harryContact(ctx, o, 0xc);
    if (o.w(0x66)) { o.w(0x66) -= 1; exitDespawnFar(ctx, o); return; }
    if (o.b(0x46)) o.b(0x46) -= 1;
    auto faceByVelocity = [&]() { o.facing = o.l(0x36) >= 0 ? 1 : 0; };   // 0x419cea
    auto walkFrame = [&]() {                                              // 0x419c7c
        o.sprite = ctx.spr("sp_walk");
        animStep(ctx, o, o.anim(0x3c), kAnim_turn4, 5);
        exitDespawnFar(ctx, o);
    };
    if (o.b(0x3e) == 0xa) {
        // Hopping.
        o.sprite = ctx.spr("sp_jump");
        if (o.frame != 4) animStep(ctx, o, o.anim(0x3c), kAnim_sparkle2, 6);
        moveX(o, o.l(0x36));
        faceByVelocity();
        o.l(0x56) += 0x7000;
        moveY(o, o.l(0x56));
        if (o.l(0x56) < 0) { walkFrame(); return; }
        ph::FloorHit f = ph::probeCell(ctx.det(), o.x, o.y + 0xb);
        if (!f.hit) { walkFrame(); return; }
        o.b(0x3e) = 2;
    }
    if (o.b(0x3e) == 0) {
        // Pacing on the ceiling, 24 px either side of home; drops when Harry is near.
        if (o.b(0x46) == 0) {
            moveX(o, o.l(0x36));
            faceByVelocity();
            bool check = false;
            if (o.l(0x36) >= 0) {
                if (o.w(0x34) + 0x18 - o.x >= 0) { walkFrame(); return; }
                o.l(0x36) = -o.l(0x36); o.facing ^= 1; check = true;
            } else {
                if (o.w(0x34) - 0x18 - o.x >= 0) { o.l(0x36) = -o.l(0x36); o.facing ^= 1; }
                check = true;
            }
            if (check) {
                const int dx = ctx.harry.x() - o.x;
                if (!between(dx, -0x68, 0x68)) { walkFrame(); return; }
                o.b(0x3e) = 1; o.w(0x3c) = 0; o.l(0x40) = 0; o.b(0x46) = 0x17;
            }
        }
    }
    if (o.b(0x3e) == 1) {
        // Dropping on its thread.
        o.l(0x40) += 0x2000;
        moveY(o, o.l(0x40));
        if (o.b(0x46) == 0) {
            int y = o.y + 0xb;
            if (ctx.floorSnap(o.x, y)) { o.y = int16_t(y - 0xb); o.vflip = 0; o.b(0x3e) = 2; o.w(0x3c) = 0; }
        } else {
            o.b(0x46) -= 1;
        }
    } else if (o.b(0x3e) == 3) {
        // Climbing back up.
        o.l(0x40) -= 0x2000;
        moveY(o, o.l(0x40));
        if (o.w(0x44) - o.y >= 0) { o.y = o.w(0x44); o.b(0x3e) = 0; }
    } else if (o.b(0x3e) == 2) {
        // Walking on the floor; hops at Harry now and then.
        o.w(0x54) -= 1;
        if (o.w(0x54) < 0) {
            o.w(0x54) = int16_t((ctx.rnd() & 0xf) + 0x1c);
            const int dx = ctx.harry.x() - o.x;
            if (between(dx, -0x30, 0x30)) {
                o.w(0x52) = o.y;
                o.l(0x56) = int32_t(ctx.rnd() & 0x1ffff) - 0x50000;
                o.b(0x3e) = 0xa; o.w(0x3c) = 0;
            }
        }
        moveX(o, o.l(0x36));
        faceByVelocity();
        o.w(0x64) -= 1;
        if (o.w(0x64) < 0) {
            o.w(0x64) = int16_t((ctx.rnd() & 0x1f) + 0x10);
            const int d = o.x - ctx.harry.x();
            if (d >= 0x10) { if (o.facing) { o.facing ^= 1; o.l(0x36) = -o.l(0x36); } }
            else if (d < -0x10) { if (o.facing == 0) { o.facing ^= 1; o.l(0x36) = -o.l(0x36); } }
        }
    }
    // 0x419afb
    if (o.b(0x46) == 0 && o.b(0x3e) != 0) {
        int y = o.y + 0x11; if (ctx.floorSnap(o.x, y)) o.y = int16_t(y - 0x11);
    }
    if (o.b(0x3e) & 1) {
        o.sprite = ctx.spr("sp_jump");
        if (o.frame != 2) animStep(ctx, o, o.anim(0x3c), kAnim_spiderThread, 6);
        exitDespawnFar(ctx, o);
        return;
    }
    if (ctx.triggerClass(o.x + (o.l(0x36) < 0 ? -0x28 : 0x10), o.y - 8) & 1) {
        o.l(0x36) = -o.l(0x36); o.facing = uint8_t((o.facing & 1) ^ 1);
    }
    if (o.l(0x36) < 0) {
        int y = o.y + 5;
        if (!ctx.floorSnap(o.x - 0x30, y)) { o.l(0x36) = 0x16000; o.facing = 0; o.w(0x64) = int16_t((ctx.rnd() & 0x1f) + 0x20); }
    } else {
        int y = o.y + 5;                      // (the original probes x itself here: d1 is clobbered)
        if (!ctx.floorSnap(o.x, y)) { o.l(0x36) = -0x16000; o.facing = 1; o.w(0x64) = int16_t((ctx.rnd() & 0x1f) + 0x20); }
    }
    walkFrame();
}


// ---------------------------------------------------------------- scorpion (type 133, 0x41baa0)

void h_scorpion(ObjCtx& ctx, Obj& o) {
    bool killed = false;
    if (weaponHits(ctx, o, 2, &killed) && killed) { killObject(ctx, o); return; }
    { int y = o.y + 0xd; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0x14); }
    harryContact(ctx, o, 0x1e);
    o.b(0x33) -= 1;
    if (int8_t(o.b(0x33)) < 0) {
        // Shuffles two pixels towards Harry every 8 ticks, stopping at wall markers.
        o.b(0x33) = 7;
        if (ctx.harry.x() != o.x && ctx.harry.x() + 1 != o.x) {
            if (ctx.harry.x() + 1 > o.x) {
                o.facing = 0xff;
                if (!(ctx.triggerClass(o.x + 8, o.y - 0x10) & 1)) { o.x += 2; o.frame ^= 1; }
            } else {
                o.facing = 0;
                if (!(ctx.triggerClass(o.x - 0x28, o.y - 0x10) & 1)) { o.x -= 2; o.frame ^= 1; }
            }
        }
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- crumbling rock (type 76, 0x41514e)

// 0x424cd4: is Harry standing inside the platform box {x0, x1, y0, y1} relative to the object?
bool harryOnBox(ObjCtx& ctx, const Obj& o, int x0, int x1, int y0, int y1) {
    // FUN_00424cd4 (a0 = {x0, x1, y0, y1}): Harry's offset from the object inside [x0, x1) x [y0, y1)
    const int16_t dx = int16_t(ctx.harry.x() - o.x), dy = int16_t(ctx.harry.y() - o.y);
    return int16_t(dx - x1) < 0 && int16_t(dx - x0) >= 0 && int16_t(dy - y0) >= 0 && int16_t(dy - y1) < 0;
}

const uint8_t kRockShake[0x21] = {5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // 0x45bbf0

void h_fallingRock(ObjCtx& ctx, Obj& o) {
    o.b(0x6d) -= 1;                                    // 0x41514e: every 16 ticks to the end of the object list
    if (int8_t(o.b(0x6d)) < 0) { o.b(0x6d) = 0x10; if (!ctx.pl.objectSystem().moveToTail(o)) return; }   // 0x435532
    if (o.b(0x50) == 0) { o.b(0x50) = 0xff; o.b(0x32) = 0x20; }
    if (o.b(0x32) != 0x20) {
        if (o.b(0x32) == 0) {
            // Falling.
            o.l(0x34) += 0x8000;
            moveY(o, o.l(0x34));
            exitDespawnFar(ctx, o);
            return;
        }
        o.b(0x32) -= 1;
        o.frame = kRockShake[o.b(0x32)];
    }
    // 0x4151ec: Harry landing on it (box 0x45bc21) starts the countdown (and rides it down).
    if (!ctx.pl.harryOnPlatform() && !ctx.pl.harryBusy() && ctx.harry.w(0x465baa) >= 0 && harryOnBox(ctx, o, -25, 25, -30, 5)) {
        ctx.pl.standOn(o.y - 0x12);
        if (o.b(0x32) == 0x20) { o.b(0x32) = 0x1f; ctx.sound0(0xe, 0xb); }   // FUN_0042633d(0xe, 0x60)
    }
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- spider web (type 8, 0x406bbc)

void h_web(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xff) {                    // angled web, bounces to the right
        o.frame = 0; o.b(0x46) = 0xff; o.facing = 0xff; o.script = kAnim_webBounceA;
    }
    if (o.frame == 0xfe) {                    // angled web, bounces to the left
        o.frame = 0; o.b(0x47) = 0xff; o.script = kAnim_webBounceA;
    }
    if (o.frame == 0xfd) {                    // wide horizontal web: two mirrored halves
        o.frame = 0; o.script = kAnim_webBounceH;
        Obj* half = ctx.spawn();
        if (!half) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        half->facing = 0xff;
        half->x = o.x; half->y = o.y;
        half->sprite = o.sprite;
        half->flags = 0x6000;
        half->w(0x26) = 0x6663;
        half->type = 0;                       // drawn only; the owner animates it
        half->link = &o;
        o.link = half;
    }
    if (o.w(0x36)) animStep(ctx, o, o.anim(0x36), o.script ? o.script : kAnim_webBounceH, 1);
    if (o.w(0x46)) webHit(ctx, o, nullptr);                 // 0x406cc7: the angled webs give under the whip
    Harry& h = ctx.harry;
    if (!ctx.pl.harryOnPlatform() && !ctx.pl.harryBusy() && h.w(0x465baa) >= 0 && harryOnBox(ctx, o, -40, 40, 12, 28)) {
        // 0x406d17: it throws Harry up (box 0x45b7a7); the angled webs fling him backwards
        o.w(0x36) = 1;
        h.y() = int16_t(o.y + 3);
        h.b(0x465b75) = 0;
        h.b(0x465b6d) = 0xff;
        h.b(0x465b7b) = 0;
        h.frame() = 0;
        h.onGround() = 0;
        ctx.sound1((ctx.rnd() & 0x80) ? 0xca : 0x46, 9);
        h.w(0x465b47) = 0;
        h.setSprite("hyivjump");
        h.setCont(0x42c518);
        h.vx() = 0;
        h.vy() = int32_t(0xfff4c000);
        if (o.b(0x47)) {
            h.setCont(0x42cdb6);
            h.facing() = 0;
            h.vx() = int32_t(0xfffb6000);
            h.vy() = int32_t(0xfff50000);
            h.setSprite("hyihjump");
            h.b(0x465bb2) = 0x60;
        }
        if (o.b(0x46)) {
            h.setCont(0x42cdb6);
            h.facing() = 0xff;
            h.vx() = 0x4a000;
            h.vy() = int32_t(0xfff50000);
            h.setSprite("hyihjump");
            h.b(0x465bb2) = 0x60;
        }
    }
    if (o.link && !o.link->freed) {
        o.link->frame = o.frame;
        const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
        if (dx < -0x78 || dx >= 0x1b8 || dy < -0x82 || dy >= 0x15e) {
            ctx.pl.restoreEntry(o);
            ctx.release(*o.link);
            ctx.release(o);
        }
        return;
    }
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- scenery bits

// 0x41f320: type 161, a static dot / hole; sn_hole sits behind Harry.
void h_dot(ObjCtx& ctx, Obj& o) {
    if (o.sprite && o.sprite->name() == "sn_hole") o.behind = 0xff;
    exitDespawnFar(ctx, o);
}

// 0x41f2c2: type 160, eyes blinking in the dark (sc_eyes1 / sc_eyes2 by column parity).
void h_eyes(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        if ((o.x >> 4) & 1) o.sprite = ctx.spr("sc_eyes2");
    }
    animStep(ctx, o, o.anim(0x50), kAnim_eyesBlink, 4);
    exitDespawnFar(ctx, o);
}

// 0x412c52: type 63, a leaf spawner: drops a drifting leaf every 0x40..0x7f ticks.
void h_leafSpawner(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) o.b(0x32) = uint8_t((ctx.rnd() & 0x3f) + 0x40);
    o.b(0x32) -= 1;
    if (o.b(0x32) == 0) {
        if (Obj* l = ctx.spawn()) {
            l->w(0x4e) = 0x46;
            l->y = o.y;
            l->x = int16_t(o.x + int(ctx.rnd() & 0x3f) - 0x20);
            l->type = 0x40; l->flags = 0;
            const int32_t r = int32_t(ctx.rnd() & 0x7fff);
            l->l(0x36) = r;
            if (r & 1) { l->sprite = ctx.spr("fx_leaf1"); l->script = kAnim_feather; l->l(0x36) += 0x6000; }
            else { l->sprite = ctx.spr("fxzleaf2"); l->script = kAnim_turn4; l->l(0x36) += 0x10000; }
        }
        o.b(0x32) = uint8_t((ctx.rnd() & 0x3f) + 0x40);
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- tree branch (types 123 / 124 / 125)

// 0x41b2da: the branch base spawns its tip, a platform that gives way.
void h_branch(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xfe) { o.facing = 1; o.frame = 0xff; }
    if (o.frame == 0xff) {
        o.frame = 0;
        Obj* tip = ctx.spawn();
        if (!tip) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        tip->facing = o.facing;
        tip->sprite = ctx.spr("obrbrnch");
        tip->frame = 1; tip->type = 0x7c; tip->b(0x32) = 0x19;
        tip->x = int16_t(o.x - 6 + (o.facing ? 0xc : 0));
        tip->y = o.y;
        tip->flags = o.flags;
        tip->link = &o; o.link = tip;
    }
    const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
    if (dx < -0x78 || dx >= 0x1b8 || dy < -0x82 || dy >= 0x15e) {
        ctx.pl.restoreEntry(o);
        if (o.link && !o.link->freed && o.link->type == 0x7c && o.link->link == &o) ctx.release(*o.link);
        ctx.release(o);
    }
}

// 0x41b424: the tip bends under Harry and snaps after 25 ticks (or when he leaves).
void h_branchTip(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) != 0x19) {
        if (o.b(0x32) == 0) {
            if (o.frame == 2) {
                ctx.sound0(4, 0x11);                                   // FUN_0042633d(4, 0x60)
                if (Obj* p = ctx.spawn()) {                            // FUN_0043546e
                    p->x = o.x; p->y = o.y;
                    p->facing = o.facing; p->type = 0x7d;
                    p->sprite = ctx.spr("obrbrnch"); p->frame = 3; p->b(0x32) = 3;
                    p->flags = o.flags;
                }
            }
            o.frame = 6;
            o.l(0x34) += 0x6000;
            moveY(o, o.l(0x34));
            exitDespawnFar(ctx, o);
            return;
        }
        o.b(0x32) -= 1;
        if (int8_t(o.b(0x32) - 0xc) < 0) o.frame = 2;
    }
    Harry& h = ctx.harry;
    const bool on = !ctx.pl.harryOnPlatform() && !ctx.pl.harryBusy() && h.w(0x465baa) >= 0 &&
                    (o.facing ? harryOnBox(ctx, o, -50, 18, -5, 16) : harryOnBox(ctx, o, -18, 50, -5, 16));   // 0x45be70 / 0x45be68
    if (on) {
        // 0x41b544
        h.y() = int16_t(o.y + 5 + (o.frame == 2 ? 5 : 0));
        h.vy() = 0;
        ctx.pl.standOn(h.y(), true);
        o.l(0x50) = 0;
        if (o.b(0x32) == 0x19) o.b(0x32) = 0x18;
    } else if (o.b(0x32) != 0x19) {
        o.b(0x32) = 0; o.frame = 2;
    }
}

// 0x41b5d8: the snapped-off piece tumbles away.
void h_branchPiece(ObjCtx& ctx, Obj& o) {
    o.x += o.facing ? -1 : 1;
    moveY(o, -0x8000);
    o.b(0x32) -= 1;
    if (int8_t(o.b(0x32)) < 0) {
        o.b(0x32) = 3;
        if (o.frame == 5) { ctx.release(o); return; }
        o.frame++;
    }
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- checkpoint marker (type 3, 0x405aae)

void h_checkpoint(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xfe) { o.facing = 0xff; o.frame = 0xff; }
    if (o.frame == 0xff) {
        o.frame = 0;
        { int y = o.y + 0x19; if (ctx.floorSnap(o.x, y)) o.y = int16_t(y - 0x19); }
        o.behind = 0xff;
        if (ctx.g.checkpoint().valid && ctx.g.checkpoint().markerX == o.x) o.frame = 8;   // already lit
    }
    if (o.frame == 8 || o.frame == 0xc) {
        // Lit: flickers 8 / 12 three times, then rests for 200 ticks.
        o.b(0x64) += 1;
        if (o.b(0x64) == 0xa) {
            o.b(0x64) = 0;
            if (o.frame != 0xc) o.frame = 0xc;
            else {
                o.frame = 8;
                o.b(0x65) += 1;
                if (o.b(0x65) == 3) { o.b(0x65) = 0; o.b(0x64) = 0xc8; }
            }
        }
        exitDespawnFar(ctx, o);
        return;
    }
    animStep(ctx, o, o.anim(0x40), kAnim_marker, 4);
    if (ctx.harry.onGround() && ctx.touchesHarry(o)) {
        ctx.sound0(0xeb, 0x12);                       // FUN_004262d9
        Game::Checkpoint& cp = ctx.g.checkpoint();
        cp.valid = true;
        cp.x = o.x + 4 + 0x18 - (o.facing == 0 ? 0x30 : 0);
        cp.y = ctx.harry.y();
        cp.markerX = o.x;
        cp.facingLeft = o.facing != 0;
        o.frame = 8;
        if (Obj* f = ctx.spawn()) {           // the flame (type 0x76)
            f->facing = o.facing; f->type = 0x76;
            f->sprite = ctx.spr("fx_mls"); f->frame = 9; f->flags = 0x6000;
            f->x = int16_t(o.x + 3 - (o.facing ? 7 : 0)); f->y = int16_t(o.y - 4);
        }
    }
    exitDespawnFar(ctx, o);
}

// 0x41ae9a: type 0x76, the flame rising from a lit marker.
void h_markerFlame(ObjCtx& ctx, Obj& o) {
    o.x += o.facing ? -5 : 5;
    o.y -= 4;
    animStep(ctx, o, o.anim(0x32), kAnim_markerFlame, 2);
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- monkey on a vine (type 72, 0x414dac)

void h_vineMonkey(ObjCtx& ctx, Obj& o) {
    o.behind = 0xff;
    bool killed = false;
    if (weaponHits(ctx, o, 10, &killed) && killed) { killObject(ctx, o); return; }
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    if (harryContact(ctx, o, 10)) {                    // 0x414e00: knocked down off his vine or rope
        Harry& h = ctx.harry;
        h.setCont(0x42f88c);
        h.w(0x465b47) = 0;
        h.b(0x465b77) = 0x1e;
        h.w(0x4658bc) = h.x();
    }
    if (o.b(0x36) == 0) {
        o.b(0x36) = 0xff;
        if (int16_t(ctx.harry.x() - o.x) >= 0) { o.x -= 1; o.facing = 0xff; }
    }
    bool animate = true;
    if (o.frame == 6) {
        // Resting between throws: only when on screen, after the pause, with Harry below.
        const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
        if (dx < -0x20 || dx >= 0x160 || dy < -0x20 || dy >= 0xe8) animate = false;
        else if (o.w(0x60)) { o.w(0x60) -= 1; animate = false; }
        else if (!between(ctx.harry.y() - o.y, -0x10, 0x90)) animate = false;
    }
    if (animate) animStep(ctx, o, o.anim(0x32), kAnim_vineMonkey, 4);
    if (o.anim(0x32).tick == 0 && o.anim(0x32).pos == 0xb) {
        // Release the cactus at the top of the swing.
        if (Obj* c = ctx.spawnFrom(o)) {                     // FUN_004356bc(1)
            c->w(0x26) = 0x6663;
            c->x = int16_t(o.x + 0x14);
            c->y = int16_t(c->y + 0xb); c->y = o.y;          // (sic: the + 0xb is overwritten)
            c->sprite = ctx.spr("obrcactu");
            c->behind = 0xff; c->frame = 0; c->b(0x5a) = 0xc; c->type = 0x1d; c->b(0x46) = 0xff;
            o.w(0x60) = int16_t((ctx.rnd() & 0x1f) + 0x10);
            c->l(0x32) = 0x38000; c->l(0x36) = 0x40000; c->l(0x3a) = -0x100; c->l(0x3e) = 0x1100;
            c->flags = 0x6000;
            if (o.facing == 0) { c->l(0x32) = -c->l(0x32); c->l(0x3a) = -c->l(0x3a); c->x -= 0x2b; }
        }
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- tree snake (types 172 / 60, 0x420090)

// 0x420530: Harry within reach of the hanging head?
bool treeSnakeBiteRange(ObjCtx& ctx, const Obj& o) {
    const int dx = o.x + (o.facing ? -0x20 : 0x20) - ctx.harry.x();
    const int dy = o.y + 0x1e - ctx.harry.y() + 0x18;
    return between(dx, -0x10, 0x10) && between(dy, -0x28, 0x28);
}

void h_treeSnake(ObjCtx& ctx, Obj& o) {
    if (o.w(0x32) == 0) {
        o.w(0x32) = 0x40;
        Obj* body = ctx.spawn();
        if (!body) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        o.link = body;
        body->x = o.x; body->y = int16_t(o.y - 2); body->flags = 0x6000;
        body->sprite = ctx.spr("tszbody1"); body->type = 0x3c; body->frame = 0;
        o.sprite = ctx.spr("tszhang"); o.frame = 0;
    }
    auto releaseBoth = [&]() { if (o.link && !o.link->freed) ctx.release(*o.link); };
    bool killed = false;
    if (weaponHits(ctx, o, 0x14, &killed)) { releaseBoth(); killObject(ctx, o); return; }
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    harryContact(ctx, o, 10);                 // 0x417688
    auto leave = [&]() {
        const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
        if (dx < -0x78 || dx >= 0x1b8 || dy < -0x82 || dy >= 0x15e) { releaseBoth(); ctx.pl.restoreEntry(o); ctx.release(o); }
    };
    for (;;) {
        if (o.w(0x36)) {
            // Spitting / biting.
            o.sprite = ctx.spr("tszspit");
            const bool looped = animStep(ctx, o, o.anim(0x36), kAnim_treeSnakeSpit, 5);
            if (o.frame == 3 && treeSnakeBiteRange(ctx, o)) ctx.hurtHarry(0x14);
            if (looped) {
                o.w(0x66) = int16_t(0x40 + (ctx.rnd() & 0x3f));
                o.w(0x64) = 0; o.w(0x34) = 0; o.w(0x36) = 0;
                o.facing ^= 1;
            } else {
                if (o.anim(0x36).pos == 5 && o.anim(0x36).tick == 0) {
                    if (Obj* st = ctx.spawn()) {
                        st->x = int16_t(o.x + 0x20); st->y = int16_t(o.y + 0x26);
                        st->sprite = ctx.spr("obrston0"); st->frame = 0; st->b(0x5a) = 0xc; st->type = 0x1d; st->b(0x46) = 0xff;
                        o.w(0x60) = int16_t((ctx.rnd() & 0x7f) + 8);
                        st->l(0x32) = 0x58000; st->l(0x36) = 0x8000; st->l(0x3a) = -0x100; st->l(0x3e) = 0x1100;
                        st->flags = 0x6000;
                        if (o.facing) { st->l(0x32) = -st->l(0x32); st->l(0x3a) = -st->l(0x3a); st->x -= 0x40; }
                    }
                }
                leave();
                return;
            }
        }
        if (o.w(0x64)) {
            // Rising to look for Harry.
            o.sprite = ctx.spr("tszup");
            if (!animStep(ctx, o, o.anim(0x64), kAnim_treeSnakeUp, 5)) {
                if (o.frame == 3) {
                    const int dx = ctx.harry.x() - o.x, dy = ctx.harry.y() - 0x1c - o.y - 0x28;
                    if (between(dx, -0x90, 0x90) && between(dy, -0x50, 0x50)) { o.w(0x36) = 1; o.facing ^= 1; continue; }
                }
                leave();
                return;
            }
            o.w(0x66) = int16_t(0x30 + (ctx.rnd() & 0x3f));
            o.w(0x64) = 0; o.w(0x36) = 0; o.w(0x34) = 0;
        }
        break;
    }
    // Hanging idle.
    o.sprite = ctx.spr("tszhang");
    animStep(ctx, o, o.anim(0x34), kAnim_treeSnakeHang, 6);
    o.w(0x64) = 0; o.w(0x36) = 0; o.w(0x38) = 0;
    if (o.w(0x66)) { o.w(0x66) -= 1; leave(); return; }
    const int dx = ctx.harry.x() - o.x, dy = ctx.harry.y() - 0x1c - o.y - 0x28;
    if (between(dx, -0xb0, 0xb0) && between(dy, -0x90, 0x90)) {
        const bool facingHarry = dx >= 0 ? o.facing != 0 : o.facing == 0;
        if (facingHarry && o.frame == 0) o.w(0x64) = 1;
    }
    leave();
}

// 0x412710: type 0x3c, the tree snake's body (static).
void h_treeSnakeBody(ObjCtx& ctx, Obj& o) { (void)ctx; (void)o; }


// ---------------------------------------------------------------- exploding plant (type 87, 0x416be0)

struct SpikeShot { int32_t vx, vy; int16_t dx, dy; uint8_t facing, frame; };
const SpikeShot kPlantSpikes[6] = {                                           // 0x45b188
    {-0x38000, -0x2c000, -41, -42, 0x81, 2}, {-0x46000, 0x3000, -46, -16, 0x80, 0}, {-0x26000, 0x20000, -44, -1, 0x80, 2},
    {0x3b000, -0x34000, 32, -38, 1, 2},      {0x4e000, -0x6000, 36, -23, 1, 0},    {0x20000, 0x20000, 27, -7, 0, 2}};

void h_explodingPlant(ObjCtx& ctx, Obj& o) {
    if (o.w(0x32) == 0) {
        o.w(0x32) = 1; o.w(0x60) = 0x600;
        if (o.flags != 0x4000) { o.flags = 0x4000; o.vflip = 0xff; }   // hangs from the ceiling
    }
    auto rearm = [&]() { o.w(0x32) = int16_t((ctx.rnd() & 0x1f) + 0x22); };
    if (o.w(0x50)) {
        // Recoiling from a whip hit.
        o.whipCool = 0;
        if (!animStep(ctx, o, o.anim(0x50), kAnim_plantHit, 4)) { exitDespawnFar(ctx, o); return; }
        o.frame = 0; o.w(0x34) = 0; o.w(0x50) = 0;
        rearm();
    }
    if (o.frame == 0 && !between(ctx.harry.x() - o.x, -0xc0, 0xc0)) { exitDespawnFar(ctx, o); return; }
    bool checkBurst = true;
    if (o.frame == 0 && o.w(0x64) == 3) {
        o.w(0x32) = 0x30;                     // three hits: it stays shut
    } else {
        o.w(0x32) -= 1;
        if (o.w(0x32) != 0) {
            animStep(ctx, o, o.anim(0x60), kAnim_plantBreathe, 7);
            if (o.w(0x32) == 1 && o.frame != 2) o.w(0x32) = 2;
        } else {
            o.w(0x32) = 1; o.w(0x60) = 0;
            if (animStep(ctx, o, o.anim(0x34), kAnim_plantOpen, 4)) { rearm(); checkBurst = false; }
            else if (o.frame == 5) {
                bool killed = false;
                const int16_t y0 = o.y;
                if (o.vflip) o.y += 0x2a;
                const bool hit = weaponHits(ctx, o, 0, &killed);
                o.y = y0;
                if (hit) { o.w(0x50) = 0x100; o.w(0x64) += 1; exitDespawnFar(ctx, o); return; }
            }
        }
    }
    if (checkBurst && o.anim(0x34).tick == 0 && o.anim(0x34).pos == 0xd) {
        // Fully open: spikes fly out in six directions.
        ctx.sound1(0x40, 8);
        for (const SpikeShot& sp : kPlantSpikes) {
            Obj* k = ctx.spawn();
            if (!k) break;
            k->sprite = ctx.spr("plzspik"); k->frame = sp.frame; k->b(0x5a) = 0xc; k->type = 0x1d; k->b(0x48) = 0xff;
            k->x = int16_t(o.x + sp.dx); k->y = o.y;
            k->l(0x32) = sp.vx; k->l(0x36) = sp.vy;
            k->facing = sp.facing; k->vflip = sp.facing & 1;
            int16_t dy = sp.dy;
            if (o.vflip) { k->l(0x36) = -k->l(0x36); dy = int16_t(-dy); k->vflip ^= 1; }
            k->y = int16_t(k->y + dy);
            k->facing &= 0x80;
            k->flags = 0x4000;
        }
    }
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- wild boar (type 80, 0x415640)

void h_boar(ObjCtx& ctx, Obj& o) {
    o.behind = 0xff;
    if (o.b(0x56) == 0) {
        o.b(0x56) = 0xff; o.b(0x64) = 0xff; o.w(0x68) = 1; o.w(0x62) = 0;
        o.frame = 0; o.facing = 0; o.y += 0x10;
        if (ctx.harry.x() - o.x >= 0) o.facing = 1;
    }
    { int y = o.y + 0xb; if (ctx.floorSnap(o.x - 0xc, y)) o.y = int16_t(y - 0xb); }
    bool killed = false;
    const bool hit = weaponHits(ctx, o, 0x1f, &killed);
    if (hit) {
        if (killed) { killObject(ctx, o); return; }
        if (o.b(0x64) == 0 && o.w(0x3c) == 0) {
            const int d = ctx.harry.x() - o.x;
            if (d >= 0 ? o.facing != 0 : o.facing == 0) { o.w(0x58) = 1; o.w(0x3e) = 0; }   // whipped head-on: tumbles
        }
    }
    harryContact(ctx, o, 0xf);
    if (o.b(0x64)) {
        // Grazing until Harry comes within 128 px (or something hits it).
        o.w(0x68) -= 1;
        if (o.w(0x68) < 0) o.w(0x68) = int16_t((ctx.rnd() & 0x3f) + 0x30);
        o.sprite = ctx.spr("whitoss");
        animStep(ctx, o, o.anim(0x62), kAnim_boarGraze, 4);
        const int d = ctx.harry.x() - o.x;
        if (!hit && !between(d, -0x80, 0x80)) { exitDespawnWide(ctx, o); return; }
        o.b(0x64) = 0;
        if (!between(d, -0x80, 0x80)) o.facing ^= 1;
    }
    moveX(o, o.l(0x32));
    auto easeTowardsFacing = [&](int32_t step, int limit) {
        if (o.facing == 0) { if (o.w(0x34) != -limit - 1) o.l(0x32) -= step; }
        else { if (o.w(0x34) != limit) o.l(0x32) += step; }
    };
    if (o.w(0x58)) {
        // Tumbling after a whip hit.
        o.sprite = ctx.spr("whiroll");
        animStep(ctx, o, o.anim(0x3e), kAnim_sparkle2, 4);
        easeTowardsFacing(0x7000, 4);
    } else if (o.w(0x3c)) {
        // Skidding round.
        o.sprite = ctx.spr("whiroll");
        animStep(ctx, o, o.anim(0x3e), kAnim_sparkle2, 4);
        if (o.l(0x32) < 0) o.l(0x32) += 0x3000; else o.l(0x32) -= 0x3000;
        if (o.w(0x34) != 0) { exitDespawnWide(ctx, o); return; }
        o.w(0x44) = 0x32; o.facing ^= 1;
        o.sprite = ctx.spr("whirun"); o.w(0x3c) = 0;
    } else {
        if (o.w(0x50)) {
            // Tossing Harry.
            o.l(0x32) = 0;
            o.sprite = ctx.spr("whitoss");
            if (!animStep(ctx, o, o.anim(0x50), kAnim_boarToss, 3)) { exitDespawnWide(ctx, o); return; }
            o.sprite = ctx.spr("whirun"); o.frame = 0;
        } else if (ctx.harry.cont() != 0x426fee && ctx.harry.b(0x465b01) == 0 && harryContact(ctx, o, 0xf)) {
            // 0x4159bf: a hit from the front tosses Harry (state 0x42c518, little air control)
            const int d = int16_t(ctx.harry.x() - o.x);
            if ((o.facing ? d >= 0 : d < 0) && ctx.harry.cont() != 0x430e3a && ctx.harry.cont() != 0x42a36f) {
                ctx.harry.b(0x465bb2) = 0x30;
                ctx.harry.onGround() = 0;
                ctx.harry.vx() = o.facing ? 0x48000 : -0x48000;
                ctx.harry.vy() = int32_t(0xfffcb000);
                ctx.harry.setCont(0x42c518);
                o.w(0x50) = 1;
            }
        }
        easeTowardsFacing(0x4000, 4);
    }
    // 0x415abb: turn when Harry is behind, or at a type-2 marker ahead.
    if (o.w(0x44)) o.w(0x44) -= 1;
    else {
        const int d = ctx.harry.x() - o.x;
        bool turn = d >= 0 ? o.facing == 0 : o.facing != 0;
        if (!turn) {
            uint8_t id = 0;
            ctx.triggerClass(o.x + (o.facing ? 0x48 : -0x48), o.y - 8, &id);
            turn = id == 2;
        }
        if (turn) { o.w(0x3c) = 1; o.w(0x3e) = 0; o.w(0x58) = 0; }
    }
    animStep(ctx, o, o.anim(0x36), kAnim_boarRun, 3);
    exitDespawnWide(ctx, o);
}


// ---------------------------------------------------------------- more rats and bats

// 0x419ec8: type 112, a faster rat that turns at edges without a turn animation.
void h_rat2(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xff) {
        o.frame = 0; o.l(0x32) = -0x28000; o.facing = 1;
        if (ctx.harry.x() - o.x >= 0) { o.facing = 0; o.l(0x32) = 0x28000; }
    }
    bool killed = false;
    if (weaponHits(ctx, o, 9, &killed) && killed) { killObject(ctx, o); return; }
    { int y = o.y + 0x17; if (ctx.floorSnap(o.x - 0xc, y)) o.y = int16_t(y - 0x17); }
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    harryContact(ctx, o, 5);
    moveX(o, o.l(0x32));
    auto reverse = [&]() { o.w(0x36) = 0; o.b(0x38) = 0xff; o.l(0x32) = -o.l(0x32); o.facing ^= 1; };
    { int y = o.y + 0x17; if (!ctx.floorSnap(o.x + (o.l(0x32) < 0 ? -0x28 : 0x10), y)) reverse(); }
    if (ctx.triggerClass(o.x + (o.l(0x32) < 0 ? -0x26 : 7), o.y + 4) & 1) reverse();
    animStep(ctx, o, o.anim(0x36), kAnim_rat2Run, 2);
    exitDespawnWide(ctx, o);
}

// 0x414ab1: type 70, a rat climbing up and down a rope / wall (trigger ids 0x19, 0x1a, 0xf9).
void h_climbingRat(ObjCtx& ctx, Obj& o) {
    o.behind = 0xff;
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        o.x -= 0x14;
        if (o.frame == 0xfe) { o.l(0x3c) = 0x11000; o.vflip = 1; }
        else o.l(0x3c) = -0x11000;
    }
    bool killed = false;
    weaponHits(ctx, o, 9, &killed);
    if (killed) { killObject(ctx, o); return; }
    if (ctx.touchesHarry(o)) {                         // 0x414b0f: every tick
        ctx.hurtHarry(10);
        Harry& h = ctx.harry;
        const uint32_t c = h.cont();
        if (c == 0x42d781 || c == 0x42cc7a || c == 0x42de4f || c == 0x42dd7e) {
            // on a rope or a vine: knocked off it
            h.setCont(0x42f88c);
            h.w(0x465b47) = 0;
            h.b(0x465b77) = 0x1e;
            h.w(0x4658bc) = h.x();
        }
    }
    animStep(ctx, o, o.anim(0x40), kAnim_turn4, 4);
    moveY(o, o.l(0x3c));
    uint8_t id = 0;
    ctx.triggerClass(o.x - 0x10, o.y + (o.l(0x3c) < 0 ? 0 : 0x20), &id);
    if (id != 0x19 && id != 0x1a && id != 0xf9) {
        o.l(0x3c) = -o.l(0x3c);
        moveY(o, o.l(0x3c)); moveY(o, o.l(0x3c));
        o.vflip ^= 1;
    }
    exitDespawnFar(ctx, o);
}

// 0x4153e0: type 78, a roost of two bats released one after the other when Harry nears.
void h_batRoost(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        for (int i = 0; i < 2; ++i) {
            Obj* b = ctx.spawn();
            if (!b) break;
            b->x = int16_t(o.x + (i ? 0x10 : 0)); b->y = o.y;
            b->type = 0x2b; b->flags = 0x6000; b->sprite = ctx.spr("btfhang"); b->frame = 0; b->behind = 0xff;
            if (i == 0) o.link = b; else o.link2 = b;
        }
    }
    auto wake = [&](Obj* b) {
        if (b && !b->freed && b->type == 0x2b) { b->facing = o.facing; b->b(0x32) = 1; b->type = 0x12; }
    };
    if (o.w(0x34)) {
        o.w(0x34) -= 1;
        if (o.w(0x34) == 0) { wake(o.link2); ctx.release(o); }
        return;
    }
    const int dx = o.x - ctx.harry.x();
    if (dx < -0x8c) o.facing = 0xff;
    else if (dx >= 0x5a) o.facing = 0;
    else {
        const int dy = o.y - ctx.harry.y();
        if (dy >= -0xc8 && dy < 0x14) {
            wake(o.link); o.link = nullptr;
            o.w(0x34) = 0x32;
            return;
        }
    }
    const int cx = o.x - ctx.camX, cy = o.y - ctx.camY;
    if (cx < -0x78 || cx >= 0x1b8 || cy < -0x82 || cy >= 0x15e) {
        if (o.link && !o.link->freed && o.link->type == 0x2b) ctx.release(*o.link);
        if (o.link2 && !o.link2->freed && o.link2->type == 0x2b) ctx.release(*o.link2);
        ctx.pl.restoreEntry(o); ctx.release(o);
    }
}

// 0x40f9dc: type 0x2b, a bat still asleep on the roost (only the whip can reach it).
void h_sleepingBat(ObjCtx& ctx, Obj& o) {
    bool killed = false;
    if (weaponHits(ctx, o, 9, &killed) && killed) killObject(ctx, o);
}


// ---------------------------------------------------------------- mine ceiling (types 96 / 150)

// 0x41836e: type 96, drops a rock from the ceiling every 0x46..0xc5 ticks.
void h_ceilingDripper(ObjCtx& ctx, Obj& o) {
    if (o.w(0x32) == 0) o.w(0x32) = int16_t(1 + (ctx.rnd() & 0x1f));
    o.w(0x32) -= 1;
    if (o.w(0x32) == 0) {
        if (Obj* r = ctx.spawn()) {
            ctx.sound0((ctx.rnd() & 0x80) ? 0x29 : 0x2c, 3);   // FUN_0042633d
            r->type = 0x96; r->x = o.x; r->y = int16_t(o.y - 8);
            r->sprite = ctx.spr("ob_ceil"); r->frame = 0; r->flags = 0;
            r->x = int16_t(r->x + int(ctx.rnd() & 0x3f) - 0x20);
            r->w(0x32) = 0x14;
        }
        o.w(0x32) = int16_t(0x46 + (ctx.rnd() & 0x7f));
    }
    exitDespawnFar(ctx, o);
}

// 0x41dc50: type 150, the falling rock: tumbles, then shatters on the floor.
void h_ceilingRock(ObjCtx& ctx, Obj& o) {
    if (o.w(0x40)) {
        if (animStep(ctx, o, o.anim(0x40), kAnim_rockShatter, 3)) ctx.release(o);
        else exitDespawnFar(ctx, o);
        return;
    }
    o.l(0x36) += 0x3c00;
    moveY(o, o.l(0x36));
    if (o.frame != 3) {
        o.b(0x5a) += 1;
        if (o.b(0x5a) == 7) { o.b(0x5a) = 0; o.frame++; }
    }
    if (o.w(0x32)) { o.w(0x32) -= 1; exitDespawnFar(ctx, o); return; }
    int y = o.y + 0x12;
    bool landed = ctx.floorSnap(o.x, y);
    if (landed) o.y = int16_t(y - 0x12);
    else { int y2 = o.y - 8; landed = ctx.floorSnap(o.x, y2, true); }
    if (landed) { o.w(0x40) = 0x100; h_ceilingRock(ctx, o); return; }
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- TNT crate (types 34 / 50)

void h_tnt(ObjCtx& ctx, Obj& o) {
    // 0x40e18d.  (The ew_ready variant of engine stage 0x18, DAT_00451a2c, is not ported.)
    Harry& h = ctx.harry;
    if (!ctx.pl.harryOnPlatform() && !ctx.pl.harryBusy() && h.w(0x465baa) >= 0 && harryOnBox(ctx, o, -15, 15, -32, -16)) {
        // 0x40e21a: Harry stands on the crate (box 0x45ba6e); no landing
        h.y() = int16_t(o.y - 0x18);
        h.vy() = 0;
        h.b(0x465b73) = 0xff;
    }
    bool killed = false;
    weaponHits(ctx, o, 0, &killed);
    if (!killed) { exitDespawnFar(ctx, o); return; }
    o.y -= 0xa;
    // The two crates of level 2 that open passages (DAT_00466524 / 25, kept across deaths).
    if (o.x != 0x6b8) {
        if (int16_t(o.x - 0x11b0) >= 0) { ctx.g.levelFlags()[0x15] = 0xff; ctx.pl.clearTriggerClass(0x5d); }
        else { ctx.g.levelFlags()[0x14] = 0xff; ctx.pl.clearTriggerClass(0x51); }
    }
    ctx.g.stats().score += scoreForType(o.type);              // 0x4081ae
    o.link = nullptr;
    ctx.release(o);
    Obj* e = ctx.spawn();                                      // FUN_0043546e
    if (!e) return;
    e->x = o.x; e->y = o.y;
    ctx.sound0(0x26, 0xa);                                     // FUN_0042633d(0x26, 0x60)
    e->type = 0x32; e->sprite = ctx.spr("fxzexpl2"); e->flags = 0xe000; e->link = nullptr;
    e->frame = 0xff; e->behind = 0xff; e->l(0x3c) = 0; e->facing = 0;
    if (Obj* half = ctx.spawn()) {                             // FUN_0043546e from the explosion: the mirrored half
        half->x = e->x; half->y = e->y;
        e->link = half; half->link = e;
        half->behind = 0xff; half->w(0x26) = 0x6663; half->type = 0;
        half->sprite = ctx.spr("fxzexpl2"); half->flags = 0xe000; half->frame = 0xff; half->facing = 0xff;
    }
}

// 0x41052a: type 0x32, the explosion; from frame 3 it blasts Harry for 40 every tick and
// shoves him away (FUN_00427167).
void h_explosion(ObjCtx& ctx, Obj& o) {
    if (animStep(ctx, o, o.anim(0x3c), kAnim_marker, 4)) {    // 0x4619cf
        ctx.release(o);
        if (o.link && !o.link->freed) ctx.release(*o.link);
        return;
    }
    if (o.link && !o.link->freed) o.link->frame = o.frame;
    if (int8_t(o.frame - 3) >= 0) {
        o.touchTick = 0;
        if (harryContact(ctx, o, 0x28)) {
            // d0 = 1 is overwritten by the word difference; its low byte is the direction (0x4105bd)
            const int16_t dx = int16_t(ctx.harry.x() - o.x);
            ctx.harry.b(0x465b55) = dx >= 0 ? uint8_t(dx) : 0;
            ctx.pl.harryCall(0x427167);
        }
    }
}


// ---------------------------------------------------------------- sticky web across a passage (type 32, 0x40dc2e)

void h_verticalWeb(ObjCtx& ctx, Obj& o) {
    if (!o.link) {
        Obj* half = ctx.spawn();                               // FUN_0043546e
        if (!half) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        half->vflip = 0xff; half->flags = 0x6000; half->sprite = o.sprite; half->w(0x26) = 0x6663;
        half->x = o.x; half->y = o.y; half->type = 0; half->link = &o;
        o.link = half;
    }
    bool cut = false;
    if (webHit(ctx, o, &cut)) {                                // 0x40dcc5
        o.w(0x3c) = 1;
        o.facing = int16_t(o.x - ctx.harry.x()) >= 0 ? 0xff : 0;
        if (cut) {                                             // torn through
            if (o.link && !o.link->freed) ctx.release(*o.link);
            killObject(ctx, o);
            return;
        }
    }
    if (o.w(0x3c)) {
        if (animStep(ctx, o, o.anim(0x3c), kAnim_webSway, 4)) o.frame = 0;   // 0x45ba32
    }
    Harry& h = ctx.harry;
    if (harryOnBox(ctx, o, -20, 20, -45, 45)) {                // 0x45ba2a: it throws Harry back
        ctx.sound1((ctx.rnd() & 0x80) ? 0xcb : 0x47, 9);
        h.b(0x465b55) = 0;
        o.facing = 0xff;
        if (int16_t(o.x - h.x()) < 0) { h.b(0x465b55) = 0xff; o.facing = 0; }
        ctx.pl.harryCall(0x427148);
        o.w(0x3c) = 1;
    }
    if (o.link && !o.link->freed) { o.link->frame = o.frame; o.link->facing = o.facing; }
    if (farFromCamera(ctx, o)) {
        ctx.pl.restoreEntry(o);
        ctx.release(o);
        if (o.link && !o.link->freed) ctx.release(*o.link);
    }
}


// ---------------------------------------------------------------- tar pit (type 40, 0x40f620)

void h_pit(ObjCtx& ctx, Obj& o) {
    if (o.b(0x54) == 0) {
        o.b(0x54) = 0xff;
        Obj* half = ctx.spawnFrom(o, true);                 // FUN_0043546e
        if (!half) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        half->flags = o.flags; half->sprite = ctx.spr("obrpit1"); half->facing = 0xff;
        std::memcpy(half->s + 0x12, o.s + 0x12, 2);
        half->w(0x26) = 0x6663;
        half->link = &o; o.link = half;
        o.w(0x5a) = int16_t((ctx.rnd() & 0x1f) + 0xc);
        o.w(0x60) = int16_t((ctx.rnd() & 0x7f) + 0x80);
    }
    if (o.b(0x36) == 0) {
        bool animate = true;
        if (o.frame == 6 && o.w(0x60)) { o.w(0x60) -= 1; animate = false; }   // stays open for a while
        if (animate) {
            if (o.w(0x5a)) o.w(0x5a) -= 1;
            const bool looped = animStep(ctx, o, o.anim(0x32), kAnim_pit, 4);
            if (o.w(0x32) == 0xc && o.w(0x5a)) o.w(0x32) = 0;             // keep bubbling until the timer runs out
            if (o.link) {
                o.link->frame = o.frame;
                if (int8_t(o.link->frame - 3) < 0) o.link->frame = 0xff;
            }
            if (looped) {
                o.w(0x5a) = int16_t((ctx.rnd() & 0x3f) + 0x40);
                o.w(0x60) = int16_t((ctx.rnd() & 0x7f) + 0x65);
            }
        }
        // 0x40f7d2: open, it draws Harry in at its edges and swallows him in the middle
        Harry& h = ctx.harry;
        const int16_t dy = int16_t(h.y() - o.y), dx = int16_t(h.x() - o.x);
        if (o.frame == 6 && int16_t(dy - 8) < 0 && int16_t(dy + 8) >= 0 && int16_t(dx - 0x3c) < 0) {
            if (int16_t(dx - 0x34) >= 0) h.x() -= 1;
            else if (int16_t(dx + 0x3c) < 0) {}
            else if (int16_t(dx + 0x34) < 0) h.x() += 1;
            else {
                o.b(0x36) = 0xff;
                h.y() = int16_t(o.y + 3);
                ctx.pl.harryCall(0x42a325);
            }
        }
    }
    if (farFromCamera(ctx, o)) {
        ctx.pl.restoreEntry(o);
        ctx.release(o);
        if (o.link) ctx.release(*o.link);
    }
}

// ---------------------------------------------------------------- thrown things (type 0x1d, 0x40d3b0)

void projectileSound(ObjCtx& ctx, Obj& o) {
    // 0x40d6b7: the crack of a thrown thing (in level 10 its own; a bone only on a free channel)
    if (ctx.levelNo == 10) { ctx.sound0(0x50, 0xb); return; }      // FUN_0042633d(0x50, 0x60)
    if (o.sprite && o.sprite->name() == "skzbone" && !ctx.channel1Busy()) ctx.sound1(0x8c, 4);
}

void projectileShatter(ObjCtx& ctx, Obj& o) {
    // 0x40d676: freed, then the stone puff from its record (FUN_004356bc hands the same one back)
    ctx.release(o);
    projectileSound(ctx, o);
    ctx.spawnEffect(o, ctx.spr("fx_stone"), kAnim_turn7, 3);       // 0x433914, script 0x4619c7
}

void h_projectile(ObjCtx& ctx, Obj& o) {
    ctx.pl.setTriggerClass(2, 0);            // 0x40d3b0: trigger id 2 does not stop thrown things
    if (o.script) {
        anim2Step(ctx, o, 0x50, 3);
        bool killed = false;
        weaponHits(ctx, o, 0, &killed);
        if (killed) { ctx.sound1(0xcd, 9); projectileShatter(ctx, o); return; }
    }
    uint8_t id = 0;
    ctx.triggerClass(o.x, o.y, &id);
    if (id == 0xd0) {                         // into water: becomes a splash drip
        o.sprite = ctx.spr("fx_drip1"); o.flags = 0xa000; o.behind = 0xff;
        o.w(0x40) = 1; o.type = 0x53;
        ctx.sound1(0xbb, 5);
        return;
    }
    if (o.b(0x68) == 0 && harryContactNow(ctx, o, 0xe)) {
        // 0x40d4d4: it breaks on Harry; in level 10 (the 0x466535 test) it also shoves him back
        if (ctx.levelNo == 10) ctx.sound0(0x50, 0xe);    // FUN_0042633d(0x50, 0x40)
        else ctx.sound1(0xcd, 9);
        ctx.spawnEffect(o, ctx.spr("fx_stone"), kAnim_turn7, 3);   // 0x433914, script 0x4619c7
        const uint8_t facing = o.facing;
        ctx.release(o);
        if (ctx.levelNo == 10) {
            ctx.harry.b(0x465b55) = uint8_t((facing & 1) ^ 1);
            ctx.pl.harryCall(0x427167);
        }
        return;
    }
    moveX(o, o.l(0x32));
    moveY(o, o.l(0x36));
    o.l(0x32) += o.l(0x3a);
    o.l(0x36) += o.l(0x3e);
    if (o.w(0x58)) { o.w(0x58) -= 1; exitDespawnFar(ctx, o); return; }
    if (o.l(0x36) >= 0) {
        if (o.w(0x6c) == 0) {
            ph::FloorHit f = ph::probeCell(ctx.det(), o.x, o.y + 0x10);
            if (f.hit) { o.y = int16_t(f.surfaceY - 0x10); projectileShatter(ctx, o); return; }
        } else {
            o.w(0x6c) -= 1;
        }
    }
    if (ctx.triggerClass(o.x, o.y) & 1) { projectileShatter(ctx, o); return; }
    exitDespawnFar(ctx, o);
}

// 0x415f4a: type 0x53, a water drop.  It forms for 0x12 ticks (b5a, creeping down 0.3 px a
// tick), then falls under 0x3800 gravity (6 px terminal); w32 ticks of grace before it
// looks for the floor (either plane), where it becomes the splash (w40 cursor).
void h_drip(ObjCtx& ctx, Obj& o) {
    if (ctx.freeze) { exitDespawnFar(ctx, o); return; }
    if (o.w(0x40) == 0) {
        if (o.frame != 1) {
            moveY(o, 0x5000);
            o.b(0x5a) += 1;
            if (o.b(0x5a) == 0x12) o.frame = 1;
            if (int8_t(o.b(0x5a) - 0x10) < 0) { exitDespawnFar(ctx, o); return; }
        }
        if (o.w(0x38) != 6) o.l(0x36) += 0x3800;
        moveY(o, o.l(0x36));
        if (o.w(0x32)) { o.w(0x32) -= 1; exitDespawnFar(ctx, o); return; }
        int y = o.y + 0xc;
        bool hit = ctx.floorSnap(o.x, y);
        o.y = int16_t(y - 0xc);
        if (!hit) { y = o.y - 6; hit = ctx.floorSnap(o.x, y, true); }
        if (!hit) { exitDespawnFar(ctx, o); return; }
        o.w(0x40) = 0x100;                                               // the splash from its first frame
        ctx.sound1(0xbb, 3);
    }
    if (animStep(ctx, o, o.anim(0x40), kAnim_splash, 3)) { ctx.release(o); return; }
    exitDespawnFar(ctx, o);
}

// 0x415e92: type 82, an unseen dripper that lets a drop (type 0x53) fall every 0x40..0x7f ticks.
void h_dripper(ObjCtx& ctx, Obj& o) {
    if (ctx.freeze) { exitDespawnTall(ctx, o); return; }
    if (o.b(0x32) == 0) { o.b(0x32) = 0xff; o.w(0x34) = int16_t((ctx.rnd() & 0x3f) + 0x40); }
    o.w(0x34) -= 1;
    if (o.w(0x34) != 0) { exitDespawnTall(ctx, o); return; }
    if (Obj* d = ctx.spawn()) {                                          // FUN_0043546e(1)
        const uint32_t r = ctx.rnd();
        d->x = int16_t(o.x + (r & 3)); d->y = int16_t(o.y - 4); d->w(0x26) = 0x6663;
        d->facing = uint8_t(r & 1);
        d->type = 0x53; d->w(0x32) = 0xa;
        d->sprite = ctx.spr("fx_drip1"); d->frame = 0; d->flags = 0x2000;
    }
    o.w(0x34) = int16_t((ctx.rnd() & 0x3f) + 0x40);
}

// ---------------------------------------------------------------- levers, doors and chutes (types 26 / 31 / 33)

// The trigger id under an object, as the level script reads it (FUN_004251f9 at (x + dx, y + dy)).
uint8_t triggerIdAt(ObjCtx& ctx, int x, int y) { return ctx.trigger(x, y).id; }   // FUN_004251f9's d3

const LevelScriptEntry* scriptByLever(uint8_t id) {
    for (const LevelScriptEntry& e : kLevelScript) if (e.lever == id) return &e;
    return nullptr;
}
const LevelScriptEntry* scriptByDoor(uint8_t id) {
    for (const LevelScriptEntry& e : kLevelScript) if (e.door == id) return &e;
    return nullptr;
}

// FUN_0041804b: a lever with trigger id `leverId` was pulled: set its flag, open the door's
// trigger cells (their class goes to 0) and tell the door / chute objects on screen.
// 0x40c4ba: Harry on the ground 0x18..0x2f px below the lever's y and within 0x22 px of it
// steps to 0x14 px from it, facing it.
bool leverReach(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    if (!h.onGround()) return false;
    const int dy = h.y() - o.y;
    if (int16_t(dy - 0x18) < 0 || int16_t(dy - 0x30) >= 0) return false;
    const int dx = h.x() - o.x;
    if (int16_t(dx + 0x22) < 0 || int16_t(dx - 0x22) >= 0) return false;
    if (int16_t(dx) < 0) { h.facing() = 0; h.x() = int16_t(o.x - 0x14); }
    else { h.facing() = 0xff; h.x() = int16_t(o.x + 0x14); }
    return true;
}

void levelScriptPull(ObjCtx& ctx, uint8_t leverId) {
    // 0x41804b (d3 = the lever's trigger id)
    const LevelScriptEntry* e = scriptByLever(leverId);
    if (!e) return;
    ctx.g.levelFlags()[e->flag] = 0xff;
    ctx.pl.clearTriggerClass(e->door);
    if (e->param == -3) { ctx.pl.clearTriggerClass(0x4d); return; }   // DAT_0046590d
    if (e->param == -4) { ctx.pl.clearTriggerClass(0x4e); return; }   // DAT_0046590e
    for (Obj* p : ctx.pl.objectSystem().list()) {
        Obj& o = *p;
        if (o.type != 0x21 && o.type != 0x1f) continue;               // chutes and doors
        uint8_t id = triggerIdAt(ctx, o.x - 0x18, o.y + 0x10);
        ctx.harry.uw(0x466782) = id;
        if (id == e->door && e->param == -2) { o.b(0x36) = 0xff; return; }
        id = triggerIdAt(ctx, o.x - 0x10, o.y + 0x18);
        ctx.harry.uw(0x466782) = id;
        if (id == e->door) {
            if (e->param == -1) { o.frame = 0xff; o.b(0x36) = 0xff; }
            else { o.w(0x34) = e->param; o.b(0x36) = 0xff; }
            return;
        }
    }
}

// 0x40c2d6: a lever (type 26).  Standing in front of it with "up" pressed pulls it, which
// runs the level script.  (The Loltun vault levers, levels 11..13, run the Simon game at
// 0x40b9e0 and are not ported.)
void h_lever(ObjCtx& ctx, Obj& o) {
    if (ctx.levelNo >= 11 && ctx.levelNo <= 13) { exitDespawnFar(ctx, o); return; }
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        const uint8_t id = triggerIdAt(ctx, o.x - 0x18, o.y + 0x18);
        if (const LevelScriptEntry* e = scriptByLever(id)) {
            o.w(0x6c) = o.y;
            int y = o.y + 0x32;
            ctx.floorSnap(o.x, y);
            o.y = int16_t(y - 0x36);
            if (ctx.g.levelFlags()[e->flag]) o.frame = 4;      // already pulled
        }
    }
    if (o.b(0x34)) { exitDespawnFar(ctx, o); return; }
    Harry& h = ctx.harry;
    // 0x40c3c7: "up" held, not pulled yet, Harry standing still (state 0x428e50 or 0x42922b,
    // the whole parts of his speeds 0)
    if (h.b(0x466765) != 0 && o.frame != 4 && (h.cont() == 0x428e50 || h.cont() == 0x42922b)
        && h.w(0x465ba6) == 0 && h.w(0x465baa) == 0 && leverReach(ctx, o)) {
        o.b(0x34) = 0xff;                                 // 0x40c42d
        levelScriptPull(ctx, triggerIdAt(ctx, o.x - 0x18, o.w(0x6c) + 0x18));
        ctx.pl.pullLever(o);
        o.facing = h.facing();
    }
    exitDespawnFar(ctx, o);
}

// 0x40d9e6: a door (type 31) that slides open once its lever has been pulled and Harry
// comes near (-112..112, -96..96): 32 ticks of sliding, then it is gone.
void h_door(ObjCtx& ctx, Obj& o) {
    if (o.w(0x34) == -1) { ctx.release(o); return; }
    if (o.b(0x6c) == 0) {
        if (o.frame == 0xfc) { o.frame = 0; o.facing = 0xff; }
        if (!(o.sprite && o.sprite->name() == "ob_door3")) {
            if (ctx.levelNo == 4 || ctx.levelNo == 9) o.sprite = ctx.spr("ob_door3");
        }
        o.b(0x6c) = 0xff;
    }
    if (o.w(0x34)) {
        if (o.b(0x67)) {
            if (o.w(0x34) == 0x40) ctx.sound0(0x16, 0x40);   // FUN_0042633d(0x16, 0x60)
            o.w(0x34) -= 2;
            if (o.facing == 0) o.x -= 1; else o.x += 1;
            moveY(o, 0x4000);
            if (o.w(0x34) == 0) { ctx.release(o); return; }
        } else if (harryOnBox(ctx, o, -112, 112, -96, 96)) {
            o.b(0x67) = 0xff;
        }
    }
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        int y = o.y + 0x22;
        ctx.floorSnap(o.x, y);
        o.y = int16_t(y - 0x20);
        const uint8_t id = triggerIdAt(ctx, o.x - 0x10, o.y - 8);
        if (const LevelScriptEntry* e = scriptByDoor(id))
            if (ctx.g.levelFlags()[e->flag]) o.w(0x34) = e->param;
    }
    exitDespawnFar(ctx, o);
}

// 0x40de86: a chute (type 33): a hatch Harry stands on while closed; once its lever has
// been pulled it opens (frames 0..5, one every 8 ticks) whenever he is near, and he drops
// through.  A mirrored second half shows the other door.
void h_chute(ObjCtx& ctx, Obj& o) {
    if (o.b(0x3c) == 0) {
        o.b(0x3c) = 0xff;
        Obj* half = ctx.spawnFrom(o, true);                    // FUN_0043546e
        if (!half) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        std::memcpy(half->s + 0x12, o.s + 0x12, 2);
        half->w(0x26) = 0x6663; half->link = &o;
        half->flags = o.flags;
        o.link = half;                                         // (+0x46)
        half->facing = 0xff; half->sprite = ctx.spr("ob_chute");
    }
    Obj* half = o.link;
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        o.frame = 0;
        const uint8_t id = triggerIdAt(ctx, o.x - 0x18, o.y + 0x10);
        if (const LevelScriptEntry* e = scriptByDoor(id))
            if (ctx.g.levelFlags()[e->flag]) o.b(0x36) = 0xff;
    }
    Harry& h = ctx.harry;
    o.b(0x34) -= 1;
    if (int8_t(o.b(0x34)) < 0) {
        o.b(0x34) = 7;
        const int16_t dy = int16_t(h.y() - o.y), dx = int16_t(h.x() - o.x);
        const bool near = int16_t(dy + 0x80) >= 0 && int16_t(dy - 0x30) < 0 && int16_t(dx + 0x80) >= 0 && int16_t(dx - 0x80) < 0;
        if (near && o.b(0x36)) {
            if (o.frame != 5) {
                if (o.frame == 0 && !ctx.channel0Busy()) ctx.sound0(0x16, 8);   // FUN_0042633d(0x16, 0x60)
                o.frame += 1;
            }
        } else if (o.frame != 0) {
            if (o.frame == 5 && !ctx.channel0Busy()) ctx.sound0(0x16, 8);
            o.frame -= 1;
        }
    }
    half->frame = o.frame;
    if (o.frame == 0 && !ctx.pl.harryOnPlatform() && !ctx.pl.harryBusy() && h.w(0x465baa) >= 0 && harryOnBox(ctx, o, -48, 48, -11, 7)) {
        // 0x40e10a: Harry stands on the closed hatch (box 0x45ba36); no landing
        h.y() = int16_t(o.y - 6);
        h.vy() = 0;
        h.b(0x465b73) = 0xff;
    }
    if (farFromCamera(ctx, o)) {
        ctx.pl.restoreEntry(o);
        ctx.release(o);
        ctx.release(*half);
        ctx.pl.restoreEntry(*half);
    }
}

// 0x42068c: a one-way door (type 173).  Open (hidden) until Harry has passed 40..144 px
// beyond it, then it slides shut and its trigger cells become a wall (class 3) for good
// (flag DAT_00466510[id - 0x90]).  b33 = template frame: 0xff mirrors the direction.
void h_oneWayDoor(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        o.b(0x33) = o.frame;
        if (ctx.levelNo == 4 || ctx.levelNo == 9) o.sprite = ctx.spr("ob_door3");
        const uint8_t id = triggerIdAt(ctx, o.x - 0x18, o.y - 0x10);
        o.b(0x32) = id;
        int y = o.y + 0x22;
        ctx.floorSnap(o.x, y);
        o.y = int16_t(y - 0x20 + 0xb);
        o.frame = 0xff;
        if (ctx.pl.triggerClassOf(id) != 0) o.frame = 0;        // already shut
    }
    if (o.frame != 0) {
        if (!between(ctx.harry.y() - o.y, -0x60, 0x60)) return;
        int d = ctx.harry.x() - o.x;
        if (o.b(0x33)) d = -d;
        if (!between(d, 0x28, 0x90)) return;
        o.frame = 0;                                             // 0x420831: shut it
        const uint8_t id = o.b(0x32);
        ctx.pl.setTriggerClass(id, 3);
        if (id >= 0x90 && id < 0xb0) ctx.g.levelFlags()[id - 0x90] = 3;
        o.x -= 0x28;
        if (o.b(0x33)) o.x += 0x50;
        o.w(0x34) = 0x28;
        ctx.sound0(0x16, 9);                          // FUN_0042633d(0x16, 0x60)
        return;
    }
    if (o.w(0x34)) {
        o.w(0x34) -= 1;
        moveY(o, -0x4000);
        o.x += 1;
        if (o.b(0x33)) o.x -= 2;
    }
}

// ---------------------------------------------------------------- gargoyle and its axes (types 61 / 62)

// 0x412b2c: a thrown axe (type 0x3e): spins, falls with gravity 0x3500, shatters on the
// floor, a wall, the whip or Harry (15 damage).
void h_axe(ObjCtx& ctx, Obj& o) {
    ctx.pl.setTriggerClass(2, 0);            // trigger id 2 does not stop thrown things
    moveX(o, o.l(0x32));
    moveY(o, o.l(0x46));
    o.l(0x46) += 0x3500;
    animStep(ctx, o, o.anim(0x36), kAnim_axeSpin, 3);
    bool shatter = false;
    ph::FloorHit f = ph::probeCell(ctx.det(), o.x, o.y + 0x12);
    if (f.hit) { o.y = int16_t(f.surfaceY - 0x12); shatter = true; }
    else if (weaponHits(ctx, o, 0, nullptr)) shatter = true;
    else if (ph::probeCell(ctx.det(), o.x, o.y).hit || (ctx.triggerClass(o.x, o.y) & 1)) shatter = true;   // 0x40d715
    else if (harryContact(ctx, o, 0xf)) shatter = true;
    if (!shatter) { exitDespawnFar(ctx, o); return; }
    ctx.sound1(0xcd, 9);
    ctx.spawnEffect(o, ctx.spr("fx_stone"), kAnim_turn7, 3);
    ctx.release(o);
}

// 0x41271a: a gargoyle statue (40 hp) that turns toward Harry and throws axes at him;
// frame 5 (the swing) hurts for 20.  The frame-0 idle only runs while Harry is within
// -96..+144 px vertically.
void h_gargoyle(ObjCtx& ctx, Obj& o) {
    bool killed = false;
    if (weaponHits(ctx, o, 0x28, &killed) && killed) { killObject(ctx, o); return; }   // (0x412987: the sling-stone chip is not ported)
    if (ctx.freeze) { freezeSparkle(ctx, o); return; }
    if (o.frame == 5) harryContact(ctx, o, 0x14);
    if (o.frame == 0 && !between(ctx.harry.y() - o.y, -0x60, 0x90)) { exitDespawnFar(ctx, o); return; }
    if (animStep(ctx, o, o.anim(0x36), kAnim_gargoyle, 6)) o.b(0x64) ^= 1;
    const uint8_t was = o.facing;
    AnimCursor& cur = o.anim(0x36);
    if (cur.tick == 0 && cur.pos < 0xc) {
        if (ctx.harry.vx() == 0 && between(ctx.harry.x() - o.x, -0x18, 0x18)) o.facing = o.b(0x64);
        else o.facing = ctx.harry.x() >= o.x ? 1 : 0;
    }
    if (o.facing != was) { cur.tick = 0; cur.pos = 0xc; }   // skip straight to the throw
    if (cur.tick == 0 && cur.pos == 0x13) {                 // 0x412891: release the axe
        if (Obj* a = ctx.spawn()) {
            a->x = o.x; a->y = int16_t(o.y + 10); a->w(0x26) = 0x6663;
            a->sprite = ctx.spr("obzaxe"); a->type = 0x3e;
            const int d = ctx.harry.x() - o.x;
            if (between(d, -0x50, 0x50)) { a->l(0x32) = -0x40000; a->l(0x46) = 0x18000; }
            else if (between(d, -0xa0, 0xa0)) { a->l(0x32) = -0x60000; a->l(0x46) = 0; }
            else { a->l(0x32) = -0x61000; a->l(0x46) = -0x30000; }
            a->flags = 0x4000; a->w(0x36) = 0;
            a->y -= 4; a->x -= 0x21;
            a->facing = o.facing;
            if (o.facing) { a->x += 0x42; a->l(0x32) = -a->l(0x32); }
        }
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- tongue plant (type 91, 0x417a54)

// A plant that opens when Harry comes near and throws him up (vy -12, somersault) when he
// lands on it.  Cursors: w50 the bounce animation, w34 opening, w36 closing, w38 unused;
// a cursor at position 0 means "not playing", so the scripts are started at position 1.
void h_tonguePlant(ObjCtx& ctx, Obj& o) {
    // 0x417a54
    o.b(0x6d) -= 1;                                    // every 16 ticks to the end of the object list
    if (int8_t(o.b(0x6d)) < 0) { o.b(0x6d) = 0x10; if (!ctx.pl.objectSystem().moveToTail(o)) return; }   // 0x435532
    if (ctx.boss) { exitDespawnFar(ctx, o); return; }
    Harry& h = ctx.harry;
    if (h.w(0x465baa) >= 0 && harryOnBox(ctx, o, -16, 19, 0, 30)) {   // 0x417c6d, box 0x45bccd: a somersault
        h.onGround() = 0;
        h.setCont(0x42cdb6);
        h.setSprite("hyisomr2");
        h.vy() = int32_t(0xfff40000);
        ctx.sound1((ctx.rnd() & 0x80) ? 0xca : 0x46, 9);
        h.frame() = 0;
        h.w(0x465b47) = 0;
        o.w(0x50) = 1; o.w(0x34) = 0; o.l(0x36) = 0;
    }
    if (o.w(0x50)) { if (!animStep(ctx, o, o.anim(0x50), kAnim_tongueBounce, 6)) { exitDespawnFar(ctx, o); return; } o.frame = 0; o.b(0x32) = 0; }
    if (o.w(0x34)) { if (!animStep(ctx, o, o.anim(0x34), kAnim_turn4, 4)) { exitDespawnFar(ctx, o); return; } o.frame = 3; }
    if (o.w(0x36)) { if (!animStep(ctx, o, o.anim(0x36), kAnim_tongueClose, 4)) { exitDespawnFar(ctx, o); return; } o.frame = 0; }
    // (0x45bcc3, the same bytes as 0x45bcc8)
    if (o.w(0x38)) { if (!animStep(ctx, o, o.anim(0x38), kAnim_tongueBounce, 4)) { exitDespawnFar(ctx, o); return; } o.frame = 0; }
    halfMinuteTick(o);                                 // 0x4057b0
    const int16_t dx = int16_t(o.x - h.x()), dy = int16_t(o.y - h.y());
    const bool nearX = int16_t(dx + 0x50) >= 0 && int16_t(dx - 0x50) < 0;
    if (o.b(0x32)) {
        if (!(nearX && int16_t(dy + 0xa0) >= 0 && int16_t(dy - 0xa0) < 0)) { o.w(0x36) = 1; o.w(0x34) = 0; o.b(0x32) = 0; }
    } else if (nearX && int16_t(dy + 0x80) >= 0 && int16_t(dy - 0x80) < 0) {
        o.b(0x32) = 0xff; o.w(0x34) = 1; o.w(0x36) = 0;
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- torches and pit creatures (types 46 / 59)

// 0x4100a0: a wall torch flickering through frames 0..4.
void h_torch(ObjCtx& ctx, Obj& o) {
    animStep(ctx, o, o.anim(0x32), kAnim_sparkle2, 4);
    exitDespawnFar(ctx, o);
}

// 0x412444: something surfaces from a temple pit at a random spot within 32 px of its
// home (obzpit2a with the 10-frame script, or obzpit2b with the condor-turn frames),
// pauses 8..15 ticks hidden, and surfaces again.  It never touches Harry.
void h_pitCreature(ObjCtx& ctx, Obj& o) {
    auto reposition = [&] {                       // 0x412525
        const int r = int(ctx.rnd() & 0x3f) - 0x20;
        o.x = int16_t(o.w(0x34) + r);
        o.facing = uint8_t(r & 1);
        const uint32_t v = ctx.rnd();
        o.b(0x33) = uint8_t((v & 7) + 8);
        if (v & 1) { o.sprite = ctx.spr("obzpit2b"); o.script = kAnim_condorTurn; }
        else { o.sprite = ctx.spr("obzpit2a"); o.script = kAnim_run10; }
    };
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        o.y -= 4;
        o.w(0x34) = o.x;
        o.y += 0x17;
        reposition();
        exitDespawnFar(ctx, o);
        return;
    }
    if (o.b(0x33)) { o.b(0x33) -= 1; exitDespawnFar(ctx, o); return; }
    if (o.frame == 0xff && !ctx.channel0Busy()) {     // 0x41248b: now and then a bubble
        const uint32_t r = ctx.rnd();
        if ((r & 2) == 0) ctx.sound0(0x14 + int(r & 1), 0);   // FUN_0042633d
    }
    if (animStep(ctx, o, o.anim(0x36), o.script, o.script == kAnim_run10 ? 4 : 3)) {
        o.frame = 0xff;
        reposition();
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- spikes and fire (types 51 / 97 / 100)

// 0x418474: ceiling spikes (type 97, drawn V-flipped) that slide down while Harry is
// underneath (box -176..176 x 5..135 below), 15 damage when out, a sparkle when fully out.
constexpr uint8_t kSpikeFrames[4] = {0, 3, 2, 1};                            // 0x45bcd6

void h_ceilingSpikes(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) { o.b(0x32) = 0xff; o.vflip = 0xff; o.behind = 0xff; }
    bool extend = false, retract = false;
    if (o.w(0x5c)) { o.w(0x5c) -= 1; retract = true; }
    else {
        o.b(0x34) -= 1;
        if (int8_t(o.b(0x34)) < 0) {                          // every third tick
            o.b(0x34) = 2;
            if (harryOnBox(ctx, o, -176, 176, 5, 135)) {
                o.w(0x5a) += 1;
                if (o.w(0x5a) >= 0x70) { o.w(0x5a) = 0; o.w(0x5c) = 0x60; }
                extend = true;
            } else retract = true;
        }
    }
    if (extend && o.b(0x3c) != 3) {
        if (o.b(0x3c) == 0) ctx.sound1(0xc6, 8);              // (0x4184fd tests b33, always 0 here)
        o.b(0x3c) += 1;
        if (o.b(0x3c) == 3) {
            if (Obj* s = ctx.spawn()) {                       // FUN_004356bc(1): a sparkle at the tips
                s->x = int16_t(o.x - 1); s->y = int16_t(o.y + 0x19);
                s->type = 0x10; s->flags = 0x6000; s->b(0x64) = 0xff; s->sprite = ctx.spr("fx_sprk2"); s->w(0x26) = 0x6663;
            }
        }
    } else if (retract && o.b(0x3c)) {
        o.b(0x3c) -= 1;
    }
    o.frame = kSpikeFrames[o.b(0x3c) & 3];
    if (o.frame == 1) harryContact(ctx, o, 0xf);
    exitDespawnFar(ctx, o);
}

// 0x41061a: floor spikes (type 51): three pieces (ob_spik2 left / ob_spik1 centre / ob_spik2
// right) that shoot up when Harry comes within 176 px (95 above .. 55 below) and sink back
// after 0x2c steps; 15 damage while out.
constexpr uint8_t kFloorSpikeFrames[4][3] = {{0, 0, 0}, {1, 3, 1}, {3, 2, 3}, {2, 1, 2}};   // 0x45bab8

void h_floorSpikes(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        Obj* parts[3] = {nullptr, nullptr, nullptr};
        for (int i = 0; i < 3; ++i) {
            parts[i] = ctx.spawn();
            if (!parts[i]) { for (int k = 0; k < i; ++k) ctx.release(*parts[k]); ctx.pl.restoreEntry(o); ctx.release(o); return; }
            parts[i]->x = o.x; parts[i]->y = o.y; parts[i]->frame = 0; parts[i]->type = 0;
            parts[i]->sprite = ctx.spr(i == 1 ? "ob_spik1" : "ob_spik2"); parts[i]->flags = 0x4000;
            parts[i]->w(0x26) = 0x6663;
        }
        parts[0]->x -= 4; parts[0]->y += 6; parts[0]->facing = 0xff; parts[0]->behind = 0xff;
        parts[2]->x += 4; parts[2]->y -= 6;
        o.link = parts[0]; o.link2 = parts[1]; o.link3 = parts[2];
        exitDespawnFar(ctx, o);
        return;
    }
    Obj* parts[3] = {o.link, o.link2, o.link3};
    for (Obj* p : parts) if (!p || p->freed) { ctx.release(o); return; }
    o.b(0x34) -= 1;
    if (int8_t(o.b(0x34)) < 0) {
        o.b(0x34) = 2;
        bool extend = false;
        if (o.w(0x5c)) { o.w(0x5c) -= 1; }
        else if (harryOnBox(ctx, o, -176, 176, -95, 55)) {
            o.w(0x5a) += 1;
            if (o.w(0x5a) >= 0x2c) { o.w(0x5a) = 0; o.w(0x5c) = 0x1c; }
            extend = true;
        }
        if (extend) {
            if (o.b(0x33) != 3) {
                if (o.b(0x33) == 0) ctx.sound1(0xc6, 8);
                o.b(0x33) += 1;
                if (o.b(0x33) == 3) {
                    if (Obj* s = ctx.spawn()) {
                        s->x = int16_t(o.x - 1); s->y = int16_t(o.y - 0x1a);
                        s->type = 0x10; s->flags = 0x6000; s->b(0x64) = 0xff; s->sprite = ctx.spr("fx_sprk2"); s->w(0x26) = 0x6663;
                    }
                }
            }
        } else if (o.b(0x33)) {
            o.b(0x33) -= 1;
        }
    }
    if (o.b(0x33) == 3) harryContact(ctx, o, 0xf);
    for (int i = 0; i < 3; ++i) parts[i]->frame = kFloorSpikeFrames[o.b(0x33) & 3][i];
    if (farFromCamera(ctx, o)) {                                // 0x4108dc
        for (Obj* p : parts) ctx.release(*p);
        ctx.pl.restoreEntry(o);
        ctx.release(o);
    }
}

// 0x418834: a fire jet (type 100): frames 6..9 burn for 25.
void h_fireJet(ObjCtx& ctx, Obj& o) {
    animStep(ctx, o, o.anim(0x32), kAnim_fireJet, 5);
    if (o.anim(0x32).tick == 0 && o.frame == 3) {
        o.b(0x46) += 1;
        if (o.b(0x46) >= 6) { o.b(0x46) = 0; ctx.sound0(0x27, 5); }   // FUN_0042633d(0x27, 0x60)
    }
    if (o.frame >= 6 && o.frame < 10) harryContact(ctx, o, 0x19);          // (DAT_0046676d, the mine-cart burn signal, is not ported)
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- clay pot (type 38, 0x40ed62)

// Spawn an object from an .als template at the pot's position (FUN_004351c5).
Obj* spawnFromTemplate(ObjCtx& ctx, const Obj& at, int templateId) {
    Obj* p = ctx.spawn();
    if (!p) return nullptr;
    const ph::ObjectTemplate& t = ph::kObjectTemplates[templateId];
    p->x = at.x; p->y = at.y;
    p->type = t.type; p->frame = t.sub; p->flags = t.flags;
    p->sprite = ctx.spr(t.sprite);
    p->w(0x26) = 0x6663;
    return p;
}

// A breakable pot.  The trigger under it (ids 0x34..) says what is inside; otherwise the
// contents are random.  A whip hit breaks it (fxzpots2 shards) and lets the contents out:
// 1 heart, 2 an explosion, 3 a bat, 4 a beating heart, 5 a stone, 6 a pouch, 7 a rat,
// 8 a treasure, 9 bat or pouch, 10 a 1-up.
void h_pot(ObjCtx& ctx, Obj& o) {
    if (o.b(0x50) == 0) {
        o.b(0x50) = 0xff;
        const int tx = o.x - 0x18 + 0x20, ty = o.y + 0x18;
        if (tx >= 0 && ty >= 0)
            if (const ph::ColumnEntry* e = ctx.lv.triggers.find(tx >> 4, ty >> 4))
                if (e->id >= 0x34) o.b(0x51) = uint8_t(e->id - 0x34);
        if (o.frame == 0xff) { o.frame = 0; o.y += 0x14; o.b(0x59) = 0xff; }   // hanging upside down
        else {
            o.y += 7;
            int y = o.y + 0x19;
            ctx.floorSnap(o.x, y);
            o.y = int16_t(y - 0x1c);
        }
    }
    if (o.b(0x32)) {                              // 0x40ee20: broken
        o.sprite = ctx.spr("fxzpots2"); o.flags = 0x6000;
        if (animStep(ctx, o, o.anim(0x36), kAnim_potShards, 4)) ctx.release(o);
        return;
    }
    if (o.b(0x6c) == 0 && !weaponHits(ctx, o, 0, nullptr)) { exitDespawnFar(ctx, o); return; }
    if (o.b(0x51) == 0) {
        uint32_t r;
        do { r = ctx.rnd() & 7; } while (r == 2 || r >= 5);
        o.b(0x51) = uint8_t(r + 1);
    }
    int kind = o.b(0x51);
    if (kind == 9) kind = (ctx.rnd() & 1) ? 6 : 3;
    auto popOut = [&](int templateId, bool pouchDrop) {
        if (Obj* p = spawnFromTemplate(ctx, o, templateId)) {
            p->w(0x68) = 8; p->b(0x6a) = 0xff;
            if (pouchDrop) { p->type = 0x9c; p->l(0x32) = 1; }   // falls as a tree pouch would
        }
    };
    switch (kind) {
    case 1: popOut(0x40, false); break;                                       // fx_hrtbt item
    case 2: {                                                                 // 0x40ef07: a bomb
        ctx.sound0(0x26, 0xf);                        // FUN_0042633d(0x26, 0x60)
        o.type = 0x32; o.l(0x56) = 0; o.flags = 0x6000; o.sprite = ctx.spr("fxzexpl2");
        o.frame = 0xff; o.l(0x3c) = 0; o.facing = 0; o.b(0x32) = 0;
        if (Obj* h = ctx.spawn()) {
            o.link = h; h->link = &o;
            h->behind = 0xff; h->w(0x26) = 0x6663; h->type = 0;
            h->sprite = ctx.spr("fxzexpl2"); h->flags = 0x6000; h->frame = 0xff; h->facing = 0xff;
            h->x = o.x; h->y = o.y;
        }
        return;
    }
    case 3: if (Obj* b = spawnFromTemplate(ctx, o, 0x70)) { b->whipCool = 0x14; b->b(0x32) = 1; } break;   // a bat
    case 4: popOut(0x51, false); break;                                       // beating heart (type 142)
    case 5: popOut(0x47, false); break;                                       // mi_stone
    case 6: popOut(0x07, true); break;                                        // mi_pouc2
    case 7: if (Obj* r = spawnFromTemplate(ctx, o, 0x7a)) { r->whipCool = 0x14; r->y += 6; } break;        // a rat
    case 8: {                                                                 // 0x40f10c: a treasure
        const uint32_t r = ctx.rnd() & 0x1f;
        const int id = r < 0x10 ? 0x5a : r < 0x16 ? 0x5b : r < 0x1a ? 0x58 : r < 0x1e ? 0x5c : 0x5a;
        popOut(id, id == 0x5b || id == 0x58);
        break;
    }
    case 10: popOut(0x59, true); break;                                       // mi_1up
    default: break;
    }
    // 0x40f211: the pot itself breaks
    ctx.sound1(0xba, 0xa);
    if (o.sprite && o.sprite->name() == "fxzpots3") o.vflip = 0xff;
    o.facing = uint8_t(ctx.rnd() & 1);
    o.vflip = o.b(0x59);
    o.b(0x32) = 0xff;
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- temple piston (type 30, 0x40d77a)

// A stone block that rises out of the floor and sinks back on a 400-tick cycle shared by
// every piston (DAT_0046650d steps every 5 ticks); the trigger under it (ids 0xa0..0xa7,
// class 0xf5) picks its phase offset.  Fully up (frame 3) it carries Harry.
constexpr uint8_t kPistonPhase[8] = {70, 60, 50, 40, 30, 20, 10, 0};        // 0x45b9c0
constexpr uint8_t kPistonFrames[0x50] = {                                   // 0x45b9d0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void h_piston(ObjCtx& ctx, Obj& o) {
    // 0x40d77a
    o.b(0x6d) -= 1;                                    // every 32 ticks to the end of the object list
    if (int8_t(o.b(0x6d)) < 0) { o.b(0x6d) = 0x20; if (!ctx.pl.objectSystem().moveToTail(o)) return; }   // 0x435532
    if (o.b(0x46) == 0) {
        o.b(0x46) = 0xff;
        const Trg t = ctx.trigger(o.x - 0x18, o.y + 0x18);
        o.x -= 0x20;
        if (t.raw == 0xf5) o.w(0x48) = kPistonPhase[(t.id - 0xa0) & 7];
    }
    Harry& h = ctx.harry;
    int16_t d0 = int16_t(h.b(0x46650d) + o.w(0x48));
    if (int16_t(d0 - 0x50) >= 0) d0 = int16_t(d0 - 0x50);
    o.frame = kPistonFrames[uint16_t(d0) % 0x50];
    if (h.b(0x46650e) == 3 && uint8_t(d0) == 0x10) {
        const int16_t dx = int16_t(o.x - h.x()), dy = int16_t(o.y - h.y());
        if (int16_t(dx + 0xa0) >= 0 && int16_t(dx - 0xa0) < 0 && int16_t(dy + 0xa0) >= 0 && int16_t(dy - 0xa0) < 0)
            ctx.sound0(0x16, 6);                                          // FUN_0042633d(0x16, 0x60)
    }
    if (o.frame == 3 && !ctx.pl.harryOnPlatform() && !ctx.pl.harryBusy() && h.w(0x465baa) >= 0 && harryOnBox(ctx, o, -10, 38, -2, 15))
        ctx.pl.standOn(o.y + 6, true);                                    // 0x40d96a (box 0x45b9c8)
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- skulls (types 116 / 117)

// 0x41a9fa: a skull lying on the floor, chattering; Harry stumbles over it when he runs
// into it at speed (hyitrip1).  Faces by column parity.
void h_floorSkull(ObjCtx& ctx, Obj& o) {
    { int y = o.y + 4; ctx.floorSnap(o.x, y, true); o.y = int16_t(y - 4); }   // FUN_00425f88 (.dt2)
    o.facing = uint8_t((o.x >> 4) & 1);
    animStep(ctx, o, o.anim(0x34), kAnim_boneIdle, 4);
    // 0x41aa79: running at full speed (the run script 0x46166b, DAT_00465b9a >= 0x14) into it
    Harry& h = ctx.harry;
    if (h.ul(0x465ba0) == 0x46166b && int16_t(h.w(0x465b9a) - 0x14) >= 0 && ctx.touchesHarry(o)) {
        h.w(0x465b47) = 0;
        h.setSprite("hyitrip1");
        h.setUl(0x465ba0, 0x46168e);                  // the stumbling script
        ctx.pl.harryCall(0x42e9b2);
    }
    exitDespawnFar(ctx, o);
}

// 0x41aade: a hopping skull.  It chatters for a random while, then springs up and drifts
// back toward where it started; a whip hit or a bite on Harry (30 damage) shatters it
// into pot shards (the type 0x26 tail at 0x40ee20).
void h_hoppingSkull(ObjCtx& ctx, Obj& o) {
    auto driftHome = [&] {                        // 0x41ac1b / 0x41ad3e: maybe 0.75 px/tick toward home
        o.l(0x5a) = 0;
        if (ctx.rnd() & 1) o.l(0x5a) = o.w(0x60) >= o.x ? 0xc000 : -0xc000;
    };
    if (o.w(0x60) == 0) {
        { int y = o.y + 0xd; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0xd); }
        o.script = kAnim_skullChatter;
        o.w(0x60) = o.x;
    }
    if (o.l(0x5a) != 0) o.facing = o.l(0x5a) >= 0 ? 0xff : 0;
    if (o.b(0x46)) {                              // in the air
        o.sprite = ctx.spr("skiroll");
        animStep(ctx, o, o.anim(0x34), kAnim_skullRoll, 3);
        moveX(o, o.l(0x5a));
        o.l(0x3c) += 0x4000;
        moveY(o, o.l(0x3c));
        if (o.y >= o.w(0x40)) {                   // landed
            ctx.sound0(0x1e, 3);                  // FUN_0042633d(0x1e, 0x60)
            o.y = o.w(0x40);
            o.b(0x46) = 0;
            driftHome();
            o.w(0x32) = int16_t((ctx.rnd() & 0x3f) + 0x28);
            o.script = kAnim_skullChatter; o.w(0x34) = 0;
            o.b(0x64) -= 1;
            if (int8_t(o.b(0x64)) < 0) { o.b(0x64) = 2; o.w(0x32) = 6; o.script = kAnim_skullQuiet; }
            else ctx.sound1(0xcf, 2);
        }
    } else {                                      // 0x41acb7: on the ground
        o.sprite = ctx.spr("skijaw");
        animStep(ctx, o, o.anim(0x34), o.script ? o.script : kAnim_skullChatter, 4);
        if (o.frame != 0) moveX(o, o.l(0x5a));
        o.w(0x32) -= 1;
        if (o.w(0x32) < 0) {                      // spring
            o.b(0x46) = 0xff;
            o.l(0x3c) = -0x3e000;
            ctx.sound1(0x45, 2);
            const uint32_t r = ctx.rnd();
            o.w(0x3e) -= int16_t(r & 3);          // a little higher at random
            o.w(0x40) = o.y;
            o.l(0x5a) = 0;
            if (r & 1) o.l(0x5a) = o.w(0x60) >= o.x ? 0xc000 : -0xc000;
            o.w(0x34) = 0;
        }
    }
    // 0x41ada3: a whip hit (100 points) or a bite (30 damage) both end it
    bool shatter = false;
    if (weaponHits(ctx, o, 0, nullptr)) { ctx.g.stats().score += 100; shatter = true; }
    else if (ctx.touchesHarry(o)) { ctx.hurtHarry(0x1e); shatter = true; }
    else { exitDespawnFar(ctx, o); return; }
    if (shatter) {                                // 0x41ade1: becomes a breaking pot (type 0x26)
        ctx.sound(0x42);
        o.sprite = ctx.spr("fxzpots2"); o.flags = 0x6000;
        o.script = kAnim_potShards; o.b(0x46) = 4; o.anim(0x32) = AnimCursor{};
        o.type = 0x68;
    }
}

// ---------------------------------------------------------------- pouch respawner (type 74, 0x415028)

// An invisible marker that keeps a pouch of sling stones in stock: while Harry has fewer
// than 10 stones (DAT_00466528) and the marker sits just outside the screen, it spawns one
// from template 7 (mi_pouc2) at its own position.  The pouch has no .als entry, so it comes
// back every time; l50 remembers it until it is picked up.
void h_pouchRespawner(ObjCtx& ctx, Obj& o) {
    if (!o.link) {
        const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
        const bool onScreen = dx >= -0x20 && dx < 0x160 && dy >= -0x20 && dy < 0xe8;   // 0x424f40
        if (!onScreen && ctx.g.stats().ammoCount[0] < 10) {
            if (Obj* p = ctx.spawn()) {                   // FUN_004351c5 from the template at 0x467fe4
                const ph::ObjectTemplate& t = ph::kObjectTemplates[7];
                p->x = o.x; p->y = o.y;
                p->type = t.type; p->frame = t.sub; p->flags = t.flags;
                p->sprite = ctx.spr(t.sprite);
                p->w(0x26) = 0x6663;
                o.link = p;
            }
        }
    } else if (o.link->freed || o.link->type != 2) {
        o.link = nullptr;
    }
    if (!o.link) exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- bungee vine (type 27, 0x40c5fe)
//
// A vine Harry grabs in mid-air and bounces on.  The record is the end Harry hangs from
// (its x / y are Harry's while he hangs).  It owns a top piece (l50, `ob_bunge` 0), a
// second piece whose frame shows the stretch (l54, `ob_bunge` 1..3) and whose state area
// +0x32.. lists the records of the rope segments drawn every 32 px down to the end
// (`parts` here), and a stub (l6a, `ob_bungs`) that sways while nobody hangs on it.
// State: w38 rest y, l34 velocity (w36 its integer part), b32 phase (0 stretching, 1 / 2
// rebounding, 6 recoiling after a release), b3a re-grab cooldown, l5a Harry's launch speed,
// w5e leaf timer, b66 "bounced once", b67 "rose fast", l42 / l4c / b48 / b49 the cursor over
// Harry's bungee animation: {ticks, op, frame} triples at 0x45aaa4.. (original addresses):
// op 0 hold, f3 pause the random jumps, f7 flip Harry, 0X maybe jump to script X, 6X jump
// to script X, 4X Harry's sprite X, anything else makes script X the base.

namespace bungee {
// 0x45aa54: the scripts by index; 0x45aa88: Harry's sprite slots by index.
constexpr uint32_t kScripts[13] = {0, 0x45aaad, 0x45ab0d, 0x45aaa4, 0x45aadd, 0x45aabf, 0x45ab37, 0x45aab6,
                                   0x45ab46, 0x45ab13, 0x45aacb, 0x45ab58, 0x45ab4c};
constexpr uint32_t kSprites[7] = {0x4700b8, 0x4700cc, 0x4700c8, 0x4700c0, 0x4700d0, 0x4700bc, 0x4700b4};

uint8_t mb(uint32_t a) {                        // a byte of the triples (0x45aaa4 .. 0x45ab66)
    return a >= 0x45aaa4 && a < 0x45aaa4 + 0xc3 ? kData_bungeeScripts[a - 0x45aaa4] : 0;
}

// 0x40d2ef: Harry hangs on this vine (state 0x429fe2, DAT_00465b4d).
bool harryOn(ObjCtx& ctx, const Obj& o) {
    return ctx.harry.cont() == 0x429fe2 && ctx.harry.ul(0x465b4d) == ctx.pl.addrOf(&o);
}

// 0x40c8d1: the triple at a0 gives Harry's frame, then its op runs.
void triple(ObjCtx& ctx, Obj& o, uint32_t a0) {
    Harry& h = ctx.harry;
    for (int guard = 0; guard < 32; ++guard) {
        h.frame() = mb(a0 + 2);
        const uint8_t op = mb(a0 + 1);
        if (op == 0) return;
        if (op == 0xf3) { o.b(0x49) = 2; return; }
        if (op == 0xf7) { h.facing() = uint8_t((h.facing() & 1) ^ 1); return; }
        const uint8_t hi = op & 0xf0, x = op & 0xf;
        if (hi == 0) {
            if (o.b(0x49)) { o.b(0x49) -= 1; return; }
            if ((ctx.rnd() & 3) == 0) return;
        } else if (hi == 0x40) {
            h.setSpriteSlot(x < 7 ? kSprites[x] : 0);
            return;
        } else if (hi != 0x60) {
            o.l(0x42) = int32_t(x < 13 ? kScripts[x] : 0);
            return;
        }
        // 0x40c966: jump to script x
        o.l(0x4c) = int32_t(x < 13 ? kScripts[x] : 0);
        o.b(0x48) = 0;
        a0 = uint32_t(o.l(0x4c));
    }
}

void restart(ObjCtx& ctx, Obj& o) {           // 0x40c84e
    o.b(0x32) = 0; o.l(0x34) = 0; o.b(0x48) = 0;
    o.l(0x4c) = o.l(0x42);
    triple(ctx, o, uint32_t(o.l(0x42)));
}

void spawnLeaf(ObjCtx& ctx, Obj& o) {         // 0x40cb29
    Obj* l = ctx.spawnFrom(o, true);           // FUN_0043546e
    if (!l) return;
    l->w(0x4e) = 0x220;
    l->y = o.w(0x38);
    l->x = o.x;
    l->y -= 6;
    l->x = int16_t(l->x + int(ctx.rnd() & 0x1f) - 0x10);
    l->w(0x58) = 0x70; l->w(0x64) = 0x50;
    l->flags = 0; l->type = 0x40;
    const uint32_t r = ctx.rnd() & 0x7fff;
    l->l(0x36) = int32_t(r);
    if (r & 1) { l->sprite = ctx.spr("fx_leaf1"); l->script = kAnim_feather; l->l(0x36) += 0x6000; }   // 0x4619df
    else { l->sprite = ctx.spr("fxzleaf2"); l->script = harryMem(0x4619e8); l->l(0x36) += 0x10000; }
}

void teardown(ObjCtx& ctx, Obj& o) {          // 0x40d21f (Harry is not let go here)
    if (o.link3) ctx.release(*o.link3);
    ctx.pl.restoreEntry(o);
    if (o.link2) for (Obj* p : o.link2->parts) ctx.release(*p);
    ctx.release(o);
    if (o.link2) ctx.release(*o.link2);
    if (o.link) ctx.release(*o.link);
}
} // namespace bungee

void h_bungee(ObjCtx& ctx, Obj& o) {
    using namespace bungee;
    Harry& h = ctx.harry;
    if (o.frame == 0xfe) {
        // set up: the top and the stretch piece at the tail of the list, the stub at the head
        o.l(0x5a) = int32_t(0xfffcc000); o.b(0x49) = 1; o.frame = 6;
        o.w(0x38) = o.y; o.w(0x5e) = 0xa;
        Obj* top = ctx.spawnFrom(o, true);
        if (!top) { ctx.release(o); return; }
        top->sprite = ctx.spr("ob_bunge"); top->frame = 0; top->y -= 0x1d; top->flags = 0x6000;
        o.link = top;
        Obj* body = ctx.spawnFrom(o, true);
        if (!body) { ctx.release(o); ctx.release(*top); return; }   // 0x40c6f0 (the .als entry stays cleared)
        body->w(0x26) = 0x6663;
        body->sprite = ctx.spr("ob_bunge"); body->frame = 1; body->y -= 0x1d; body->flags = 0x6000;
        o.link2 = body;
        if (Obj* stub = ctx.spawnFrom(o)) {
            stub->w(0x26) = 0x6663;
            stub->sprite = ctx.spr("ob_bungs"); stub->frame = 0xff;
            stub->x += 1; stub->y -= 0x10; stub->flags = 0x6000;
            stub->w(0x50) = stub->x;
            o.link3 = stub;
        }
    }
    Obj* top = o.link; Obj* body = o.link2; Obj* stub = o.link3;
    if (!top || !body) { teardown(ctx, o); return; }

    bool physics = true;
    if (o.y == o.w(0x38)) {                       // 0x40c70d: hanging still
        physics = false;
        if (o.b(0x3a)) {
            o.b(0x3a) -= 1;
        } else if (h.ul(0x465b7e) == 0 && !h.onGround() && h.vy() >= 0 && ctx.touchesHarry(o)) {
            if (!harryOn(ctx, o)) {
                // 0x40c784: Harry catches the end
                h.b(0x465b46) = 0;
                o.y += 1;
                h.setCont(0x429fe2);
                ctx.sound1(0x4d, 7);
                o.b(0x66) = 0;
                h.setUl(0x465b4d, ctx.pl.addrOf(&o));
                uint32_t r;
                do { r = ctx.rnd() & 3; } while (r == 3);
                if (r == 0) { o.l(0x42) = 0x45aadd; h.setSprite("hyibung1"); }
                else if (r == 1) { o.l(0x42) = 0x45ab0d; h.setSprite("hyibung2"); }
                else { o.l(0x42) = 0x45ab37; h.setSprite("hyibung3"); }
                h.frame() = 0;
                h.setUl(0x465b7e, ctx.pl.addrOf(&o));
            }
            restart(ctx, o);
            physics = true;
        }
    } else if (harryOn(ctx, o)) {                 // 0x40c873: stretched, with Harry on it
        h.b(0x465b73) = 0xff;
        o.b(0x48) += 1;
        const uint32_t a0 = uint32_t(o.l(0x4c));
        if (o.b(0x48) != mb(a0)) {
            h.frame() = mb(a0 + 2);
        } else {
            o.b(0x48) = 0;
            o.l(0x4c) += 3;
            triple(ctx, o, a0 + 3);
        }
    }

    for (int guard = 0; physics && guard < 64; ++guard) {   // 0x40ca97
        bool release = false;
        if (o.b(0x32) == 6) {
            o.l(0x34) -= 0x9000;                  // recoiling up after a release
        } else if (o.b(0x32) == 0) {              // stretching down
            o.l(0x34) += 0x3700;
            if (o.l(0x34) == 0x22600) ctx.sound1(0xc9, 0xa);
            if (int16_t(o.w(0x36) - 4) < 0) {
                o.w(0x5e) -= 1;
                if (o.w(0x5e) == 0) { o.w(0x5e) = int16_t((ctx.rnd() & 7) + 7); spawnLeaf(ctx, o); }
            }
            if (harryOn(ctx, o) && h.b(0x46676a) == 1) {
                h.w(0x465b47) = 0;
                h.b(0x46676a) = 0x80;
                const int16_t d0 = int16_t(o.y - o.w(0x38) + 0x10);
                if (o.b(0x66) && int16_t(d0 - 0x18) < 0) {        // a second pass near the top: the big jump
                    h.vy() = int32_t(0xfff3e000);
                    h.setCont(0x42c3bc);
                    ctx.sound0(0xc7, 8);                          // FUN_004262d9
                } else {                                          // let go on the way down: fall with the vine's speed
                    h.vy() = o.l(0x34);
                    h.w(0x465baa) += 1;
                    if (int16_t(h.w(0x465baa) - 6) >= 0) h.vy() = 0x60000;
                    h.w(0x465b47) = 0;
                    h.setCont(0x42c2cc);
                }
                release = true;
            } else {
                o.l(0x5a) = int32_t(0xfffc0000);
                if (o.w(0x36) == 7) { o.b(0x32) = 1; o.b(0x67) = 0; }
            }
        } else {                                  // 0x40ccfd: rebounding
            o.b(0x66) = 0xff;
            int path = 0;                         // 1: 0x40cda0, 2: 0x40cdf2, 0: neither
            if (o.b(0x32) != 1) {
                if (o.w(0x36) != -1) o.l(0x34) += 0x7400;
                path = 1;
            } else {
                if (int16_t(o.w(0x36) + 4) < 0 && o.b(0x67) == 0) o.b(0x67) = 0xff;
                o.l(0x34) -= 0x1100;
                if (int16_t(o.w(0x36) - 1) < 0 && o.l(0x34) >= -0xc000) {
                    path = 2;
                } else {
                    o.l(0x34) -= 0x7100;
                    if (int16_t(o.w(0x36) + 9) < 0) {
                        o.b(0x32) = 2;
                        if (o.w(0x36) != -1) o.l(0x34) += 0x7400;
                        path = 1;
                    } else {
                        if (o.l(0x5a) >= int32_t(0xfff24000)) o.l(0x5a) -= 0x4c00;
                        path = int16_t(o.w(0x36) - 1) >= 0 ? 0 : 1;
                    }
                }
            }
            if (path != 0 && harryOn(ctx, o) && h.b(0x46676a) == 1) {
                h.b(0x46676a) = 0x80;
                if (path == 1) {                  // 0x40cdb8: launched up with the stored speed
                    h.vy() = o.l(0x5a);
                    h.setCont(0x42c3bc);
                    ctx.sound0(0xc7, 8);
                } else {                          // 0x40ce02: let go at the bottom
                    h.vy() = o.l(0x34);
                    h.l(0x465baa) += 2;           // (a long add over the speed's whole part and the next word)
                    h.w(0x465b47) = 0;
                    h.setCont(0x42c2cc);
                }
                release = true;
            }
        }
        if (release) {                            // 0x40ce31
            h.setUl(0x465b7e, 0);
            o.b(0x32) = 6;
            o.b(0x3a) = 0x1e;
            h.b(0x465b6d) = 0xff;
            h.b(0x465b7b) = 8;
            h.vx() = 0;
            h.onGround() = 0;
            h.b(0x465b73) = 0;
            h.b(0x465b6d) = 0xff;
        }
        // 0x40ce76: move the end; back at the top it snaps and, with Harry on it, starts over
        // (from 0x40c84e: the original goes round until the fraction carries).
        moveY(o, o.l(0x34));
        if (int16_t(o.y - 2 - o.w(0x38)) < 0) {
            o.y = o.w(0x38);
            o.b(0x32) = 0;
            if (harryOn(ctx, o)) { o.y += 1; restart(ctx, o); continue; }
        } else if (harryOn(ctx, o)) {
            h.x() = o.x; h.xFrac() = o.xFrac;
            h.y() = o.y; h.yFrac() = o.yFrac;
        }
        break;
    }

    // 0x40cef4: the pieces
    bool still;
    if (o.y == o.w(0x38)) {
        if (stub) {
            animStep(ctx, *stub, stub->anim(0x32), kAnim_bungeeStub, 7);   // 0x45ab67
            stub->x = stub->w(0x50);
            if (stub->facing == 0) stub->x -= 1;
        }
        still = true;
        o.frame = 0xff;
    } else {
        still = false;
        if (stub) stub->frame = 0xff;
        o.frame = 6;
    }
    int16_t d0 = int16_t(o.y - o.w(0x38) + 0x10);
    const uint8_t segFrame = int16_t(d0 - 0xc8) >= 0 ? 5 : 4;
    {
        int f = (uint16_t(d0) / 0x1e) & 3;
        if (f >= 3) f = 2;
        body->frame = uint8_t(f + 1);
    }
    int16_t d1 = int16_t(o.w(0x38) - 0xa);
    size_t used = 0;
    do {
        if (used >= body->parts.size()) {
            Obj* p = ctx.spawn();                 // FUN_00435905
            if (!p) { teardown(ctx, o); return; }   // 0x40d0af
            body->parts.push_back(p);
            p->sprite = ctx.spr("ob_bunge"); p->flags = 0x6000; p->x = o.x;
        }
        Obj* p = body->parts[used++];
        p->frame = still ? 0xff : segFrame;
        p->y = d1;
        if (int16_t(d0 - 0x20) < 0) p->y = int16_t(p->y + d0 - 0x20);
        d1 = int16_t(d1 + 0x20);
        d0 = int16_t(d0 - 0x20);
    } while (d0 >= 0);
    for (size_t i = used; i < body->parts.size(); ++i) ctx.release(*body->parts[i]);   // 0x40d1a6: the pieces no longer needed
    body->parts.resize(used);
    if (farFromCamera(ctx, *top) && farFromCamera(ctx, o)) teardown(ctx, o);
}

// ---------------------------------------------------------------- jaguar boss (type 55, 0x410ce6)
//
// The end of Ceiba Jungle.  The original keeps a continuation address at +0x32; the
// states below are those addresses:
//   INTRO 0x411391 (two roars, spawns the health meter) -> GO 0x41124d / 0x4112e0 (the
//   "GO" sign, then a first leap) -> POUNCE 0x411054 -> REVERSE 0x41190f -> RUN 0x411651
//   -> CROUCH 0x410f1f (jgurn2lo, then a leap at Harry) -> POUNCE ...; ROAR 0x41119c and
//   STOP 0x41156b (at a wall) are the pauses that give Harry his openings.
// Health: a whip hit adds its damage to w6a, hits = w6a >> 3, and 0x1e kills (24 hits).
// State: w36 anim cursor, l38 vx (w3a its integer part), l3e/b42 and l46/b4a pointer-script
// cursors (one shared script pointer here: every state that uses one sets it), w4c a tick
// counter (b4d: "hit while roaring"), l50 vy or a counter, w58 the fight timer (0x960 ticks
// make it aggressive), b5a death-burst spread, b5b "charging to the wall", w5c the pounce
// distance (trg+0x1ea in the original), w68 the GO sign timer, w6a accumulated damage.

namespace jaguar {
enum State : uint8_t { kIntro = 1, kGoWait, kPounce, kReverse, kRun, kCrouch, kRoar, kStop };
constexpr int kHitPoints = 0x1e;
constexpr int kLeapMinX = 0x9fc, kLeapMaxX = 0xbf8;   // a leap stays inside the arena
constexpr int kWallLeft = 0x9dc, kWallRight = 0xc18;  // a hurt jaguar stops at these walls
constexpr int kFloorY = 0x3d0;

// 0x411a3e (max 5 px/tick, 6 on level 8) / 0x411a7a (max 6): accelerate toward `right`.
void accelerate(Obj& o, int32_t accel, int maxSpeed, bool right) {
    if (right) { if (o.w(0x3a) < maxSpeed) o.l(0x38) += accel; }
    else if (o.w(0x3a) >= -maxSpeed) o.l(0x38) -= accel;
}
void moveClamped(Obj& o) {
    moveX(o, o.l(0x38));
    if (o.x < kLeapMinX) o.x = kLeapMinX;
    if (o.x >= kLeapMaxX) o.x = kLeapMaxX;
}
bool aggressive(Obj& o) { return o.w(0x58) == 0x960 || o.hits >= 0xa; }

void startLeap(ObjCtx& ctx, Obj& o) {          // 0x410ff2 / 0x41135c / 0x411629
    o.script = kAnim2_jaguarLeap; o.l(0x3e) = 0;
    o.sprite = ctx.spr("jguleap");
    o.b(0x32) = kPounce;
}
void toRun(ObjCtx& ctx, Obj& o, int16_t counter, int16_t animPos) {
    o.sprite = ctx.spr("jgurun");
    o.b(0x32) = kRun;
    o.w(0x4c) = counter;
    o.w(0x36) = animPos;
}
void toReverse(ObjCtx& ctx, Obj& o) {          // 0x411070 / 0x4118ea
    o.sprite = ctx.spr("jgurevrs");
    o.b(0x32) = kReverse;
    o.w(0x36) = 0;
}
void toRoar(Obj& o) {                          // 0x4119a3
    o.b(0x32) = kRoar;
    o.script = kAnim2_jaguarRoar; o.l(0x46) = 0; o.w(0x4a) = 0;
    o.w(0x4c) = 0;
    o.facing ^= 1;
}
void toStop(ObjCtx& ctx, Obj& o, int16_t wallX, bool faceRight) {   // 0x411703 / 0x41173f
    o.x = wallX;
    o.l(0x50) = 0;
    o.sprite = ctx.spr("jgurun");
    o.b(0x32) = kStop;
    o.facing = faceRight ? 1 : 0;
    o.l(0x38) = faceRight ? 0x80000 : -0x80000;
    o.b(0x5b) = 0;
}

// 0x4113d5: three "scoline" digits (the first carries the type 0x30 handler) and the
// "mi_prcnt" sign, spawned during the intro's pause.
void spawnMeter(ObjCtx& ctx, Obj& boss) {
    Obj* d[4];
    for (int i = 0; i < 4; ++i) {
        d[i] = ctx.spawn();
        if (!d[i]) { for (int k = 0; k < i; ++k) ctx.release(*d[k]); return; }
        d[i]->x = boss.x; d[i]->y = boss.y;
        d[i]->w(0x26) = 0x6663;
        d[i]->sprite = ctx.spr(i == 3 ? "mi_prcnt" : "scoline");
        d[i]->flags = 0xe000;
        d[i]->frame = 0xff;
    }
    d[0]->type = 0x30;
    d[0]->link = d[1]; d[0]->link2 = d[2]; d[0]->link3 = d[3];
    // 0x411499: the Genesis boss music (0x42); FUN_004263a2 is a stub in the PC build.
}

// 0x415b92: the boss is dead - score, a death cloud, then the type 0x51 burst sequence.
void bossDeath(ObjCtx& ctx, Obj& o) {
    ctx.g.stats().score += scoreForType(o.type);
    o.y -= 0x14;
    o.b(0x5a) = 0xff;
    if (Obj* c = ctx.spawn()) {                 // FUN_004356bc(3)
        c->x = o.x; c->y = o.y;                 // (the +8 at 0x415c98 is overwritten right after)
        c->w(0x26) = 0x6663;
        c->script = kAnim2_bossCloud; c->l(0x64) = 0;
        c->link = &o; c->b(0x3c) = 1;
        c->type = 0x82; c->behind = 0xff;
        c->flags = uint16_t((o.flags & 0x8000) + 0x6000);
        c->sprite = ctx.spr("fx_deat2");
    }
    o.type = 0x51; o.w(0x32) = 0; o.frame = 0xff;
}
} // namespace jaguar

// 0x415d15: type 0x51, the dead boss: 22 fx_stone bursts around it, then gone.
void h_bossDeathBurst(ObjCtx& ctx, Obj& o) {
    o.w(0x32) -= 1;
    if (o.w(0x32) >= 0) return;
    o.w(0x32) = int16_t((ctx.rnd() & 1) + 2);
    o.w(0x34) += 1;
    if (o.w(0x34) == 0x16) { ctx.release(o); return; }
    Obj* e = ctx.spawnEffect(o, ctx.spr("fx_stone"), kAnim_turn7, 3);
    if (!e) return;
    e->flags = 0x6000; e->b(0x55) = 0xff; e->w(0x26) = 0x6663;
    if (o.b(0x5a) == 0) {
        e->x += int16_t((ctx.rnd() & 0x1f) - 0x10); e->y += int16_t((ctx.rnd() & 0x3f) - 0x20);
    } else {
        e->x += int16_t((ctx.rnd() & 0x3f) - 0x20); e->y += int16_t((ctx.rnd() & 0x1f) - 0x10);
    }
    ctx.rnd(); ctx.sound(6);                    // FUN_0042633d(6, 0x60, 0x18)
}

// 0x41018c (+ the screen placement at 0x4335ce): the boss health meter, type 0x30.
void h_bossMeter(ObjCtx& ctx, Obj& o) {
    using jaguar::kHitPoints;
    Obj* d2 = o.link; Obj* d3 = o.link2; Obj* pct = o.link3;
    if (!d2 || !d3 || !pct || d2->freed || d3->freed || pct->freed) { ctx.release(o); return; }
    o.behind = d2->behind = d3->behind = pct->behind = 0xff;
    pct->frame = 0;
    // Only the level-0 jaguar is ported (the type 0x69 / 0x8c and level-8 bosses read other records).
    Obj* boss = ctx.boss ? ctx.pl.bossObject() : nullptr;
    if (boss && boss->type == 55) {
        const int left = std::max(0, kHitPoints - int(boss->hits));
        const int percent = (left * 100 / kHitPoints) & 0x7f;
        o.frame = uint8_t(percent / 100); d2->frame = uint8_t(percent / 10 % 10); d3->frame = uint8_t(percent % 10);
        if (o.frame == 0) { o.frame = 0xff; if (d2->frame == 0) d2->frame = 0xff; }   // FUN_00431a3f + 0x41047e
        if (o.w(0x3c) != -0x30) o.w(0x3c) -= 1;                                      // slides in from below
    } else {
        o.frame = 0xff; d2->frame = 0xff; d3->frame = 0;
        if (o.w(0x3c) < 0) o.w(0x3c) += 1;                                           // and back out
    }
    int x = ctx.camX + 0x82;
    if (o.b(0x5a) == 1) x -= 0x1c; else if (o.b(0x5a) == 2) x += 0x14;
    o.x = int16_t(x); d2->x = int16_t(x + 8); d3->x = int16_t(x + 16); pct->x = int16_t(x + 16 + 9);
    const int y = ctx.camY + 0xde + o.w(0x3c);                                       // DAT_004667a8 (status bar slide) = 0
    o.y = d2->y = d3->y = int16_t(y); pct->y = int16_t(y + 0xe);
}

void h_jaguar(ObjCtx& ctx, Obj& o) {
    using namespace jaguar;
    for (int pass = 0; pass < 8; ++pass) {        // `jmp 0x410ce6`: a state change re-runs the handler
        o.behind = 0xff;
        if (o.w(0x58) != 0x960) o.w(0x58) += 1;
        if (o.frame == 0xff) {
            // Dormant (released, and respawned by the .als scan) until Harry is in the arena.
            if (ctx.harry.y() < 0x368 || ctx.harry.x() < 0xbce) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
            o.b(0x5b) = 0;
            ctx.pl.lockCameraLeft(0x9fa);           // DAT_00465b09
            ctx.harry.facing() = 1;
            ctx.pl.setBoss(&o);                     // DAT_00466507 = 0xff, trg+0x1f4 = this
            o.w(0x5c) = 0x50;                       // trg+0x1ea = 0x5000 (big-endian 80)
            o.b(0x3c) = uint8_t((ctx.rnd() & 3) + 2);
            o.y += 0x18;
            o.b(0x32) = kIntro;
            o.facing = 1;
            o.l(0x38) = -0x10000;
        }
        // 0x410dbe: weapon hits count outside the intro and only inside the arena (in the intro
        // stones bounce off).  DAT_00466782 marks a hit for this tick.
        bool hitNow = false;
        ctx.harry.b(0x466782) = 0;
        if (o.b(0x32) == kIntro) {
            deflectStones(ctx, o);
        } else if (int16_t(o.x - 0xbd0) < 0 && int16_t(o.x - 0xa30) >= 0) {
            o.hits = 0;
            if (weaponHits(ctx, o, kHitPoints, nullptr)) { hitNow = true; ctx.harry.b(0x466782) = 0xff; o.w(0x6a) += o.hits; }
        }
        o.hits = uint8_t(uint16_t(o.w(0x6a)) >> 3);
        if (int16_t((uint16_t(o.w(0x6a)) >> 3) - 0x1e) >= 0) {
            ctx.pl.setBoss(nullptr);
            bossDeath(ctx, o);
            ctx.pl.startBossOutro();                // DAT_00465d49 = 1
            h_bossDeathBurst(ctx, o);               // 0x415b92 runs straight into the 0x51 handler
            return;
        }
        if (ctx.harry.invulnerable() == 0 && harryContact(ctx, o, 0xf) && o.b(0x32) == kPounce) {
            ctx.harry.b(0x465b55) = o.facing;       // shoved the way the jaguar is going
            ctx.pl.harryCall(0x427167);
        }
        if (o.b(0x32) != kPounce) { int y = o.y + 0xd; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0xd); }

        switch (o.b(0x32)) {
        case kIntro:                                // 0x411391
            if (!animStep(ctx, o, o.anim(0x36), kAnim_jaguarIntro, 3)) {
                if (o.anim(0x36).tick == 0 && o.anim(0x36).pos == 0x25) spawnMeter(ctx, o);
                return;
            }
            // 0x41124d: the "GO" sign in the middle of the screen, then wait for it
            if (Obj* s = ctx.spawn()) {
                s->flags = 0x6000; s->sprite = ctx.spr("mi_go"); s->frame = 0;
                s->x = int16_t(ctx.camX + 0x80); s->y = int16_t(ctx.camY + 0x50);
            }
            o.w(0x68) = 0x3c;
            o.b(0x32) = kGoWait;
            [[fallthrough]];
        case kGoWait: {                             // 0x4112e0
            o.w(0x68) -= 1;
            if (o.w(0x68) != 0) {
                if (int16_t(o.w(0x68) - 0x14) >= 0) return;
                // the last 20 ticks: a key (the joypad bytes, but start) cuts the wait short
                const Harry& h = ctx.harry;
                if (h.l(0x466765) == 0 && h.w(0x466761) == 0 && h.b(0x466763) == 0) return;
            }
            // 0x411315: the intro is over (DAT_00465d49); take the sign down and leap into the arena
            ctx.harry.b(0x465d49) = 0;
            for (Obj* p : ctx.pl.objectSystem().list())
                if (p->sprite && p->sprite->name() == "mi_go") { ctx.release(*p); break; }
            startLeap(ctx, o);
            o.l(0x50) = -0x30000;
            o.l(0x38) = 0x68000;
            continue;
        }
        case kPounce: {                             // 0x411054
            if (anim2Step(ctx, o, 0x3e, 5)) { toReverse(ctx, o); continue; }
            moveClamped(o);
            const int32_t vy = o.l(0x50);
            moveY(o, vy);
            o.l(0x50) += 0x4300;
            if (vy >= 0) {
                if (o.y >= kFloorY) { o.y = kFloorY; o.l(0x50) = 0; }
                else {
                    int y = o.y + 0xd;
                    const bool landed = ctx.floorSnap(o.x, y);
                    o.y = int16_t(y - 0xd);
                    if (landed) o.l(0x50) = 0;
                }
            }
            // 0x411161: the spring comes at the leap's second frame
            if (o.l(0x50) >= 0 && o.b(0x42) == 0 && o.l(0x3e) == 1) {
                o.l(0x50) = -0x4e000;
                o.l(0x38) = o.facing ? 0x70000 : -0x70000;
            }
            return;
        }
        case kReverse: {                            // 0x41190f
            if (animStep(ctx, o, o.anim(0x36), kAnim_jaguarReverse, 4)) {
                o.facing ^= 1;
                toRun(ctx, o, 0, 6);
                if (o.hits >= 0x14 && (ctx.rnd() & 1) == 0) o.b(0x5b) = 0xff;   // charge to the wall
                continue;
            }
            if (o.frame >= 6) {
                if (!aggressive(o)) { toRoar(o); continue; }
                accelerate(o, 0x3000, ctx.levelNo == 8 ? 6 : 5, o.facing == 0);   // already toward the new way
            } else if (o.w(0x3a) != 0) {
                o.l(0x38) += o.w(0x3a) < 0 ? 0x3000 : -0x3000;                    // 0x411a17: slow down
            }
            moveClamped(o);
            return;
        }
        case kRun: {                                // 0x411651
            animStep(ctx, o, o.anim(0x36), kAnim_run8, 3);
            const bool right = o.facing != 0;       // the call sites flip the facing byte around 0x411a3e
            if (aggressive(o) && o.hits < 0x14) accelerate(o, 0x7500, 6, right);
            else accelerate(o, 0x7500, ctx.levelNo == 8 ? 6 : 5, right);
            moveX(o, o.l(0x38));
            if (o.hits >= 0xa) {                    // 0x4116f6: once hurt, the walls stop it
                if (o.x < kWallLeft) { toStop(ctx, o, kWallLeft, true); continue; }
                if (o.x >= kWallRight) { toStop(ctx, o, kWallRight, false); continue; }
            }
            if (o.b(0x5b)) return;                  // charging: straight to the wall
            if (o.w(0x4c) < 8) {
                o.w(0x4c) += 1;
            } else if (aggressive(o) && o.hits < 0x14) {
                // 0x4117ab: Harry about w5c px ahead at speed -> crouch and leap at him
                int d = ctx.harry.x() - o.x;
                bool fast = o.w(0x3a) >= 3;
                if (!fast && o.w(0x3a) < -3) { fast = true; d = -d; }
                if (fast && between(d - o.w(0x5c), -10, 10)) {
                    o.w(0x5c) = o.w(0x5c) == 0x64 ? 0x90 : 0x64;
                    o.script = kAnim2_jaguarCrouchA;
                    if (ctx.rnd() & 1) o.script = kAnim2_jaguarCrouchB;
                    if (!(ctx.rnd() & 1)) o.script = kAnim2_jaguarCrouchRoar;
                    o.l(0x3e) = 0;
                    o.sprite = ctx.spr("jgurn2lo");
                    o.b(0x32) = kCrouch;
                    continue;
                }
            }
            // 0x41189e: Harry 80 px behind at speed -> turn around
            if (o.w(0x3a) >= 4 || o.w(0x3a) < -5) {
                const int d = ctx.harry.x() - o.x;
                if (o.facing ? d < -0x50 : d >= 0x50) { toReverse(ctx, o); continue; }
            }
            return;
        }
        case kCrouch: {                             // 0x410f1f
            if (o.w(0x3a) == 0) o.l(0x38) = 0;
            else o.l(0x38) += o.w(0x3a) < 0 ? 0x8000 : -0x8000;
            moveX(o, o.l(0x38));
            const bool roaring = o.sprite && o.sprite->name() == "jguroar";
            if (!roaring && hitNow) {               // 0x41101a: a hit breaks the crouch
                toRun(ctx, o, -0x50, 4);
                o.l(0x38) = o.facing ? 0x30000 : -0x30000;
                continue;
            }
            if (!anim2Step(ctx, o, 0x3e, 4)) return;
            const int d = ctx.harry.x() - o.x;
            const bool behind = (d >= 0) == (o.facing == 0);
            if (behind || (ctx.harry.onGround() && (ctx.rnd() & 1) == 0)) {
                toRun(ctx, o, -0x50, 4);
                o.l(0x38) = o.facing ? 0x30000 : -0x30000;
                continue;
            }
            startLeap(ctx, o);
            o.l(0x50) = 0;
            continue;
        }
        case kRoar: {                               // 0x41119c
            anim2Step(ctx, o, 0x46, 4);
            o.b(0x4c) += 1;
            bool resume = o.b(0x4c) == 0x70;
            if (!resume) {
                if (hitNow) o.b(0x4d) = 0xff;
                const bool roaring = o.sprite && o.sprite->name() == "jguroar";
                if (roaring && o.frame != 5) return;
                resume = o.b(0x4d) != 0 || between(o.x - ctx.harry.x(), -0x68, 0x68);
            }
            if (!resume) return;
            toRun(ctx, o, 0, 4);                    // 0x411222
            continue;
        }
        case kStop: {                               // 0x41156b: at a wall, then a leap at Harry
            animStep(ctx, o, o.anim(0x36), kAnim_run8, 2);
            o.w(0x50) += 1;
            if (o.w(0x50) == 0x18) ctx.sound0(0xf, 9);                   // FUN_0042633d(0xf, 0x60)
            if (o.w(0x50) < 0x30) return;
            moveX(o, o.l(0x38));
            if (between(ctx.harry.x() - o.x, -0xc0, 0xc0) || o.w(0x50) == 0x50) {
                startLeap(ctx, o);
                o.l(0x50) = 0;
                continue;
            }
            return;
        }
        default:
            o.b(0x32) = kIntro;
            return;
        }
    }
}

// ---------------------------------------------------------------- skull warrior (type 66, 0x412fc2)
//
// The skeleton of the temple levels.  Three variants by the template's initial frame:
//   0xff  b50 = 1  a pile of bones that assembles when Harry comes within 160 px, walks up
//                  and chops; a whip hit collapses it into a rolling skull that returns home
//                  and re-forms - whipping the re-formed pile shatters it like a pot
//   0xfe  b50 = 2  a standing thrower that only throws bones
//   0xfd  b50 = 3  the tough walker (40 hp): walks at range, chops up close, throws between
// States (b32, jump table 0x45acc4):
//   0 idle pile   1 assemble   2 ready (b36 countdown)   3 turn    4 morph to skull   5 roll
//   6 throw       7 chop       8 re-form                 9 walk   10 roll home       11 dying
// Byte-script cursors live at w34; the throw / chop pointer scripts use l34 (pc) + b38
// (tick); l38 is the roll velocity, w38 the re-form counter, w52 the home x, b5a the hit
// points, b5b the contact damage, b5c the bones' damage.
namespace skull {

enum : uint8_t { kIdle, kAssemble, kReady, kTurn, kMorph, kRoll, kThrow, kChop, kReform, kWalk, kRollHome, kDying };

void idle(ObjCtx&, Obj&); void ready(ObjCtx&, Obj&); void roll(ObjCtx&, Obj&); void reform(ObjCtx&, Obj&);
void walk(ObjCtx&, Obj&); void rollHome(ObjCtx&, Obj&); void dying(ObjCtx&, Obj&);

int harryDx(const ObjCtx& ctx, const Obj& o) { return int16_t(ctx.harry.x() - o.x); }
bool harryBehind(const ObjCtx& ctx, const Obj& o) { return ctx.harry.x() < o.x ? o.facing != 0 : o.facing == 0; }
bool spriteIs(const Obj& o, const char* name) { return o.sprite && o.sprite->name() == name; }

// 0x413a5b
void toTurn(ObjCtx& ctx, Obj& o) { o.b(0x32) = kTurn; o.w(0x34) = 0; exitDespawnWide(ctx, o); }

// 0x413b1e / 0x413b3a: start a pointer script (pc at l34, tick byte at b38).
void startScript(Obj& o, uint8_t state, const uint8_t* script) {
    o.b(0x32) = state; o.script = script; o.l(0x34) = 0; o.w(0x38) = 0;
}

// 0x414054: the floor ends, or a wall marker stands, 0x30 px ahead.
bool blockedAhead(ObjCtx& ctx, Obj& o) {
    const int ax = o.x + (o.facing ? 0x30 : -0x30);
    int y = o.y + 0xb;
    if (!ctx.floorSnap(ax, y)) return true;
    return (ctx.triggerClass(ax, o.y) & 1) != 0;
}

// A flying bone (type 0x1d) with the skeleton's bone damage and the tumbling script.
Obj* spawnBone(ObjCtx& ctx, Obj& o) {
    Obj* b = ctx.spawn();                       // FUN_0043546e(1)
    if (!b) return nullptr;
    b->x = o.x; b->y = o.y; b->w(0x26) = 0x6663;
    b->sprite = ctx.spr("skzbone"); b->frame = 0;
    b->script = kAnim2_boneTumble; b->l(0x50) = 0;
    b->b(0x5a) = o.b(0x5c); b->type = 0x1d;
    b->flags = o.flags; b->b(0x46) = 0xff;
    return b;
}

// 0x413a70: the READY countdown ran out - pick the next move.
void decide(ObjCtx& ctx, Obj& o) {
    const uint8_t v = o.b(0x50);
    if (v == 2) { startScript(o, kThrow, kAnim2_skullThrow); exitDespawnWide(ctx, o); return; }
    const int d = harryDx(ctx, o);
    if (!between(d, -0x50, 0x50)) {                                   // Harry out of reach
        if (v == 3) { o.b(0x32) = kWalk; o.w(0x34) = 0; walk(ctx, o); return; }
        o.b(0x32) = kMorph; o.w(0x34) = 0; exitDespawnWide(ctx, o); return;
    }
    if (v == 3 && !between(d, -0x30, 0x30)) { startScript(o, kThrow, kAnim2_skullThrow); exitDespawnWide(ctx, o); return; }
    startScript(o, kChop, kAnim2_skullChop);
    exitDespawnWide(ctx, o);
}

// state 0 (0x41383d): the pile of bones stirs until Harry comes within 160 px.
void idle(ObjCtx& ctx, Obj& o) {
    animStep(ctx, o, o.anim(0x34), kAnim_boneIdle, 4);
    if (between(harryDx(ctx, o), -0xa0, 0xa0)) { o.w(0x34) = 0; o.b(0x32) = kAssemble; }
    exitDespawnWide(ctx, o);
}

// state 1 (0x4138ad): assembling (skimorph), then straight into walking or standing ready.
void assemble(ObjCtx& ctx, Obj& o) {
    o.sprite = ctx.spr("skimorph");
    if (!animStep(ctx, o, o.anim(0x34), kAnim_turn4, 5)) { exitDespawnWide(ctx, o); return; }
    o.b(0x36) = 0xc;
    if (o.b(0x50) == 1) { o.b(0x32) = kWalk; walk(ctx, o); return; }
    o.b(0x32) = kReady; ready(ctx, o);
}

// state 2 (0x413a0f): standing ready while b36 counts down; turns if Harry gets behind it.
void ready(ObjCtx& ctx, Obj& o) {
    o.sprite = ctx.spr("skiready"); o.frame = 0;
    o.b(0x36) -= 1;
    if (o.b(0x36) == 0) { decide(ctx, o); return; }
    if (harryBehind(ctx, o)) { toTurn(ctx, o); return; }
    exitDespawnWide(ctx, o);
}

// state 3 (0x413b56): turning round (skiturn), then a throw.
void turn(ObjCtx& ctx, Obj& o) {
    o.sprite = ctx.spr("skiturn");
    if (!animStep(ctx, o, o.anim(0x34), kAnim_skullRoll, 5)) { exitDespawnWide(ctx, o); return; }
    o.facing ^= 1;
    startScript(o, kThrow, kAnim2_skullThrow);
    exitDespawnWide(ctx, o);
}

// 0x413bda: morph into the rolling skull (skimorph), then roll at Harry at 2 px/tick.
void morph(ObjCtx& ctx, Obj& o) {
    o.sprite = ctx.spr("skimorph");
    if (!animStep(ctx, o, o.anim(0x34), kAnim_skullMorph, 5)) { exitDespawnWide(ctx, o); return; }
    o.whipCool = 0;
    o.b(0x32) = kRoll;
    o.l(0x38) = harryDx(ctx, o) >= 0 ? 0x20000 : -0x20000;
    o.w(0x34) = 0;
    roll(ctx, o);
}

// state 5 (0x413c4d): rolling (skiroll); stops at an edge or wall, re-forms within 64 px of Harry.
void roll(ObjCtx& ctx, Obj& o) {
    o.sprite = ctx.spr("skiroll");
    animStep(ctx, o, o.anim(0x34), kAnim_skullRoll, 5);
    if (between(harryDx(ctx, o), -0x40, 0x40)) { o.b(0x32) = kAssemble; o.w(0x34) = 0; exitDespawnWide(ctx, o); return; }
    if (!blockedAhead(ctx, o)) moveX(o, o.l(0x38));
    exitDespawnWide(ctx, o);
}

// state 6 (0x413cea): the throw (skirib, then skithrow; pointer script at 3 ticks).
void throwBone(ObjCtx& ctx, Obj& o) {
    if (anim2Step(ctx, o, 0x34, 3)) {
        o.b(0x32) = kReady;
        o.b(0x36) = uint8_t((ctx.rnd() & 0x1f) + 0x20);
        ready(ctx, o);
        return;
    }
    // The bone leaves the hand on skithrow's fifth frame (script offset 18).  The Genesis
    // code (ROM 0x1252dc) tests the script's tick byte, so one bone per throw; the PC
    // translation reads that byte at +0x3b (the big-endian high byte of the long at 0x38),
    // which the interpreter never writes: it is the top byte of the last roll velocity, 0
    // after a roll to the right (a bone on each of the frame's four ticks, 8 px apart) and
    // 0xff after a roll to the left (no bones until it rolls right again).  The PC build is
    // what is reproduced; kGenesisThrow restores the original.
    constexpr bool kGenesisThrow = false;
    const bool release = kGenesisThrow ? o.b(0x38) == 0 : o.b(0x3b) == 0;
    if (release && o.script == kAnim2_skullThrow && o.l(0x34) == 18) {
        if (Obj* b = spawnBone(ctx, o)) {
            b->x = int16_t(o.x - 0x1c); b->y = int16_t(o.y - 0x50);
            o.b(0x68) = 0xff;                                 // (sic: written to the thrower, so the bone keeps b68 = 0 and hurts)
            b->l(0x32) = 0x70000; b->l(0x36) = -0x4000; b->l(0x3a) = -0x100; b->l(0x3e) = 0x5000;
            const int d = harryDx(ctx, o);
            if (between(d, -0x40, 0x40)) {                    // lob at a close Harry
                b->l(0x32) = 0x30000; b->l(0x36) = 0; b->l(0x3a) = -0x100; b->l(0x3e) = 0x9800;
                if (o.facing) b->x += 0x18;
            }
            if (!between(d, -0x90, 0x90)) {                   // a long throw
                b->l(0x32) = 0x80000; b->l(0x36) = -0x1a000; b->l(0x3a) = -0x100; b->l(0x3e) = 0x4000;
            }
            b->facing = o.facing;
            if (o.facing == 0) { b->l(0x32) = -b->l(0x32); b->l(0x3a) = -b->l(0x3a); b->x += 0x38; }
        }
    }
    if (harryBehind(ctx, o)) { toTurn(ctx, o); return; }
    exitDespawnWide(ctx, o);
}

// state 7 (0x413f14): the chop (skzarm / skzantic / skichop / sk0chop; pointer script at 4
// ticks); frame 2 of skichop or sk0chop hits 40 px ahead for 20.
void chop(ObjCtx& ctx, Obj& o) {
    if (anim2Step(ctx, o, 0x34, 4)) {
        if (o.b(0x50) == 1) { o.b(0x32) = kWalk; o.w(0x34) = 0; walk(ctx, o); return; }
        o.b(0x32) = kReady; o.b(0x36) = uint8_t((ctx.rnd() & 0x1f) + 0x20); ready(ctx, o);
        return;
    }
    if (harryBehind(ctx, o)) { toTurn(ctx, o); return; }
    if (o.frame == 2 && (spriteIs(o, "skichop") || spriteIs(o, "sk0chop"))) {
        const int16_t sx = o.x;
        o.x = int16_t(o.x + (o.facing ? 0x28 : -0x28));
        const bool hit = ctx.touchesHarry(o);               // FUN_00424af1
        o.x = sx;
        if (hit) ctx.hurtHarry(0x14);
    }
    exitDespawnWide(ctx, o);
}

// state 8 (0x41378a): re-forming (skimorph), then 0x40 ticks still before acting again.
void reform(ObjCtx& ctx, Obj& o) {
    if (o.w(0x38) != 0) {
        o.w(0x38) += 1;
        if (o.w(0x38) != 0x40) { exitDespawnWide(ctx, o); return; }
        if (o.b(0x50) == 1) { o.b(0x32) = kIdle; idle(ctx, o); return; }
        o.b(0x32) = kRoll;
        o.l(0x38) = harryDx(ctx, o) >= 0 ? 0x20000 : -0x20000;
        o.w(0x34) = 0;
        roll(ctx, o);
        return;
    }
    o.sprite = ctx.spr("skimorph");
    if (!animStep(ctx, o, o.anim(0x34), kAnim_skullMorph, 4)) { exitDespawnWide(ctx, o); return; }
    o.w(0x38) = 1; o.frame = 0;
    exitDespawnWide(ctx, o);
}

// state 9 (0x413901): walking at Harry at 1.9375 px/tick (skiwalk); stops ready at an edge.
void walk(ObjCtx& ctx, Obj& o) {
    o.sprite = ctx.spr("skiwalk");
    animStep(ctx, o, o.anim(0x34), kAnim_skullWalk, 4);
    if (harryBehind(ctx, o)) { toTurn(ctx, o); return; }
    if (blockedAhead(ctx, o)) {                                       // 0x4139f0
        o.b(0x32) = kReady; o.b(0x36) = 0x14;
        o.sprite = ctx.spr("skiready"); o.frame = 0;
        exitDespawnWide(ctx, o);
        return;
    }
    moveX(o, o.facing ? 0x1f000 : -0x1f000);
    if (between(harryDx(ctx, o), -0x2e, 0x2e)) {
        if (o.b(0x50) == 3) { o.b(0x36) = 0x12; o.b(0x32) = kReady; }
        else startScript(o, kChop, kAnim2_skullChop);
    }
    exitDespawnWide(ctx, o);
}

// state 10 (0x41369d): collapsed by the whip: morph down, roll home at 2 px/tick, re-form.
void rollHome(ObjCtx& ctx, Obj& o) {
    if (o.w(0x38) != 0) {
        o.sprite = ctx.spr("skiroll");
        animStep(ctx, o, o.anim(0x34), kAnim_skullRoll, 5);
        const int home = o.w(0x52);
        if (o.x == home || o.x + 1 == home) {
            o.w(0x38) = 0x20; o.b(0x32) = kReform; o.frame = 0;
            reform(ctx, o);
            return;
        }
        if (o.x + 1 < home) { o.x += 2; o.facing = 0; }
        else { o.x -= 2; o.facing = 1; }
        exitDespawnWide(ctx, o);
        return;
    }
    o.sprite = ctx.spr("skimorph");
    if (!animStep(ctx, o, o.anim(0x34), kAnim_skullMorph, 4)) { exitDespawnWide(ctx, o); return; }
    o.w(0x38) = 1; o.frame = 0;
    exitDespawnWide(ctx, o);
}

struct BoneLaunch { int32_t vx, vy; int16_t dx, dy; };
constexpr BoneLaunch kBoneScatter[4] = {                                 // 0x45acf4
    {-0x24000, -0x48000, -4, -12}, {-0x14000, -0x77000, -2, -16}, {0x14000, -0x79000, 2, -16}, {0x24000, -0x47000, 4, -12}};

// state 11 (0x413501): dying (skidie at 2 ticks, 750 points); its first frame scatters four bones.
void dying(ObjCtx& ctx, Obj& o) {
    o.sprite = ctx.spr("skidie");
    if (animStep(ctx, o, o.anim(0x34), kAnim_skullDie, 2)) {
        ctx.release(o);
        ctx.g.stats().score += 750;
        return;
    }
    if (o.anim(0x34).pos != 1 || o.anim(0x34).tick != 0) { exitDespawnWide(ctx, o); return; }
    for (const BoneLaunch& l : kBoneScatter) {                            // 0x41356b, 4 passes
        Obj* b = spawnBone(ctx, o);
        if (!b) continue;
        b->b(0x68) = 0xff;                                                // debris: cannot hurt
        b->l(0x32) = l.vx; b->l(0x3a) = 0; b->l(0x3e) = 0x5200;
        b->x += l.dx; b->y += l.dy;
        b->l(0x36) = l.vy + int32_t(ctx.rnd() & 0xffff) - 0x8000;
    }
}

// 0x4131a3: the re-formed pile shatters like a clay pot (750 points).
void shatterToPot(ObjCtx& ctx, Obj& o) {
    o.y -= 6;
    ctx.g.stats().score += 750;
    o.w(0x26) = 0x6663;                                                   // FUN_004263c4 / FUN_004263a6(8)
    o.b(0x32) = 0xff; o.b(0x50) = 0xff; o.type = 0x26; o.w(0x36) = 0;
    o.sprite = ctx.spr("fxzpots2"); o.frame = 1;
    ctx.sound(0x42);
    h_pot(ctx, o);                                                        // jmp 0x40ed62: the broken-pot branch
}

} // namespace skull

void h_skullWarrior(ObjCtx& ctx, Obj& o) {
    using namespace skull;
    if (o.frame >= 0xfd) {                                                // 0x412fc8: first tick
        o.b(0x5a) = 0x1e; o.b(0x5b) = 0xf; o.b(0x5c) = 0xd;
        if (o.frame == 0xff) o.b(0x50) = 1;
        else if (o.frame == 0xfe) { o.b(0x50) = 2; o.b(0x36) = 0x30; o.b(0x32) = kReady; }
        else { o.b(0x50) = 3; o.b(0x5a) = 0x28; o.b(0x5c) = 0xf; o.b(0x36) = 0x30; o.b(0x32) = kReady; }
        o.frame = 0;
        o.w(0x52) = o.x;                                                  // home
        if (harryDx(ctx, o) >= 0) o.facing = 1;
    }
    if (ctx.freeze) {                                                     // 0x413065: stop-watch
        bool killed = false;
        if (weaponHits(ctx, o, 0, &killed)) {
            if (spriteIs(o, "skiroll") || spriteIs(o, "skimorph") || spriteIs(o, "skijaw") || spriteIs(o, "skidie")) {
                ctx.sparkleBurst(o, (ctx.rnd() & 1) ? kSparkleOffsetsB : kSparkleOffsetsA);   // 0x433725
                ctx.g.stats().score += 750;
                ctx.spawnEffect(o, ctx.spr("fx_stone"), kAnim_turn7, 3);
                ctx.release(o);
            } else {
                killObject(ctx, o);
            }
            return;
        }
        freezeSparkle(ctx, o);
        return;
    }
    if (o.b(0x32) != kDying) {
        bool killed = false;
        const bool hit = weaponHits(ctx, o, o.b(0x5a), &killed);
        // (0x4132ec: a sling stone that hits knocks a bone loose from the stone's position;
        // stones are not thrown yet.)
        if (o.b(0x50) == 1) {
            if (hit) {
                const uint8_t s = o.b(0x32);
                if (s == kIdle || s == kRollHome || s == kRoll || s == kReform) {
                    if (o.w(0x38) == 0) { exitDespawnWide(ctx, o); return; }
                    shatterToPot(ctx, o);
                    return;
                }
                o.w(0x34) = 0; o.w(0x38) = 0;                               // 0x413214: collapse
                o.b(0x32) = kRollHome;
                rollHome(ctx, o);
                return;
            }
        } else if (killed) {                                                // 0x4134d8
            o.b(0x32) = kDying; o.w(0x34) = 0;
            ctx.sound(0x42);
            dying(ctx, o);
            return;
        }
    }
    int y = o.y + 0xd;                                                      // 0x413244: stay on the floor
    ctx.floorSnap(o.x, y);
    o.y = int16_t(y - 0xd);
    if (o.b(0x32) != kDying) harryContact(ctx, o, o.b(0x5b));
    switch (o.b(0x32)) {
    case kIdle:     idle(ctx, o); break;
    case kAssemble: assemble(ctx, o); break;
    case kReady:    ready(ctx, o); break;
    case kTurn:     turn(ctx, o); break;
    case kMorph:    morph(ctx, o); break;
    case kRoll:     roll(ctx, o); break;
    case kThrow:    throwBone(ctx, o); break;
    case kChop:     chop(ctx, o); break;
    case kReform:   reform(ctx, o); break;
    case kWalk:     walk(ctx, o); break;
    case kRollHome: rollHome(ctx, o); break;
    case kDying:    dying(ctx, o); break;
    default: break;
    }
}

// ---------------------------------------------------------------- stone balls (types 9 / 68)

// 0x406ea6: a stone ball (type 9) that drops and rises in place: the frame picks a drop from
// the 0x45b7c6 table below its home y; frames 2..5 crush Harry for 40.
void h_dropBall(ObjCtx& ctx, Obj& o) {
    if (o.w(0x5a) == 0) o.w(0x5a) = o.y;                                 // home y
    if (!ctx.freeze) animStep(ctx, o, o.anim(0x32), kAnim_ballDrop, 4);
    static const int16_t kDrop[6] = {1, 3, 6, 8, 12, 15};                // 0x45b7c6
    o.y = int16_t(o.w(0x5a) + (o.frame < 6 ? kDrop[o.frame] : 0));
    if (o.frame != 0xff && o.frame >= 2) harryContact(ctx, o, 0x28);
}

// 0x41479e: a stone ball (type 68) swinging on a chain (obichain child, l4a).  The trigger
// under it picks the pattern: id 0xc7 swings both ways (A), 0xc8 swings once and hides (B),
// anything else hides, then swings (C).  The pointer script's frame indexes the chain's
// frame (0x45b08a) and the ball's offset from the pivot (0x45b05a); frames 4 / 5 of the ball
// (script frames 8..11) hit Harry for 40 and knock him back.
void h_swingBall(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xfe) {                                               // first tick
        o.frame = 0xff;
        if (Obj* c = ctx.spawn()) {                                      // FUN_0043546e(2)
            c->x = int16_t(o.x - 6); c->y = int16_t(o.y + 0xd); c->w(0x26) = 0x6663;
            c->sprite = ctx.spr("obichain"); c->flags = 0x2000;
            o.link = c;
        }
        uint8_t id = 0;
        ctx.triggerClass(o.x - 0x10, o.y + 0x18, &id);
        o.x -= 8; o.y += 0xc;
        o.w(0x50) = o.x; o.w(0x52) = o.y;                                // pivot
        if (id == 0xc7)      { o.script = kAnim2_ballSwingA; o.b(0x5a) = 2; }
        else if (id == 0xc8) { o.script = kAnim2_ballSwingB; o.b(0x5a) = 0; }
        else                 { o.script = kAnim2_ballSwingC; o.b(0x5a) = 1; }
        o.l(0x32) = 0; o.b(0x36) = 0;
    }
    if (!ctx.freeze) anim2Step(ctx, o, 0x32, 2);
    if (o.frame != 0xff) {
        static const uint8_t kChainFrame[12] = {0xff, 0xff, 0xff, 0, 1, 2, 3, 4, 5, 6, 7, 8};   // 0x45b08a
        static const int16_t kSwing[12][2] = {                                                  // 0x45b05a
            {60, -8}, {63, -8}, {68, -5}, {70, -4}, {70, -2}, {67, 4}, {64, 7}, {58, 10}, {49, 15}, {38, 18}, {25, 21}, {13, 22}};
        const int i = std::min<int>(o.frame, 11);
        if (o.link && !o.link->freed) { o.link->frame = kChainFrame[i]; o.link->facing = o.facing; }
        o.frame = uint8_t(i >> 1);
        o.x = int16_t(o.w(0x50) + 3 + (o.facing ? -kSwing[i][0] : kSwing[i][0]));
        o.y = int16_t(o.w(0x52) + kSwing[i][1]);
        if (ctx.harry.invulnerable() == 0 && (o.frame == 4 || o.frame == 5)) {
            if (!ctx.channel0Busy()) ctx.sound0(0xc3, 8);                // FUN_0042633d(0xc3, 0x60)
            if (ctx.touchesHarry(o)) {
                // 0x414a10: shoved by the swing's direction (b5a; pattern A by its half: pc >= 0x45b0b5)
                Harry& h = ctx.harry;
                h.b(0x465b55) = o.b(0x5a);
                if (o.b(0x5a) == 2) h.b(0x465b55) = o.l(0x32) >= 0x1f ? 1 : 0;
                ctx.hurtHarry(0x28);
                ctx.pl.harryCall(0x427167);
            }
        }
    }
    if (beyondWide(ctx, o)) {                                            // 0x425051: both go
        if (o.link && !o.link->freed) ctx.release(*o.link);
        exitDespawnWide(ctx, o);
    }
}

// ---------------------------------------------------------------- water ripples (types 129 / 79) and the turtle (type 11)

// 0x4079c8: a ring of ripples on the water at (x + dx, y + dy): the type 0x81 half plays the
// script, its type 0x4f twin (l50, drawn H-flipped) mirrors the frame.
void spawnRipple(ObjCtx& ctx, Obj& at, int dx, int dy, uint16_t flagAdd = 0) {
    Obj* a = ctx.spawnFrom(at, true);                                     // FUN_0043546e(3)
    if (!a) return;
    a->w(0x26) = 0x6663;
    a->type = 0x81; a->sprite = ctx.spr("fx_rippl"); a->flags = uint16_t(0x2000 + flagAdd);
    a->x = int16_t(at.x + dx); a->y = int16_t(at.y + dy);
    Obj* b = ctx.spawnFrom(at, true);                                     // FUN_0043546e(1)
    if (!b) { ctx.release(*a); return; }
    a->link = b;
    b->type = 0x4f;
    std::memcpy(b->s + 0x12, a->s + 0x12, 2);
    b->w(0x26) = 0x6663;
    b->sprite = ctx.spr("fx_rippl"); b->link = a;
    b->flags = uint16_t(0x2000 + flagAdd);
    b->x = int16_t(at.x + dx); b->y = int16_t(at.y + dy);
    b->facing = 0xff;
}

// 0x41b92e: the ripple ring (fx_rippl, 17 frames; fxisplsh, 12) at 4 ticks; the twin copies
// the frame; both go when the script ends.
void h_ripple(ObjCtx& ctx, Obj& o) {
    const bool splash = o.sprite && o.sprite->name() == "fxisplsh";
    const bool done = animStep(ctx, o, o.anim(0x32), splash ? kAnim_splashRing : kAnim_rippleRing, 4);
    if (o.link && !o.link->freed) o.link->frame = o.frame;
    if (done) {
        if (o.link && !o.link->freed) ctx.release(*o.link);
        ctx.release(o);
    }
}

// 0x4078b0: a turtle (obiturtl) floating in the water.  Harry can stand on it; it sinks a
// little (w32 counts to 8, frame from the 0x45b7e4 table) while he does, with a ripple
// ring and a splash when he lands, and rises again when he leaves.
void h_turtle(ObjCtx& ctx, Obj& o) {
    if (o.b(0x34) == 0) o.b(0x34) = 0xff;
    static const uint8_t kSink[9] = {0, 0, 0, 0, 1, 1, 1, 1, 1};           // 0x45b7e4
    o.frame = kSink[std::min<int>(o.w(0x32), 8)];
    Harry& h = ctx.harry;
    if (!ctx.pl.harryOnPlatform() && !ctx.pl.harryBusy() && h.w(0x465baa) >= 0 && harryOnBox(ctx, o, -20, 20, -12, 10)) {
        // 0x40793b: Harry stands on it (box 0x45b7ed); no landing
        h.y() = o.y;
        h.vy() = 0;
        h.b(0x465b73) = 0xff;
        if (o.w(0x32) != 8) {
            if (o.w(0x32) == 0) { spawnRipple(ctx, o, -1, -1); ctx.sound1(0xbb, 6); }
            o.w(0x32) += 1;
        }
    } else if (o.w(0x32) != 0) {
        o.w(0x32) -= 1;
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- fish (type 47, 0x4100ca)

// A fish (scffish) leaping out of the water at a random spot within 32 px of its home
// (w34), then hiding for 0x30..0x6f ticks (b33) before the next leap.
void h_fish(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff; o.w(0x34) = o.x;
    } else {
        if (o.b(0x33)) { o.b(0x33) -= 1; exitDespawnFar(ctx, o); return; }
        if (!animStep(ctx, o, o.anim(0x36), kAnim_turn7, 4)) { exitDespawnFar(ctx, o); return; }
        o.frame = 0xff;
    }
    const uint32_t r = ctx.rnd();
    o.x = int16_t(o.w(0x34) + int(r & 0x3f) - 0x20);
    o.facing = uint8_t(r & 1);
    o.b(0x33) = uint8_t((ctx.rnd() & 0x3f) + 0x30);
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- the rolling wheels (types 67 / 148 / 149)

// Five sprites make a wheel: the object itself and four children (obrwhee1 flipped three ways
// and obrwhee2) that follow it and take their frames from the 0x45bbba table by the roll
// angle (l54 in 1/0xc0000 turns per row, w32 = row * 5).
namespace wheel {

// 0x414356: first tick (frame 0xff): behind Harry, roll angle 4.0 and the four children.
// 1 = set up now, 0 = already running, -1 = the pool ran out (everything is released).
int init(ObjCtx& ctx, Obj& o) {
    if (o.frame != 0xff) return 0;
    o.behind = 0xff;
    o.l(0x54) = 0x40000;
    o.parts.clear();
    for (int i = 0; i < 4; ++i) {                                        // FUN_0043546e(4) x 4
        Obj* c = ctx.spawn();
        if (!c) {                                                        // 0x4146f1
            for (Obj* p : o.parts) ctx.release(*p);
            o.parts.clear();
            ctx.release(o);
            return -1;
        }
        c->x = o.x; c->y = o.y; c->w(0x26) = 0x6663;
        c->sprite = ctx.spr(i == 3 ? "obrwhee2" : "obrwhee1"); c->flags = 0x2000; c->behind = 0xff;
        if (i == 0 || i == 1) c->facing = 0xff;
        if (i == 1 || i == 2) c->vflip = 0xff;
        o.parts.push_back(c);
    }
    return 1;
}

// 0x414491: the common tail - the in-front trigger, the rumble, the roll angle and frames,
// the children following, and the vertical exit (0x424e73 / 0x4146f1, which also stops the
// rumble).
void tail(ObjCtx& ctx, Obj& o) {
    priorityTrigger(ctx, o, 0);                                          // 0x4301ce
    if (!ctx.g.soundPlaying(0xd1)) ctx.sound(0xd1);                      // (FUN_004261d1: channel 1 free?)
    o.l(0x54) += o.l(0x50);                                              // roll angle
    if (o.l(0x54) < 0) {
        o.l(0x54) += 0xc0000;
        o.w(0x32) -= 5;
        if (o.w(0x32) < 0) o.w(0x32) = 0xf;
    } else if (o.w(0x56) >= 0xc) {
        o.l(0x54) -= 0xc0000;
        o.w(0x32) += 5;
        if (o.w(0x32) == 0x14) o.w(0x32) = 0;
    }
    static const uint8_t kWheelFrames[20] = {                            // 0x45bbba: 4 rows of 5
        0, 0, 0, 0, 3,  1, 3, 1, 3, 2,  2, 2, 2, 2, 1,  3, 1, 3, 1, 0};
    const int row = std::clamp<int>(o.w(0x32), 0, 15);
    o.frame = kWheelFrames[row];
    for (size_t i = 0; i < o.parts.size(); ++i) {
        Obj* c = o.parts[i];
        if (!c || c->freed) continue;
        c->frame = kWheelFrames[row + 1 + int(i)];
        c->x = o.x; c->y = o.y; c->flags = o.flags;
    }
    const int dy = o.y - ctx.camY;
    if (dy < -0xa0 || dy >= 0x17c) {
        if (ctx.g.soundPlaying(0xd1)) ctx.g.stopSound(0xd1);
        for (Obj* p : o.parts) if (p && !p->freed) ctx.release(*p);
        o.parts.clear();
        exitDespawnVertical(ctx, o);
    }
}

} // namespace wheel

// 0x414100: the great stone wheel of the mines (type 67).  It falls (l4c, gravity 0x3000,
// 10 px terminal) until it finds a floor 0x36 px under it, then rolls (l50 toward +-3.19
// px/tick; b39 set = to the left), shaking the screen (DAT_00465b02) while it rolls and
// harder for 0x14 ticks after a landing.  Touching it is death (0xa0) plus a shove the way
// it is going.
void h_wheel(ObjCtx& ctx, Obj& o) {
    const int first = wheel::init(ctx, o);
    if (first < 0) return;
    if (first) {
        o.b(0x6c) = 0xff; o.l(0x54) = 0;
        if (o.b(0x39) == 0) o.l(0x4c) = 0x40000;
    }
    if (ctx.harry.invulnerable() == 0 && harryContact(ctx, o, 0xa0)) {
        ctx.harry.b(0x465b55) = o.l(0x50) < 0 ? 0 : 0xff;                 // shoved the way it rolls
        ctx.pl.harryCall(0x42715b);
    }
    bool grounded = false;
    if (o.w(0x38) < 0xc8) {                                              // not yet fallen 200 px (l36)
        int y = o.y + 0x36;
        grounded = ctx.floorSnap(o.x, y);
        o.y = int16_t(y - 0x36);
        if (!grounded) { o.b(0x6c) = 0; o.w(0x3a) = 0x14; }
    }
    if (!grounded) {                                                     // falling
        if (o.w(0x4e) != 0xa) o.l(0x4c) += 0x3000;
        moveY(o, o.l(0x4c));
        o.l(0x36) += o.l(0x4c);
        if (o.w(0x52) != 0) o.l(0x50) += (o.w(0x52) < 0) ? 0x1800 : -0x1800;
        moveX(o, o.l(0x50));
    } else {                                                             // 0x414264: rolling
        if (o.b(0x6c) == 0) { ctx.sound0(0xbc, 0xb); o.b(0x6c) = 0xff; }
        o.l(0x4c) = 0;
        ctx.pl.setShake(int(ctx.rnd() & 3));
        if (o.b(0x39)) { if (o.l(0x50) >= -0x33000) o.l(0x50) -= 0x1300; }
        else           { if (o.l(0x50) < 0x33000) o.l(0x50) += 0x1300; }
        moveX(o, o.l(0x50));
        if (o.w(0x3a)) {
            ctx.pl.setShake(int(ctx.rnd() & 0xf) - 8);
            o.w(0x3a) -= 1;
            if (o.w(0x3a) == 0) ctx.pl.setShake(0);
        }
    }
    wheel::tail(ctx, o);
}

// 0x41d9e6: the wheel in the pit of the Copan Temple (type 148).  It rolls
// down whichever slope it stands on (the floor shape 0x3c px under it: heights at its two
// ends), +0x800 / -0x1000 per tick up to 4 px/tick, so it swings back and forth in the
// U-shaped pit.  Once the camera reaches the pit (camY >= 0x2be, 0xb45 <= camX < 0xc90) the
// camera is held to 0xb3a..0xc98 and the screen shakes for as long as the hold lasts.
void h_pitWheel(ObjCtx& ctx, Obj& o) {
    // (The original compares the left hold with 0xc98, the right one's value, so the test
    // never short-cuts and the hold is re-applied every tick.)
    if (ctx.pl.cameraLeftLock() != 0xc98 && ctx.camY >= 0x2be && ctx.camX < 0xc90 && ctx.camX >= 0xb45) {
        ctx.pl.lockCameraLeft(0xb3a);
        ctx.pl.lockCameraRight(0xc98);
    }
    if (ctx.pl.cameraRightLock() == 0xc98) {
        // (0x41da38 then tests d0 against -2..1, but d0 still holds the dispatcher's 4 * type,
        // so the shake is redrawn every tick.)
        ctx.pl.setShake(int(ctx.rnd() & 3));
    }
    const int first = wheel::init(ctx, o);
    if (first < 0) return;
    if (first) { o.l(0x50) = 0x40000; o.l(0x54) = 0x40000; o.b(0x4f) = 0xff; }
    if (ctx.harry.invulnerable() == 0 && harryContact(ctx, o, 0xa0)) {
        ctx.harry.b(0x465b55) = o.l(0x50) < 0 ? 0 : 0xff;                 // shoved the way it rolls
        ctx.pl.harryCall(0x42715b);
    }
    const ph::FloorHit f = ph::probeNear(ctx.det(), o.x, o.y + 0x3c);    // FUN_00425d6f
    if (f.hit) {
        o.y = int16_t(f.surfaceY - 0x3c);
        const int slope = int(ph::kCollisionHeights[f.shape * 16]) - int(ph::kCollisionHeights[f.shape * 16 + 15]);
        if (slope != 0) o.b(0x4f) = slope < 0 ? 0xff : 0;                // left end higher: roll right
    }
    if (o.b(0x4f) == 0) { if (o.w(0x52) != -4) o.l(0x50) -= 0x1000; }
    else                { if (o.w(0x52) != 4) o.l(0x50) += 0x800; }
    moveX(o, o.l(0x50));
    wheel::tail(ctx, o);
}

// 0x41dbf2: type 149, the wheel's source: while Harry is above row 18 it releases a wheel
// (template 0x4680f4: type 67, obrwhee1, rolling left) 0x260 px to the right and 0x20 px
// up every 0xe0 ticks.
void h_wheelSpawner(ObjCtx& ctx, Obj& o) {
    if (ctx.harry.y() < 0x124) {
        if (o.b(0x5a) == 0) { o.b(0x5a) = 0xff; o.w(0x32) = 2; }
        o.w(0x32) -= 1;
        if (o.w(0x32) < 0) {
            o.w(0x32) = 0xe0;
            if (Obj* w = ctx.spawn()) {                                  // FUN_004351c5
                w->x = int16_t(o.x + 0x260); w->y = int16_t(o.y - 0x20); w->w(0x26) = 0x6663;
                w->type = 67; w->frame = 0xff; w->flags = 0x2000;
                w->sprite = ctx.spr("obrwhee1");
                w->b(0x39) = 0xff;
            }
        }
    }
    exitDespawnVertical(ctx, o);                                         // 0x42501f
}

// ---------------------------------------------------------------- crumbling stone block (type 52, 0x410946)

// A stone block (obfbston) that cracks when Harry lands on it: 0x1f ticks of crumbling frames
// (turn7 script, held on frame 6), then it drops under 0x8000 gravity.
void h_stoneBlock(ObjCtx& ctx, Obj& o) {
    // 0x410946
    o.b(0x6d) -= 1;                                    // every 16 ticks to the end of the object list
    if (int8_t(o.b(0x6d)) < 0) { o.b(0x6d) = 0x10; if (!ctx.pl.objectSystem().moveToTail(o)) return; }   // 0x435532
    if (o.frame == 0xff) { o.b(0x32) = 0x20; o.frame = 0; }
    if (int8_t(o.b(0x32) - 0x1f) < 0) {
        if (animStep(ctx, o, o.anim(0x50), kAnim_turn7, 4)) { o.w(0x50) = 6; o.frame = 6; }
    }
    if (o.b(0x32) != 0x20) {
        if (o.b(0x32) == 0) {                                            // falling
            o.l(0x34) += 0x8000;
            moveY(o, o.l(0x34));
            exitDespawnFar(ctx, o);
            return;
        }
        o.b(0x32) -= 1;
    }
    if (!ctx.pl.harryOnPlatform() && !ctx.pl.harryBusy() && ctx.harry.w(0x465baa) >= 0 && harryOnBox(ctx, o, -6, 64, -7, 9)) {
        ctx.pl.standOn(o.y + 7, true);                                    // 0x410a31 (box 0x45bad0)
        if (o.b(0x32) == 0x20) { o.b(0x32) = 0x1f; ctx.sound0(0xe, 0xb); }   // FUN_0042633d(0xe, 0x60)
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- spear trap (type 28, 0x40d322)

// A spear (obzspear) that thrusts out of the wall every 0x10..0x4f ticks; frame 4 of the
// thrust stabs Harry for 15.
void h_spear(ObjCtx& ctx, Obj& o) {
    if (o.w(0x32) == 0) { o.w(0x32) = int16_t((ctx.rnd() & 0x3f) + 0x10); o.w(0x3c) = 0; }
    o.w(0x32) -= 1;
    if (o.w(0x32) == 0) {
        o.w(0x32) = 1;
        if (animStep(ctx, o, o.anim(0x3c), kAnim_spearThrust, 4)) {      // thrust over: a new wait
            o.w(0x32) = int16_t((ctx.rnd() & 0x3f) + 0x10); o.w(0x3c) = 0;
        } else if (o.frame == 4 && ctx.touchesHarry(o)) {
            ctx.hurtHarry(0xf);                                          // 0x407b0f
        }
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- fire pit (type 39, 0x40f2ba)

// A gout of fire (fxrfirep / fx0firep, 38 frames across the two sheets) that follows the 59
// keyframes at 0x45ad94 around its home (w46/w48), advancing one every 0x10000/0x8800 ticks,
// with a trailing flame (fxifirer child, l50) on its own keyframe columns.  Frames 10 and
// up of the first sheet burn Harry for 40.
void h_firePit(ObjCtx& ctx, Obj& o) {
    constexpr int kFrames = 59;
    auto key = [](int i, int col) { int16_t v; std::memcpy(&v, kData_firePath + i * 12 + col * 2, 2); return int(v); };
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        o.w(0x36) = 0;                                                   // keyframe index (l36 = 0x45ad94 + 12 * i)
        o.w(0x46) = o.x; o.w(0x48) = o.y;
        o.l(0x5a) = 0;
        Obj* t = ctx.spawn();                                            // FUN_0043546e(2)
        if (!t) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        t->x = o.x; t->y = o.y; t->w(0x26) = 0x6663;
        t->sprite = ctx.spr("fxifirer"); t->flags = 0x2000;
        o.link = t;
    }
    if (!ctx.freeze) {
        o.l(0x5a) -= 0x8800;
        if (o.l(0x5a) < 0) {
            o.l(0x5a) += 0x10000;
            o.w(0x36) += 1;
            if (o.w(0x36) == 15) ctx.sound0(8, 5);                       // 0x45ae48: the roar (FUN_0042633d(8, 0x60))
        }
    }
    if (o.w(0x36) >= kFrames) o.w(0x36) = 0;
    const int i = o.w(0x36);
    int f = key(i, 0) - 1;
    o.sprite = ctx.spr("fxrfirep");
    if (int8_t(f) >= 0x13) { f -= 0x13; o.sprite = ctx.spr("fx0firep"); }
    o.frame = uint8_t(f);
    o.x = int16_t(o.w(0x46) + key(i, 1));
    o.y = int16_t(o.w(0x48) - key(i, 2));
    int tailY = 0;
    if (o.link && !o.link->freed) {
        Obj& t = *o.link;
        t.frame = uint8_t(key(i, 3) - 1);                                // (0x8004 entries read as frame 3)
        t.x = int16_t(o.w(0x46) + key(i, 4));
        tailY = o.w(0x48) + key(i, 5) - 0x3a;
        t.y = int16_t(tailY);
    }
    o.behind = 0;
    const bool secondSheet = o.sprite && o.sprite->name() == "fx0firep";
    if (!secondSheet && between(tailY, 0xd, 0x11)) o.behind = 0xff;    // (sic: compares the tail's absolute y)
    if (!ctx.freeze && !secondSheet && o.frame >= 0xa) harryContact(ctx, o, 0x28);
    const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;                  // 0x40f584: its own window
    if (dx < -0xc8 || dx >= 0x208 || dy < -0xa0 || dy >= 0x17c) {
        if (o.link && !o.link->freed) ctx.release(*o.link);
        ctx.pl.restoreEntry(o); ctx.release(o);
    }
}

// ---------------------------------------------------------------- whip lever (type 119, 0x41aee0)

// A lever (obfleve2) pulled by the whip rather than by hand: a hit runs the level script
// for the trigger under it and turns it (turn4 at 3 ticks, held on frame 3).  Placed with
// frame 0xff it is the mirrored variant (16 px to the left).
void h_whipLever(ObjCtx& ctx, Obj& o) {
    if (o.w(0x4e)) {
        if (animStep(ctx, o, o.anim(0x4e), kAnim_turn4, 3)) o.frame = 3;
    }
    if (o.b(0x46) == 0) {
        o.b(0x46) = 0xff;
        if (o.frame == 0xff) { o.frame = 0; o.facing ^= 1; o.w(0x64) = 0x10; o.x -= 0x10; }
        else o.w(0x64) = 0;
        const uint8_t id = triggerIdAt(ctx, o.x + o.w(0x64) - 0x18, o.y + 0x18);
        if (const LevelScriptEntry* e = scriptByLever(id))
            if (ctx.g.levelFlags()[e->flag]) { o.frame = 3; o.b(0x4b) = 0xff; }   // already pulled
    }
    if (o.b(0x4b) == 0 && weaponHits(ctx, o, 0, nullptr)) {
        levelScriptPull(ctx, triggerIdAt(ctx, o.x + o.w(0x64) - 0x18, o.y + 0x18));
        o.b(0x4b) = 0xff;
        o.w(0x4e) = 1;
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- snake respawner (type 164, 0x41f5e6)

// An unseen marker that keeps a snake (template 0x468314: type 0x6d, snixhole, its own
// placement offsets) in its hole: when the one it spawned is gone (its record reused by
// something else), it waits 0xd0..0x14f ticks and spawns another.  (The original counts
// spawns in b5a but overwrites that byte with the timer, so the "ten at most" never holds.)
void h_snakeRespawner(ObjCtx& ctx, Obj& o) {
    if (!o.link) {
        if (o.b(0x5a) != 0xa) {
            o.b(0x5a) += 1;
            if (Obj* s = ctx.spawn()) {                                  // FUN_004351c5
                const ph::ObjectTemplate& t = ph::kObjectTemplates[116];
                s->x = o.x; s->y = o.y; s->w(0x26) = 0x6663;
                s->type = t.type; s->frame = t.sub; s->flags = t.flags; s->sprite = ctx.spr(t.sprite);
                o.link = s;
                o.w(0x5a) = int16_t((ctx.rnd() & 0x7f) + 0xd0);
            }
        }
    } else if (o.link->freed || o.link->type != 0x6d) {
        if (o.w(0x5a)) o.w(0x5a) -= 1;
        else o.link = nullptr;
    }
    if (!o.link) exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- roaming snake and its spit (types 12 / 36)

// 0x40ebb0: the snake's spit (type 0x24, fxzspit): flies at 5.5 px/tick, 12 damage, splats
// on Harry or a wall marker.
void h_spit(ObjCtx& ctx, Obj& o) {
    priorityTrigger(ctx, o, 0);
    if (o.w(0x46)) {
        if (animStep(ctx, o, o.anim(0x46), kAnim_spitSplat, 4)) ctx.release(o);
        else exitDespawnFar(ctx, o);
        return;
    }
    moveX(o, o.l(0x32));
    animStep(ctx, o, o.anim(0x58), kAnim_spitFly, 7);
    bool splat = false;
    if (ctx.touchesHarry(o)) { ctx.hurtHarry(0xc); splat = true; }      // 0x407b0f
    else if (ctx.triggerClass(o.x - 0x10, o.y) & 1) splat = true;
    if (splat) { o.w(0x46) = 1; h_spit(ctx, o); return; }
    exitDespawnFar(ctx, o);
}

// 0x407bfc: a snake out of its hole (type 12, placed as snix_dot): it rears up when Harry
// comes within 0xc0 x 0xa0, then slithers back and forth within 32 px of home (w58) at
// 1.25 px/tick, turning at edges, wall markers and the leash, and every 0x10..0x2f ticks
// lunges and spits (type 0x24) when Harry is ahead of it.  10 hp, 10 contact damage.
void h_roamingSnake(ObjCtx& ctx, Obj& o) {
    priorityTrigger(ctx, o, o.facing ? -0x20 : 0);
    if (o.b(0x32) == 0) {
        o.w(0x58) = o.x;
        o.w(0x4c) = int16_t((ctx.rnd() & 0x1f) + 0x10);
        o.l(0x54) = -0x14000;
        o.facing = 0; o.b(0x32) = 0xff;
    }
    bool killed = false;
    if (weaponHits(ctx, o, 10, &killed) && killed) { killObject(ctx, o); return; }
    if (ctx.freeze) {
        if (o.frame == 0xff) { ctx.release(o); return; }                 // (sic: an unseen frozen snake is dropped)
        harryContact(ctx, o, 10);
        return;
    }
    if (!o.sprite || o.sprite->name() != "snimiss") harryContact(ctx, o, 10);
    if (o.b(0x33) == 0) {                                                // still a dot: rear up when Harry nears
        if (o.w(0x34) == 0) {
            const int dx = o.x - ctx.harry.x(), dy = ctx.harry.y() - o.y;
            if (!between(dx, -0xc0, 0xc0) || !between(dy, -0xa0, 0xa0)) { exitDespawnFar(ctx, o); return; }
        }
        o.sprite = ctx.spr("snix_dot");
        if (animStep(ctx, o, o.anim(0x34), kAnim_turn7, 5)) {
            o.sprite = ctx.spr("snislthr"); o.b(0x33) = 0xff; o.frame = 0;
            o.b(0x34) = 2; o.facing = 1;
        }
        exitDespawnFar(ctx, o);
        return;
    }
    if (o.b(0x38) == 0xfb) {                                             // lunging
        o.sprite = ctx.spr("snilunge");
        const bool done = animStep(ctx, o, o.anim(0x3c), kAnim_snakeLunge2, 3);
        if (int8_t(o.frame) >= 0xe) { o.frame -= 0x10; o.sprite = ctx.spr("snispit"); }
        if (o.anim(0x3c).pos == 0xa && o.anim(0x3c).tick == 0) {        // the spit leaves the mouth
            if (Obj* s = ctx.spawn()) {                                  // FUN_0043546e(2)
                s->x = o.x; s->y = o.y; s->w(0x26) = 0x6663;
                s->facing = uint8_t((o.facing & 1) ^ 1);
                s->sprite = ctx.spr("fxzspit"); s->frame = 0;
                s->y -= 0x14; s->type = 0x24; s->l(0x32) = 0x58000;
                s->flags = uint16_t((o.flags & 0x8000) + 0x6000);
                s->x += 0x10;
                if (o.facing) { s->x -= 0x20; s->l(0x32) = -s->l(0x32); }
            }
            exitDespawnWide(ctx, o);
            return;
        }
        if (!done) { exitDespawnWide(ctx, o); return; }
        o.b(0x38) = 0; o.sprite = ctx.spr("snislthr"); o.frame = 0;
    }
    if (o.b(0x40)) {                                                     // turning round
        o.sprite = ctx.spr("sniturn");
        if (!animStep(ctx, o, o.anim(0x42), kAnim_turn7, 4)) { exitDespawnWide(ctx, o); return; }
        o.b(0x40) = 0; o.frame = 0;
        o.l(0x54) = -o.l(0x54); o.facing ^= 1;
        o.x += 8;
        if (o.facing == 0) o.x -= 0x10;
    }
    o.b(0x34) -= 1;                                                      // slither frames 0..5 every 4 ticks
    if (int8_t(o.b(0x34)) < 0) {
        o.sprite = ctx.spr("snislthr"); o.b(0x34) = 3;
        if (o.frame >= 6) o.frame = 0;
        if (++o.frame == 6) o.frame = 0;
    }
    moveX(o, o.l(0x54));
    o.w(0x4c) -= 1;
    if (o.w(0x4c) < 0) {
        o.w(0x4c) = int16_t((ctx.rnd() & 0x1f) + 0x10);
        const bool ahead = (ctx.harry.x() - o.x >= 0) ? o.l(0x54) >= 0 : o.l(0x54) < 0;
        if (ahead) { o.b(0x38) = 0xfb; o.w(0x3c) = 0; }
    }
    { int y = o.y + 0xe; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0xe); }
    auto turn = [&] {                                                    // 0x11d1ac
        o.b(0x40) = 0xff; moveX(o, -o.l(0x54)); o.w(0x42) = 0;
        o.sprite = ctx.spr("sniturn"); o.frame = 0;
        exitDespawnWide(ctx, o);
    };
    const int leash = o.w(0x58) - o.x;
    if (o.facing ? leash >= 0x20 : leash < -0x20) { turn(); return; }
    const int lv = ctx.levelNo;
    if (lv == 3 || lv == 8 || lv == 1 || lv == 6) {                     // these levels: look for the floor's edge
        int y = o.y + 0xe;
        if (!ctx.floorSnap(o.x + (o.facing ? -0x28 : 0x28), y)) { turn(); return; }
    } else {
        o.b(0x48) -= 1;
        if (int8_t(o.b(0x48)) < 0) {
            o.b(0x48) = 5;
            int y = o.y + 0xb;
            if (!ctx.floorSnap(o.x + 0x20 - (o.facing ? 0x58 : 0), y)) { turn(); return; }
        }
        if (o.b(0x48) == 3 && (ctx.triggerClass(o.x + (o.facing ? -0x10 : 0xc), o.y - 0x10) & 1)) { turn(); return; }
    }
    exitDespawnWide(ctx, o);
}

// ---------------------------------------------------------------- crocodiles (types 13 / 20)

// 0x4085fe: a crocodile (type 13) that surfaces, swims a stretch and sinks again; Harry can
// ride it.  Variants by the initial frame: 0xfc patrols back and forth (b56) at 1.125
// px/tick, 0xfd swims left from 0x30 right of home, 0xfe swims right from 0x30 left of
// home (both return home when they sink).  w32 counts the 0x90-tick surface time (it only
// moves from 0x16 to 0x79), w44 the hidden time (0x20 / 0x30 ticks).
void h_crocSwimmer(ObjCtx& ctx, Obj& o) {
    o.behind = 0xff;
    Harry& h = ctx.harry;
    auto surface = [&] {                                                 // 0x408663
        o.w(0x32) = 0x90;
        o.script = kAnim2_crocSwim; o.l(0x3c) = 0;                       // 0x45a9e2 (its tick, b40, is kept)
        o.l(0x48) = 0;                                                   // the sink script's pc (0x45a9ef)
        o.w(0x44) = o.b(0x56) ? 0x20 : 0x30;
        o.frame = 0xff;
    };
    auto carry = [&] {                                                   // 0x40880a: Harry rides it (box 0x45a9da)
        if (ctx.pl.harryOnPlatform() || ctx.pl.harryBusy() || h.w(0x465baa) < 0) return;
        if (!harryOnBox(ctx, o, -26, 43, -5, 5)) return;
        if (int16_t(o.w(0x32) - 0x7a) < 0 && int16_t(o.w(0x32) - 0x16) >= 0) ctx.pl.carryHarry(o.l(0x36));
        h.y() = int16_t(o.y - 1);
        if (o.sprite && o.sprite->name() == "crzrippl") h.y() += 2;
        h.vy() = 0;
        h.b(0x465b73) = 0xff;
    };
    if (o.frame == 0xfc) { o.b(0x56) = 0xff; o.l(0x36) = 0x12000; o.facing = 1; surface(); }
    else if (o.frame == 0xfd) { o.w(0x46) = int16_t(o.x + 0x30); o.l(0x36) = -0x12000; surface(); }
    else if (o.frame == 0xfe) { o.facing = 0xff; o.w(0x46) = int16_t(o.x - 0x30); o.l(0x36) = 0x12000; surface(); }
    for (int guard = 0; guard < 4; ++guard) {
        if (o.w(0x44)) {                                                 // 0x40869f: under water
            o.w(0x44) -= 1;
            if (o.w(0x44) == 0) {
                o.script = kAnim2_crocSwim; o.l(0x3c) = 0;
                spawnRipple(ctx, o, 0, 0);
                if (!ctx.channel1Busy()) ctx.sound1(0xbb, 6);
            }
            if (o.b(0x56)) carry();
            exitDespawnWide(ctx, o);
            return;
        }
        if (int16_t(o.w(0x32) - 0x7a) < 0 && int16_t(o.w(0x32) - 0x16) >= 0) moveX(o, o.l(0x36));
        if (o.w(0x32)) {
            o.w(0x32) -= 1;
            if (o.w(0x32) == 0) {
                spawnRipple(ctx, o, 0, 0);
                if (!ctx.channel1Busy()) ctx.sound1(0x41, 6);
            }
            anim2Step(ctx, o, 0x3c, 6);                                  // (o.script = kAnim2_crocSwim)
            carry();
            exitDespawnWide(ctx, o);
            return;
        }
        o.script = kAnim2_crocSink;                                      // sinking (pc at l48, tick b4c)
        anim2Step(ctx, o, 0x48, 6);
        if (o.frame != 0xff) { exitDespawnWide(ctx, o); return; }
        if (o.b(0x56)) { o.l(0x36) = -o.l(0x36); o.facing ^= 1; }
        else o.x = o.w(0x46);
        surface();                                                       // and on from 0x40869f
    }
}

// 0x40b08a: a crocodile lying in the water (type 20, crzopen) that Harry can stand on.
// It blinks (frames 0 / 1), snaps its jaws every 0x34..0x73 ticks (frames 2..5: nothing
// to stand on), and when Harry is drowning within 0x50 px it dives after 30 ticks and
// swims off (crzdive, then gone).
void h_crocLurker(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xff) {
        o.w(0x56) = 0x1e; o.behind = 0xff; o.frame = 0;
        o.w(0x32) = int16_t((ctx.rnd() & 0x3f) + 0x54);
    }
    Harry& h = ctx.harry;
    const bool diving = o.sprite && o.sprite->name() == "crzdive";
    if (!diving && h.cont() == 0x427528 && int16_t(h.x() - o.x + 0x50) >= 0 && int16_t(h.x() - o.x - 0x50) < 0) {
        // 0x40b0dd: Harry drowning within 0x50 px
        if (o.w(0x56)) { o.w(0x56) -= 1; exitDespawnFar(ctx, o); return; }
        o.sprite = ctx.spr("crzdive");                                   // (FUN_004263c4 / FUN_004263a6(5): +0x26 only)
    }
    if (diving || (o.sprite && o.sprite->name() == "crzdive")) {
        if (animStep(ctx, o, o.anim(0x58), kAnim_crocDive, 4)) {
            ctx.sound0(0xd, 0x50);                                       // FUN_0042633d(0xd, 0x60)
            ctx.pl.restoreEntry(o); ctx.release(o);
            return;
        }
        exitDespawnFar(ctx, o);
        return;
    }
    if (int8_t(o.frame - 4) < 0 && h.w(0x465baa) >= 0 && harryOnBox(ctx, o, -26, 32, -5, 5)) {
        // 0x40b1f6: he stands on it (box 0x45b936)
        h.y() = int16_t(o.y - 4);
        o.w(0x34) = 0;
        ctx.rnd();                                                       // (sic: a new snap timer is drawn but never stored)
        o.frame = 0;
        h.vy() = 0;
        h.b(0x465b73) = 0xff;
        exitDespawnFar(ctx, o);
        return;
    }
    if (o.frame == 0 && ctx.freeze) { exitDespawnFar(ctx, o); return; }
    if (o.w(0x34)) {                                                     // snapping
        o.sprite = ctx.spr("crzopen");
        if (animStep(ctx, o, o.anim(0x34), kAnim_crocBite, o.anim(0x34).pos < 0xa ? 8 : 2)) o.frame = 0;
        else { exitDespawnFar(ctx, o); return; }
    }
    o.b(0x50) -= 1;                                                      // blink
    if (int8_t(o.b(0x50)) < 0) { o.b(0x50) = uint8_t((ctx.rnd() & 0xf) + 0xb); o.frame ^= 1; }
    o.w(0x32) -= 1;
    if (o.w(0x32) == 0) { o.w(0x32) = int16_t((ctx.rnd() & 0x3f) + 0x34); o.w(0x34) = 1; }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- fire priest and its fireball (types 45 / 44)

// 0x40fa04: the fireball (type 0x2c, fxzfireb).  While it is still in the priest's hands
// (frames below 4) it waits whenever the priest flinches and fizzles out (w32, the 0x45abd7
// script) when the priest is beaten; then it flies, homing on Harry (x speed +-0x5000 per tick
// up to 7 px, y +-0x1000 up to 1 px), for at most 250 ticks and within -0x28..0x40 px of its
// launch height.  Frames follow its x speed (2 while it hangs still, 3 at 1 px/tick).  17
// damage; the whip, Harry or a wall marker put it out.
void h_fireball(ObjCtx& ctx, Obj& o) {
    if (o.w(0x32)) {
        if (animStep(ctx, o, o.anim(0x32), kAnim_fireballDie, 3)) { ctx.release(o); return; }
        exitDespawnWide(ctx, o);
        return;
    }
    auto die = [&] { o.w(0x32) = 0x100; exitDespawnWide(ctx, o); };
    if (o.frame < 4) {
        Obj* p = o.link;
        if (!p || p->freed || p->type != 45 || p->b(0x32) != 0) { o.w(0x32) = 0x100; return; }
        if (p->b(0x6b) != 0) { exitDespawnWide(ctx, o); return; }        // the priest flinches: hold
    }
    o.w(0x6a) += 1;
    if (o.w(0x6a) >= 0xfa) o.w(0x32) = 0x100;
    const int vx = o.w(0x5c);                                            // whole pixels of l5a
    if (vx == 0) o.frame = 2;
    else if (vx == 1 || vx == -1) o.frame = 3;
    else {
        anim2Step(ctx, o, 0x36, 3);
        if (o.frame != 5 && o.frame != 4) { exitDespawnWide(ctx, o); return; }
    }
    if (weaponHits(ctx, o, 0, nullptr)) { die(); return; }
    if (ctx.touchesHarry(o)) { ctx.hurtHarry(0x11); o.w(0x32) = 0x100; }  // 0x407b0f
    if (vx > 0) o.facing = 0; else if (vx < 0) o.facing = 0xff;
    if (!between(o.w(0x64) - o.y, -0x28, 0x40)) { die(); return; }
    moveX(o, o.l(0x5a));
    moveY(o, o.l(0x5e));
    if (ctx.harry.y() - 0x20 - o.y >= 0) { if (o.l(0x5e) < 0x10000) o.l(0x5e) += 0x1000; }
    else                                { if (o.l(0x5e) >= -0x10000) o.l(0x5e) -= 0x1000; }
    if (ctx.harry.x() - o.x >= 0) { if (o.l(0x5a) < 0x70000) o.l(0x5a) += 0x5000; }
    else                         { if (o.l(0x5a) >= -0x70000) o.l(0x5a) -= 0x5000; }
    if (ctx.triggerClass(o.x, o.y) & 1) { die(); return; }
    exitDespawnWide(ctx, o);
}

// 0x40fc84: a temple fire priest (type 45).  It rises from the floor (tprapea2 frames 0..7,
// then tprhigh), conjures a fireball (type 0x2c) on each pass of its main script - holding
// on frame 6 while the last one is still alive - and turns (tprapea2 7..2..7) when Harry gets
// behind it.  69 hp; a hit makes it flinch for 9 ticks (tpzhit).  Beaten, it collapses
// (0x45a849, tpzhit): on normal it falls apart for good (2000 points) - a whip on frame 4 of
// the collapse hurries it - but on hard it reassembles with its hit count full, and the whip
// only sends it back to the start of the fall.  15 contact damage while upright.
void h_firePriest(ObjCtx& ctx, Obj& o) {
    auto spriteIs = [&](const char* n) { return o.sprite && o.sprite->name() == n; };
    if (o.frame == 0xfe) {                                               // first tick
        int y = o.y + 0xd; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0xd);
        o.frame = 0xff;
        o.l(0x36) = 0; o.b(0x3a) = 0;                                    // main script (0x45a82f)
        o.l(0x46) = 0; o.b(0x4a) = 0; o.b(0x4b) = 0xff;                  // rising script (0x45a814) running
        if (ctx.harry.x() - o.x < 0) o.facing = 1;
    }
    if (!spriteIs("tpzhit")) harryContact(ctx, o, 0xf);
    if (o.w(0x52)) {                                                     // turning
        o.sprite = ctx.spr("tprapea2");
        const bool done = animStep(ctx, o, o.anim(0x52), kAnim_priestTurn, 4);
        if (o.w(0x52) == 5) o.facing ^= 1;                               // (cursor at frame 5, first tick)
        if (!done) { exitDespawnFar(ctx, o); return; }
        o.sprite = ctx.spr("tprhigh");
    }
    if (o.b(0x4b)) {                                                     // rising
        if (!anim2Step(ctx, o, kAnim2_priestRise, 0x46, 4)) { exitDespawnFar(ctx, o); return; }
        o.b(0x4b) = 0;
    }
    auto collapse = [&] {                                                // 0x40fdba
        if (animStep(ctx, o, o.anim(0x34), kData_priestDeath, 4)) {
            if (!ctx.hard) { ctx.g.stats().score += 2000; ctx.release(o); return; }
            o.b(0x32) = 0; o.hits = 0x45;                                // hard: back on its feet
            o.sprite = ctx.spr("tprhigh"); o.frame = 0;
            return;
        }
        if (o.w(0x34) == 0x31 && !ctx.hard) { o.w(0x34) = 0x5d; o.hits = 0; }   // skip the reassembly
        if (o.frame == 4 && weaponHits(ctx, o, 0, nullptr)) o.b(0x34) = ctx.hard ? 0x31 : 0x5d;
        exitDespawnFar(ctx, o);
    };
    if (o.b(0x32)) { collapse(); return; }
    if (spriteIs("tprhigh")) {
        bool killed = false;
        const bool hit = weaponHits(ctx, o, 0x45, &killed);
        if (killed) { o.b(0x32) = 0xff; o.b(0x6b) = 0; o.sprite = ctx.spr("tpzhit"); collapse(); return; }
        if (hit) { o.b(0x6b) = 9; o.b(0x6a) = o.frame; }
    }
    if (o.b(0x6b)) {                                                     // flinching
        o.sprite = ctx.spr("tpzhit"); o.frame = 4;
        o.b(0x6b) -= 1;
        if (o.b(0x6b)) { exitDespawnFar(ctx, o); return; }
        o.sprite = ctx.spr("tprhigh"); o.frame = o.b(0x6a);
    }
    if (o.frame == 6) {
        const bool behind = ctx.harry.x() < o.x ? o.facing == 0 : o.facing != 0;
        if (behind) { o.w(0x52) = 0x100; exitDespawnFar(ctx, o); return; }
        Obj* f = o.link2;
        if (f && !f->freed && f->type == 0x2c) { exitDespawnFar(ctx, o); return; }   // its fireball still burns
        o.link2 = nullptr;
    }
    anim2Step(ctx, o, kAnim2_priestMain, 0x36, 4);
    if (o.b(0x3a) == 0 && o.l(0x36) == 4) {                              // 0x45a833: the fireball appears
        if (Obj* f = ctx.spawn()) {                                      // FUN_004356bc(3)
            f->w(0x26) = 0x6663;
            o.link2 = f;
            ctx.sound0(0x28, 8);                                         // FUN_0042633d(0x28, 0x60)
            f->type = 0x2c; f->facing = o.facing;
            f->x = int16_t(o.x - 0x1a); f->y = int16_t(o.y - 0x38);
            f->sprite = ctx.spr("fxzfireb"); f->flags = 0x2000;
            f->script = kAnim2_fireball; f->l(0x36) = 0; f->b(0x3a) = 0;
            f->l(0x5e) = 0; f->w(0x64) = f->y;
            f->link = &o;
            f->l(0x5a) = 0x60000;
            if (f->facing) { f->l(0x5a) = -0x60000; f->x += 0x33; }
        }
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- temple archer and its arrow (types 56 / 57)

// 0x412124: the archer's arrow (type 0x39, tgfarrow): flies at l32 (+-6.1 px/tick) until a
// wall marker, where it sticks for 30 ticks (blinking on odd counts from 15 up - so while
// it is fresh) and vanishes.  It hurts Harry for 15 and bounces off him (a quarter of its
// speed back, falling under 0x5000 until it hits the floor).
void h_arrow(ObjCtx& ctx, Obj& o) {
    ctx.pl.setTriggerClass(2, 0);            // the arrow flies through id-2 markers
    if (o.l(0x64)) {                                                     // bounced off Harry
        o.l(0x64) += 0x5000;
        moveY(o, o.l(0x64));
        moveX(o, o.l(0x68));
        int y = o.y;
        if (ctx.floorSnap(o.x, y)) { ctx.release(o); return; }
        exitDespawnFar(ctx, o);
        return;
    }
    if (ctx.touchesHarry(o)) {                                           // 0x407b0f
        ctx.hurtHarry(0xf);
        o.l(0x64) = 0x200;
        o.l(0x68) = -(o.l(0x32) >> 2);
    }
    if (o.w(0x5a)) {                                                     // stuck in the wall
        if (o.w(0x5a) >= 0xf && (o.w(0x5a) & 1)) o.frame ^= 1;
        o.w(0x5a) -= 1;
        if (o.w(0x5a) == 0) { ctx.release(o); return; }
        exitDespawnFar(ctx, o);
        return;
    }
    if (ctx.triggerClass(o.x - (o.l(0x32) < 0 ? 0x10 : 0), o.y) & 1) o.w(0x5a) = 0x1e;
    moveX(o, o.l(0x32));
    exitDespawnFar(ctx, o);
}

// 0x411aae: a temple archer (type 56).  It waits inside the wall until Harry comes within
// 0xd8 x 0x90 px, then peels out of it (tgrpeel; harmless until frame 7), drops to the
// floor and walks its ledge (tgrwalk, 0.5 px/tick, turning at edges, wall markers and - at
// random intervals - to face Harry).  Every 6..9 steps it stops and shoots an arrow (type
// 0x39), high or low at random; every 3rd or 4th time it pauses longer.  30 hp, 13 contact
// damage.  Faces left when b0b is set.
void h_templeArcher(ObjCtx& ctx, Obj& o) {
    auto spriteIs = [&](const char* n) { return o.sprite && o.sprite->name() == n; };
    for (int pass = 0; pass < 2; ++pass) {                               // (a turn re-enters at the top)
        if (o.b(0x32) == 0) {                                            // still in the wall
            o.w(0x64) = 1; o.w(0x66) = 4;
            o.facing = uint8_t((o.x >> 4) & 1);
            if (!between(o.x - ctx.harry.x(), -0xd8, 0xd8) || !between(o.y - ctx.harry.y(), -0x90, 0x90)) {
                exitDespawnMid(ctx, o);
                return;
            }
            o.b(0x32) = 0xff;
            o.w(0x34) = 1;                                               // (sic: the peel starts at its second frame)
            o.l(0x3c) = 0x20000;
            o.b(0x42) = 1;
            if (o.facing) o.l(0x3c) = -o.l(0x3c);
        }
        if (!(spriteIs("tgrpeel") && o.frame < 7)) harryContact(ctx, o, 0xd);
        bool killed = false;
        if (weaponHits(ctx, o, 0x1e, &killed) && killed) { killObject(ctx, o); return; }
        if (o.b(0x33)) { o.b(0x33) -= 1; return; }                       // pausing (no exit test)
        if (o.w(0x46)) {                                                 // shooting
            const bool low = o.l(0x48) == 1;
            o.sprite = ctx.spr(low ? "tgrlarro" : "tgrharro");
            const bool done = animStep(ctx, o, o.anim(0x46), low ? kAnim_guardShotLow : kAnim_guardShotHigh, 6);
            if (o.w(0x46) == (low ? 0xc : 0xb)) {                        // the arrow leaves the bow
                if (Obj* a = ctx.spawn()) {                              // FUN_0043546e(1)
                    a->w(0x26) = 0x6663;
                    a->type = 0x39; a->frame = 1;
                    a->sprite = ctx.spr("tgfarrow");
                    a->l(0x32) = 0x62000;
                    a->x = o.x; a->xFrac = o.xFrac; a->y = o.y; a->yFrac = o.yFrac;
                    a->x += 0x10; a->y -= 0x29;
                    if (low) a->y += 0x14;
                    if (o.facing) { a->facing ^= 1; a->l(0x32) = -a->l(0x32); a->x -= 0x20; }
                }
                exitDespawnMid(ctx, o);
                return;
            }
            if (!done) { exitDespawnMid(ctx, o); return; }
            o.w(0x3c) = 0;
        }
        if (o.w(0x3a)) {                                                 // turning (tgrturn)
            o.sprite = ctx.spr("tgrturn");
            const bool done = animStep(ctx, o, o.anim(0x3a), kAnim_guardTurn, 4);
            if (o.w(0x3a) == 3) o.facing ^= 1;
            if (!done) { exitDespawnMid(ctx, o); return; }
        }
        if (o.w(0x34)) {                                                 // peeling out of the wall
            if (o.w(0x34) == 6) {                                        // ...and dropping to the floor
                if (o.w(0x36) != -5) o.l(0x36) += 0x3000;                // (sic: the cap tests the fraction)
                moveY(o, o.l(0x36));
                if (o.w(0x3c)) { o.l(0x3c) -= 0x900; moveX(o, o.l(0x3c)); }   // (sic: so it never moves sideways)
                int y = o.y + 0xe;
                const bool landed = ctx.floorSnap(o.x, y);
                o.y = int16_t(y - 0xe);
                if (landed) { o.w(0x3a) = 0; o.w(0x34) = 0xa; }
                exitDespawnMid(ctx, o);
                return;
            }
            o.sprite = ctx.spr("tgrpeel");
            if (!animStep(ctx, o, o.anim(0x34), kAnim_guardPeel, 5)) { exitDespawnMid(ctx, o); return; }
            o.frame = 0; o.sprite = ctx.spr("tgrharro");
            o.b(0x33) = 5;
            o.w(0x34) = 0; o.w(0x38) = 0; o.w(0x3c) = 0;
            o.l(0x3e) = o.facing ? -0x8000 : 0x8000;
            exitDespawnMid(ctx, o);
            return;
        }
        // walking (0x411e99)
        bool turn = false;
        o.w(0x38) -= 1;
        if (o.w(0x38) < 0) {
            o.w(0x38) = int16_t((ctx.rnd() & 0x1f) + 0x30);
            turn = ctx.harry.x() < o.x ? o.facing == 0 : o.facing != 0;
        }
        if (!turn) {
            o.sprite = ctx.spr("tgrwalk"); o.frame = 0;
            if (o.w(0x66)) { o.w(0x66) -= 1; o.w(0x38) = 0; }
            else if (ctx.triggerClass(o.x + (o.l(0x3e) < 0 ? -0x28 : 0x28), o.y - 0x10) & 1) o.frame = 5;
            else { animStep(ctx, o, o.anim(0x3c), kAnim_guardWalk, 4); moveX(o, o.l(0x3e)); }
            o.b(0x42) -= 1;
            if (o.b(0x42) == 0) {                                        // time to shoot
                const uint32_t r = ctx.rnd() & 3;
                o.b(0x42) = uint8_t(r + 6);
                o.w(0x66) = int16_t(r + 6 + 2);
                o.w(0x64) -= 1;
                if (o.w(0x64) < 0) { o.w(0x64) = int16_t((ctx.rnd() & 1) + 3); o.b(0x42) += 0x50; }
                o.w(0x46) = 1;                                           // (pos 1)
                o.l(0x48) = (ctx.rnd() & 1) ? 1 : 2;                     // 0x45bb42 low / 0x45bb52 high
            }
            int y = o.y + 0xe; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0xe);
            int y2 = o.y + 0xe;
            if (ctx.floorSnap(o.x - (o.facing ? 0x20 : 0), y2)) { exitDespawnMid(ctx, o); return; }
            o.w(0x38) = int16_t((ctx.rnd() & 0x1f) + 0x30);             // the ledge ends: turn round
        }
        o.l(0x3e) = -o.l(0x3e);                                          // 0x411ef7
        o.w(0x3a) = 1;
        o.w(0x3c) = 0;
    }
    exitDespawnMid(ctx, o);
}

// ---------------------------------------------------------------- default

// Types without a port yet: show the template frame and leave when far away.
void h_default(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xff && o.sprite) o.frame = 0;
    exitDespawnFar(ctx, o);
}

} // namespace

Handler ObjectSystem::handler(uint8_t type) {
    switch (type) {
    case 2:    return h_item;
    case 3:    return h_checkpoint;
    case 0x10: return h_sparkle;
    case 8:    return h_web;
    case 10:   return h_condor;
    case 18:   return h_bat;
    case 23:   return h_rat;
    case 0x1d: return h_projectile;
    case 30:   return h_piston;
    case 38:   return h_pot;
    case 46:   return h_torch;
    case 61:   return h_gargoyle;
    case 62:   return h_axe;
    case 91:   return h_tonguePlant;
    case 59:   return h_pitCreature;
    case 51:   return h_floorSpikes;
    case 97:   return h_ceilingSpikes;
    case 100:  return h_fireJet;
    case 26:   return h_lever;
    case 27:   return h_bungee;
    case 31:   return h_door;
    case 33:   return h_chute;
    case 0x28: return h_pit;
    case 0x30: return h_bossMeter;
    case 55:   return h_jaguar;
    case 0x51: return h_bossDeathBurst;
    case 0x2b: return h_sleepingBat;
    case 0x3c: return h_treeSnakeBody;
    case 0x40: return h_feather;
    case 70:   return h_climbingRat;
    case 74:   return h_pouchRespawner;
    case 72:   return h_vineMonkey;
    case 78:   return h_batRoost;
    case 32:   return h_verticalWeb;
    case 34:   return h_tnt;
    case 35:   return h_monkey;
    case 0x32: return h_explosion;
    case 0x53: return h_drip;
    case 0x68: return h_effect;
    case 80:   return h_boar;
    case 87:   return h_explodingPlant;
    case 96:   return h_ceilingDripper;
    case 102:  return h_fly;
    case 63:   return h_leafSpawner;
    case 76:   return h_fallingRock;
    case 116:  return h_floorSkull;
    case 117:  return h_hoppingSkull;
    case 0x76: return h_markerFlame;
    case 109:  return h_snake;
    case 110:  return h_spider;
    case 112:  return h_rat2;
    case 123:  return h_branch;
    case 124:  return h_branchTip;
    case 125:  return h_branchPiece;
    case 133:  return h_scorpion;
    case 0x82: return h_deathCloud;
    case 142:  return h_heart;
    case 160:  return h_eyes;
    case 161:  return h_dot;
    case 147:  return h_coin;
    case 150:  return h_ceilingRock;
    case 156:  return h_hangingPouch;
    case 0xaa: return h_oneUpFly;
    case 172:  return h_treeSnake;
    case 9:    return h_dropBall;
    case 11:   return h_turtle;
    case 12:   return h_roamingSnake;
    case 13:   return h_crocSwimmer;
    case 20:   return h_crocLurker;
    case 36:   return h_spit;
    case 28:   return h_spear;
    case 39:   return h_firePit;
    case 44:   return h_fireball;
    case 45:   return h_firePriest;
    case 119:  return h_whipLever;
    case 164:  return h_snakeRespawner;
    case 47:   return h_fish;
    case 52:   return h_stoneBlock;
    case 56:   return h_templeArcher;
    case 57:   return h_arrow;
    case 67:   return h_wheel;
    case 148:  return h_pitWheel;
    case 149:  return h_wheelSpawner;
    case 82:   return h_dripper;
    case 0x81: return h_ripple;
    case 66:   return h_skullWarrior;
    case 68:   return h_swingBall;
    case 85:   return h_hawk;
    case 1:    return h_static;
    case 113:  return h_hidden1up;
    case 174:  return h_leanLever;
    case 173:  return h_oneWayDoor;
    case 0:    return nullptr;
    default:   return h_default;
    }
}

} // namespace game
