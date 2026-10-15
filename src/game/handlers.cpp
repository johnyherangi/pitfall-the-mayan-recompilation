// Per-type object behaviour, ported from the handler table at 0x462350.
// Each handler quotes the address it was read from; state offsets are the
// original ones (see objects.h).  Helper names follow objects.h.
#include "game/objects.h"
#include "ph/arena.h"

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
    if (o.b(0x57) && (priorityTrigger(ctx, o, 0).cls & 1)) { ctx.release(o); return; }   // 0x4301ce
    if (o.w(0x60)) { o.w(0x60) -= 1; return; }
    if (o.link && o.link->type != 5) { ctx.release(o); return; }   // +0x5c: dies once that record is no longer a stone
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
        st.ammoCount[0] = uint8_t(st.ammoCount[0] + (ctx.hard ? 8 : 10));
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
            ctx.harry.w(0x46652f) = 0x300; st.score += 750; ctx.harry.b(0x465897) = 0;
            ctx.g.setCdVolumeCmd(1);                     // DAT_00450500: the CD turned down
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
        animStep(ctx, o, o.anim(0x42), ctx.pl.bossActive() ? kAnim_heartBoss : kAnim_heart, 6);   // 0x405589: faster beat during a boss fight
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

Obj* spawnFromTemplate(ObjCtx& ctx, const Obj& at, int templateId);

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
        Obj* u = spawnFromTemplate(ctx, o, 0x59);      // the template at 0x467f34 (mi_1up)
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
        if (Obj* e = ctx.spawnFrom(o)) {              // FUN_004356bc(1)
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
    halfMinuteTick(o);                        // 0x4057b0
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
        weaponHits(ctx, o, 0, &killed);
        if (!killed) return;
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
        ctx.g.lifeIconOneUp();                        // DAT_00451a24 = DAT_004601d9, DAT_0046fa38 = 1
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
        o.w(0x4c) = int16_t((ctx.rnd() & 0x3f) + 0x45);   // (overwritten at once by the speed's high word)
        o.l(0x4a) = -0x1e000;                 // slither speed, 16.16 (negative = left)
        o.facing = 1;
        o.b(0x32) = 0xff;
    }
    priorityTrigger(ctx, o, o.facing ? -0x20 : 0);
    bool killed = false;
    weaponHits(ctx, o, 10, &killed);
    if (killed) { killObject(ctx, o); return; }
    if (ctx.frozen()) { if (o.frame != 0xff) freezeSparkle(ctx, o); return; }
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
    if (o.w(0x66)) { o.w(0x66) -= 1; moveX(o, o.l(0x4a)); }   // double speed
    {
        // 0x41932d: it hurries while Harry is more than 0x88 px away, and for 0x32 ticks after
        // he comes nearer.
        const int16_t dx = int16_t(ctx.harry.x() - o.x);
        if (int16_t(dx - 0x88) >= 0 || int16_t(dx + 0x88) < 0) o.w(0x66) = 0x32;
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
    Obj* c = ctx.spawn();                       // FUN_004356bc(1)
    if (!c) return;
    c->w(0x26) = 0x6663;                        // (d0 != 0: FUN_004263a6)
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
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
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
        // Running; faster when Harry is just ahead.  (Facing left the test at 0x40e76d falls
        // through into the facing-right one, which then fails: only a monkey facing right speeds up.)
        int32_t speed = 0x12000;
        const int dy = ctx.harry.y() - o.y, dx = ctx.harry.x() - o.x;
        if (between(dy, -0x18, 0x38) && o.facing == 0 && between(dx, 0, 0x60)) speed = 0x21000;
        if (o.w(0x32) < 0) {
            moveX(o, -speed);
            if (o.x < 0) {                    // off the left end of the level: turn and step back
                o.w(0x32) = -o.w(0x32); o.facing ^= 1;
                moveX(o, speed);
            }
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
        if (between(dy, -0x18, 0x38) && between(dx, -0x60, 0x60)) { exitDespawnWide(ctx, o); return; }
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
        o.script = kAnim2_monkeyToss; o.l(0x3e) = 0; o.w(0x42) = 0;
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
    weaponHits(ctx, o, 9, &killed);
    if (killed) { killObject(ctx, o); return; }
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
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
        Obj* f = ctx.spawnFrom(o, true);      // FUN_0043546e(1)
        if (!f) break;
        f->w(0x26) = 0x6663;
        f->w(0x4e) = 0x46;
        int dx = int(ctx.rnd() & 0x1f); if (d7 & 1) dx = -dx;
        f->x = int16_t(f->x + dx);
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
        if (ctx.frozen() || killed) { killObject(ctx, o); return; }
        condorStun(ctx, o);
        condorStates(ctx, o);
        return;
    }
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
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
    // (0x412d88 skips the next part when +0x57 of the record in a5 is set; a5 is left over from
    // the code before - usually Harry's record, whose +0x57 nothing sets - so it never skips here.)
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

// 0x412f14: type 65, a puff of dust from Harry's feet (his skids and landings): it drifts by
// l32 and rises 0x6000 a tick, gone when turn7 has run once or at a wall cell.
void h_dust(ObjCtx& ctx, Obj& o) {
    ctx.pl.setTriggerClass(2, 0);             // trigger id 2 does not stop thrown things
    o.flags = 0x6000;
    moveY(o, -0x6000);
    moveX(o, o.l(0x32));
    if ((ctx.triggerClass(o.x + (o.l(0x32) < 0 ? -0x18 : 6), o.y) & 1) ||
        animStep(ctx, o, o.anim(0x3a), kAnim_turn7, 4)) {      // 0x4619c7
        ctx.release(o);
        return;
    }
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- Harry's stones (types 4 / 5 / 0x89 / 0x8a)

// *(l3a) = 0: a thrown stone leaves its in-flight slot (DAT_00466808 .. 0x466818), where
// weaponHit looks for it.
void leaveSlot(ObjCtx& ctx, Obj& o) {
    const uint32_t a = uint32_t(o.l(0x3a));
    if (a >= 0x466808 && a <= 0x466818) ctx.harry.setUl(a, 0);   // (always one of the five)
}

// 0x40613c: type 5, a sling stone (obrston0).  It flies with gravity 0x300 a tick (plus l4a,
// set by a bounce off a boss) and a pull of 0x50 on l32, trailing sparkles after a power shot
// (b54), and breaks (fx_stone, turn7) at a wall cell, on a floor it falls onto at 2 px a tick
// or more, or at a slope that rises more than 2 px against it; off the screen it is dropped.
// A hit on an enemy (weaponHit) sets w40 and frees its slot itself.
void h_slingStone(ObjCtx& ctx, Obj& o) {
    for (;;) {                                                    // 0x40613c: a break runs it again
        ctx.pl.setTriggerClass(2, 0);                             // trigger id 2 does not stop thrown things
        if (o.b(0x54)) {
            o.b(0x55) -= 1;
            if (int8_t(o.b(0x55)) < 0) {
                o.b(0x55) = 2;
                if (Obj* s = ctx.spawnFrom(o, true, 0)) {          // FUN_0043546e(0)
                    s->type = 0x10; s->b(0x57) = 0xff; s->flags = o.flags;
                    s->link = &o;                                  // (+0x5c: it goes when this is no longer a stone)
                    s->sprite = ctx.spr("fx_sprk1");
                    s->l(0x32) = o.l(0x32) >> 2; s->l(0x36) = o.l(0x36) >> 2;
                }
            }
        }
        if (o.w(0x40)) {                                          // 0x4061f8: breaking
            o.sprite = ctx.spr("fx_stone");
            if (animStep(ctx, o, o.anim(0x40), kAnim_turn7, o.b(0x55) ? 1 : 3)) ctx.release(o);   // 0x4619c7
            return;
        }
        bool stop = (priorityTrigger(ctx, o, -0x10).cls & 1) != 0;   // 0x4301ce
        if (!stop) {
            moveX(o, o.l(0x32));
            moveY(o, o.l(0x36));
            const ph::ColumnTable& plane = ctx.harry.b(0x465bb3) ? ctx.dt2() : ctx.det();   // Harry's plane
            if (o.l(0x36) >= 0x20000) {
                stop = ph::probeCell(plane, o.x - 8, o.y).hit;    // FUN_0042571f / FUN_004258a2
            } else if (const ph::FloorHit f = ph::probeCell(plane, o.x - 8, o.y + 0xc); f.hit) {
                // the shape's left end less its right end, against the way it flies
                int16_t d = int16_t(ph::kCollisionHeights[f.shape * 16] - ph::kCollisionHeights[f.shape * 16 + 15]);
                if (o.l(0x32) >= 0) d = int16_t(-d);
                stop = int16_t(d + 2) < 0;
            }
            if (stop) o.facing = 0xff;
            else {
                o.l(0x32) += 0x50;
                o.l(0x36) += 0x300;
                o.l(0x36) += o.l(0x4a);
                if (!(ctx.triggerClass(o.x, o.y) & 1)) {
                    if (offScreen(ctx, o)) { leaveSlot(ctx, o); ctx.release(o); }   // 0x424f40
                    return;
                }
            }
        }
        // 0x406260: it breaks
        ctx.sound1(0xcd, 9);
        o.w(0x26) = 0x6663;                                       // FUN_004263a6(2), which never fails on the PC
        leaveSlot(ctx, o);
        o.w(0x40) = 0x100;
    }
}

// 0x45a968: the eight pieces of the big stone {l46, l4a}, taken away from x / y each tick.
const int32_t kStonePieces[8][2] = {
    {0, -0x44000}, {0x30000, -0x30000}, {0x44000, 0}, {0x30000, 0x30000},
    {0, 0x44000}, {-0x30000, 0x30000}, {-0x44000, 0}, {-0x30000, -0x30000}};

// 0x405c86: type 4, the big stone (mi_stone, thrown while DAT_004667af is set).  It rolls
// (turn4 at 2 ticks) with l32 and the vertical speed l5c for 15 ticks, less when it comes
// against a wall cell, or until an enemy marks it (b37, weaponHit), then explodes: it and three
// more fx_expl3 quarters, chained through +0x5c (the last three show on frame 2 only); at the
// start of that frame eight pieces (type 0x89) fly out.
void h_bigStone(ObjCtx& ctx, Obj& o) {
    ctx.pl.setTriggerClass(2, 0);                                 // trigger id 2 does not stop thrown things
    animStep(ctx, o, o.anim(0x5a), kAnim_turn4, 2);               // 0x461746
    auto next = [&](Obj* p) { return ctx.pl.objAt(uint32_t(p->l(0x5c))); };   // the +0x5c chain
    auto releaseAll = [&]() {                                     // 0x406003
        // (before the explosion +0x5c is the vertical speed; the original would free whatever
        // it points at)
        Obj* p = &o;
        for (int i = 0; i < 3 && (p = next(p)) != nullptr; ++i) ctx.release(*p);
        leaveSlot(ctx, o);
        ctx.release(o);
    };
    if (o.b(0x36) == 0) {
        if (o.b(0x37) == 0) {
            moveX(o, o.l(0x32));
            if (ctx.triggerClass(o.x + (o.l(0x32) < 0 ? -0x28 : 8), o.y - 0x18) & 1) o.w(0x46) = 0xe;
            moveY(o, o.l(0x5c));
            o.w(0x46) += 1;
            if (o.w(0x46) != 0xf) return;
        }
        // 0x405d6a: it explodes
        leaveSlot(ctx, o);
        o.b(0x36) = 0xff;
        o.w(0x26) = 0x6663;                                       // FUN_004263c4 / FUN_004263a6(2), which never fails on the PC
        o.facing = 0; o.vflip = 0;
        Obj* a3 = &o;
        for (int i = 0; i < 3; ++i) {
            Obj* p = ctx.spawnFrom(o, true);                      // FUN_0043546e(1)
            if (!p) { releaseAll(); return; }
            p->l(0x50) = int32_t(ctx.pl.addrOf(&o));
            p->flags = o.flags;
            std::memcpy(p->s + 0x12, o.s + 0x12, 2);
            p->sprite = ctx.spr("fx_expl3");
            if (i != 2) p->facing = 0xff;
            if (i != 1) p->vflip = 0xff;
            a3->l(0x5c) = int32_t(ctx.pl.addrOf(p));
            a3 = p;
        }
    }
    // 0x405f49
    o.sprite = ctx.spr("fx_expl3");
    if (animStep(ctx, o, o.anim(0x4a), kAnim_turn7, 4)) { releaseAll(); return; }   // 0x4619c7
    Obj* q1 = next(&o);
    Obj* q2 = q1 ? next(q1) : nullptr;
    Obj* q3 = q2 ? next(q2) : nullptr;
    if (q1) q1->frame = o.frame;
    const uint8_t f = o.frame == 2 ? o.frame : 0xff;
    if (q2) q2->frame = f;
    if (q3) q3->frame = f;
    if (o.w(0x4a) == 2) {                                         // the first tick of frame 2
        for (const auto& v : kStonePieces) {
            Obj* p = ctx.spawnFrom(o);                            // FUN_004356bc(1)
            if (!p) break;
            p->behind = 0xff; p->sprite = ctx.spr("obrston0"); p->frame = 0;
            p->flags = o.flags; p->type = 0x89;
            p->l(0x46) = v[0]; p->l(0x4a) = v[1];
            p->b(0x32) = 0x14; p->l(0x36) = 0;
        }
    }
}

// 0x41c0d6: the big stone's blast.  Every object in the list scores (but during a boss fight,
// level 5's aside); those whose class has bit 0 die (0x40855e, 0x433725), with these
// exceptions: the Warrior Spirit (0x69) and the later jaguars take damage, the Ceiba jaguar
// only inside its arena, pouches in the trees and pots (0x9c / 0x26) drop, the limp snake
// (0x58) lets go, and objects drawn hidden (frame 0xff) are spared.
void stoneBlast(ObjCtx& ctx) {
    ObjectSystem& os = ctx.pl.objectSystem();
    const bool scores = ctx.levelNo == 5 || !ctx.pl.bossActive();
    for (Obj* a0 = os.first(); a0; a0 = os.nextOf(*a0)) {
        const uint8_t t = a0->type;
        if (t == 0) continue;
        if (scores) ctx.g.stats().score += scoreForType(t);
        if (!(typeClass(t) & 1)) continue;
        if (t == 0x69) {
            a0->b(0x68) = 1;
            if (Obj* a2 = ctx.pl.objAt(uint32_t(a0->l(0x64)))) a2->w(0x6a) += 0x14;
            continue;
        }
        if (a0->frame == 0xff) continue;
        if (t == 0x9c || t == 0x26) { a0->b(0x6c) = 0xff; continue; }
        if (t == 0x58) { a0->b(0x38) = 0xff; a0->w(0x50) = 0; a0->b(0x42) = 0xff; continue; }
        if (t == 0x9e) { a0->w(0x6c) += 0x1e; continue; }
        if (t == 0x8c) { a0->w(0x6a) += 0x1e; continue; }
        if (t == 0x37) {
            if (int16_t(a0->x - 0xbd0) < 0 && int16_t(a0->x - 0xa30) >= 0) a0->w(0x6a) += 0x1e;
            continue;
        }
        if ((t == 0x20 || t == 0xac) && a0->link) ctx.release(*a0->link);   // the web's half (+0x5a), the snake's body (+0x6a)
        deathFxAdjust(*a0);                                       // 0x40855e
        debrisPuff(ctx, *a0);                                     // 0x433725
        ctx.release(*a0);
    }
}

// 0x41bfe2: type 0x89, a piece of the big stone's explosion.  It flies off (l46 / l4a, taken
// away from x / y) for 20 ticks.  The first piece to end in an object pass (DAT_004658a0,
// cleared by each pass) sounds the blast, flashes the screen white (DAT_0046b1b0 = 0xc0) and
// strikes every object (stoneBlast); the others copy its marks.  Each piece then turns into a
// spark (type 0x8a) where it was 2 ticks earlier.
void h_stonePiece(ObjCtx& ctx, Obj& o) {
    priorityTrigger(ctx, o, 0);                                   // 0x4301ce
    Harry& h = ctx.harry;
    if (h.b(0x465b72) != 0) { ctx.release(o); return; }
    if (o.b(0x32) == 0x10 && h.b(0x4658a1) != 0) return;          // (the high byte of the word: never set on the PC)
    moveX(o, -o.l(0x46));
    moveY(o, -o.l(0x4a));
    o.b(0x32) -= 1;
    if (o.b(0x32) != 0) {
        if (o.b(0x32) == 2) {
            if (o.l(0x36) == 0) { o.w(0x38) = o.x; o.w(0x3a) = o.y; }
            else {
                // (a piece that follows the record at l36 while it keeps the type in b3a; the big
                // stone's pieces have l36 = 0)
                Obj* t = ctx.pl.objAt(uint32_t(o.l(0x36)));
                if (t && t->type == o.b(0x3a)) { ctx.release(*t); o.w(0x38) = t->x; o.w(0x3a) = t->y; }
                else ctx.release(o);
            }
        }
        return;
    }
    bool copy = true;
    if (h.w(0x4658a0) == 0) {
        o.w(0x26) = 0x6663;                                       // FUN_004263a6(2), which never fails on the PC
        h.w(0x4658a0) = 1;
        h.l(0x4658a2) = int32_t(ctx.pl.addrOf(&o));
        ctx.sound0(7, 0x39);                                      // FUN_0042633d(7, 0x60)
        if (h.b(0x465b72) == 0) {
            ctx.pl.fadeCmd() = 0xc0;
            stoneBlast(ctx);
            copy = false;
        }
    }
    if (copy) {                                                   // 0x41c2e2
        o.w(0x12) = h.w(0x4658a0);
        o.w(0x26) = 0x6663;
        o.l(0x50) = h.l(0x4658a2);
    }
    // 0x41c305
    o.x = o.w(0x38); o.y = o.w(0x3a);
    o.facing = o.l(0x46) >= 0 ? 0xff : 0;
    o.sprite = ctx.spr("fx_stone");
    o.type = 0x8a;
    o.w(0x32) = 0;
}

// 0x41c420: type 0x8a, a spark from the big stone's explosion: it drifts back an eighth of the
// piece's speed and is gone when turn7 has run.
void h_stoneSpark(ObjCtx& ctx, Obj& o) {
    priorityTrigger(ctx, o, 0);                                   // 0x4301ce
    moveX(o, -(o.l(0x46) >> 3));
    moveY(o, -(o.l(0x4a) >> 3));
    o.flags = 0x6000;
    if (animStep(ctx, o, o.anim(0x32), kAnim_turn7, 4)) ctx.release(o);   // 0x4619c7
}


// ---------------------------------------------------------------- the boomerang (types 6 / 0x87)

bool harryOnBox(ObjCtx& ctx, const Obj& o, int x0, int x1, int y0, int y1);

int16_t exeWord(uint32_t va) { const uint8_t* m = harryMem(va); return m ? int16_t(m[0] | m[1] << 8) : 0; }

// 0x40695f: a bounce cell (trigger id 1) ahead of the boomerang, or above / below it, throws it
// back at 5 px a tick and off its path for 6 ticks (w5a); levels 0 and 5 have none.
void boomerangBounce(ObjCtx& ctx, Obj& o) {
    if (ctx.levelNo == 0 || ctx.levelNo == 5) return;
    if (ctx.trigger(o.x + (o.l(0x46) < 0 ? -0x18 : 8), o.y + 8).id == 1) {
        o.w(0x5a) = 6;
        if (o.type == 6) ctx.sound1(0xce, 4);
        o.l(0x46) = o.l(0x46) < 0 ? 0x50000 : int32_t(0xfffb0000);
    }
    if (ctx.trigger(o.x - 8, o.y + (o.l(0x4a) < 0 ? -8 : 0x18)).id == 1) {
        o.w(0x5a) = 6;
        if (o.type == 6) ctx.sound1(0xce, 4);
        o.l(0x4a) = o.l(0x4a) < 0 ? 0x50000 : int32_t(0xfffb0000);
    }
}

// 0x41be24: the boomerang breaks (sparkles, a stone puff, sound 0xed).
void boomerangBreak(ObjCtx& ctx, Obj& o) {
    debrisPuff(ctx, o);                                           // 0x433725
    ctx.spawnEffect(o, ctx.spr("fx_stone"), kAnim_turn7, 3);      // 0x433914, script 0x4619c7
    ctx.release(o);
    ctx.sound1(0xed, 7);
}

void h_boomerangBack(ObjCtx& ctx, Obj& o);

// 0x406487: type 6, the boomerang (fxrbmrng) on its way out.  It steers along the path at
// 0x461a90 ({dx, dy} words from where it was thrown, w50 / w52, mirrored when it faces left; one
// entry every 4 ticks), or homes in on an enemy (class bit 2) that comes within 0x34 px (l5c /
// b60, one entry every 12 ticks then; such a boomerang breaks at the end of the path).  At the
// path's end its slot (DAT_0046681c) is freed and it turns into type 0x87, which flies back.
void h_boomerang(ObjCtx& ctx, Obj& o) {
    ObjectSystem& os = ctx.pl.objectSystem();
    priorityTrigger(ctx, o, 0);                                   // 0x4301ce
    o.b(0x40) += 1;
    if (o.b(0x40) == 4) {
        o.b(0x40) = o.l(0x5c) ? 0xf8 : 0;
        o.l(0x3c) += 4;
        const uint32_t p = uint32_t(o.l(0x3c));
        if (exeWord(p) == int16_t(0x8000)) {
            ctx.harry.setUl(0x46681c, 0);
            if (o.b(0x6d)) { boomerangBreak(ctx, o); return; }
            for (Obj* a = os.first(); a; a = os.nextOf(*a))
                if (a->type == 0x87) { a->b(0x6d) = 0xff; break; }   // an older one on its way back breaks
            o.w(0x42) = 0; o.w(0x44) = 0xfa; o.type = 0x87;
            o.w(0x3c) = int16_t(o.w(0x50) + exeWord(p - 4));         // it heads for the path's last point
            o.w(0x3e) = int16_t(o.w(0x52) + exeWord(p - 2));
            h_boomerangBack(ctx, o);                                  // jmp 0x41bcb2
            return;
        }
    }
    bool follow = true;
    if (o.w(0x5a)) o.w(0x5a) -= 1;                                    // just bounced
    else if (o.l(0x5c)) {
        Obj* t = ctx.pl.objAt(uint32_t(o.l(0x5c)));
        if (t && t->type == o.b(0x60) && t->sprite != ctx.spr("skiroll") && t->sprite != ctx.spr("skimorph") &&
            t->sprite != ctx.spr("skijaw") && t->sprite != ctx.spr("skidie")) {
            follow = false;                                           // 0x406612: homing, 8 px jumps
            if (int16_t(t->x - o.x) >= 0) { if (int16_t(o.w(0x48) - 5) < 0) { o.l(0x46) += 0x18000; o.x += 8; } }
            else if (int16_t(o.w(0x48) + 5) >= 0) { o.l(0x46) -= 0x18000; o.x -= 8; }
            const int16_t ty = int16_t(t->y + deathFxYOff(t->type));
            if (int16_t(ty - o.y) >= 0) { if (int16_t(o.w(0x4c) - 5) < 0) { o.l(0x4a) += 0x18000; o.y += 8; } }
            else if (int16_t(o.w(0x4c) + 5) >= 0) { o.l(0x4a) -= 0x18000; o.y -= 8; }
        }
    } else {
        for (Obj* a = os.first(); a; a = os.nextOf(*a)) {             // 0x406716: something to home in on
            if (a->type == 0 || !(typeClass(a->type) & 4) || a->frame == 0xff) continue;
            const int16_t dx = int16_t(a->x - o.x), dy = int16_t(a->y - o.y);
            if (int16_t(dx + 0x34) < 0 || int16_t(dx - 0x34) >= 0 || int16_t(dy + 0x34) < 0 || int16_t(dy - 0x34) >= 0) continue;
            o.l(0x5c) = int32_t(ctx.pl.addrOf(a)); o.b(0x60) = a->type; o.b(0x6d) = 0xff;
            follow = false;
            break;
        }
    }
    if (follow) {                                                     // 0x40682a: along the path
        o.l(0x5c) = 0; o.b(0x60) = 0;
        const uint32_t p = uint32_t(o.l(0x3c));
        int16_t dx = exeWord(p);
        if (o.facing) dx = int16_t(-dx);
        if (int16_t(o.w(0x50) + dx - o.x) >= 0) { if (int16_t(o.w(0x48) - 0xa) < 0) o.l(0x46) += 0x26000; }
        else if (int16_t(o.w(0x48) + 0xa) >= 0) o.l(0x46) -= 0x26000;
        if (int16_t(o.w(0x52) + exeWord(p + 2) - o.y) >= 0) { if (int16_t(o.w(0x4c) - 9) < 0) o.l(0x4a) += 0x14000; }
        else if (int16_t(o.w(0x4c) + 9) >= 0) o.l(0x4a) -= 0x14000;
    }
    moveX(o, o.l(0x46));                                              // 0x4068fe
    moveY(o, o.l(0x4a));
    animStep(ctx, o, o.anim(0x42), kAnim_turn4, 3);                   // 0x461746
    boomerangBounce(ctx, o);
}

// 0x41bcb2: type 0x87, the boomerang on its way back (fxrbmrot): it steers for the end of its
// path (w3c / w3e) and Harry gets it back (one more boomerang) by touching it; he can also
// land on it from a jump and ride it (state 0x42f574, 0x406ab4).  After 250 ticks it breaks.
void h_boomerangBack(ObjCtx& ctx, Obj& o) {
    if (o.b(0x6d)) { boomerangBreak(ctx, o); return; }
    boomerangBounce(ctx, o);
    {
        Harry& h = ctx.harry;                                         // 0x406ab4
        if (h.vy() < 0x20000 && (h.cont() == 0x42cdb6 || h.cont() == 0x42c518) &&
            harryOnBox(ctx, o, -12, 12, 0x30, 0x50)) {                // 0x45ba46
            ctx.sound0(0x1b, 9);                                      // FUN_0042633d(0x1b, 0x60)
            h.setCont(0x42f574);
            h.w(0x465b47) = 0;
            h.setUl(0x465b41, ctx.pl.addrOf(&o));
        }
    }
    if (int16_t(o.x - o.w(0x3c)) < 0) {
        if (int16_t(o.w(0x48) - 4) >= 0) o.l(0x46) -= 0x6000;
        else { if (int16_t(o.w(0x48) + 4) < 0) o.l(0x46) += 0x7000; o.l(0x46) += 0x6000; }
    } else {
        if (int16_t(o.w(0x48) + 4) < 0) o.l(0x46) += 0x6000;
        else { if (int16_t(o.w(0x48) - 4) >= 0) o.l(0x46) -= 0x7000; o.l(0x46) -= 0x6000; }
    }
    if (int16_t(o.y - o.w(0x3e)) < 0) { if (int16_t(o.w(0x4c) - 4) < 0) o.l(0x4a) += 0xd000; }
    else if (int16_t(o.w(0x4c) + 4) >= 0) o.l(0x4a) -= 0xd000;
    moveX(o, o.l(0x46));
    moveY(o, o.l(0x4a));
    o.sprite = ctx.spr("fxrbmrot");
    animStep(ctx, o, o.anim(0x42), kAnim_boomerangSpin, 4);           // 0x45a8cd
    if (ctx.touchesHarry(o)) {
        ctx.sound0(0xeb, 8);                                          // FUN_004262d9
        ctx.g.stats().ammoCount[1] += 1;                              // DAT_00466529
        ctx.sparkleBurst(o, ctx.levelNo == 7 ? kSparkleOffsetsC : kSparkleOffsetsB);   // 0x433706
        ctx.release(o);
        return;
    }
    o.w(0x44) -= 1;
    if (o.w(0x44) == 0) boomerangBreak(ctx, o);
}


// ---------------------------------------------------------------- hollows, push levers, runaway carts (types 41 / 92 / 98 / 0x63)

uint8_t triggerIdAt(ObjCtx& ctx, int x, int y);
const LevelScriptEntry* scriptByLever(uint8_t id);
void levelScriptPull(ObjCtx& ctx, uint8_t leverId);

// 0x40f900: type 41, a hollow Harry can crawl into: crouched long enough (state 0x42f2f9 with
// its counter DAT_00465b71 at 0x16), with down held, over it (box 0x45bb1c) he crawls in
// (state 0x426fee).
void h_hollow(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    if (h.b(0x466766) && h.cont() == 0x42f2f9 && h.b(0x465b71) == 0x16 && harryOnBox(ctx, o, -0x1e, 0x1e, -0x32, 0x28)) {
        h.setUl(0x465b41, 0);
        h.x() = int16_t(o.x + 8);
        if (h.facing()) h.x() += 8;
        h.setCont(0x426fee);
    }
    exitDespawnFar(ctx, o);
}

// 0x417d0e: type 92, a lever (obfleve2) pushed over in three steps.  Harry within 0x30 px of
// its height, 2..0x36 px off it and facing it is held against it (his x is set from its own,
// 0x24 px plus the travel left, w3e, away); walking into it on the ground starts the push
// states (0x42f0c4 from the right, 0x42f123 from the left).  Every 6 ticks of a push it gives
// 2 px; on the third step it runs the level script for the trigger under it (FUN_0041804b).
void h_pushLever(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    auto leverTrigger = [&]() { return triggerIdAt(ctx, o.x + o.w(0x64) - 0x20, o.y + 0x18); };
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff; o.facing = 0xff; o.x -= 0x10;
        o.w(0x64) = 0x10; o.w(0x3e) = 8;
        if (const LevelScriptEntry* e = scriptByLever(leverTrigger()))
            if (ctx.g.levelFlags()[e->flag]) { o.frame = 3; o.w(0x3e) = 0; o.b(0x34) = 0xff; }   // pushed already
    }
    if (o.b(0x34)) { exitDespawnFar(ctx, o); return; }
    const int16_t dy = int16_t(o.y - h.y());
    if (int16_t(dy + 0x30) >= 0 && int16_t(dy - 0x30) < 0) {
        const int16_t dx = int16_t(o.x - h.x());
        auto hold = [&](int16_t off) { h.x() = int16_t(o.x + off); h.xFrac() = o.xFrac; h.vx() = 0; };   // (the long at +0)
        if (dx < 0) {                                              // Harry right of it, facing left
            if (h.facing() && int16_t(dx + 0x36) >= 0 && int16_t(dx + 2) < 0) {
                hold(int16_t(o.w(0x3e) + 0x24));
                if (h.onGround() && h.cont() != 0x42f0e1 && h.b(0x466767)) {
                    ctx.pl.harryCall(0x42f0c4);
                    ctx.sound0(0x2e, 0x12);                        // FUN_0042633d(0x2e, 0x60)
                    o.b(0x3c) = 6;
                }
            }
        } else if (!h.facing() && int16_t(dx - 2) >= 0 && int16_t(dx - 0x36) < 0) {
            hold(int16_t(-o.w(0x3e) - 0x24));
            if (h.onGround() && h.cont() != 0x42f140 && h.b(0x466768)) {
                ctx.pl.harryCall(0x42f123);
                ctx.sound0(0x2e, 0x12);
                o.b(0x3c) = 6;
            }
        }
    }
    if (o.b(0x3c)) {                                               // 0x417fb5
        o.b(0x3c) -= 1;
        if (o.b(0x3c) == 0) {
            o.b(0x3c) = 6;
            o.w(0x3e) -= 2;
            if (h.cont() == 0x42f140 || h.cont() == 0x42f0e1) h.x() -= 2;   // (either way)
            o.frame += 1;
            if (o.frame == 3) {
                levelScriptPull(ctx, leverTrigger());              // FUN_0041804b
                o.b(0x34) = 0xff;
            }
        }
    }
    exitDespawnFar(ctx, o);
}

// 0x4186c4: type 0x63, a runaway mine cart (ob_mcar) from the spawner (type 98).  It rolls
// right 5 px a tick on the track, falling with gravity 0x3000 where there is none, and is gone
// when it falls at 6 px a tick; its rumble plays while channel 1 is free.  It knocks Harry back
// with 50 damage (and 120 ticks of invulnerability).  Weapons meet a box of its own (0x4186b4),
// Harry another (0x4186bc): FUN_00424af1 takes these two addresses as box records.
void h_runawayCart(ObjCtx& ctx, Obj& o) {
    static const ph::SpriteHitbox kWeaponBox{"", -0x20, 0x3e, -0xf, 0x24}, kHarryBox{"", 0xd, 0x10, -0x14, 0x27};
    o.rawBox = &kWeaponBox;
    weaponHit(ctx, o, 0);                     // (with whatever d5 holds; nothing tests the result)
    o.setSprite(ctx.spr("ob_mcar"));
    if (!ctx.channel1Busy()) ctx.sound1(0xd1, 6);
    Harry& h = ctx.harry;
    if (h.cont() != 0x427207 && h.w(0x46652d) == 0) {
        o.rawBox = &kHarryBox;
        const bool hit = ctx.touchesHarry(o);
        o.setSprite(ctx.spr("ob_mcar"));
        if (hit) {
            h.b(0x465b55) = 1;
            ctx.hurtHarry(0x32);
            ctx.pl.harryCall(0x427167);
            h.w(0x46652d) = 0x78;
            ctx.sound0(0xbc, 0xc);                // FUN_004262d9
        }
    }
    o.x += 5;
    int y = o.y + 0x25;
    const bool onTrack = ctx.floorSnap(o.x - 8, y);
    o.y = int16_t(y - 0x25);
    if (!onTrack) { o.l(0x46) += 0x3000; moveY(o, o.l(0x46)); }
    if (o.w(0x48) == 6) ctx.release(o);
}

// 0x4185ce: type 98, where the runaway carts come from (Tazamul Mines).  Once Harry has come
// into its box (0x45bce2) it holds the camera's left edge at 0x1d0, and from 0x348 px to the
// left of its place it sends a cart every 40 ticks, plus one tick for every 16 px Harry is to
// the right of it.
void h_cartSpawner(ObjCtx& ctx, Obj& o) {
    if (o.b(0x5a) == 0) {
        if (!harryOnBox(ctx, o, -0x100, 0x80, -0x50, 0x50)) return;
        o.b(0x5a) = 0xff;
        ctx.pl.lockCameraLeft(0x1d0);
    }
    if (o.b(0x3c) == 0) { o.frame = 0; o.b(0x3c) = 0xff; o.w(0x32) = 1; o.x -= 0x348; }
    o.w(0x32) -= 1;
    if (o.w(0x32) != 0) return;
    o.w(0x32) = 0x28;
    const int16_t d = int16_t(ctx.harry.x() - o.x);
    if (d >= 0) o.w(0x32) += int16_t(uint16_t(d) >> 4);
    if (Obj* c = ctx.spawnFrom(o, true, 0)) {                      // FUN_0043546e(0)
        std::memcpy(c->s + 0x12, o.s + 0x12, 2);
        c->w(0x26) = 0x6663;
        c->l(0x50) = int32_t(ctx.pl.addrOf(&o));
        c->type = 0x63; c->flags = 0x2000; c->sprite = ctx.spr("ob_mcar"); c->frame = 0;
    }
}

// ---------------------------------------------------------------- mine carts (types 15, 0x25, 19)

// 0x45b864 .. 0x45b8df.  The wheel offsets by cart frame (7 x {front dx, dy, back dx, dy}),
// then the bytes behind the two frame tables the cart indexes: 0x45b8a8 by the height
// difference across the track shape under it (-15 .. 15) and 0x45b8d6 by the integer part of
// its fall speed (-9 .. 8); both are read on either side of their address.
const uint8_t kCartTables[0x7c] = {
    0xf0, 0xff, 0xfe, 0xff, 0x0b, 0x00, 0xf5, 0xff, 0xef, 0xff, 0xfd, 0xff, 0x0c, 0x00, 0xf6, 0xff,
    0xf2, 0xff, 0xfc, 0xff, 0x0d, 0x00, 0xf8, 0xff, 0xf2, 0xff, 0xfa, 0xff, 0x0f, 0x00, 0xfa, 0xff,
    0xf3, 0xff, 0xf7, 0xff, 0x10, 0x00, 0xfc, 0xff, 0xf8, 0xff, 0xf6, 0xff, 0x11, 0x00, 0x00, 0x00,
    0xf7, 0xff, 0xf5, 0xff, 0x10, 0x00, 0x03, 0x00, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x05,
    0x05, 0x04, 0x04, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02,
    0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06};
uint8_t cartByte(uint32_t va) { const uint32_t i = va - 0x45b864; return i < sizeof kCartTables ? kCartTables[i] : 0; }
int16_t cartWord(uint32_t va) { return int16_t(cartByte(va) | cartByte(va + 1) << 8); }

// The translated `add.l d0, (w6a:w6c)` on a 16.16 pair kept in the state area.
void addFixedAt(Obj& o, int whole, int frac, int32_t v) {
    int16_t w = o.w(whole); uint16_t f = uint16_t(o.w(frac));
    addFixed(w, f, v);
    o.w(whole) = w; o.w(frac) = int16_t(f);
}

// 0x408f5a: type 15, the mine cart Harry rides (ob_scar on two ob_whelz wheels, with the brake
// lever obzleve3 but in level 7).  w6a:w6c is its x, l3c its speed (b4a: it runs left), l50 its
// fall speed.  Harry drops into it (box 0x45b854; 0x45b85c once he rides or on the ride of level
// 7) and rides in state 0x42aa3b; it speeds up by 0x2800 a tick to 7 px, and pulling the lever
// back at full speed (state 0x42abaa) brakes it to 3 px, with sparks at a wheel (type 0x25),
// until he pushes forward again.  It follows the track of both collision planes, tilting with
// the slope, and falls where there is none; in level 2 a fast fall throws Harry out (0x42c518).
// A wall in front of Harry (trigger class bit 0) throws the cart back and Harry against it
// (0x42afe9).  In level 7 the ride (DAT_00465c56, the director type 115) carries the cart along
// with the scroll: the pad moves it inside the screen, jump lifts it and down drops it through
// the track, and the ride's 0x600-px loop moves the cart, Harry, the camera and every object.
void h_mineCart(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    ObjectSystem& os = ctx.pl.objectSystem();
    const bool l7 = ctx.levelNo == 7;
    auto in = [&](uint32_t s) { return h.cont() == s; };
    auto rides = [&] { return in(0x42abaa) || in(0x42f2f9) || in(0x42aa3b); };
    auto part = [&](int off) { return ctx.pl.objAt(uint32_t(o.l(off))); };
    auto stopRumble = [&] { if (ctx.pl.scriptEvent() == 0xd1) { ctx.soundNow(0x89); ctx.pl.scriptEvent() = 0; } };
    auto wrapRide = [&] {                                   // 0x409172 / 0x4098d5
        const int16_t d = 0x600;
        o.w(0x6a) += d;
        h.w(0x4658b4) += d; ctx.camX = h.w(0x4658b4);
        h.x() += d;
        for (Obj* a = os.first(); a; a = os.nextOf(*a)) a->x = int16_t(a->x + d);
    };

    // 0x40a00d: the lever and the wheels follow the cart; far from the camera all four go.
    auto parts = [&] {
        if (Obj* lv = part(0x56)) {
            lv->x = o.x; lv->y = int16_t(o.y - 0x10); lv->facing = o.b(0x4a);
            if (lv->frame == 0 && in(0x42abaa)) lv->y = int16_t(lv->y - 0x200);   // in Harry's hands
        }
        if (Obj* w1 = part(0x5a)) {
            const uint32_t t = 0x45b864 + uint32_t(o.frame) * 8;
            int16_t d2 = cartWord(t), d3 = cartWord(t + 2), d4 = cartWord(t + 4), d5 = cartWord(t + 6);
            if (o.facing) { d2 = int16_t(-d2); d4 = int16_t(-d4); }
            w1->x = int16_t(o.x + d2); w1->y = int16_t(o.y + d3);
            w1->behind = in(0x42aa3b) ? 0xff : 0;
            w1->facing = o.facing; w1->frame = uint8_t(o.b(0x62) & 3);
            if (Obj* w2 = part(0x5e)) {
                w2->x = int16_t(o.x + d4); w2->y = int16_t(o.y + d5);
                w2->behind = in(0x42aa3b) ? 0xff : 0;
                w2->facing = o.facing; w2->frame = uint8_t((o.b(0x62) + 2) & 3);
            }
        }
        if (!farFromCamera(ctx, o)) return;
        stopRumble();
        ctx.pl.restoreEntry(o);
        ctx.release(o);
        for (int off : {0x5e, 0x5a, 0x56}) if (Obj* p = part(off)) ctx.release(*p);
    };
    // 0x409ee5: the wheels turn with the speed and the cart's x follows w6a.
    auto roll = [&] {
        int32_t d0 = o.l(0x3c);
        if (d0 != 0) { o.b(0x46) -= 1; if (int8_t(o.b(0x46)) < 0) o.b(0x46) = 3; }
        if (o.b(0x4a)) d0 = -d0;
        if (h.b(0x465c56) != 0) o.w(0x6a) -= h.w(0x466182);   // the ride: with the scroll
        else addFixedAt(o, 0x6a, 0x6c, d0);
        int8_t v = int8_t(o.b(0x3e));
        if (v < 0) v = int8_t(-v);
        o.b(0x63) += uint8_t(v);
        if (int8_t(o.b(0x63) - 0x14) >= 0) {
            o.b(0x63) -= 0x14;
            o.b(0x62) -= 1;
            if (int8_t(o.b(0x62)) < 0) o.b(0x62) = 2;
        }
        if (o.l(0x50) != 0) stopRumble();
        else if (o.l(0x3c) != 0 && !ctx.channel1Busy()) ctx.sound1(0xd1, 3);
        o.x = o.w(0x6a);
        if (o.facing) o.x += 0xb;
        parts();
    };
    // 0x409e1c: Harry rides along (but while he jumps out, hyihjump).
    auto carry = [&] {
        if (in(0x42b049)) { roll(); return; }
        if (!h.spriteIs("hyihjump")) {
            h.y() = int16_t(o.y - 0xc);
            ctx.pl.carryHarry(o.b(0x4a) ? -int32_t(o.l(0x3c)) : int32_t(o.l(0x3c)));
            if (o.b(0x68) != 0) { roll(); return; }
            if (o.b(0x47) != 0) { h.b(0x465b73) = 0xff; roll(); return; }
        }
        h.x() = int16_t(o.w(0x6a) + 5); h.vx() = 0; h.facing() = 0;
        if (o.b(0x4a)) { h.x() = int16_t(o.w(0x6a) - 3); h.facing() ^= 1; }
        h.b(0x465b74) = 0xff;
        h.b(0x465b73) = 0xff;
        roll();
    };

    if (o.b(0x6d) != 0) { o.b(0x6d) -= 1; h.w(0x466767) = 0; }     // no pushing for a while after the brake
    if (o.w(0x6a) == 0) o.w(0x6a) = o.x;
    if (l7 && h.b(0x466190) != 0) h.b(0x466766) = 0;

    bool flying = in(0x42b049);
    if (!flying && rides() && o.l(0x3c) != 0 && (ctx.trigger(h.x() - 0x28, h.y() - 0x10).cls & 1)) {
        // 0x409022: into a wall
        o.l(0x3c) = int32_t(0xfffb0000); o.l(0x50) = int32_t(0xffff0000); o.b(0x65) = 0xff;
        ctx.sound0(0xbc, 0xb);
        if (ctx.pl.scriptEvent() == 0xd1) ctx.soundNow(0x89);
        ctx.pl.harryCall(0x42afe9);
        flying = true;
    }
    if (flying) {
        // 0x409086: the cart flies on by itself (x is not mirrored)
        moveX(o, o.l(0x3c)); moveY(o, o.l(0x50));
        o.l(0x50) += 0x7000;
        parts();
        return;
    }

    if (l7 && !in(0x42aa3b) && !in(0x42709d)) {
        // 0x4090f8: a crashed cart meeting a type-0x7f object on the ride
        for (Obj* a = os.first(); a; a = os.nextOf(*a)) {
            if (a->type != 0x7f || !ctx.boxesOverlap(*a, o)) continue;
            if (o.b(0x65) == 0) break;
            o.b(0x65) = 0xff;
            h.b(0x465b74) = 0; h.setCont(0x42c244); h.w(0x465b47) = 0; h.b(0x465bb2) = 0x70;
            if (int16_t(h.w(0x4658b4) - 0x200) < 0) wrapRide();
            return;
        }
        if (o.b(0x65) != 0) { parts(); return; }
    }

    if (o.b(0x64) == 0) {
        // 0x4091f1: the first tick
        o.b(0x64) = 0xff;
        if (in(0x42aa3b)) { ctx.pl.restoreEntry(o); ctx.release(o); return; }   // Harry rides another one
        h.b(0x466190) = 0;
        if (Obj* w = ctx.spawnFrom(o)) {                                      // FUN_004356bc(1)
            o.l(0x5a) = int32_t(ctx.pl.addrOf(w));
            w->behind = 0xff; w->setSprite(ctx.spr("ob_whelz")); w->flags = 0x2000; w->type = 0x3c;
            if (Obj* w2 = ctx.spawnFrom(o)) {
                o.l(0x5e) = int32_t(ctx.pl.addrOf(w2));
                w2->setSprite(ctx.spr("ob_whelz")); w2->flags = 0x2000; w2->type = 0x3c;
                if (!l7) {
                    if (Obj* lv = ctx.spawnFrom(o, true, 2)) {                // FUN_0043546e(2)
                        o.l(0x56) = int32_t(ctx.pl.addrOf(lv));
                        lv->setSprite(ctx.spr("obzleve3")); lv->flags = 0x6000; lv->frame = 1; lv->type = 0x3c;
                    }
                }
            }
        }
    }
    if (in(0x42aa3b)) { h.b(0x465b46) = o.b(0x4a); h.w(0x465b51) = o.w(0x3e); }
    if (o.frame == 0xff) { o.frame = 0; o.b(0x4a) = 0xff; }                  // template 0x18: runs left

    // 0x409318: in the air
    bool probe = true;
    if (o.l(0x3c) != 0 && o.l(0x50) != 0) {
        const int32_t d0 = o.l(0x50);
        if (in(0x42aa3b)) h.b(0x465b45) = 0xff;
        moveY(o, d0);
        if (rides()) h.y() = int16_t(o.y - 0xb);
        probe = false;
        if (h.b(0x465c56) == 0) {
            if (int16_t(o.w(0x52) - 8) < 0) o.l(0x50) += 0x6500;
            if (o.l(0x50) >= 0x20000 && ctx.levelNo == 2 && in(0x42aa3b)) {
                // falling fast: Harry is thrown out
                h.vx() = o.l(0x3c); h.onGround() = 0;
                h.b(0x465b73) = 0; h.b(0x465b74) = 0; h.b(0x465b7b) = 0xc;
                h.setCont(0x42c518); h.b(0x465bb2) = 0x70; h.w(0x465b47) = 0;
                o.b(0x68) = 0xff;
            }
        } else if (o.w(0x52) == 8) {
            probe = true;
        } else {
            // the ride: lifted, then through the track while b54 counts down
            o.l(0x50) += 0x6000;
            if (d0 >= 0x3e000) {
                if (o.b(0x54) == 0) probe = true;
                else o.b(0x54) -= 1;
            }
        }
    }
    if (probe) {
        // 0x409473: the track under the front (or back, running left) of the cart
        const int16_t px = int16_t(o.w(0x6a) - (o.b(0x4a) ? 0xd : 0));
        ph::FloorHit f = ph::probeNear(ctx.det(), px, o.y + 0xd);          // FUN_00425d6f
        if (f.hit) o.y = int16_t(f.surfaceY - 0xd);
        if (!f.hit) {
            f = ph::probeNear(ctx.dt2(), px, o.y + 0xd);                    // FUN_00425f88
            if (f.hit) o.y = int16_t(f.surfaceY - 0xd);
        }
        if (!f.hit) {
            if (o.l(0x50) == 0) o.l(0x50) = 1;
        } else {
            // 0x40953b: on the track
            if (ctx.levelNo != 2 && o.l(0x50) != 0) {
                o.l(0x50) = 0;
                ctx.sound0(0x1e, 8);                                         // FUN_0042633d
                if (!in(0x42709d) && h.b(0x465b71) == 0) {
                    h.w(0x465b5d) = 0;
                    h.setUl(0x465b59, 0x4617ee);                             // Harry's bump (0x4617f6 had he been crouching)
                    if (h.b(0x465b72) == 0 && !in(0x42b049)) {
                        h.setCont(0x42aa3b); h.b(0x466190) = 0; h.l(0x465b45) = 0;
                        h.w(0x465b5d) = 0; h.w(0x465b51) = 0;
                    }
                }
            }
            if (in(0x42aa3b)) h.b(0x465b45) = 0;
            o.l(0x50) = 0;
            o.setSprite(ctx.spr("ob_scar"));
            int16_t d = int16_t(ph::kCollisionHeights[f.shape * 16] - ph::kCollisionHeights[f.shape * 16 + 15]);
            if (d < 0) o.facing = 0;
            else if (d > 0) o.facing = 0xff;
            else { o.facing = 0; if (in(0x42aa3b)) h.b(0x465b45) = 0; }
            o.frame = cartByte(uint32_t(0x45b8a8 + d));
        }
    }
    // 0x4096e3
    if (o.l(0x50) != 0) {
        o.facing = 0;
        o.frame = cartByte(uint32_t(0x45b8d6 + o.w(0x52)));
        if (l7) o.facing = 0xff;
    }
    if (h.b(0x465b74) != 0 && (in(0x42f2f9) || in(0x42aa3b))) h.y() = int16_t(o.y - 0xb);

    if (l7) {
        // 0x409772: the ride
        if (in(0x42709d) || in(0x430e3a)) { roll(); return; }
        if (h.b(0x466190) == 0) {
            if (h.b(0x465c56) == 0) goto board;
            if (h.w(0x465b59) == 0 && !in(0x42f2f9) && !in(0x42c518) && h.b(0x466766) != 0) {
                h.b(0x465b71) = 0;
                ctx.pl.harryCall(0x42f2d2);                                  // crouch in the cart
            }
            if (h.b(0x466767) != 0) o.w(0x6a) -= 2;
            if (h.b(0x466768) != 0) o.w(0x6a) += 3;
            const int16_t cx = h.w(0x4658b4);
            if (int16_t(cx - 0x120) >= 0) {
                const int16_t d = int16_t(o.w(0x6a) - cx);
                if (int16_t(d - 0x4a) < 0) o.w(0x6a) = int16_t(cx + 0x4a);
                else if (int16_t(d - 0xfc) >= 0) o.w(0x6a) = int16_t(cx + 0xfc);
            }
            // (with the camera left of 0x120 the right-hand test at 0x409873 reads the d0 left
            // from earlier in the handler: the slope, the fall speed, or 1 after the crouch call,
            // never as much as 0xfc)
        }
        if (o.b(0x65) == 0) {
            h.x() = int16_t(o.w(0x6a) - 3);
            if (h.b(0x4661c1) == 0 && int16_t(h.w(0x4658b4) - 0x200) < 0) wrapRide();
        }
    }
board:
    // 0x40992e: Harry drops into the cart, and drives it while he is in its box
    if (h.b(0x465b73) != 0 || h.l(0x465b7e) != 0 || h.onGround() || h.w(0x465baa) < 0 || o.b(0x68) != 0) { roll(); return; }
    {
        const bool big = in(0x42aa3b) || h.b(0x465c56) != 0;
        if (!(big ? harryOnBox(ctx, o, -86, 84, -56, 56) : harryOnBox(ctx, o, -24, 30, -30, 12))) { roll(); return; }   // 0x45b85c / 0x45b854
    }
    if (o.b(0x47) == 0) {
        if (h.b(0x465bb2) != 0) { roll(); return; }
        if (!in(0x42f2f9) && !in(0x42f508) && !in(0x42aa3b)) {
            ctx.sound0(0x34, 0x15);
            o.l(0x3c) = 0x2800;
            if (h.b(0x465b72) == 0 && !in(0x42b049) && !(ctx.levelNo == 2 && o.l(0x50) != 0)) {
                h.setCont(0x42aa3b); h.b(0x466190) = 0;
                h.l(0x465b45) = 0; h.l(0x465b59) = 0; h.w(0x465b5d) = 0; h.w(0x465b51) = 0;
                h.b(0x465b46) = o.b(0x4a); h.w(0x465b47) = 0;
                h.vy() = 0;
            }
        }
    }
    if (h.b(0x466190) != 0) { carry(); return; }
    {
        // 0x409abb
        auto ahead = [&] { return o.b(0x4a) == 0 ? h.b(0x466768) != 0 : h.b(0x466767) != 0; };
        auto back = [&] { return o.b(0x4a) != 0 ? h.b(0x466768) != 0 : h.b(0x466767) != 0; };
        if (o.w(0x3e) == 3 && ahead()) {
            o.b(0x47) = 0;                                                    // the brake is let go
            if (Obj* lv = part(0x56)) lv->frame = 1;
        }
        int32_t d0 = 0;
        if (o.b(0x47) == 0) {
            bool go = o.l(0x3c) != 0;
            if (!go) {
                if (h.b(0x466762) != 0 && !l7) { ctx.pl.harryCall(0x42c490); return; }   // jumps out
                go = ahead();
            }
            if (go) {
                if (o.w(0x3e) != 7) d0 = 0x2800;
                else o.l(0x3c) = 0x70000;
            }
        } else {
            if (h.b(0x466762) != 0 && in(0x42aa3b)) ctx.pl.harryCall(0x42c490);
            if (o.w(0x3e) != 3) d0 = int32_t(0xffffd000);
        }
        o.l(0x3c) += d0;
        if (l7) {
            // 0x409c2f: jump lifts the cart, down drops it through the track
            if (int16_t(h.w(0x4658b4) - 0x120) >= 0 && h.w(0x465b59) == 0 && h.b(0x465c56) != 0 && o.l(0x50) == 0
                && h.b(0x46676a) == 1) {
                h.b(0x46676a) = 0x80;
                if (h.b(0x466766) == 0) {
                    if (!in(0x42f2f9)) {
                        o.l(0x50) = int32_t(0xfff7ffff);
                        if (int16_t(h.y() - 0x245) < 0) o.b(0x54) = 0x58;
                    }
                } else if (int16_t(h.y() - 0x2b0) < 0) {
                    o.l(0x50) = 0x10000;
                    o.b(0x54) = 7;
                }
            }
        } else if (o.l(0x50) == 0 && o.w(0x3e) == 7 && back() && o.b(0x47) == 0) {
            // 0x409d6d: Harry pulls the lever back
            if (Obj* lv = part(0x56)) lv->frame = 0;
            o.b(0x47) = 0xff;
            h.setCont(0x42abaa);
            o.b(0x6d) = 0x30;
            ctx.sound0(0x1c, 0xc);                                            // FUN_0042633d
            h.w(0x465b47) = 0;
            if (Obj* s = ctx.spawnFrom(o, true, 3)) {                         // FUN_0043546e(3)
                s->type = 0x25; s->flags = 0x6000; s->setSprite(ctx.spr("fxzspark"));
                s->l(0x32) = int32_t(ctx.pl.addrOf(&o));
                s->facing = o.b(0x4a);
            }
        }
    }
    carry();
}

// 0x40eca2: type 0x25, the sparks at the back wheel of a braking mine cart (l32): l5a when the
// cart's (slope) facing matches the sparks' (its direction, b4a), else l5e.
void h_cartSpark(ObjCtx& ctx, Obj& o) {
    o.behind = 0xff;
    if (Obj* cart = ctx.pl.objAt(uint32_t(o.l(0x32)))) {
        const bool other = (cart->facing != 0) != (o.facing != 0);
        if (Obj* w = ctx.pl.objAt(uint32_t(cart->l(other ? 0x5e : 0x5a)))) {
            o.x = w->x; o.xFrac = w->xFrac; o.y = w->y; o.yFrac = w->yFrac;
        }
    }
    o.x += 2; o.y += 0x13;
    if (animStep(ctx, o, o.anim(0x36), kAnim_cartSpark, 2)) ctx.release(o);
}

// 0x40ab62: type 19, a mine cart Harry pushes along the track (ob_mcar on two ob_whelz wheels,
// Tazamul Mines).  Pushing (hyipush, states 0x42f0e1 / 0x42f140) moves it a pixel a tick unless
// a wall (trigger class bit 0) is ahead; Harry can stand on it (box 0x45a9f6).  It never leaves:
// out of view it is hidden and its display entry dropped.  Its x is kept in DAT_0046589c (left of
// 0x500) or DAT_0046589e for the level start (FUN_00433d0b).  Weapons meet the box record 0x4186b4.
void h_pushCart(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    if (int16_t(o.x - 0x500) < 0) h.w(0x46589c) = o.x;
    else h.w(0x46589e) = o.x;
    static const ph::SpriteHitbox kWeaponBox{"", -0x20, 0x3e, -0xf, 0x24};
    o.rawBox = &kWeaponBox;
    weaponHit(ctx, o, 0);                     // (with whatever d5 holds; nothing tests the result)
    o.setSprite(ctx.spr("ob_mcar"));
    for (int off : {0x5a, 0x5e}) {
        if (o.l(off) != 0) continue;
        if (Obj* w = ctx.spawnFrom(o)) {                                      // FUN_004356bc(1)
            o.l(off) = int32_t(ctx.pl.addrOf(w));
            w->setSprite(ctx.spr("ob_whelz")); w->flags = 0x2000; w->type = 0x3c;
        }
    }
    { int y = o.y + 0x25; ctx.floorSnap(o.x - 8, y); o.y = int16_t(y - 0x25); }
    bool pushed = false;
    int8_t step = 0;
    if (h.onGround()) {
        const int16_t dy = int16_t(o.y - h.y()), dx = int16_t(o.x - h.x());
        const int16_t vx = h.w(0x465ba6);
        if (int16_t(dy + 0x40) >= 0 && int16_t(dy - 0x40) < 0 && vx != 0) {
            if (vx < 0) {
                if (h.facing() != 0 && int16_t(dx + 0x44) >= 0 && int16_t(dx + 0x38) < 0) {
                    h.x() = int16_t(o.x + 0x40); h.xFrac() = o.xFrac;
                    if (h.cont() != 0x42f0e1 && h.b(0x466767) != 0) { o.b(0x50) = 1; ctx.pl.harryCall(0x42f0c4); }
                    h.vx() = -1;
                    if (h.spriteIs("hyipush") && !(ctx.trigger(o.x - 0x34, o.y - 0x10).cls & 1)) {
                        h.x() -= 1; o.x -= 1; step = -1; pushed = true;
                    }
                }
            } else if (h.facing() == 0 && int16_t(dx - 0x38) >= 0 && int16_t(dx - 0x44) < 0) {
                h.x() = int16_t(o.x - 0x40); h.xFrac() = o.xFrac;
                if (h.cont() != 0x42f140 && h.b(0x466768) != 0) { o.b(0x50) = 1; ctx.pl.harryCall(0x42f123); }
                h.vx() = 0x10000;
                if (h.spriteIs("hyipush") && !(ctx.trigger(o.x + 0x14, o.y - 0x10).cls & 1)) {
                    h.x() += 1; o.x += 1; step = 1; pushed = true;
                }
            }
        }
    }
    if (pushed) {
        // 0x40ae96: the wheels turn, and the rumble sounds every 0x30 ticks
        o.b(0x64) -= 1;
        if (int8_t(o.b(0x64)) < 0) { o.b(0x64) = 5; o.b(0x65) = uint8_t((o.b(0x65) + step) & 3); }
        o.b(0x50) -= 1;
        if (int8_t(o.b(0x50)) < 0) {
            o.b(0x50) = 0x30;
            ctx.pl.clearChannel0Hold();
            ctx.sound0(0x11, 0xb);                                            // FUN_0042633d
        }
    } else if (ctx.pl.channel0Id() == 0x11 && h.vx() == 0) {
        ctx.pl.stopChannel0();
    }
    if (Obj* w = ctx.pl.objAt(uint32_t(o.l(0x5a)))) { w->x = int16_t(o.x + 0xa); w->y = int16_t(o.y + 0x14); w->frame = o.b(0x65); }
    if (Obj* w = ctx.pl.objAt(uint32_t(o.l(0x5e)))) {
        w->x = int16_t(o.x - 0x10); w->y = int16_t(o.y + 0x14); w->frame = uint8_t((o.b(0x65) + 2) & 3);
    }
    if (h.w(0x465baa) >= 0 && harryOnBox(ctx, o, -28, 40, -14, -4)) {     // 0x45a9f6: on top of it
        h.y() = int16_t(o.y - 0xe); h.vy() = 0; h.b(0x465b73) = 0xff;
    }
    if (farFromCamera(ctx, o)) {
        ctx.pl.objectSystem().cancelCurrentDisplay();
        o.w(0x26) = 0;                                                        // FUN_004263c4 (when it was set)
        o.frame = 0xff;
        return;
    }
    if (o.w(0x26) == 0) {
        // back in view: FUN_004263a6(7) marks it again (and returns 0, so the cancel after it is skipped)
        o.b(0x21) = 0xff; o.w(0x26) = 0x6663;
        o.frame = 0xff;
        return;
    }
    o.frame = 0;
}

// ---------------------------------------------------------------- the Balankanche ride (types 115, 0x7e, 7, 0x7f, 0x79, 0x80)

// 0x45b2c0: what the ride sends along a lane, by the script's kinds 1 .. 7 {sprite slot, flags,
// type, frame} (kinds 8 .. 10 are empty, the bytes after them not a table).
struct RideThing { const char* sprite; uint16_t flags; uint8_t type, frame; };
const RideThing kRideThings[7] = {
    {"ob_mcar", 0x2000, 0x7f, 0}, {"mi_coin", 0, 0x79, 0xff}, {"mi_coin", 0x6000, 0x93, 0}, {"skiready", 0x4000, 0x79, 0},
    {"tpzhit", 0x4000, 0x79, 0}, {"gvrvapor", 0x4000, 0x80, 0}, {"mi_1up", 0x6000, 2, 0}};

int16_t rideWord(uint32_t va) {
    const uint32_t i = va - 0x45fdc4;
    return i + 1 < 0x2a8 ? int16_t(kData_rideScript[i] | kData_rideScript[i + 1] << 8) : 0;
}

// 0x41a2ae: type 115, the Balankanche ride.  It switches the ride on (DAT_00465c56: the camera
// and the cart run left at DAT_00466182 px a tick, which approaches the target DAT_0046617e by
// 0x2000 a tick), makes the level 0x30 cells high and, 0x28 ticks in, lines up the three lane
// lights (type 0x7e).  Its script (0x45fdc4) sends things along the lanes: kind 1 a runaway cart
// (type 0x7f) announced by a blinking light (type 7), 2 a marker after each speed change, 3 a
// coin with three more above it, 6 a vapour (type 0x80), 7 a 1-up; kind 9 sets the target speed
// and kind 10 ends the ride's loop (DAT_004661c1).  A zero distance takes the last nonzero one.
// Everything waits while a cart has Harry (state 0x42709d).
void h_rideDirector(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    ObjectSystem& os = ctx.pl.objectSystem();
    if (o.b(0x32) == 0) {
        o.b(0x32) = 0xff;
        h.w(0x4658ae) = 0x30;
        o.l(0x54) = 0x6400000;
        h.b(0x465c56) = 0xff;
        h.l(0x46617c) = 0x60000;
        h.l(0x466180) = 0x60000;
        o.l(0x34) = 0x45fdc4;
        o.w(0x4e) = 0x28;
        h.b(0x4661c1) = 0;
    }
    if (h.cont() == 0x42709d) return;
    const int16_t dv = int16_t(h.w(0x46617e) - h.w(0x466182));
    if (dv < 0) h.l(0x466180) -= 0x2000;
    else if (dv > 0) h.l(0x466180) += 0x2000;
    if (o.w(0x4e) != 0) {
        o.w(0x4e) -= 1;
        if (o.w(0x4e) == 0) {
            h.w(0x466782) = 0x231;
            for (int d7 = 2; d7 >= 0; --d7) {
                if (Obj* l = ctx.spawnFrom(o, true, 1)) {                   // FUN_0043546e(1)
                    l->setSprite(ctx.spr("ob_lite"));
                    l->y = h.w(0x466782);
                    h.w(0x466782) += 0x40;
                    l->x = 0; l->frame = 0; l->type = 0x7e; l->behind = 0xff; l->flags = 0xa000;
                    l->w(0x32) = 0x24;
                }
            }
        }
    }
    o.l(0x54) -= h.l(0x466180);
    if (o.l(0x54) >= 0) return;
    auto distance = [](int16_t d) { return int32_t(uint32_t(uint16_t(uint16_t(d) * 0xa0)) << 16); };
    uint32_t a0 = uint32_t(o.l(0x34));
    if (rideWord(a0 + 4) == 0xa) { h.b(0x4661c1) = 0xff; o.l(0x54) = 0x60000000; return; }
    if (rideWord(a0 + 4) == 9) {
        h.w(0x46617e) = rideWord(a0 + 6);
        o.l(0x34) += 8;
        o.l(0x54) = distance(rideWord(uint32_t(o.l(0x34))));
        return;
    }
    const int kind = rideWord(a0 + 4);
    if (kind >= 1 && kind <= 7) {
        const RideThing& t = kRideThings[kind - 1];
        if (Obj* a5 = ctx.spawnFrom(o)) {                                   // FUN_004356bc(1)
            h.setUl(0x466788, ctx.pl.addrOf(a5));
            a5->x = int16_t(h.w(0x4658b4) - 0x28);
            if (kind == 6) a5->x += 0x186;
            a5->y = 0x243;
            if (rideWord(a0 + 2) != 1) { a5->y = 0x283; if (rideWord(a0 + 2) != 2) a5->y = 0x2c3; }
            a5->setSprite(ctx.spr(t.sprite));
            if (kind == 1) a5->y -= 0x13;
            a5->flags = t.flags; a5->frame = t.frame; a5->type = t.type;
            ctx.pl.setAlsLong(0x384, ctx.pl.addrOf(a5));
            if (kind == 3) {
                // three more coins above it, to the left
                a5->y -= 0x18;
                int16_t x = a5->x;
                const int16_t y = a5->y;
                for (int d2 = 2; d2 >= 0; --d2) {
                    Obj* c = ctx.spawnFrom(o);
                    if (!c) break;
                    c->setSprite(ctx.spr("mi_coin")); c->flags = 0x6000; c->frame = 0; c->type = 2;
                    x = int16_t(x - 0x28);
                    c->x = x; c->y = y;
                }
            }
        }
    }
    // 0x41a692: the next entry, and the distance to it
    o.l(0x34) += 6;
    a0 = uint32_t(o.l(0x34));
    uint32_t a1 = a0;
    int16_t d0;
    while ((d0 = rideWord(a1)) == 0) {
        a1 -= 6;
        if (rideWord(a1 + 2) == 9) a1 -= 2;                                 // (a speed entry is 8 bytes)
    }
    o.l(0x54) = distance(d0);
    const int next = rideWord(a0 + 4);
    if (next == 1 || next == 0xb) {
        // a light on the cart's lane, lit about when the cart comes; the cart comes 0x1b0 px later
        int16_t n = 1;
        for (Obj* a = os.first(); a; a = os.nextOf(*a)) {
            if (a->type != 0x7e) continue;
            if (n == rideWord(a0 + 2)) {
                if (Obj* l = ctx.spawnFrom(o)) {
                    l->l(0x64) = int32_t(ctx.pl.alsLong(0x384));
                    l->type = 7;
                    l->x = a->x; l->y = a->y;
                    l->behind = 0xff;
                    l->l(0x4a) = int32_t(ctx.pl.addrOf(a));
                    l->setSprite(ctx.spr("ob_liter"));
                    l->frame = 0xff;
                    l->flags = 0xe000;
                    l->w(0x46) = int16_t(0xaa - 10 * h.w(0x46617e));
                    l->l(0x54) = o.l(0x54) + 0x20000;
                    o.l(0x54) += 0x1b00000;
                }
                break;
            }
            n += 1;
        }
    }
    if (next == 7) {
        // the 1-up: the top and bottom lights
        int16_t n = 1;
        for (Obj* a = os.first(); a; a = os.nextOf(*a)) {
            if (a->type != 0x7e) continue;
            if (n == 3 || n == 1) {
                if (Obj* l = ctx.spawnFrom(o)) {
                    l->type = 7;
                    l->x = a->x; l->y = a->y;
                    l->behind = 0xff;
                    l->l(0x4a) = int32_t(ctx.pl.addrOf(a));
                    l->setSprite(ctx.spr("ob_liter"));
                    l->frame = 0xff;
                    l->flags = 0xe000;
                    l->w(0x46) = 0x1260;
                }
            }
            n += 1;
        }
    }
}

// 0x41b628: type 0x7e, a lane light at the left edge of the screen (ob_lite): it slides in over
// 0x24 ticks (FUN_0043348e puts it at camX - w32).
void h_rideLaneLight(ObjCtx& ctx, Obj& o) {
    if (o.w(0x32) != 0) o.w(0x32) -= 1;
    ctx.pl.hudAdd(o);                                                       // 0x4150a2
}

// 0x406b32: type 7, a lane's warning light (ob_liter, at the screen's left edge): hidden until
// its distance (l54) runs out, then it beeps and blinks for w46 ticks (+0x14).
void h_rideWarning(ObjCtx& ctx, Obj& o) {
    ctx.pl.hudAdd(o);                                                       // 0x4150a2
    if (o.frame == 0xff) {
        o.l(0x54) -= ctx.harry.l(0x466180);
        if (o.l(0x54) >= 0) return;
        o.frame = 0;
        ctx.sound1(0x57, 9);
    }
    o.w(0x50) += 1;
    if (o.w(0x50) == 0xa) { o.w(0x50) = 0; o.frame ^= 1; }
    if (ctx.harry.cont() == 0x42709d) return;
    o.w(0x46) -= 1;
    if (int16_t(o.w(0x46) + 0x14) < 0) ctx.release(o);
}

// 0x41b644: type 0x7f, a runaway cart on a lane of the ride (ob_mcar), knocked back (l5a,
// slowing by 0xc00) when it runs into Harry.  Running into him stops the ride: a cart carries
// him (state 0x42709d, DAT_00465b45 = the cart, hyihwall) and DAT_0046618a holds the camera.
void h_rideCart(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    moveX(o, -o.l(0x5a));
    if (o.l(0x5a) != 0) {
        o.l(0x5a) -= 0xc00;
        if (o.l(0x5a) < 0) o.l(0x5a) = 0;
    }
    if (h.b(0x465b72) == 0 && h.cont() != 0x42709d && h.cont() != 0x430e3a && ctx.touchesHarry(o)) {
        o.l(0x5a) = 0x1c000;
        if (Obj* a0 = ctx.pl.objAt(uint32_t(o.l(0x64)))) a0->w(0x46) = 0x1000;
        ctx.sound1(0xbf, 0x10);
        h.setCont(0x42709d);
        h.setUl(0x465b45, ctx.pl.addrOf(&o));
        h.w(0x465b43) = 0x28;
        h.setSprite("hyihwall");
        h.facing() = 0xff;
        h.frame() = 0;
        h.b(0x46618a) = 0xff;
    }
    exitDespawnFar(ctx, o);                                                 // 0x424d6a
}

// 0x41b208: type 0x79, what the ride leaves behind (a marker after each speed change): gone once
// it is 0x17c px right of the camera.
void h_rideMarker(ObjCtx& ctx, Obj& o) {
    if (int16_t(o.x - ctx.camX - 0x17c) < 0) return;
    ctx.pl.restoreEntry(o);
    ctx.release(o);
}

// 0x41b736: type 0x80, a vapour on the ride (gvrvapor): it rises 0x21 px with a hiss, bobs along
// the path 0x45be7e, drifts with the scroll, first back (-2 px a tick, easing off, then a sound)
// and then speeding up to the left.  Touching Harry sets DAT_00466190, which makes the cart throw
// up his arms (the reset after it compares the state with 0x465b71, an address, and never runs).
void h_rideVapor(ObjCtx& ctx, Obj& o) {
    static const int16_t kPath[45] = {0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, -1, 0, -1, 0,
                                      -1, 0, -1, -1, -1, -1, -1, 0, -1, 0, -1, 0, -1, 0, 0, 0, 0, int16_t(0x8000)};
    Harry& h = ctx.harry;
    if (o.b(0x59) == 0) {
        o.behind = 0xff;
        o.b(0x59) = 0xff;
        o.y -= 0x21;
        ctx.sound0(0x2d, 0xa);                                              // FUN_0042633d
    }
    if (h.cont() != 0x42709d && ctx.touchesHarry(o)) h.b(0x466190) = 0xff;
    animStep(ctx, o, o.anim(0x50), kAnim_vaporSpin, 4);
    o.w(0x32) += 2;
    const int i = uint16_t(o.w(0x32)) >> 1;
    const int16_t dy = i < 45 ? kPath[i] : 0;
    if (dy == int16_t(0x8000)) o.w(0x32) = 0;
    else o.y += dy;
    o.x -= h.w(0x466182);
    if (o.b(0x34) == 0) { o.b(0x34) = 0xff; o.l(0x36) = int32_t(0xfffe0000); }
    if (o.l(0x36) != 0) {
        o.l(0x36) += 0x800;
        if (o.l(0x36) >= 0) { o.l(0x36) = 0; ctx.sound0(8, 8); }
        moveX(o, o.l(0x36));
    } else {
        if (o.w(0x32) != int16_t(0xfff8)) o.l(0x3c) -= 0x2000;
        moveX(o, o.l(0x3c));
    }
    exitDespawnFar(ctx, o);                                                 // 0x424d6a
}

// 0x41dd6e: type 0x97, a line of text crossing the screen (FUN_0043348e moves it 5 px a tick and
// holds it in the middle for 0x90 ticks); gone at w32 0x1c7.
void h_hudText(ObjCtx& ctx, Obj& o) {
    if (int16_t(o.w(0x32) - 0x1c7) >= 0) { ctx.release(o); return; }
    ctx.pl.hudAdd(o);                                                       // 0x4150a2
}

// 0x41e110: type 0x9b, the "GO" sign in the middle of the screen, for 0x50 ticks.
void h_hudGo(ObjCtx& ctx, Obj& o) {
    ctx.pl.hudAdd(o);                                                       // 0x4150a2
    o.w(0x32) += 1;
    if (o.w(0x32) == 0x50) ctx.release(o);
}

// ---------------------------------------------------------------- Harry's splash (type 0x7a)

// 0x41b24e: type 0x7a, Harry's splash where he lands in water (fxisplsh, from 0x426c52), with a
// mirrored twin (l50, typeless: drawn, never run) that takes its frame and sprite; at the end of
// the script 0x45be4a both go (without a twin it goes at once).
void h_harrySplash(ObjCtx& ctx, Obj& o) {
    const bool done = animStep(ctx, o, o.anim(0x32), kAnim_harrySplash, 4);
    Obj* twin = ctx.pl.objAt(uint32_t(o.l(0x50)));
    if (!twin) { ctx.release(o); return; }
    twin->frame = o.frame;
    twin->sprite = o.sprite; twin->rawBox = o.rawBox;
    if (!done) return;
    ctx.release(o);
    ctx.release(*twin);
}

// ---------------------------------------------------------------- ropes (types 0xa9, 14) and drifting platforms (0x6f)

int16_t ropeWord(const uint8_t* t, size_t off) { return int16_t(t[off] | t[off + 1] << 8); }

// 0x4088ba: type 14, a swinging rope (ropes) and its 13 segments (linked by l32; l40 is the
// eleventh, the one Harry catches, box 0x45ba66).  Every tick the rope and its segments take
// their places for the swing phase DAT_00466796 from the tables 0x45dfd0 / 0x45bfde (the second
// while Harry hangs on it), mirrored by DAT_00466799 and, for a rope on a trigger of raw class
// 0xf5, by b46.  Harry (DAT_00465b7e = l40, state 0x42a048) takes his place from his entry and
// the segments after it go behind him; jump lets go (0x42c1ea) with a push along the swing.
// Its spawner (type 0xa9) dismisses it out of view (b68); the segments go with it.
void h_rope(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    auto part = [&](Obj& p, int off) { return ctx.pl.objAt(uint32_t(p.l(off))); };
    auto releaseFrom = [&](Obj& a6) {                   // 0x408ed4
        ctx.pl.restoreEntry(a6);
        if (Obj* l = part(a6, 0x50)) { ctx.pl.restoreEntry(*l); ctx.release(*l); }
        for (Obj* p = &a6; p; ) { Obj* next = part(*p, 0x32); ctx.release(*p); p = next; }
    };
    if (o.frame == 0xff || o.frame == 0xfe) {
        o.b(0x44) = 8;
        if (ctx.trigger(o.x - 0x20, o.y + 8).raw == 0xf5) o.b(0x46) = 1;
        if (o.frame == 0xfe) o.y -= 0x30;                                  // (template 0x4683b4)
        o.w(0x36) = o.x; o.w(0x38) = o.y;
        Obj* a4 = &o;
        for (int d7 = 0xc; d7 >= 0; --d7) {
            Obj* a5 = ctx.spawn();                                          // FUN_00435905
            if (!a5) { releaseFrom(*a4); return; }                          // (the last one made, or the rope)
            a5->setSprite(ctx.spr("ropes")); a5->flags = 0x4000;
            a4->l(0x32) = int32_t(ctx.pl.addrOf(a5));
            a4 = a5;
            if (d7 == 2) o.l(0x40) = int32_t(ctx.pl.addrOf(a5));
        }
        a4->l(0x32) = 0;
    }
    if (o.b(0x44) != 0) {
        o.b(0x44) -= 1;                                                     // just let go
    } else if (h.l(0x465b7e) == 0 && !h.onGround()) {
        Obj* seg = part(o, 0x40);
        if (seg && harryOnBox(ctx, *seg, -24, 24, 36, 80) && h.cont() != 0x42a36f) {   // 0x45ba66
            h.l(0x465b7e) = o.l(0x40);
            h.setCont(0x42a048);
            ctx.sound1(0x44, 7);
            h.w(0x4658a6) = 0x30;
            h.w(0x465b49) = 1;
        }
    }
    // 0x408a77: the rope's places for this phase
    const bool held = h.l(0x465b7e) == o.l(0x40);
    const uint8_t* tbl = held ? kData_ropeSwingHarry : kData_ropeSwing;
    size_t a0 = size_t(int16_t(ropeWord(tbl, size_t(h.b(0x466796)) * 2)));
    const int16_t d4 = o.w(0x36), d5 = o.w(0x38);
    uint8_t d6 = 0;
    for (Obj* a4 = &o; a4; ) {
        if (int8_t((ropeWord(tbl, a0 + 4) & 0x7f) - 0x15) >= 0) {
            // Harry's entry
            if (!held) { a0 += 6; continue; }
            h.x() = d4;
            const int16_t dx = ropeWord(tbl, a0); a0 += 2;
            h.facing() = uint8_t(h.b(0x466799) ^ o.b(0x46));
            if (h.facing()) h.x() -= dx; else h.x() += dx;
            h.y() = int16_t(d5 + ropeWord(tbl, a0)); a0 += 2;
            d6 = 1;
            const int16_t d1 = ropeWord(tbl, a0); a0 += 2;
            const uint8_t ph = h.b(0x466796);
            h.frame() = (int8_t(ph - 0x23) >= 0 && int8_t(ph - 0x2b) < 0) ? 0xa : uint8_t((d1 & 0x7f) - 0x15);
            h.setSprite("hyisvine");
            if (d1 & 0x80) h.facing() ^= 1;
            continue;
        }
        a4->x = d4;
        const int16_t dx = ropeWord(tbl, a0); a0 += 2;
        a4->facing = uint8_t(h.b(0x466799) ^ o.b(0x46));
        a4->behind = d6;
        if (a4->facing) a4->x -= dx; else a4->x += dx;
        a4->y = int16_t(d5 + ropeWord(tbl, a0)); a0 += 2;
        const int16_t d1 = ropeWord(tbl, a0); a0 += 2;
        uint8_t f = uint8_t(d1 & 0x7f);
        if (f == 0x13) f = 0x10;
        a4->frame = f;
        if (d1 & 0x80) a4->facing ^= 1;
        a4 = part(*a4, 0x32);
    }
    if (h.l(0x465b7e) == o.l(0x40)) {
        // the swing's direction for the camera's lead, and jump lets go
        h.b(0x465b47) = 0;
        if (o.b(0x46)) h.b(0x465b47) ^= 1;
        if (h.b(0x466797)) h.b(0x465b47) ^= 1;
        if (h.b(0x46676d) == 1) {
            h.b(0x46676d) = 0x80;
            h.vy() = int32_t(0xfff86000);
            o.b(0x44) = 0x28;
            ctx.pl.harryCall(0x42c1ea);
            h.l(0x465b7e) = 0;
            h.onGround() = 0;
            h.b(0x465b6d) = 0xff;
            h.b(0x465b7b) = 8;
            ctx.sound0(0x8a, 5);
            h.vx() = 0x46000; h.facing() = 0;
            if (o.b(0x46)) { h.facing() ^= 1; h.vx() = -int32_t(h.vx()); }
            if (h.b(0x466797)) { h.facing() ^= 1; h.w(0x465ba6) = int16_t(-h.w(0x465ba6)); }   // (the integer word only)
            h.y() += 0x48;
            return;
        }
    }
    if (o.b(0x68) != 0) releaseFrom(o);
}

// 0x41fc1a: type 0xa9, where a rope hangs (placed by FUN_00433d0b from the .als ids 0x15 / 0x16;
// b32 for 0x16, a rope from 0x30 px higher).  The rope (type 14, template 0x4683a4 / 0x4683b4)
// comes when the spot is within 0x122 x (-0xd2 .. 0x8c) px of the screen's middle and is
// dismissed (its b68) beyond 0x132 x (-0xe2 .. 0x9c).
void h_ropeSpawner(ObjCtx& ctx, Obj& o) {
    const int16_t dx = int16_t(ctx.camX + 0xa0 - o.x), dy = int16_t(ctx.camY - o.y);
    if (o.l(0x3c) != 0) {
        if (int16_t(dx - 0x132) < 0 && int16_t(dx + 0x132) >= 0 && int16_t(dy - 0x9c) < 0 && int16_t(dy + 0xe2) >= 0) return;
        if (Obj* r = ctx.pl.objAt(uint32_t(o.l(0x3c)))) r->b(0x68) = 0xff;
        o.l(0x3c) = 0;
        return;
    }
    if (int16_t(dx - 0x122) < 0 && int16_t(dx + 0x122) >= 0 && int16_t(dy - 0x8c) < 0 && int16_t(dy + 0xd2) >= 0) {
        Obj* r = ctx.spawnFrom(o, true, 0);                                 // FUN_004351c5
        if (!r) return;                                                     // (the original writes through a5 = -1)
        r->type = 14; r->frame = o.b(0x32) ? 0xfe : 0xff; r->flags = 0x4000;
        r->setSprite(ctx.spr("ropes"));
        r->y -= 0x10;
        o.l(0x3c) = int32_t(ctx.pl.addrOf(r));
    }
}

// 0x419d00: type 0x6f, a platform drifting right (ob_float, placed by FUN_00433d0b from the .als
// id 0xc9): 1 px a tick, from the level's end (w32) back to -0x30, bobbing by the table 0x45bdcd
// every fourth tick.  Only with the camera at 0x1d7 or lower and the platform within -0x170 .. 0x30
// px of the camera's left edge is it handled; otherwise its display entry is taken back.  Harry
// stands on it (box 0x45bded) and goes along unless a wall (trigger class bit 0) is ahead.
void h_floatPlatform(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    static const int16_t kBob[16] = {0, 0, 1, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, -1, 0, 0};
    if (int16_t(ctx.camY - 0x1d7) < 0) { ctx.pl.objectSystem().popFrontDisplay(); return; }
    o.x += 1;
    if (o.x == o.w(0x32)) o.x = int16_t(0xffd0);
    const int16_t d0 = int16_t(ctx.camX - o.x);
    if (int16_t(d0 - 0x30) >= 0 || int16_t(d0 + 0x170) < 0) { ctx.pl.objectSystem().popFrontDisplay(); return; }
    o.b(0x36) -= 1;
    if (int8_t(o.b(0x36)) < 0) {
        o.b(0x36) = 3;
        o.w(0x34) = int16_t((o.w(0x34) + 2) & 0x1e);
        o.y += kBob[o.w(0x34) >> 1];
    }
    animStep(ctx, o, o.anim(0x5a), kAnim_floatBob, 9);
    if (h.b(0x465bb3) == 0 && h.b(0x465b73) == 0 && h.l(0x465b7e) == 0 && h.w(0x465baa) >= 0
        && harryOnBox(ctx, o, -28, 28, -7, 9)) {                            // 0x45bded
        h.y() = o.y;
        h.b(0x465b73) = 0xff;
        h.onGround() = 0xff;
        if (!(ctx.trigger(h.x() + 0x18, h.y()).cls & 1)) h.x() += 1;
        h.vy() = 0;
    }
}


// ---------------------------------------------------------------- rat (type 23, 0x40b5d2)

// 0x40b4d6: Harry running into it (states 0x42e297 / 0x42ea8b) stumbles (hyitrip1 with the
// run script 0x46168e); touching it again once he is 5 frames into the stumble knocks him
// back.  d0: he started to stumble.
bool tripHarry(ObjCtx& ctx, Obj& o) {
    if (!ctx.touchesHarry(o)) return false;
    Harry& h = ctx.harry;
    if (h.spriteIs("hyitrip1")) {
        if (int8_t(uint8_t(h.frame()) - 5) < 0) return false;
        h.b(0x465b55) = h.facing() != 0 ? 0 : 0xff;
        ctx.pl.harryCall(0x427167);
        return false;
    }
    if (h.cont() != 0x42e297 && h.cont() != 0x42ea8b) return false;
    if (h.facing() != 0) { h.setCont(0x42e297); h.vx() = -0x30000; }
    else { h.setCont(0x42ea8b); h.vx() = 0x30000; }
    h.w(0x465b47) = 0;
    h.setSprite("hyitrip1");
    h.setUl(0x465ba0, 0x46168e);
    ctx.pl.harryCall(0x42e9b2);
    return true;
}

// 0x40b84f: a rat trips Harry when he runs (0x40b4d6), otherwise it bites (5).
void ratContact(ObjCtx& ctx, Obj& o) {
    const uint32_t c = ctx.harry.cont();
    if (c == 0x42e297 || c == 0x42ea8b) tripHarry(ctx, o);
    else harryContact(ctx, o, 5);
}

void h_rat(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xff) {
        o.frame = 0;
        o.l(0x32) = -0x20000; o.facing = 1;
        { int y = o.y + 0x19; if (ctx.floorSnap(o.x - 0xc, y)) o.y = int16_t(y - 0x19); }
        if (ctx.harry.x() - o.x >= 0) { o.facing = 0; o.l(0x32) = 0x20000; }
    }
    bool killed = false;
    weaponHits(ctx, o, 9, &killed);
    if (killed) { killObject(ctx, o); return; }
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
    ratContact(ctx, o);                       // 0x40b84f
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
    weaponHits(ctx, o, 9, &killed);
    if (killed) { killObject(ctx, o); return; }
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
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
    const bool swinging = ctx.harry.cont() == 0x42aa3b;   // 0x40a8a7: a wider speed band while Harry rides (state 0x42aa3b)
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
    weaponHits(ctx, o, 9, &killed);
    if (killed) { killObject(ctx, o); return; }
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
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


// ---------------------------------------------------------------- scorpion (types 132 / 133, 0x41baa0)

void h_scorpion(ObjCtx& ctx, Obj& o) {
    bool killed = false;
    weaponHits(ctx, o, 2, &killed);
    if (killed) { killObject(ctx, o); return; }
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
        Obj* half = ctx.spawnFrom(o, true);   // FUN_0043546e(1)
        if (!half) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        half->facing = 0xff;
        std::memcpy(half->s + 0x12, o.s + 0x12, 2);
        half->sprite = o.sprite;
        half->flags = 0x6000;                 // (type 0: drawn only; the owner animates it)
        half->link = &o;                      // (+0x50)
        o.link = half;                        // (+0x5a)
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
    if (o.link) {                                     // 0x406e3f: the mirrored half (+0x5a)
        o.link->frame = o.frame;
        if (farFromCamera(ctx, o)) {                  // 0x424ec4
            ctx.pl.restoreEntry(o);
            ctx.release(o);
            ctx.release(*o.link);
        }
        return;
    }
    exitDespawnFar(ctx, o);
}


// ---------------------------------------------------------------- scenery bits

// 0x41f320: type 161, static scenery (snake holes and dots, leaves); the one drawn with the
// sprite of slot 0x47006c (fx_leaf4, fxleaf42 on level 5) sits behind Harry.
void h_dot(ObjCtx& ctx, Obj& o) {
    if (o.sprite == ctx.spr("fx_leaf4")) o.behind = 0xff;
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
        if (Obj* l = ctx.spawnFrom(o, true, 0)) {     // FUN_0043546e(0)
            l->w(0x4e) = 0x46;
            l->y = o.y;
            l->x = int16_t(o.x + int(ctx.rnd() & 0x3f) - 0x20);
            l->type = 0x40; l->flags = 0;
            const int32_t r = int32_t(ctx.rnd() & 0x7fff);
            l->l(0x36) = r;
            if (r & 1) { l->sprite = ctx.spr("fx_leaf1"); l->script = kAnim_feather; l->l(0x36) += 0x6000; }
            else { l->sprite = ctx.spr("fxzleaf2"); l->script = kAnim_turn4; l->l(0x36) += 0x10000; }   // (0x4619e8: kAnim_turn4's bytes)
        }
        // back to 0x412c5e: a new wait, counted down once at once
        o.b(0x32) = uint8_t((ctx.rnd() & 0x3f) + 0x40);
        o.b(0x32) -= 1;
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- tree branch (types 123 / 124 / 125)

// 0x41b2da: the branch base spawns its tip, a platform that gives way.
void h_branch(ObjCtx& ctx, Obj& o) {
    if (o.frame == 0xfe) { o.facing = 1; o.frame = 0xff; }
    if (o.frame == 0xff) {
        o.frame = 0;
        Obj* tip = ctx.spawnFrom(o, true);            // FUN_0043546e(2)
        if (!tip) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        tip->facing = o.facing;
        tip->sprite = ctx.spr("obrbrnch");
        tip->frame = 1; tip->type = 0x7c; tip->b(0x32) = 0x19;
        tip->x = int16_t(o.x - 6 + (o.facing ? 0xc : 0));
        tip->y = o.y;                                 // (its flags stay 0)
        tip->link = &o; o.link = tip;                 // (+0x50 both ways)
    }
    if (farFromCamera(ctx, o)) {                      // 0x424ec4
        ctx.pl.restoreEntry(o);
        ctx.release(o);
        Obj* tip = o.link;                            // (a freed record keeps its fields)
        if (tip && tip->type == 0x7c && tip->link == &o) ctx.release(*tip);
    }
}

// 0x41b424: the tip bends under Harry and snaps after 25 ticks (or when he leaves).
void h_branchTip(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) != 0x19) {
        if (o.b(0x32) == 0) {
            if (o.frame == 2) {
                ctx.sound0(4, 0x11);                                   // FUN_0042633d(4, 0x60)
                if (Obj* p = ctx.spawnFrom(o, true)) {                 // FUN_0043546e(1)
                    p->facing = o.facing; p->type = 0x7d;
                    p->sprite = ctx.spr("obrbrnch"); p->frame = 3; p->b(0x32) = 3;   // (its flags stay 0)
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
        if (ctx.harry.w(0x4667a2) == o.x) o.frame = 8;      // already lit (DAT_004667a2: the lit marker's x)
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
        // The checkpoint (DAT_0046679e / a0 / a4) a little in front of the marker, and its x.
        ctx.harry.w(0x46679e) = int16_t(o.x + 4 + 0x18 - (o.facing == 0 ? 0x30 : 0));
        ctx.harry.w(0x4667a0) = ctx.harry.y();
        ctx.harry.w(0x4667a2) = o.x;
        ctx.harry.b(0x4667a4) = o.facing;
        o.frame = 8;
        if (Obj* f = ctx.spawnFrom(o, true)) {    // the flame (type 0x76; FUN_0043546e(1): it runs this tick)
            f->w(0x26) = 0x6663;
            f->facing = o.facing; f->type = 0x76;
            f->sprite = ctx.spr("fx_mls"); f->frame = 9; f->flags = 0x6000;
            f->y -= 4; f->x += 3;
            if (f->facing) f->x -= 7;
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
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
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
        Obj* body = ctx.spawnFrom(o);                 // FUN_004356bc(2)
        if (!body) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        o.link = body;                                // (+0x6a)
        body->y = int16_t(body->y - 2); body->flags = 0x6000;
        body->sprite = ctx.spr("tszbody1"); body->type = 0x3c; body->frame = 0;
        o.sprite = ctx.spr("tszhang"); o.frame = 0;
    }
    auto releaseBoth = [&]() { if (o.link) ctx.release(*o.link); };   // (FUN_0043598f ignores a null record)
    if (!weaponHit(ctx, o, 0x14).z) { releaseBoth(); killObject(ctx, o); return; }
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
    // 0x417688: meant to move the box 0x12 px while the snake hangs facing right, but it
    // compares Harry's sprite (0x465b1d) with the tszhang slot, so the plain test runs.
    if (ctx.harry.spriteIs("tszhang") && o.facing) { o.x += 0x12; harryContact(ctx, o, 10); o.x -= 0x12; }
    else harryContact(ctx, o, 10);
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
                    if (Obj* st = ctx.spawnFrom(o, true)) {   // FUN_0043546e
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

// 0x416f48: type 88, a tree snake that a hit (or the stop-watch, or the big stone's blast) leaves
// hanging limp (b38; w42 counts up to 0x68), so that Harry can hang on it (tszlimp; his hang
// states 0x42d781 / 0x42cc7a / 0x42de4f / 0x42dd7e take it as a vine through DAT_00466508).
// When it comes to, a Harry still on it falls hurt (state 0x42f88c); while it is lively it knocks
// a hanging Harry off.  Otherwise it is the tree snake of type 172 with a shorter rest (0x20)
// after a spit, and its body record (type 0x3c, +0x6a).
void h_limpSnake(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    if (o.w(0x32) == 0) {
        o.w(0x32) = 0x40; o.behind = 0xff;
        Obj* body = ctx.spawnFrom(o);                             // FUN_004356bc(2)
        if (!body) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        o.link = body;                                            // (+0x6a)
        body->y = int16_t(body->y - 2); body->flags = 0x6000;
        body->sprite = ctx.spr("tszbody1"); body->type = 0x3c; body->frame = 0; body->behind = 0xff;
    }
    auto hanging = [&]() {
        const uint32_t c = h.cont();
        return c == 0x42d781 || c == 0x42cc7a || c == 0x42de4f || c == 0x42dd7e;
    };
    auto onIt = [&]() {                                           // Harry right under it
        const int16_t dx = int16_t(h.x() - o.x), dy = int16_t(h.y() - o.y);
        return int16_t(dx - 0x20) < 0 && int16_t(dx + 0x20) >= 0 && int16_t(dy - 6) >= 0 && int16_t(dy - 0x88) < 0;
    };
    auto hangTest = [&]() {                                       // the box test as tszlimp against hyihang
        const uint32_t hs = h.spriteSlot();
        const ph::Sprite* os = o.sprite;
        o.sprite = ctx.spr("tszlimp"); h.setSprite("hyihang");
        const bool r = ctx.touchesHarry(o);
        o.sprite = os; h.setSpriteSlot(hs);
        return r;
    };
    auto knockOff = [&]() {
        h.setCont(0x42f88c); h.w(0x465b47) = 0;
        h.b(0x465b77) = 0x1e; h.w(0x4658bc) = h.x();
    };
    if (!weaponHit(ctx, o, 0x14).z) { o.b(0x38) = 0xff; o.w(0x50) = 0; o.b(0x43) = 0xff; }
    if (h.b(0x46652f) != 0 || o.b(0x38) != 0) {
        // 0x41700c: limp
        o.w(0x64) = 0; o.w(0x36) = 0;
        o.w(0x42) += 1;
        if (o.w(0x42) == 0x68) {
            o.b(0x38) = 0; o.hits = 0;
            if (hangTest()) knockOff();
        }
        o.sprite = ctx.spr("tszlimp");
        animStep(ctx, o, o.anim(0x3a), kAnim_treeSnakeLimp, 5);   // 0x45bc8e
        bool hold = hanging() && onIt();
        if (!hold && hangTest()) {
            if (hanging() || h.b(0x465b77) == 0) hold = true;
            else {
                // just knocked off: not again within 0x30 px of that place
                const int16_t d = int16_t(h.w(0x4658bc) - h.x());
                hold = int16_t(d + 0x30) < 0 || int16_t(d - 0x30) >= 0;
            }
        }
        if (hold) { h.w(0x466508) = o.x; o.frame = 3; o.facing = 0; }   // 0x417210
    } else {
        // 0x41722c
        if (hanging() && onIt()) knockOff();
        // 0x417688 (see h_treeSnake)
        if (h.spriteIs("tszhang") && o.facing) { o.x += 0x12; harryContact(ctx, o, 10); o.x -= 0x12; }
        else harryContact(ctx, o, 10);
        for (;;) {
            if (o.w(0x36)) {                                      // 0x4172f8: spitting / biting
                o.sprite = ctx.spr("tszspit");
                const bool looped = animStep(ctx, o, o.anim(0x36), kAnim_treeSnakeSpit, 5);   // 0x45bc94
                if (o.frame == 3 && treeSnakeBiteRange(ctx, o)) ctx.hurtHarry(0x14);       // 0x420530
                if (!looped) {
                    if (o.w(0x36) == 5) {
                        if (Obj* st = ctx.spawnFrom(o, true)) {   // FUN_0043546e(1)
                            st->x = int16_t(o.x + 0x20); st->y = int16_t(st->y + 0x26);
                            st->sprite = ctx.spr("obrston0"); st->frame = 0; st->b(0x5a) = 0xc; st->type = 0x1d; st->b(0x46) = 0xff;
                            o.w(0x60) = int16_t((ctx.rnd() & 0x7f) + 8);
                            st->l(0x32) = 0x58000; st->l(0x36) = 0x8000; st->l(0x3a) = -0x100; st->l(0x3e) = 0x1100;
                            st->flags = 0x6000;
                            if (o.facing) { st->l(0x32) = -st->l(0x32); st->l(0x3a) = -st->l(0x3a); st->x -= 0x40; }
                        }
                    }
                    break;
                }
                o.w(0x66) = 0x20; o.w(0x64) = 0; o.w(0x34) = 0; o.w(0x36) = 0;
                o.facing ^= 1;
            }
            if (o.w(0x64)) {                                      // 0x417432: rising to look for Harry
                o.sprite = ctx.spr("tszup");
                if (!animStep(ctx, o, o.anim(0x64), kAnim_treeSnakeUp, 5)) {   // 0x45bc7a
                    if (o.frame == 3) {
                        const int16_t dx = int16_t(h.x() - o.x), dy = int16_t(h.y() - 0x1c - o.y - 0x28);
                        if (int16_t(dx - 0x90) < 0 && int16_t(dx + 0x90) >= 0 && int16_t(dy - 0x50) < 0 && int16_t(dy + 0x50) >= 0) {
                            o.w(0x36) = 1; o.facing ^= 1; continue;
                        }
                    }
                    break;
                }
                o.w(0x66) = int16_t(0x30 + (ctx.rnd() & 0x3f));
                o.w(0x64) = 0; o.w(0x36) = 0; o.w(0x34) = 0;
            }
            // 0x417543: hanging idle
            o.sprite = ctx.spr("tszhang");
            animStep(ctx, o, o.anim(0x34), kAnim_treeSnakeHang, 6);   // 0x45bc74
            o.w(0x64) = 0; o.w(0x36) = 0; o.w(0x38) = 0;
            if (o.w(0x66)) { o.w(0x66) -= 1; break; }
            const int16_t dx = int16_t(h.x() - o.x), dy = int16_t(h.y() - 0x1c - o.y - 0x28);
            if (int16_t(dx - 0xb0) < 0 && int16_t(dx + 0xb0) >= 0 && int16_t(dy - 0x90) < 0 && int16_t(dy + 0x90) >= 0) {
                const bool facingHarry = dx >= 0 ? o.facing != 0 : o.facing == 0;
                if (facingHarry && o.frame == 0) o.w(0x64) = 1;
            }
            break;
        }
    }
    if (farFromCamera(ctx, o)) {                                  // 0x424ec4
        if (o.link) ctx.release(*o.link);
        ctx.pl.restoreEntry(o);
        ctx.release(o);
    }
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
    auto rearm = [&]() { o.w(0x32) = int16_t((ctx.rnd() & 0x1f) + 0x22); };   // 0x416c0d
    // 0x416c32.  Both the recoil's end and the open animation's end rearm and run the
    // handler again from here on the same tick.
    for (;;) {
        if (o.w(0x50)) {
            // Recoiling from a whip hit.
            o.whipCool = 0;
            if (!animStep(ctx, o, o.anim(0x50), kAnim_plantHit, 4)) { exitDespawnFar(ctx, o); return; }
            o.frame = 0; o.w(0x34) = 0; o.w(0x50) = 0;
            rearm();
            continue;
        }
        if (o.frame == 0 && !between(ctx.harry.x() - o.x, -0xc0, 0xc0)) { exitDespawnFar(ctx, o); return; }
        if (o.frame == 0 && o.w(0x64) == 3) {
            o.w(0x32) = 0x30;                     // three hits: it stays shut
        } else {
            o.w(0x32) -= 1;
            if (o.w(0x32) != 0) {
                animStep(ctx, o, o.anim(0x60), kAnim_plantBreathe, 7);
                if (o.w(0x32) == 1 && o.frame != 2) o.w(0x32) = 2;
            } else {
                o.w(0x32) = 1; o.w(0x60) = 0;
                if (animStep(ctx, o, o.anim(0x34), kAnim_plantOpen, 4)) { rearm(); continue; }
                if (o.frame == 5) {
                    bool killed = false;
                    const int16_t y0 = o.y;
                    if (o.vflip) o.y += 0x2a;
                    weaponHits(ctx, o, 0, &killed);
                    o.y = y0;
                    if (killed) { o.w(0x50) = 0x100; o.w(0x64) += 1; exitDespawnFar(ctx, o); return; }
                }
            }
        }
        break;
    }
    if (o.anim(0x34).tick == 0 && o.anim(0x34).pos == 0xd) {     // 0x416dc4
        // Fully open: spikes fly out in six directions.
        ctx.sound1(0x40, 8);
        for (const SpikeShot& sp : kPlantSpikes) {
            Obj* k = ctx.spawnFrom(o, true);          // FUN_0043546e(1)
            if (!k) break;                            // (the original tries the same entry again: no record frees up)
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
    const WeaponHit r = weaponHit(ctx, o, 0x1f);
    if (r.d1) {
        if (r.d0) { killObject(ctx, o); return; }
        if (o.b(0x64) == 0 && o.w(0x3c) == 0) {
            const int d = ctx.harry.x() - o.x;
            if (d >= 0 ? o.facing != 0 : o.facing == 0) { o.w(0x58) = 1; o.w(0x3e) = 0; }   // whipped head-on: tumbles
        }
    }
    // The grazing test below reads d1 after the contact test, which on its every-6th-call ticks
    // leaves a box edge (or 0x60) there instead of the weapon test's flag: so a grazing boar on
    // screen wakes up within a few ticks whatever the distance, unless that byte happens to be 0.
    int16_t d1 = r.d1 ? 1 : 0;
    harryContact(ctx, o, 0xf, &d1);
    if (o.b(0x64)) {
        // Grazing until Harry comes within 128 px (or something hits it).
        o.w(0x68) -= 1;
        if (o.w(0x68) < 0) o.w(0x68) = int16_t((ctx.rnd() & 0x3f) + 0x30);
        o.sprite = ctx.spr("whitoss");
        animStep(ctx, o, o.anim(0x62), kAnim_boarGraze, 4);
        const int d = ctx.harry.x() - o.x;
        if (uint8_t(d1) == 0 && !between(d, -0x80, 0x80)) { exitDespawnWide(ctx, o); return; }
        o.b(0x64) = 0;
        if (!between(d, -0x80, 0x80)) o.facing ^= 1;
    }
    moveX(o, o.l(0x32));
    auto easeTowardsFacing = [&](int32_t step, int limit) {
        if (o.facing == 0) { if (o.w(0x34) != -limit - 1) o.l(0x32) -= step; }
        else { if (o.w(0x34) != limit) o.l(0x32) += step; }
    };
    bool run = true;                                     // on to 0x415965
    if (o.w(0x58)) {
        // Tumbling after a whip hit (then straight to 0x415abb).
        o.sprite = ctx.spr("whiroll");
        animStep(ctx, o, o.anim(0x3e), kAnim_sparkle2, 4);
        easeTowardsFacing(0x7000, 4);
        run = false;
    } else if (o.w(0x3c)) {
        // Skidding round; once stopped it turns and runs on this tick.
        o.sprite = ctx.spr("whiroll");
        animStep(ctx, o, o.anim(0x3e), kAnim_sparkle2, 4);
        if (o.l(0x32) < 0) o.l(0x32) += 0x3000; else o.l(0x32) -= 0x3000;
        if (o.w(0x34) != 0) { exitDespawnWide(ctx, o); return; }
        o.w(0x44) = 0x32; o.facing ^= 1;
        o.sprite = ctx.spr("whirun"); o.w(0x3c) = 0;
    }
    if (run) {
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
        easeTowardsFacing(0x4000, 4);                    // 0x415a8b
    }
    // 0x415abb: skid round when Harry is behind (not for 0x32 ticks after a turn) or at a
    // trigger id 2 ahead (always).
    bool brake = false;
    if (o.w(0x44)) o.w(0x44) -= 1;
    else {
        const int d = int16_t(ctx.harry.x() - o.x);
        brake = d >= 0 ? o.facing == 0 : o.facing != 0;
    }
    if (!brake) {
        uint8_t id = 0;
        ctx.triggerClass(o.x + (o.facing ? 0x48 : -0x48), o.y - 8, &id);
        brake = id == 2;
    }
    if (brake) { o.w(0x3c) = 1; o.w(0x3e) = 0; o.w(0x58) = 0; }
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
    weaponHits(ctx, o, 9, &killed);
    if (killed) { killObject(ctx, o); return; }
    { int y = o.y + 0x17; if (ctx.floorSnap(o.x - 0xc, y)) o.y = int16_t(y - 0x17); }
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
    ratContact(ctx, o);                       // 0x40b84f
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
            Obj* b = ctx.spawnFrom(o, true);          // FUN_0043546e(3)
            if (!b) break;
            if (i == 0) o.link = b; else o.link2 = b; // l3c / l40
            b->w(0x26) = 0x6663;                      // (d0 != 0: FUN_004263a6)
            b->type = 0x2b; b->flags = 0x6000; b->sprite = ctx.spr("btfhang"); b->frame = 0; b->behind = 0xff;
            if (i == 1) b->x = int16_t(b->x + 0x10);
        }
    }
    auto asleep = [&](Obj* b) { return b && b->type == 0x2b; };
    auto wake = [&](Obj* b) { b->facing = o.facing; b->b(0x32) = 1; b->type = 0x12; };
    bool counting = false;
    if (o.w(0x34)) {                                  // the second bat follows 0x32 ticks later
        o.w(0x34) -= 1;
        if (o.w(0x34) == 0) {
            if (asleep(o.link2)) wake(o.link2);
            ctx.release(o);
            return;
        }
        counting = true;
    }
    if (!counting) {
        const int16_t dx = int16_t(o.x - ctx.harry.x());
        if (dx < -0x8c) o.facing = 0xff;
        else if (dx >= 0x5a) o.facing = 0;
        else {
            const int16_t dy = int16_t(o.y - ctx.harry.y());
            if (dy >= -0xc8 && dy < 0x14) {
                if (asleep(o.link)) { Obj* b = o.link; o.link = nullptr; wake(b); }
                o.w(0x34) = 0x32;
                return;
            }
        }
    }
    if (farFromCamera(ctx, o)) {                      // 0x424ec4: both bats still asleep go with it
        if (asleep(o.link)) ctx.release(*o.link);
        if (asleep(o.link2)) ctx.release(*o.link2);
        ctx.pl.restoreEntry(o);
        ctx.release(o);
    }
}

// 0x40f9dc: type 0x2b, a bat still asleep on the roost (only the whip can reach it).
void h_sleepingBat(ObjCtx& ctx, Obj& o) {
    bool killed = false;
    weaponHits(ctx, o, 9, &killed);
    if (killed) killObject(ctx, o);
}


// ---------------------------------------------------------------- mine ceiling (types 96 / 150)

// 0x41836e: type 96, drops a rock from the ceiling every 0x46..0xc5 ticks.
void h_ceilingDripper(ObjCtx& ctx, Obj& o) {
    if (o.w(0x32) == 0) o.w(0x32) = int16_t(1 + (ctx.rnd() & 0x1f));
    o.w(0x32) -= 1;
    if (o.w(0x32) == 0) {
        if (Obj* r = ctx.spawnFrom(o, true)) {        // FUN_0043546e(1)
            ctx.sound0((ctx.rnd() & 0x80) ? 0x29 : 0x2c, 3);   // FUN_0042633d
            r->type = 0x96; r->y = int16_t(r->y - 8);
            r->sprite = ctx.spr("ob_ceil"); r->frame = 0; r->flags = 0;
            r->x = int16_t(r->x + int(ctx.rnd() & 0x3f) - 0x20);
            r->w(0x32) = 0x14;
            o.w(0x32) = int16_t(0x46 + (ctx.rnd() & 0x7f));
        }                                             // (no record: the count stays 0 and restarts next tick)
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
    // 0x40e18d
    Harry& h = ctx.harry;
    if (ctx.levelNo == 0x18) {
        // On level 24 the crate is ew_ready, facing left, a frame every 7 ticks (0 .. 5); a hit
        // blows it up as a crate (without the passages of level 2) and starts DAT_0046b1b4: 200
        // ticks later FUN_0043628c leaves with "*6".
        o.setSprite(ctx.spr("ew_ready"));
        o.facing = 1;
        o.b(0x36) += 1;
        if (o.b(0x36) >= 7) { o.b(0x36) = 0; if (++o.frame >= 6) o.frame = 0; }
        if (weaponHit(ctx, o, 0).z) { exitDespawnFar(ctx, o); return; }
        ctx.pl.startEnding();
    } else {
        if (!ctx.pl.harryOnPlatform() && !ctx.pl.harryBusy() && h.w(0x465baa) >= 0 && harryOnBox(ctx, o, -15, 15, -32, -16)) {
            // 0x40e21a: Harry stands on the crate (box 0x45ba6e); no landing
            h.y() = int16_t(o.y - 0x18);
            h.vy() = 0;
            h.b(0x465b73) = 0xff;
        }
        if (weaponHit(ctx, o, 0).z) { exitDespawnFar(ctx, o); return; }
        o.y -= 0xa;
        // The two crates of level 2 that open passages (DAT_00466524 / 25, kept across deaths).
        if (o.x != 0x6b8) {
            if (int16_t(o.x - 0x11b0) >= 0) { ctx.g.levelFlags()[0x15] = 0xff; ctx.pl.clearTriggerClass(0x5d); }
            else { ctx.g.levelFlags()[0x14] = 0xff; ctx.pl.clearTriggerClass(0x51); }
        }
    }
    ctx.g.stats().score += scoreForType(o.type);              // 0x40e28f: 0x4081ae
    o.link = nullptr;
    ctx.release(o);
    Obj* e = ctx.spawnFrom(o, true);                           // FUN_0043546e(7): the crate's own record, x / y kept
    if (!e) return;
    ctx.sound0(0x26, 0xa);                                     // FUN_0042633d(0x26, 0x60)
    e->type = 0x32; e->sprite = ctx.spr("fxzexpl2"); e->flags = 0xe000; e->link = nullptr;
    e->frame = 0xff; e->behind = 0xff; e->l(0x3c) = 0; e->facing = 0;
    if (Obj* half = ctx.spawnFrom(*e, true, 0)) {              // FUN_0043546e(0) from the explosion: the mirrored half
        e->link = half; half->link = e;                        // (+0x54 / +0x50)
        half->behind = 0xff;
        std::memcpy(half->s + 0x12, e->s + 0x12, 2);
        half->w(0x26) = 0x6663; half->type = 0;
        half->sprite = ctx.spr("fxzexpl2"); half->flags = 0xe000; half->frame = 0xff; half->facing = 0xff;
    }
}

// 0x41052a: type 0x32, the explosion; from frame 3 it blasts Harry for 40 every tick and
// shoves him away (FUN_00427167).
void h_explosion(ObjCtx& ctx, Obj& o) {
    if (animStep(ctx, o, o.anim(0x3c), kAnim_marker, 4)) {    // 0x4619cf
        ctx.release(o);
        if (o.link) ctx.release(*o.link);                     // (+0x54, the mirrored half)
        return;
    }
    if (o.link) o.link->frame = o.frame;
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
        Obj* half = ctx.spawnFrom(o, true);                    // FUN_0043546e(1)
        if (!half) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        half->vflip = 0xff;
        std::memcpy(half->s + 0x12, o.s + 0x12, 2);
        half->flags = 0x6000; half->sprite = o.sprite;
        half->link = &o;                                       // (+0x50)
        o.link = half;                                         // (+0x5a)
    }
    bool cut = false;
    if (webHit(ctx, o, &cut)) {                                // 0x40dcc5
        o.w(0x3c) = 1;
        o.facing = int16_t(o.x - ctx.harry.x()) >= 0 ? 0xff : 0;
        if (cut) {                                             // torn through
            if (o.link) ctx.release(*o.link);
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
    if (o.link) { o.link->frame = o.frame; o.link->facing = o.facing; }
    if (farFromCamera(ctx, o)) {
        ctx.pl.restoreEntry(o);
        ctx.release(o);
        if (o.link) ctx.release(*o.link);
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

void h_drip(ObjCtx& ctx, Obj& o);

void h_projectile(ObjCtx& ctx, Obj& o) {
    ctx.pl.setTriggerClass(2, 0);            // 0x40d3b0: trigger id 2 does not stop thrown things
    if (o.script) {
        anim2Step(ctx, o, 0x50, 3);
        if (!weaponHit(ctx, o, 0).z) { ctx.sound1(0xcd, 9); projectileShatter(ctx, o); return; }
    }
    if (priorityTrigger(ctx, o, 0).id == 0xd0) {   // 0x4301ce; into water: becomes a splash drip
        o.sprite = ctx.spr("fx_drip1"); o.flags = 0xa000; o.behind = 0xff;
        o.w(0x40) = 1; o.type = 0x53;
        ctx.sound1(0xbb, 5);
        h_drip(ctx, o);                       // jmp 0x415f4a: the drip's handler runs at once
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
    if (ctx.frozen()) { exitDespawnFar(ctx, o); return; }
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
    if (ctx.frozen()) { exitDespawnTall(ctx, o); return; }
    if (o.b(0x32) == 0) { o.b(0x32) = 0xff; o.w(0x34) = int16_t((ctx.rnd() & 0x3f) + 0x40); }
    o.w(0x34) -= 1;
    if (o.w(0x34) != 0) { exitDespawnTall(ctx, o); return; }
    if (Obj* d = ctx.spawnFrom(o, true)) {                               // FUN_0043546e(1)
        const uint32_t r = ctx.rnd();
        d->x = int16_t(o.x + (r & 3)); d->y = int16_t(o.y - 4); d->w(0x26) = 0x6663;
        d->facing = uint8_t(r & 1);
        d->type = 0x53; d->w(0x32) = 0xa;
        d->sprite = ctx.spr("fx_drip1"); d->frame = 0; d->flags = 0x2000;
    }
    // back to 0x415eb0: a new wait, counted down once at once, and the exit test
    o.w(0x34) = int16_t((ctx.rnd() & 0x3f) + 0x40);
    o.w(0x34) -= 1;
    exitDespawnTall(ctx, o);
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

// ---------------------------------------------------------------- the Loltun vaults (levels 11..13)

// The vaults' Simon game, run by their three levers (type 26, 0x40b9e0).  The first lever to
// run leads (DAT_0046617c; its top byte DAT_0046617f doubles as "the combination is being
// shown"): it collects the three letters (type 103) into 0x466186, then each round shows the
// combination at 0x466194 (one more random lever 0..2 each round, a step every 0x18 ticks: the
// lever's letter lit for 16 ticks, its palette line and its note) and waits for Harry to pull
// the levers in that order (the keys 1..3 send him to them, Harry's state 0x42756d).  A wrong
// lever drops the ceiling (trg+0x214) and spells "wrong" in type 0x6f letters (which run the
// float platform's handler and so never show on the PC); ten rounds open the way out (trg+0x215)
// and drop a 1-up; rounds 3 / 5 / 7 / 9 drop a coin, silver, gold and a ring (type 166).
// Every lever runs its own pull: it springs back (0x45bab2) once Harry lets go.
namespace {

void vaultNote(ObjCtx& ctx, int lever) {
    // 0x40b960 copies the lever's palette line (0x45b954 + lever * 0x20) into the Genesis colours
    // at 0x462654 (DAT_0046654f: changed); the port does not keep those colours, so the flash
    // is not shown.  Then the lever's note (0x45aa04).
    static const uint8_t kNote[3] = {0xcc, 0xc5, 0xee};
    ctx.sound1(kNote[lever], 0x12);
}

void vaultKeysReset(Harry& h) {
    // the queue of the keys 1..3 (0x4661a8; read 0x4661a0, write 0x4661a4)
    h.setUl(0x4661a0, 0x4661a8);
    h.setUl(0x4661a4, 0x4661a8);
    h.b(0x4661a8) = 0;
}

void vaultLight(ObjCtx& ctx, int lever) {
    Harry& h = ctx.harry;
    vaultNote(ctx, lever);
    if (Obj* letter = ctx.pl.objAt(h.ul(0x466186 + uint32_t(lever) * 4))) letter->b(0x32) = 0x10;
}

// 0x40c597: Harry, facing a vault lever, has it within reach.
bool vaultReach(ObjCtx& ctx, const Obj& o) {
    Harry& h = ctx.harry;
    int16_t d0 = int16_t(h.x() + 0x20);
    if (h.facing()) d0 = int16_t(d0 - 0x40);
    d0 = int16_t(d0 - o.x);
    return int16_t(d0 + 0x20) >= 0 && int16_t(d0 - 0x20) < 0;
}

void vaultDrop(ObjCtx& ctx, Obj& o, int x, const char* sprite) {
    // a reward falling from the top of the room (type 166)
    if (Obj* a5 = ctx.spawnFrom(o, true, 2)) {           // FUN_0043546e(2)
        a5->x = int16_t(x);
        a5->y = -0x30;
        a5->setSprite(ctx.spr(sprite));
        a5->frame = 0;
        a5->flags = 0x6000;
        a5->behind = 0xff;
        a5->type = 0xa6;
        ctx.harry.b(0x465b59) = 1;
    }
}

void vaultRound(ObjCtx& ctx, Obj& o) {
    // 0x40bb1c: a round starts after 0x100 (first) or 0x60 ticks; its reward, if it has one.
    Harry& h = ctx.harry;
    o.l(0x52) = 0x40bc8d;
    h.b(0x4661e0) = 0xff;
    h.b(0x4661e1) = 0xff;
    const int d0 = (o.w(0x5c) + 1) >> 1;
    if (h.b(0x4661e0 + uint32_t(d0)) == 0) {
        h.b(0x4661e0 + uint32_t(d0)) = 0xff;
        static const char* const kReward[8] = {"mi_coin", "mi_coin", "mi_coin", "mi_silvr", "mi_gold", "mi_ring", "mi_1up", "mi_coin"};   // 0x45aa08
        vaultDrop(ctx, o, 0x40, kReward[d0 & 7]);
        ctx.sound1(0x22, 0xa);
    }
    h.b(0x46617f) = 0xff;                                 // the combination is shown
}

void vaultPull(ObjCtx& ctx, Obj& o) {
    // 0x40c0c6: every lever.  Pulled (b34), it springs back once Harry lets go; otherwise Harry
    // arriving next to it (DAT_00465b45) on the player's turn pulls it (0x427d92).
    Harry& h = ctx.harry;
    PlayLogic& pl = ctx.pl;
    if (o.b(0x34) != 0) {
        if (h.ul(0x465b4f) != pl.addrOf(&o) || h.b(0x465b53) == 0) {
            if (animStep(ctx, o, o.anim(0x36), kAnim_leverBack, 1)) { o.frame = 0; o.b(0x34) = 0; }
        }
        return;
    }
    if (h.b(0x465b45) != 0 && h.b(0x46617f) == 0) {
        if (o.frame == 4 || !vaultReach(ctx, o)) return;  // (another lever may be the one)
        o.b(0x34) = 0xff;
        if (Obj* lead = pl.objAt(h.ul(0x46617c)))
            if (h.b(uint32_t(lead->l(0x58)) + 1) == 0xff) vaultKeysReset(h);   // the last of the combination
        h.l(0x465b4b) = 0;
        h.setUl(0x465b4f, pl.addrOf(&o));
        h.b(0x465b53) = 0xff;
        o.facing = h.facing();
        h.setUl(0x466180, pl.addrOf(&o));                 // for the leading lever to check
    }
    h.b(0x465b45) = 0;
}

void leverVault(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    PlayLogic& pl = ctx.pl;
    if (pl.tb(0x215) != 0 || pl.tb(0x214) != 0) { vaultPull(ctx, o); return; }   // solved, or the ceiling falls
    if (o.b(0x6a) != 0) {                                 // the pause after a round
        o.b(0x6a) -= 1;
        if (o.b(0x6a) != 0) { vaultPull(ctx, o); return; }
        vaultRound(ctx, o);
    } else if (o.b(0x35) == 0) {
        // 0x40ba49: the first tick
        h.b(0x465b59) = 0;
        for (uint32_t a = 0x4661e0; a < 0x4661f0; a += 4) h.l(a) = 0;        // no reward given
        o.b(0x35) = 0xff;
        o.l(0x58) = 0x466194;
        for (uint32_t a = 0x466194; a < 0x4661a0; a += 4) h.l(a) = -1;      // no combination yet
        o.l(0x52) = 0x40c0c6;
        if (h.l(0x46617c) != 0) { vaultPull(ctx, o); return; }              // another lever leads
        h.setUl(0x46617c, pl.addrOf(&o));
        uint32_t a0 = 0x466186;
        for (Obj* p : pl.objectSystem().list())
            if (p->type == 0x67) { h.setUl(a0, pl.addrOf(p)); a0 += 4; }
        o.w(0x5c) = 1;                                    // the length of the combination
        o.w(0x50) = 0x100;
        vaultRound(ctx, o);
    }
    for (int pass = 0; pass < 2; ++pass) {                // (a wrong lever starts a round and runs its wait)
    switch (uint32_t(o.l(0x52))) {                        // jmp [a6 + 0x52]
    case 0x40bc8d:                                        // the wait before the round
        o.w(0x50) -= 1;
        if (o.w(0x50) != 0) break;
        o.l(0x58) = 0x466194;
        o.w(0x50) = 0x18;
        o.l(0x52) = 0x40bd36;
        o.w(0x56) = o.w(0x5c);
        o.w(0x5c) += 1;
        if (o.w(0x5c) == 0xb) {                           // ten rounds: the way out opens
            pl.tb(0x215) = 0xff;
            pl.setTl(0x21c, 0);
            vaultDrop(ctx, o, 0x100, "mi_1up");
            return;
        }
        for (Obj* p : pl.objectSystem().list()) if (p->type == 0x6c) ctx.release(*p);   // 0x40c295: the instructions go
        break;
    case 0x40bd36: {                                      // show a step of the combination
        o.w(0x50) -= 1;
        if (o.w(0x50) != 0) break;
        const uint32_t a0 = uint32_t(o.l(0x58));
        uint32_t d0 = h.b(a0);
        if (d0 == 0xff) {                                 // a new step
            do d0 = ctx.rnd() & 3; while (d0 == 3);
            h.b(a0) = uint8_t(d0);
            h.b(a0 + 1) = 0xff;
            pl.tb(0x1fc) = 0xff;                          // the ceiling creeps down from now on
        }
        o.l(0x58) += 1;
        vaultLight(ctx, int(d0));
        o.w(0x50) = 0x18;
        o.w(0x56) -= 1;
        if (o.w(0x56) != 0) break;
        vaultKeysReset(h);
        o.l(0x52) = 0x40be67;
        break;
    }
    case 0x40be67:                                        // then the player's turn
        o.w(0x50) -= 1;
        if (o.w(0x50) != 0) break;
        h.b(0x46617f) = 0;
        o.l(0x52) = 0x40be92;
        o.l(0x58) = 0x466194;
        break;
    case 0x40be92: {                                      // a lever pulled: the right one?
        if (h.l(0x466180) == 0) break;
        const uint32_t pulled = h.ul(0x466180);
        h.l(0x466180) = 0;
        uint8_t d0 = 0;
        for (Obj* p : pl.objectSystem().list()) {
            if (pl.addrOf(p) == pulled) break;
            if (p->type == 0x1a) d0 += 1;
        }
        const uint32_t a0 = uint32_t(o.l(0x58));
        if (d0 != h.b(a0)) {                              // wrong
            vaultKeysReset(h);
            o.w(0x5c) = 1;
            o.w(0x50) = 0x60;
            pl.spawnText("wrong", 0, 0x50, 0x6f);         // 0x45a9fe
            pl.tb(0x214) = 0xff;
            ctx.sound0(0x17, 0x1d);                       // FUN_0042633d(0x17, 0x60)
            o.l(0x58) = 0x466194;
            for (uint32_t a = 0x466194; a < 0x4661a0; a += 4) h.l(a) = -1;
            o.b(0x64) += 1;
            if (o.b(0x64) == 8) h.b(0x465b72) = 0x7e;
            vaultRound(ctx, o);                           // (0x40bb1c runs on into the wait)
            continue;
        }
        vaultLight(ctx, d0);
        o.w(0x50) = 0x40;
        if (h.b(a0 + 1) == 0xff) {                        // the round is done
            vaultKeysReset(h);
            o.b(0x6a) = 0x1e;
            pl.setTw(0x1fc, 0);
            return;
        }
        o.l(0x58) += 1;
        h.b(0x46617f) = 0;
        break;
    }
    default: break;                                       // 0x40c0c6
    }
    break;
    }
    vaultPull(ctx, o);
}

} // namespace

// 0x418e24: type 103, a vault lever's letter (abc, abc1 while lit: b32 counts the 16 ticks
// down).  The first one also brings in the room's doors (type 21, the template 0x4684a4) at
// x -0x25 and 0x12d.
void h_vaultLetter(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    if (o.b(0x3c) == 0) {
        o.x += 8;
        o.b(0x3c) = 0xff;
        o.frame = h.b(0x4661d6);
        h.b(0x4661d6) += 1;
        if (h.b(0x4661d6) == 1) {
            for (int i = 0; i < 2; ++i) {
                Obj* a5 = ctx.spawnFrom(o, true, 6);      // FUN_004351c5: {0x15, 0, 0, ob_door3, 6}
                if (!a5) continue;
                a5->type = 0x15; a5->frame = 0; a5->flags = 0;
                a5->setSprite(ctx.spr("ob_door3"));
                a5->x = int16_t(i == 0 ? -0x25 : 0x12d);
                a5->y = 0x99;
                if (i == 1) a5->facing = 0xff;
            }
        }
    }
    if (o.b(0x32) != 0) {
        o.b(0x32) -= 1;
        if (o.b(0x32) != 0) { o.setSprite(ctx.spr("abc1")); exitDespawnFar(ctx, o); return; }
    }
    o.setSprite(ctx.spr("abc"));
    exitDespawnFar(ctx, o);
}

// 0x40b390: type 21, a vault door.  The left one slides in behind Harry at the start (to x 1
// on vault 3, 3 on the others: a clunk), the right one slides away once the combination is
// done.  Vault 3 has the plain door 14 px left and 6 up; vault 1's doors are not drawn.
void h_vaultDoor(ObjCtx& ctx, Obj& o) {
    const int stop = ctx.levelNo == 0xc ? 1 : 3;
    if (ctx.levelNo == 0xc) {
        o.setSprite(ctx.spr("ob_door"));
        if (o.b(0x64) == 0) { o.x -= 0xe; o.y -= 6; o.b(0x64) = 0xff; }
    } else if (ctx.levelNo == 0xd) {
        o.frame = 0xff;
    }
    if (int16_t(o.x - 0x50) < 0) {
        if (o.x == stop) return;
        o.x += 1;
        if (o.x == stop && ctx.levelNo != 0xd) ctx.sound0(0xbc, 0xb);
        return;
    }
    if (ctx.pl.tb(0x215) != 0) o.x += 2;
}

// 0x418f00: type 107, set up with the vault: 30 ticks in, the instructions (type 108 letters).
void h_vaultIntro(ObjCtx& ctx, Obj& o) {
    if (o.b(0x32) == 0x1e) return;
    o.b(0x32) += 1;
    if (o.b(0x32) != 0x1e) return;
    ctx.pl.spawnText("watch the combination", -3, 0x10, 0x6c);   // 0x45bd6c
    ctx.pl.spawnText("then repeat using the", -3, 0x1a, 0x6c);   // 0x45bd82
    ctx.pl.spawnText("keyboard", -3, 0x24, 0x6c);                // 0x45bd98
}

// 0x418f98: type 108, a letter of a text (behind the others; in front on the continue screen).
void h_textLetter(ObjCtx& ctx, Obj& o) {
    o.behind = 0xff;
    o.flags = 0x6000;
    if (ctx.levelNo == 0xe) o.flags = 0x8000;            // (tests [DAT_004504e4] + 0x37a both ways)
}

// 0x41f6ae: type 166, a vault reward falling (up to 9 px a tick) onto the floor at y 0xa0;
// Harry touching it takes it (a 1-up also scores 5000).
void h_vaultReward(ObjCtx& ctx, Obj& o) {
    if (o.w(0x34) != 9) o.l(0x32) += 0x3000;
    moveY(o, o.l(0x32));
    if (int16_t(o.y - 0xa0) >= 0) o.y = 0xa0;
    if (ctx.touchesHarry(o)) {
        if (o.sprite && o.sprite->name() == "mi_1up") ctx.g.stats().score += 5000;
        applyPickup(ctx, o);                              // 0x4057c9
        ctx.release(o);
    }
}

// 0x40c2d6: a lever (type 26).  Standing in front of it with "up" pressed pulls it, which
// runs the level script.  In the Loltun vaults (levels 11..13) it runs the Simon game above.
void h_lever(ObjCtx& ctx, Obj& o) {
    if (ctx.levelNo >= 11 && ctx.levelNo <= 13) { leverVault(ctx, o); return; }
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
        if (!(o.sprite && o.sprite->name() == "ob_door3")) {   // (already ob_door3: b6c stays 0)
            if (ctx.levelNo == 4 || ctx.levelNo == 9) {
                o.sprite = ctx.spr("ob_door3");
                o.w(0x26) = 0x6663;                 // FUN_004263c4 / FUN_004263a6(6), which never fails on the PC
            }
            o.b(0x6c) = 0xff;
        }
    }
    halfMinuteTick(o);                              // 0x4057b0
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
        Obj* half = ctx.spawnFrom(o, true, 0);                 // FUN_0043546e(0)
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
        if (ctx.levelNo == 4 || ctx.levelNo == 9) {
            o.sprite = ctx.spr("ob_door3");
            o.w(0x26) = 0x6663;                                  // FUN_004263c4 / FUN_004263a6(6), which never fails on the PC
        }
        const uint8_t id = triggerIdAt(ctx, o.x - 0x18, o.y - 0x10);
        o.b(0x32) = id;
        int y = o.y + 0x22;
        // 0x420779 means to test the class of the door's trigger, but the floor probe has
        // replaced d3 by then (with shape * 16 + (x & 15) on a floor), so whether a door shut
        // before a restart comes back shut depends on that class byte, not its own.
        const uint32_t d3 = floorProbeD3(ctx, o.x, y, id);
        ctx.floorSnap(o.x, y);
        o.y = int16_t(y - 0x20 + 0xb);
        o.frame = 0xff;
        if (ctx.pl.triggerClassOf(uint8_t(d3)) != 0) o.frame = 0;
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
    else if (!weaponHit(ctx, o, 0).z) shatter = true;
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
// 0x412987: a hit while a sling stone's velocity is on record (DAT_00466788, see the skull
// warrior's 0x4132ec) chips a piece of rock (gargrok, a type 0x1d debris) off the gargoyle
// where that stone hit.
void gargoyleChip(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    if (h.l(0x466788) == 0) return;
    Obj* c = ctx.spawnFrom(o, true);                    // FUN_0043546e(1)
    if (!c) return;
    c->w(0x26) = 0x6663;
    c->sprite = ctx.spr("gargrok");
    uint32_t r;
    do { r = ctx.rnd() & 3; } while (r == 3);
    c->frame = uint8_t(r);
    c->b(0x68) = 0xff;
    c->b(0x5a) = o.b(0x5c);
    c->type = 0x1d;
    c->x = h.w(0x466782); c->y = h.w(0x466784);
    const int32_t v = h.l(0x466788);
    moveX(*c, v);
    c->l(0x32) = v;
    c->l(0x3a) = 0; c->l(0x3e) = 0x6e00;
    c->flags = o.flags;
    c->b(0x46) = 0xff;
    c->l(0x36) = int32_t(ctx.rnd() & 0x1ffff) - 0x50000;
}

void h_gargoyle(ObjCtx& ctx, Obj& o) {
    bool killed = false;
    if (weaponHits(ctx, o, 0x28, &killed)) {
        gargoyleChip(ctx, o);
        if (killed) { killObject(ctx, o); return; }
    }
    if (ctx.frozen()) { freezeSparkle(ctx, o); return; }
    if (o.frame == 5) harryContact(ctx, o, 0x14);
    if (o.frame == 0 && !between(ctx.harry.y() - o.y, -0x60, 0x90)) { exitDespawnFar(ctx, o); return; }
    if (animStep(ctx, o, o.anim(0x36), kAnim_gargoyle, 6)) o.b(0x64) ^= 1;
    const uint8_t was = o.facing;
    AnimCursor& cur = o.anim(0x36);
    if (cur.tick == 0 && int8_t(cur.pos - 0xc) < 0) {
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
            else { a->l(0x32) = -0x61000; a->l(0x46) = -0x3000; }
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
    if (ctx.pl.bossActive()) { exitDespawnFar(ctx, o); return; }
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
        o.facing = uint8_t(o.x & 1);              // the parity of the new x
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
        ctx.pl.objectSystem().cancelCurrentDisplay();   // [DAT_00465b8e] = -2
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
        if (o.b(0x33) == 0 && !ctx.channel1Busy()) ctx.sound1(0xc6, 8);   // (b33 stays 0)
        o.b(0x3c) += 1;
        if (o.b(0x3c) == 3) {
            if (Obj* s = ctx.spawnFrom(o)) {                  // FUN_004356bc(1): a sparkle at the tips
                s->x = int16_t(s->x - 1); s->y = int16_t(s->y + 0x19);
                s->type = 0x10; s->flags = 0x6000; s->b(0x64) = 0xff; s->sprite = ctx.spr("fx_sprk2");
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
            parts[i] = ctx.spawnFrom(o, true);        // FUN_0043546e
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
    Obj* parts[3] = {o.link, o.link2, o.link3};                 // l3c / l40 / l44
    for (Obj* p : parts) if (!p) { ctx.release(o); return; }
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
                if (o.b(0x33) == 0 && !ctx.channel1Busy()) ctx.sound1(0xc6, 8);
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
    // 0x418858 tests byte 0x33 of a0, which still points at the cursor (+0x32): that is
    // o+0x65, always 0, so the count runs on every tick of frame 3 and the roar comes once a
    // cycle while channel 0 is free.
    if (o.b(0x65) == 0 && o.frame == 3) {
        o.b(0x46) += 1;
        if (o.b(0x46) >= 6 && !ctx.channel0Busy()) { o.b(0x46) = 0; ctx.sound0(0x27, 5); }   // FUN_0042633d(0x27, 0x60)
    }
    if (int8_t(o.frame - 6) >= 0 && int8_t(o.frame - 0xa) < 0) {
        // Burnt in the air: a fresh press of button B (DAT_0046676d = 1), which lets go of a
        // vine whose handler runs later in this pass.
        if (harryContact(ctx, o, 0x19) && ctx.harry.onGround() == 0) ctx.harry.b(0x46676d) = 1;
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- clay pot (type 38, 0x40ed62)

// Spawn an object from an .als template at the pot's position (FUN_004351c5).
Obj* spawnFromTemplate(ObjCtx& ctx, const Obj& at, int templateId) {
    // FUN_004351c5 with a3 = the .als template: {type, frame, flags, sprite slot, d0} at the
    // parent's x / y, at the tail of the list (FUN_0043546e).
    const ph::ObjectTemplate& t = ph::kObjectTemplates[templateId];
    Obj* p = ctx.spawnFrom(const_cast<Obj&>(at), true, t.soundSlot);   // d0 = template +8
    if (!p) return nullptr;
    p->type = t.type; p->frame = t.sub; p->flags = t.flags;
    p->sprite = t.sprite[0] ? ctx.spr(t.sprite) : nullptr;
    return p;
}

// A breakable pot.  The trigger under it (ids 0x34..) says what is inside; otherwise the
// contents are random.  A whip hit breaks it (fxzpots2 shards) and lets the contents out:
// 1 heart, 2 an explosion, 3 a bat, 4 a beating heart, 5 a stone, 6 a pouch, 7 a rat,
// 8 a treasure, 9 bat or pouch, 10 a 1-up.
void h_pot(ObjCtx& ctx, Obj& o) {
    if (o.b(0x50) == 0) {
        o.b(0x50) = 0xff;
        const uint8_t id = ctx.trigger(o.x - 0x18, o.y + 0x18).id;
        if (int8_t(id - 0x34) >= 0) o.b(0x51) = uint8_t(id - 0x34);   // ids 0x34 .. 0xb3
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
    if (o.b(0x6c) == 0 && weaponHit(ctx, o, 0).z) { exitDespawnFar(ctx, o); return; }
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
        o.w(0x26) = 0x6663;                           // FUN_004263c4 / FUN_004263a6(7), which never fails on the PC
        ctx.sound0(0x26, 0xf);                        // FUN_0042633d(0x26, 0x60)
        o.type = 0x32; o.l(0x56) = 0; o.flags = 0x6000; o.sprite = ctx.spr("fxzexpl2");
        o.frame = 0xff; o.l(0x3c) = 0; o.facing = 0;
        if (Obj* h = ctx.spawnFrom(o, true, 0)) {     // FUN_0043546e(0): the mirrored half (type 0)
            o.link = h; h->link = &o;                 // +0x54 / +0x50
            h->behind = 0xff;
            std::memcpy(h->s + 0x12, o.s + 0x12, 2);
            h->w(0x26) = 0x6663;
            h->sprite = ctx.spr("fxzexpl2"); h->flags = 0x6000; h->frame = 0xff; h->facing = 0xff;
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
    o.w(0x26) = 0x6663;                               // FUN_004263c4 / FUN_004263a6(8)
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
    auto driftHome = [&](uint32_t r) {            // 0x41ac1b / 0x41ad6a: maybe 0.75 px/tick toward home
        o.l(0x5a) = 0;
        if (r & 1) o.l(0x5a) = int16_t(o.w(0x60) - o.x) >= 0 ? 0xc000 : -0xc000;
    };
    if (o.w(0x60) == 0) {
        { int y = o.y + 0xd; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0xd); }
        o.script = kAnim_skullChatter;            // l50
        o.w(0x60) = o.x;
    }
    if (o.l(0x5a) != 0) o.facing = o.l(0x5a) >= 0 ? 0xff : 0;
    bool ground = true;
    if (o.b(0x46)) {                              // in the air
        o.sprite = ctx.spr("skiroll");
        animStep(ctx, o, o.anim(0x34), kAnim_skullRoll, 3);
        moveX(o, o.l(0x5a));
        o.l(0x3c) += 0x4000;
        moveY(o, o.l(0x3c));
        if (int16_t(o.y - o.w(0x40)) < 0) {
            ground = false;
        } else {                                  // landed: the ground code runs this tick too
            ctx.sound0(0x1e, 3);                  // FUN_0042633d(0x1e, 0x60)
            o.y = o.w(0x40);
            o.b(0x46) = 0;
            driftHome(ctx.rnd());
            o.w(0x32) = int16_t((ctx.rnd() & 0x3f) + 0x28);
            o.script = kAnim_skullChatter; o.w(0x34) = 0;
            o.b(0x64) -= 1;
            if (int8_t(o.b(0x64)) < 0) { o.b(0x64) = 2; o.w(0x32) = 6; o.script = kAnim_skullQuiet; }
            else ctx.sound1(0xcf, 2);
        }
    }
    if (ground) {                                 // 0x41acb7: on the ground
        o.sprite = ctx.spr("skijaw");
        animStep(ctx, o, o.anim(0x34), o.script ? o.script : kAnim_skullChatter, 4);
        if (o.frame != 0) moveX(o, o.l(0x5a));
        o.w(0x32) -= 1;
        if (o.w(0x32) < 0) {                      // spring
            o.b(0x46) = 0xff;
            o.l(0x3c) = -0x3e000;
            ctx.sound1(0x45, 2);
            const uint32_t r = ctx.rnd() & 3;
            o.w(0x3e) -= int16_t(r);              // a little higher at random
            o.w(0x40) = o.y;
            driftHome(r);
            o.w(0x34) = 0;
        }
    }
    // 0x41ada3: a whip hit (100 points) or a bite (30 damage) both shatter it
    if (!weaponHit(ctx, o, 0).z) ctx.g.stats().score += 100;
    else if (ctx.touchesHarry(o)) ctx.hurtHarry(0x1e);
    else { exitDespawnFar(ctx, o); return; }
    // 0x41ade1: it becomes a breaking pot (type 0x26) and runs the pot handler at once
    if (ctx.pl.scriptEvent() == 0xcf) { ctx.soundNow(0x89); ctx.pl.scriptEvent() = 0; }   // stop the chatter
    o.w(0x26) = 0x6663;                           // FUN_004263c4 / FUN_004263a6(8)
    o.b(0x32) = 0xff; o.b(0x50) = 0xff; o.w(0x36) = 0;
    o.type = 0x26;
    o.sprite = ctx.spr("fxzpots2");
    ctx.sound0(0x42, 0xa);                        // FUN_004262d9
    h_pot(ctx, o);
}

// ---------------------------------------------------------------- pouch respawner (type 74, 0x415028)

// An invisible marker that keeps a pouch of sling stones in stock: while Harry has fewer
// than 10 stones (DAT_00466528) and the marker sits just outside the screen, it spawns one
// from template 7 (mi_pouc2) at its own position.  The pouch has no .als entry, so it comes
// back every time; l50 remembers it until it is picked up.
void h_pouchRespawner(ObjCtx& ctx, Obj& o) {
    if (!o.link) {
        if (offScreen(ctx, o) && int8_t(ctx.g.stats().ammoCount[0] - 10) < 0) {     // 0x424f40
            if (Obj* p = spawnFromTemplate(ctx, o, 7))   // FUN_004351c5 from the template at 0x467fe4
                o.link = p;                               // l50
        }
    } else if (o.link->type != 2) {                       // picked up (or anything else now)
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
    Obj* l = ctx.spawnFrom(o, true, 0);        // FUN_0043546e(0)
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
        Obj* top = ctx.spawnFrom(o, true, 0);         // FUN_0043546e(0)
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
// make it aggressive), b5a death-burst spread, b5b "charging to the wall", w68 the GO sign
// timer, w6a accumulated damage; its pounce distance is the word at trg+0x1ea (PlayLogic::tw).

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
    o.w(0x26) = 0;                              // FUN_004263c4
    ctx.g.stats().score += scoreForType(o.type);
    o.y -= 0x14;
    o.b(0x5a) = 0xff;
    for (const char* n : {"jmzmorf2", "jmzmorfa", "jmzmorfb", "jmzswipe", "jm0swipe", "jm1swipe", "jmzready"})
        if (o.sprite && o.sprite->name() == n) { o.y -= 0x10; o.b(0x5a) = 0; break; }
    ctx.g.setRandomSeed(0x12d233, 0x84a43c94);  // 0x415c48 (and DAT_0046677e = 0x13477db9, read by nothing)
    if (Obj* c = ctx.spawn()) {                 // FUN_004356bc(3)
        c->x = o.x; c->y = o.y;                 // (the +8 at 0x415c98 is overwritten right after)
        c->w(0x26) = 0x6663;
        c->script = kAnim2_bossCloud; c->l(0x64) = 0;
        c->link = &o; c->b(0x3c) = 1;
        c->type = 0x82; c->behind = 0xff;
        c->flags = uint16_t((o.flags & 0x8000) + 0x6000);
        c->sprite = ctx.spr("fx_deat2");
    }
    o.type = 0x51; o.l(0x32) = 0; o.frame = 0xff;   // (the long: the burst counts in w32 and w34)
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
    e->flags = 0x6000; e->b(0x55) = 0xff;
    if (o.b(0x5a) == 0) {
        e->x += int16_t((ctx.rnd() & 0x1f) - 0x10); e->y += int16_t((ctx.rnd() & 0x3f) - 0x20);
    } else {
        e->x += int16_t((ctx.rnd() & 0x3f) - 0x20); e->y += int16_t((ctx.rnd() & 0x1f) - 0x10);
    }
    ctx.sound0(6, 0x18);                        // FUN_0042633d(6, 0x60, 0x18)
}

// 0x41018c (+ the screen placement at 0x4335ce): the boss health meter, type 0x30.
void h_bossMeter(ObjCtx& ctx, Obj& o) {
    ctx.pl.hudAdd(o);                                                   // 0x4150a2: placed by FUN_0043348e
    Obj* d2 = o.link; Obj* d3 = o.link2; Obj* pct = o.link3;            // l32 / l36 / l60
    if (!d2 || !d3 || !pct) return;
    o.behind = d2->behind = d3->behind = pct->behind = 0xff;
    pct->frame = 0;
    // Whose hit count it shows: on level 10 the record at l5a while l56 is a type 0x69 boss,
    // else a type 0x8c boss at l50, else the boss at trg+0x1f4 (0x3e hit points on level 8,
    // 0x1e elsewhere).
    const Obj* hp = nullptr;
    int full = 0;
    if (ctx.levelNo == 10) {
        const Obj* b = ctx.pl.objAt(uint32_t(o.l(0x56)));
        if (b && b->type == 0x69) { hp = ctx.pl.objAt(uint32_t(o.l(0x5a))); full = 0x30; }
    } else if (o.l(0x50) != 0) {
        const Obj* b = ctx.pl.objAt(uint32_t(o.l(0x50)));
        if (b && b->type == 0x8c) { hp = b; full = 0x26; }
    } else if (ctx.pl.bossActive()) {
        hp = ctx.pl.bossObject();
        full = ctx.levelNo == 8 ? 0x3e : 0x1e;
    }
    if (!hp) {                                                          // 0x4104b5: hidden, sliding back out
        o.frame = 0xff; d2->frame = 0xff; d3->frame = 0;
        if (o.w(0x3c) < 0) o.w(0x3c) += 1;
        return;
    }
    const int left = uint8_t(full - hp->hits);                         // (a byte subtraction: an overkill wraps round)
    const int percent = (left * 100 / full) & 0x7f;
    Harry& h = ctx.harry;                                               // FUN_00431a3f: the digits at 0x466556..5b
    for (int k = 0, v = percent, p = 100000; k < 6; ++k, p /= 10) { h.b(0x466556 + k) = uint8_t(v / p); v %= p; }
    o.frame = h.b(0x466559); d2->frame = h.b(0x46655a); d3->frame = h.b(0x46655b);
    if (o.frame == 0) { o.frame = 0xff; if (d2->frame == 0) d2->frame = 0xff; }
    if (o.w(0x3c) != -0x30) o.w(0x3c) -= 1;                             // slides in from below
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
            ctx.harry.facing() = 0xff;
            ctx.pl.setBoss(&o);                     // DAT_00466507 = 0xff, trg+0x1f4 = this
            ctx.pl.setTw(0x1ea, 0x50);              // trg+0x1ea: a Genesis variable the PC keeps inside the level's
                                                    // trigger record (on level 0 over the end of column 0x22's list)
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
            ctx.pl.endBoss();                       // DAT_00466507 = 0
            bossDeath(ctx, o);
            h_bossDeathBurst(ctx, o);               // 0x415b92 runs straight into the 0x51 handler
            // The `call 0x415b92` never returns here (the handler ends with `jmp 0x405313`); its
            // return address is popped by the `ret` that ends the object pass, which then sets
            // DAT_00465d49 = 1 at 0x410e6a (the walk-out).
            ctx.pl.strayReturn(0x410e6a);
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
            o.l(0x64) = 0;
            if (Obj* s = ctx.spawnFrom(o, true)) {        // FUN_0043546e(2)
                o.l(0x64) = int32_t(ctx.pl.addrOf(s));
                s->w(0x26) = 0x6663;                      // (d0 != 0: FUN_004263a6)
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
            if (int8_t(o.frame - 6) >= 0) {
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
            const bool right = uint8_t(o.facing ^ 1) == 0;   // the call sites flip the facing byte around 0x411a3e
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
                if (fast && between(d - int16_t(ctx.pl.tw(0x1ea)), -10, 10)) {
                    ctx.pl.setTw(0x1ea, ctx.pl.tw(0x1ea) == 0x64 ? 0x90 : 0x64);
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
            auto run = [&] {                        // 0x41101a (and back to the object pass)
                toRun(ctx, o, -0x50, 4);
                o.l(0x38) = o.facing ? 0x30000 : -0x30000;
            };
            if (!roaring && hitNow) { run(); return; }   // a hit breaks the crouch
            if (!anim2Step(ctx, o, 0x3e, 4)) return;
            const int16_t d = int16_t(ctx.harry.x() - o.x);
            const bool behind = (d >= 0) == (o.facing == 0);
            if (behind || (ctx.harry.onGround() && (ctx.rnd() & 1) == 0)) { run(); return; }
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

// ---------------------------------------------------------------- stone jaguars (types 140 / 141 / 153 / 154)
//
// The end of the Lost City of Copan: two jaguar statues (type 140, 0x41c4c2) on stone bases
// (type 141).  Harry walking into the arena (or a stone bouncing off a statue) starts the
// fight: the statue that sees it locks the camera, spawns the fight's controller (type 153)
// and a health meter for each jaguar.  The controller wakes the first jaguar after 0x30 ticks
// (its base crumbles, and 0x27 ticks later it roars and runs) and the second once the first has
// taken 6 hits, then keeps the two taking turns: every 750 / 700 ticks the roles change between
// b5b (charging at full speed), b5c (attacking: it crouches and pounces at Harry, and leaps up
// onto the ledge on the left) and b5d (jumps back down from the ledge).  With both dead the
// controller becomes the rope out of the arena (type 154).  0x465c58 / 0x465c5c hold the two
// jaguars (0 once dead), 0x465c60 "the fight has started".
// Jaguar state: l32 the continuation address (the states below), w36 anim cursor, l38 vx (w3a
// its integer part), l3e / b42 the pounce scripts' cursor, l50 the wake counter, the pounce's
// vy or the ledge scripts' cursor (b54 its tick, w56 the jump-down timer), l52 the base, w4c a
// tick counter, l5e the ledge jumps' vy, w6a the damage taken (hits = w6a >> 3, 0x26 kill),
// b6c "a stone bounced off", b6d "the fight is on".
namespace stoneJaguar {
constexpr uint32_t kStatue = 0x41cb3a, kCrouch = 0x41cb8b, kPounce = 0x41cccb, kWake = 0x41cdef, kRun = 0x41ce7c,
                   kLedgeLeap = 0x41d067, kLedge = 0x41d195, kJumpDown = 0x41d251, kReverse = 0x41d356;
constexpr int kHitPoints = 0x26;

void setState(Obj& o, uint32_t st) { o.l(0x32) = int32_t(st); }

bool snapFloor(ObjCtx& ctx, Obj& o) {              // FUN_00425d6f at y + 0xd
    int y = o.y + 0xd;
    const bool found = ctx.floorSnap(o.x, y);
    o.y = int16_t(y - 0xd);
    return found;
}

// The other jaguar of the pair (null when there is none or it is dead).
Obj* other(ObjCtx& ctx, const Obj& o) {
    const Harry& h = ctx.harry;
    const uint32_t a = h.ul(0x465c58) == ctx.pl.addrOf(&o) ? h.ul(0x465c5c) : h.ul(0x465c58);
    return ctx.pl.objAt(a);
}

void toReverse(ObjCtx& ctx, Obj& o) {               // 0x41d042
    o.sprite = ctx.spr("jgurevrs");
    setState(o, kReverse);
    o.w(0x36) = 0;
}

// 0x41c7dd: a health meter (type 0x30) for the jaguar at `jag`: b5a = 1 puts it left, 2 right.
// (The original does not test the spawns.)
void spawnMeter(ObjCtx& ctx, Obj& from, uint32_t jag, uint8_t side) {
    Obj* m = ctx.spawnFrom(from, false, 1);         // FUN_004356bc(1)
    if (!m) return;
    m->sprite = ctx.spr("scoline"); m->flags = 0xe000;
    m->b(0x5a) = side;
    m->l(0x50) = int32_t(jag);
    m->type = 0x30;
    Obj** links[3] = {&m->link, &m->link2, &m->link3};   // l32 / l36 / l60
    for (int i = 0; i < 3; ++i) {
        Obj* d = ctx.spawnFrom(from, false, 1);
        if (!d) return;
        d->sprite = ctx.spr(i == 2 ? "mi_prcnt" : "scoline"); d->flags = 0xe000;
        *links[i] = d;
    }
}

// 0x41c697: the fight starts.
void startFight(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    o.b(0x6c) = 0;
    o.b(0x6d) = 0xff;
    h.b(0x466507) = 0xff;                            // a boss fight (trg+0x1f4 is left alone)
    // 0x41c6ac: the Genesis boss music (FUN_004263a2(0x3c), a stub in the PC build) and the
    // Genesis driver's byte DAT_004627bd (= DAT_00462696 * 3), which nothing on the PC reads.
    if (Obj* f = ctx.spawnFrom(o, true, 1)) f->type = 0x99;   // FUN_0043546e(1)
    h.b(0x465c60) = 0xff;
    h.setUl(0x465c58, ctx.pl.addrOf(&o));
    // 0x41c730: the jaguars' colours (0x45b3e4) into the Genesis palette line 2 at 0x462654
    // (DAT_0046654f: changed); the PC build draws from its own palettes and reads neither.
    o.flags = 0x4000;
    ObjectSystem& os = ctx.pl.objectSystem();
    for (Obj* p = os.first(); p; p = os.nextOf(*p))
        if (p->type == 0x8c && p != &o) { h.setUl(0x465c5c, ctx.pl.addrOf(p)); p->flags = 0x6000; break; }
    spawnMeter(ctx, o, h.ul(0x465c58), 1);
    spawnMeter(ctx, o, h.ul(0x465c5c), 2);
}
} // namespace stoneJaguar

void h_stoneJaguar(ObjCtx& ctx, Obj& o) {
    using namespace stoneJaguar;
    Harry& h = ctx.harry;
    for (int pass = 0; pass < 8; ++pass) {         // `jmp 0x41c4c2`: a state change re-runs the handler
        if (o.frame == 0xff) {
            // Asleep (released, and respawned by the .als scan) until Harry is up in the arena.
            if (h.x() < 0x7ba || h.y() >= 0xcb) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
            o.frame = 0;
            if (o.x < 0x800) o.facing = 1;           // the left one looks right
            h.setUl(0x465c58, 0); h.setUl(0x465c5c, 0); h.w(0x465c60) = 0;
            o.w(0x6c) = 0;
            snapFloor(ctx, o);
            o.b(0x3c) = uint8_t((ctx.rnd() & 3) + 2);
            o.y += 0xd;
            setState(o, kStatue);
            o.l(0x38) = -0x10000;
            Obj* base = ctx.spawnFrom(o, false, 6);  // FUN_004356bc(6)
            if (!base) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
            base->type = 0x8d;
            base->sprite = ctx.spr("jgustone");
            base->y += 3;
            base->frame = 0;
            base->facing = o.facing;
            base->flags = 0x6000;
            o.l(0x52) = int32_t(ctx.pl.addrOf(base));
        }
        // 0x41c60c: the camera stops on the arena once it gets there
        const int16_t camX = h.w(0x4658b4);
        if (h.w(0x465b0d) != 0 && camX < 0x7e4 && camX >= 0x7c0 && h.w(0x4658b6) == 0) {
            h.w(0x465b09) = 0x71d; h.w(0x465b0b) = 0x7d5; h.w(0x465b0d) = 0;
        }
        if (h.b(0x465c60) == 0 && (o.b(0x6c) != 0 || (h.y() < 0xb8 && h.x() >= 0x7c0 && h.x() < 0x840)))
            startFight(ctx, o);
        // 0x41c97d: a statue sends stones back (and that starts the fight); awake, the weapons
        // hurt it.  DAT_00466782 marks a hit for this tick.
        h.b(0x466782) = 0;
        if (uint32_t(o.l(0x32)) == kStatue) {
            if (deflectStones(ctx, o)) o.b(0x6c) = 0xff;
        } else {
            o.hits = 0;
            if (weaponHits(ctx, o, 0x1e, nullptr)) {
                if (!ctx.channel0Busy() && (ctx.rnd() & 1) == 0) { ctx.rnd(); ctx.sound0(0x2a, 0xa); }   // FUN_0042633d(0x2a, 0x60, 0xa)
                h.b(0x466782) = 0xff;
                o.w(0x6a) += o.hits;
            }
            o.hits = uint8_t(uint16_t(o.w(0x6a)) >> 3);
            if (o.hits + 1 > kHitPoints) {
                if (h.ul(0x465c58) != ctx.pl.addrOf(&o)) h.setUl(0x465c5c, 0);
                else h.setUl(0x465c58, 0);
                jaguar::bossDeath(ctx, o);
                h_bossDeathBurst(ctx, o);            // 0x415b92 runs straight into the 0x51 handler
                return;
            }
            if (h.invulnerable() == 0 && harryContact(ctx, o, 0xf) && uint32_t(o.l(0x32)) == kPounce) {
                h.b(0x465b55) = o.facing;            // shoved the way the jaguar is going
                ctx.pl.harryCall(0x427167);
            }
        }

        switch (uint32_t(o.l(0x32))) {
        case kStatue:                                // 0x41cb3a: the controller sets w50 to wake it
            o.hits = 0;
            o.w(0x6a) = 0;
            if (o.w(0x50) != 0) {
                o.w(0x50) += 1;
                if (o.w(0x50) == 0x27) {
                    setState(o, kWake);
                    o.l(0x38) = 0;
                    o.sprite = ctx.spr("jguroar");
                    o.y += 6;
                    o.frame = 0;
                    o.behind = 0xff;
                    o.w(0x36) = 0;
                }
            }
            return;
        case kCrouch: {                              // 0x41cb8b: slows down, then pounces or runs on
            if (o.w(0x3a) == 0) o.l(0x38) = 0;
            else o.l(0x38) += o.w(0x3a) < 0 ? 0x4000 : -0x4000;
            moveX(o, o.l(0x38));
            if (!anim2Step(ctx, o, 0x3e, 4)) return;
            // a pounce at Harry ahead, out at the arena's ends (but now and then not while he
            // stands on the ground)
            bool pounce = !h.onGround() || (ctx.rnd() & 3) != 0;
            if (pounce) pounce = o.facing ? h.x() < 0x7b5 : h.x() >= 0x860;
            if (pounce) pounce = int16_t(h.x() - o.x) >= 0 ? o.facing != 0 : o.facing == 0;
            if (pounce) {
                o.script = kAnim2_stoneJagLeap; o.l(0x3e) = 0;
                o.l(0x50) = 0;
                o.sprite = ctx.spr("jguleap");
                setState(o, kPounce);
                continue;
            }
            o.sprite = ctx.spr("jgurun");            // 0x41cc91
            setState(o, kRun);
            o.w(0x4c) = -0x50;
            o.w(0x36) = 0x400;                       // (the cursor's tick byte: frame 0, 4 ticks into it)
            o.l(0x38) = o.facing ? 0x30000 : -0x30000;
            return;
        }
        case kPounce: {                              // 0x41cccb
            if (anim2Step(ctx, o, 0x3e, 5)) {
                o.sprite = ctx.spr("jgurun");
                o.w(0x4c) = 0;
                setState(o, kRun);
                o.w(0x36) = 0x400;
                return;
            }
            moveX(o, o.l(0x38));
            const int32_t vy = o.l(0x50);
            moveY(o, vy);
            o.l(0x50) += 0x5000;
            if (vy >= 0) {
                if (o.y >= 0x3d0) { o.y = 0x3d0; o.l(0x50) = 0; }
                else if (snapFloor(ctx, o)) o.l(0x50) = 0;
            }
            // (0x41cdba: the type 55 jaguar's spring at its leap's second frame, 0x45ac59, which
            // this script's cursor never points at)
            return;
        }
        case kWake:                                  // 0x41cdef: comes to life, then runs
            if (animStep(ctx, o, o.anim(0x36), kAnim_stoneJagWake, 3)) {
                o.w(0x4c) = 0;
                setState(o, kRun);
                o.sprite = ctx.spr("jgurun");
                o.b(0x5a) = 0xff;
                continue;
            }
            snapFloor(ctx, o);
            return;
        case kRun: {                                 // 0x41ce7c
            animStep(ctx, o, o.anim(0x36), kAnim_run8, 3);
            const bool right = uint8_t(o.facing ^ 1) == 0;   // (the facing byte is flipped around the call)
            if (o.b(0x5b)) jaguar::accelerate(o, 0x5500, 6, right);                          // 0x411a7a
            else jaguar::accelerate(o, 0x5500, ctx.levelNo == 8 ? 6 : 5, right);              // 0x411a3e
            moveX(o, o.l(0x38));
            if (o.b(0x5c) != 0 && o.b(0x5b) == 0) {
                // attacking: Harry 0x50 .. 0x70 px ahead at speed -> crouch for a pounce
                if (o.w(0x4c) < 8) {
                    o.w(0x4c) += 1;
                } else if (int8_t(o.hits - 0x13) < 0) {
                    int16_t d = int16_t(h.x() - o.x);
                    bool fast = o.w(0x3a) >= 3;
                    if (!fast && o.w(0x3a) < -3) { fast = true; d = int16_t(-d); }
                    if (fast && d >= 0x50 && d < 0x70) {
                        o.script = kAnim2_stoneJagCrouch; o.l(0x3e) = 0;
                        o.sprite = ctx.spr("jgurn2lo");
                        setState(o, kCrouch);
                        continue;
                    }
                }
            }
            // the arena's ends
            if (o.x >= 0x888) { if (o.facing != 0) { toReverse(ctx, o); continue; } return; }
            if (o.x < 0x778) { if (o.facing == 0) { toReverse(ctx, o); continue; } return; }
            if (o.b(0x5c) != 0 && o.facing == 0 && o.x >= 0x81a && o.x < 0x83a) {   // 0x41cfa4: up onto the ledge
                o.script = kAnim2_stoneJagBigLeap; o.l(0x50) = 0; o.w(0x54) = 0;
                setState(o, kLedgeLeap);
                o.w(0x36) = 0;
                o.l(0x5e) = 0;
                continue;
            }
            if (o.b(0x5c) != 0 || o.b(0x5b) != 0) return;
            // 0x41cffc: Harry 80 px behind at speed -> turn around
            if (o.w(0x3a) < 4 && o.w(0x3a) >= -5) return;
            const int16_t d = int16_t(h.x() - o.x);
            if (o.facing ? d < -0x50 : d >= 0x50) { toReverse(ctx, o); continue; }
            return;
        }
        case kLedgeLeap: {                           // 0x41d067: crouches to a stop, then leaps up to the left
            anim2Step(ctx, o, 0x50, 4);
            moveX(o, o.l(0x38));
            if (o.w(0x3a) != 0) { o.l(0x38) += 0x6000; o.l(0x5e) = int32_t(0xfff83fff); }
            else o.l(0x38) = 0;
            if (o.l(0x50) < 0x1c) return;            // the leap starts at the script's jguleap frames
            o.x -= 5;
            moveY(o, o.l(0x5e));
            o.l(0x5e) += 0x4200;
            if (o.l(0x5e) < 0) return;
            if (!snapFloor(ctx, o)) return;
            ctx.sound1(0x5a, 9);
            setState(o, kLedge);
            o.w(0x36) = 0;
            o.script = kAnim2_stoneJagLand; o.l(0x50) = 0; o.l(0x54) = 0;
            o.l(0x38) = -0x30000;
            continue;
        }
        case kLedge:                                 // 0x41d195: on the ledge until the controller sets b5d
            if (o.w(0x3a) != 0) o.l(0x38) += 0x2000;
            else o.l(0x38) = 0;
            moveX(o, o.l(0x38));
            o.b(0x5c) = 0;
            if (o.sprite && o.sprite->name() == "jgulow" && o.b(0x5d) != 0) {
                o.l(0x5e) = int32_t(0xfffd8000);
                setState(o, kJumpDown);
                o.w(0x36) = 0;
                o.script = kAnim2_stoneJagHop; o.l(0x50) = 0; o.l(0x54) = 0;
                o.l(0x38) = 0x2a000;
                ctx.sound0(0xc7, 0xa);               // FUN_004262d9
                continue;
            }
            anim2Step(ctx, o, 0x50, 4);
            return;
        case kJumpDown:                              // 0x41d251: back down into the arena
            anim2Step(ctx, o, 0x50, 4);
            moveX(o, o.l(0x38));
            moveY(o, o.l(0x5e));
            o.l(0x5e) += 0x4000;
            o.w(0x56) += 1;
            if (o.w(0x56) < 0x20 || o.l(0x5e) < 0) return;
            if (!snapFloor(ctx, o)) return;
            ctx.sound1(0x5a, 9);
            o.w(0x4c) = 0;
            setState(o, kRun);
            o.sprite = ctx.spr("jgurun");
            o.b(0x5a) = 0xff;
            o.facing = 1;
            o.l(0x38) = 0x30000;
            continue;
        case kReverse: {                             // 0x41d356
            // charging past the right end: the other one is told to come down off the ledge and charge
            if (o.b(0x5b) != 0 && o.facing != 0 && o.x >= 0x8a0 && o.x < 0x8c0)
                if (Obj* p = other(ctx, o)) { p->b(0x5d) = 0xff; p->b(0x5b) = 0xff; }
            if (animStep(ctx, o, o.anim(0x36), kAnim_stoneJagReverse, 4)) {
                o.facing ^= 1;
                o.sprite = ctx.spr("jgurun");
                o.w(0x4c) = 0;
                setState(o, kRun);
                o.w(0x36) = 6;
                continue;
            }
            if (int8_t(o.frame - 6) >= 0) jaguar::accelerate(o, 0x3000, ctx.levelNo == 8 ? 6 : 5, o.facing == 0);
            else if (o.w(0x3a) != 0) o.l(0x38) += o.w(0x3a) < 0 ? 0x5000 : -0x5000;
            moveX(o, o.l(0x38));
            snapFloor(ctx, o);
            return;
        }
        default:
            return;
        }
    }
}

// 0x41d47a: type 141, a statue's base.  It keeps 0x14 px above the floor; when the controller
// wakes its jaguar (w32 = 1, which starts the anim cursor at its second frame) it crumbles.
void h_stoneJaguarBase(ObjCtx& ctx, Obj& o) {
    int y = o.y + 9;
    ctx.floorSnap(o.x, y);
    o.y = int16_t(y - 0x14);
    if (o.w(0x32) == 0) return;
    if (o.b(0x3c) == 0) { o.b(0x3c) = 0xff; ctx.sound0(0xe, 0xe); }   // FUN_0042633d(0xe, 0x40, 0xe)
    if (animStep(ctx, o, o.anim(0x32), kAnim_turn7, 4)) ctx.release(o);
}

// 0x41ddac: type 153, the stone jaguars' fight (see h_stoneJaguar).  w32 the wake-up count,
// b34 "both awake", w3c the turn timer, b42 the turn, l46 / l4a the jaguars in their roles.
void h_jaguarFight(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    auto wake = [&](uint32_t jag) {                 // the jaguar's wake counter and its base's cursor
        Obj* j = ctx.pl.objAt(jag);
        if (!j) return;
        j->w(0x50) = 1;
        if (Obj* base = ctx.pl.objAt(uint32_t(j->l(0x52)))) base->w(0x32) = 1;
    };
    if (o.w(0x32) != 0x30) {
        o.w(0x32) += 1;
        o.b(0x42) = 1;
        if (o.w(0x32) != 0x30) return;
        wake(h.ul(0x465c58));
    }
    if (o.b(0x34) == 0) {
        Obj* first = ctx.pl.objAt(h.ul(0x465c58));
        if (!first || int8_t(first->hits - 6) < 0) return;
        o.b(0x34) = 0xff;
        wake(h.ul(0x465c5c));
        first->b(0x5c) = 0xff;
        o.w(0x3c) = 0x2ee;
        o.l(0x46) = int32_t(h.ul(0x465c58));
        o.l(0x4a) = int32_t(h.ul(0x465c5c));
    }
    const uint32_t j1 = h.ul(0x465c58), j2 = h.ul(0x465c5c);
    if (j1 + j2 == 0) {
        // 0x41de9c: both dead.  The camera may go left again, and this becomes the head (not
        // drawn) of a rope of eight segments (type 0, placed by the head) that comes down.
        h.w(0x465b09) = 0x600;
        o.type = 0x9a;
        o.l(0x32) = 0;
        ctx.pl.endBoss();                           // DAT_00466507 = 0
        o.x = 0x7b0; o.y = -0xa0;
        o.sprite = ctx.spr("r_r");
        // (0x41ded0: the original also clears the frame of whichever record a5 was left pointing at)
        Obj* prev = &o;
        for (int i = 0; i < 8; ++i) {
            Obj* s = ctx.spawnFrom(*prev, true, 0);  // FUN_0043546e(0)
            if (!s) return;                          // (the original then writes through a null pointer)
            s->frame = 0;
            s->sprite = ctx.spr("r_r");
            prev->l(0x46) = int32_t(ctx.pl.addrOf(s));
            prev = s;
        }
        prev->frame = 1;                             // the rope's end
        return;
    }
    if (j1 == 0) { if (Obj* j = ctx.pl.objAt(j2)) j->b(0x5d) = 0xff; return; }   // one left: off the ledge
    if (j2 == 0) { if (Obj* j = ctx.pl.objAt(j1)) j->b(0x5d) = 0xff; return; }
    o.w(0x3c) -= 1;
    if (o.w(0x3c) != 0) return;
    o.b(0x42) += 1;
    Obj* a = ctx.pl.objAt(uint32_t(o.l(0x46)));
    Obj* b = ctx.pl.objAt(uint32_t(o.l(0x4a)));
    if (o.b(0x42) == 2) {                            // 700 ticks on: the second one charges
        o.b(0x42) = 0;
        o.w(0x3c) = 0x2bc;
        if (b) b->b(0x5b) = 0xff;
        return;
    }
    o.w(0x3c) = 0x2ee;                               // 750 ticks on: the roles swap
    if (a) { a->b(0x5b) = 0; a->b(0x5c) = 0; a->b(0x5d) = 0xff; }
    if (b) { b->b(0x5b) = 0; b->b(0x5c) = 0xff; b->b(0x5d) = 0; }
    const int32_t t = o.l(0x46); o.l(0x46) = o.l(0x4a); o.l(0x4a) = t;
}

// 0x41e032: type 154, the rope out of the jaguars' arena: the head slides down 0x80 px, the
// segments hang below it 16 px apart, and Harry in reach of it can climb it (DAT_00466508).
// While he climbs it (hyidrope) the rope is drawn in front of him, but for his frames 1, 2, 6, 7.
void h_jaguarRope(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    uint8_t behind = 0xff;
    if (h.spriteIs("hyidrope")) {
        const uint8_t f = h.frame();
        if (f != 1 && f != 2 && f != 6 && f != 7) behind = 0;
    }
    o.behind = behind;
    if (o.w(0x32) != 0x80) { o.w(0x32) += 2; o.y += 2; }
    int16_t y = o.y;
    Obj* s = &o;
    for (int guard = 0; guard < ObjectSystem::kPoolSize; ++guard) {
        y = int16_t(y + 0x10);
        s->behind = behind;
        s = ctx.pl.objAt(uint32_t(s->l(0x46)));
        if (!s) break;
        s->y = y;
    }
    if (harryOnBox(ctx, o, -8, 8, -192, 160)) h.w(0x466508) = o.x;   // the box at 0x45bfca
}

// ---------------------------------------------------------------- the jaguar man (types 158 / 159)
//
// The end of Tazumal (level 8): a jaguar that turns into a man to strike.  It keeps hidden until
// Harry is up at its arena (x >= 0x1640 above y 0xba), then shows 0xf0 px further right, roars
// and runs about as the Ceiba Jungle jaguar does, but where that one pounces this one crouches
// into its man form, faces Harry and swipes at him within 0x70 px; 70 ticks later it changes
// back, charging at 6 px/tick (a shove on contact) when Harry is 0x80 px away or more, else at 3.
// The swipe's blow (jm0swipe, frame 3) costs 0x1e; a touch that leaves Harry done for gets him
// dazed and finished off by the next blow (his states 0x428cc5 / 0x428d7b), and the swipe comes
// with a claw (type 159) when Harry's damage byte tests 0x95 or more as a signed byte - near
// death, but also unhurt.  Health: w6c >> 3, 0x3e kills; the boss meter shows it, and the arena
// keeps it in x 0x1660 .. 0x17b0.
// State: l32 the continuation address (below), w36 anim cursor, l38 vx (w3a its integer part),
// l3e / b42 the crouch script's cursor, l46 / b4a the roar's, l50 / b54 the man form's, b4c / b4d
// the roar's count and "hit while roaring", w56 the man form's count (l56 also the claw, 0 when
// none), b5c "its touch has done for Harry", b6a "set up".
namespace jagMan {
constexpr uint32_t kHidden = 0x41ecc3, kWake = 0x41ef24, kRoar = 0x41ee76, kRun = 0x41efd1, kReverse = 0x41f140,
                   kCrouch = 0x41e80c, kReady = 0x41e8bd, kWait = 0x41eaa1, kSwipe = 0x41eac3, kMorph = 0x41eb76,
                   kCharge = 0x41ebf4;
enum Flow { kDone, kRestart, kJump };              // return / `jmp 0x41e462` / a jump into another state's code

void setState(Obj& o, uint32_t st) { o.l(0x32) = int32_t(st); }
bool spriteIs(const Obj& o, const char* name) { return o.sprite && o.sprite->name() == name; }
bool manForm(const Obj& o) {
    for (const char* n : {"jmzmorf2", "jmzmorfa", "jmzmorfb", "jmzswipe", "jm0swipe", "jm1swipe", "jmzready"})
        if (spriteIs(o, n)) return true;
    return false;
}
void snap(ObjCtx& ctx, Obj& o) {                   // 0x41ef8e
    int y = o.y + 0xd;
    ctx.floorSnap(o.x, y);
    o.y = int16_t(y - 0xd);
}
void toRun(ObjCtx& ctx, Obj& o, int16_t animPos) {
    o.sprite = ctx.spr("jgurun");
    o.w(0x4c) = 0;
    setState(o, kRun);
    o.w(0x36) = animPos;
}
void toRoar(Obj& o) {
    setState(o, kRoar);
    o.w(0x4c) = 0;
    o.script = kAnim2_jaguarRoar; o.l(0x46) = 0; o.w(0x4a) = 0;   // 0x45ac6f
}
void toReady(ObjCtx& ctx, Obj& o, uint32_t st) {   // the man form's idle script (0x45b428)
    o.sprite = ctx.spr("jmzready");
    setState(o, st);
    o.script = kAnim2_jagManReady; o.l(0x50) = 0;
}
// 0x41ea03: back to the jaguar, charging when Harry is 0x80 px away or more.
void changeBack(ObjCtx& ctx, Obj& o) {
    const int16_t d = int16_t(ctx.harry.x() - o.x);
    o.script = kAnim2_jagManMorph; o.l(0x50) = 0; o.l(0x54) = 0;   // 0x45b476
    const bool far = d >= 0x80 || d < -0x80;
    setState(o, far ? kCharge : kMorph);
    const int32_t v = far ? 0x60000 : 0x30000;
    o.l(0x38) = o.facing ? v : -v;
}

Flow body(ObjCtx& ctx, Obj& o, uint32_t st, uint32_t& next) {
    Harry& h = ctx.harry;
    switch (st) {
    case kHidden:                                  // 0x41ecc3
        o.sprite = ctx.spr("jgustill");
        o.frame = 0xff;
        if (h.y() >= 0xba || h.x() < 0x1640) { exitDespawnTall(ctx, o); return kDone; }
        h.w(0x465b09) = 0x1625;
        // 0x41ecfc: its colours (0x45b4d4) into the Genesis palette line 2 at 0x462654, which the
        // PC build does not draw from (see the stone jaguars)
        stoneJaguar::spawnMeter(ctx, o, 0, 0);     // the boss meter (b5a 0, l50 0: the boss at trg+0x1f4)
        o.x += 0xf0;
        o.frame = 0;
        h.b(0x465d49) = 1;
        ctx.pl.setBoss(&o);                        // trg+0x1f4 (stored big-endian) and DAT_00466507
        // (FUN_004263a2(0x47): the Genesis boss music, a stub in the PC build)
        h.w(0x465b0d) = 0x92;
        o.sprite = ctx.spr("jguroar");
        setState(o, kWake);
        return kRestart;
    case kWake:                                    // 0x41ef24: comes to life (the stone jaguars' script)
        if (animStep(ctx, o, o.anim(0x36), kAnim_stoneJagWake, 3)) {
            o.w(0x4c) = 0;
            setState(o, kRun);
            o.sprite = ctx.spr("jgurun");
            o.b(0x5a) = 0xff;
            if (int8_t(o.hits - 0x19) < 0) toRoar(o);
            return kRestart;
        }
        snap(ctx, o);
        return kDone;
    case kRoar: {                                  // 0x41ee76: a pause that gives Harry his opening
        anim2Step(ctx, o, 0x46, 4);
        o.b(0x4c) += 1;
        if (o.b(0x4c) != 0x70) {
            if (h.b(0x466782)) o.b(0x4d) = 0xff;
            if (spriteIs(o, "jguroar") && o.frame != 5) { snap(ctx, o); return kDone; }
            if (o.b(0x4d) == 0) {
                const int16_t d = int16_t(o.x - h.x());
                if (d < -0x68 || d >= 0x68) { snap(ctx, o); return kDone; }
            }
        }
        toRun(ctx, o, 4);                          // 0x41eefc
        return kRestart;
    }
    case kRun: {                                   // 0x41efd1
        animStep(ctx, o, o.anim(0x36), kAnim_run8, 3);
        jaguar::accelerate(o, 0x3500, ctx.levelNo == 8 ? 6 : 5, uint8_t(o.facing ^ 1) == 0);   // 0x411a3e (facing flipped around it)
        moveX(o, o.l(0x38));
        if (o.b(0x5c) != 0 || int8_t(o.hits - 0xf) >= 0) {
            // Harry 0x50 .. 0x75 px ahead at speed: crouch into the man form
            int16_t d = int16_t(h.x() - o.x);
            bool fast = o.w(0x3a) >= 3;
            if (!fast && o.w(0x3a) < -3) { fast = true; d = int16_t(-d); }
            if (fast && d >= 0x50 && d < 0x75) {
                o.script = kAnim2_jagManCrouch; o.l(0x3e) = 0;   // 0x45b404
                o.sprite = ctx.spr("jgurn2lo");
                setState(o, kCrouch);
                return kRestart;
            }
        }
        auto toReverse = [&] { o.sprite = ctx.spr("jgurevrs"); setState(o, kReverse); o.w(0x36) = 0; return kRestart; };
        if (o.x >= 0x175c) { if (o.facing != 0) return toReverse(); snap(ctx, o); return kDone; }
        if (o.x < 0x1698) { if (o.facing == 0) return toReverse(); snap(ctx, o); return kDone; }
        if (o.w(0x3a) >= 4 || o.w(0x3a) < -5) {    // Harry 80 px behind at speed: turn
            const int16_t d = int16_t(h.x() - o.x);
            if (o.facing ? d < -0x50 : d >= 0x50) return toReverse();
        }
        snap(ctx, o);
        return kDone;
    }
    case kReverse:                                 // 0x41f140
        if (animStep(ctx, o, o.anim(0x36), kAnim_stoneJagReverse, 4)) {
            o.facing ^= 1;
            o.sprite = ctx.spr("jgurun");
            o.w(0x4c) = 0;
            setState(o, kRun);
            o.w(0x36) = 6;
            if (int8_t(o.hits - 0x19) >= 0 && (ctx.rnd() & 1) == 0) o.b(0x5b) = 0xff;   // (nothing reads it here)
            return kRestart;
        }
        if (int8_t(o.frame - 6) >= 0) {
            if (o.b(0x5c) == 0 && int8_t(o.hits - 0xf) < 0) { toRoar(o); o.facing ^= 1; return kRestart; }
            jaguar::accelerate(o, 0x3000, ctx.levelNo == 8 ? 6 : 5, o.facing == 0);
        } else if (o.w(0x3a) != 0) {
            o.l(0x38) += o.w(0x3a) < 0 ? 0x5000 : -0x5000;
        }
        moveX(o, o.l(0x38));
        snap(ctx, o);
        return kDone;
    case kCrouch:                                  // 0x41e80c: slows down into the man form
        if (o.w(0x3a) == 0) o.l(0x38) = 0;
        else o.l(0x38) += o.w(0x3a) < 0 ? 0x6000 : -0x6000;
        moveX(o, o.l(0x38));
        if (!anim2Step(ctx, o, 0x3e, 5)) {
            if (o.b(0x42) == 0 && o.l(0x3e) == 0x20) o.flags = 0x4000;   // (0x45b424: the Genesis palette line)
            return kDone;
        }
        toReady(ctx, o, kReady);
        o.l(0x54) = 0;
        next = kReady;
        return kJump;
    case kReady: {                                 // 0x41e8bd: faces Harry, swipes when he comes near
        anim2Step(ctx, o, 0x50, 8);
        int16_t d = int16_t(h.x() - o.x);
        bool turn;
        if (d < 0) { turn = o.facing != 0; d = int16_t(-d); }
        else turn = o.facing == 0;
        if (turn) {
            o.facing ^= 1;
            o.w(0x56) = 0;
        } else if (d >= 0 && d < 0x70) {
            o.script = kAnim2_jagManSwipe; o.l(0x50) = 0; o.l(0x54) = 0;   // 0x45b440
            setState(o, kSwipe);
            o.l(0x56) = 0;
            o.sprite = ctx.spr("jmzswipe");
            if (int8_t(h.damage() - (ctx.hard ? 0x63 : 0x95)) >= 0) {
                if (Obj* c = ctx.spawnFrom(o, true, 4)) {   // FUN_0043546e(4): the claw
                    o.l(0x56) = int32_t(ctx.pl.addrOf(c));
                    c->l(0x5a) = int32_t(ctx.pl.addrOf(&o));
                    c->facing = o.facing;
                    c->type = 0x9f;
                    c->sprite = ctx.spr("jmzswipe");
                    c->flags = 0x4000;
                }
            }
            next = kSwipe;
            return kJump;
        }
        o.w(0x56) += 1;                            // 0x41e9ed
        if (o.w(0x56) != 0x46) return kDone;
        changeBack(ctx, o);
        next = kMorph;                             // (both ways start with the slow change's code)
        return kJump;
    }
    case kWait:                                    // 0x41eaa1: while Harry is finished off
        anim2Step(ctx, o, 0x50, 8);
        return kDone;
    case kSwipe: {                                 // 0x41eac3
        bool done = false;
        for (int guard = 0; guard < 4; ++guard) {
            done = anim2Step(ctx, o, 0x50, 3);
            // without the claw, straight from the raised paw to the blow (0x45b45c)
            if (spriteIs(o, "jmzswipe") && o.frame == 3 && o.l(0x56) == 0) { o.l(0x50) = 0x1c; continue; }
            break;
        }
        if (!done) return kDone;
        if (h.cont() == 0x428dce) { toReady(ctx, o, kWait); next = kWait; return kJump; }
        if (h.invulnerable() != 0) { changeBack(ctx, o); next = kMorph; return kJump; }
        toReady(ctx, o, kReady);
        o.l(0x54) = 0;
        next = kReady;
        return kJump;
    }
    case kMorph:                                   // 0x41eb76: back to the jaguar
        moveX(o, o.l(0x38));
        if (anim2Step(ctx, o, 0x50, 4)) { toRun(ctx, o, 4); return kRestart; }
        if (o.b(0x54) == 0 && o.l(0x50) == 0x13) o.flags = 0x6000;   // (0x45b489)
        return kDone;
    case kCharge: {                                // 0x41ebf4: the same, held in the charge until past Harry
        moveX(o, o.l(0x38));
        bool hold = false;
        if (o.x >= 0x1690 && o.x < 0x1780 && spriteIs(o, "jmzmorf2") && o.frame == 2) {
            int16_t d = int16_t(h.x() - o.x);
            if (o.facing) d = int16_t(-d);
            hold = d < -0x60;
        }
        if (!hold && anim2Step(ctx, o, 0x50, 4)) { toRun(ctx, o, 4); return kRestart; }
        if (o.b(0x54) == 0 && o.l(0x50) == 0x13) o.flags = 0x6000;
        return kDone;
    }
    default:
        return kDone;
    }
}
} // namespace jagMan

void h_jagMan(ObjCtx& ctx, Obj& o) {
    using namespace jagMan;
    Harry& h = ctx.harry;
    for (int pass = 0; pass < 8; ++pass) {         // `jmp 0x41e462`
        if (o.b(0x6a) == 0 && o.frame == 0xff) {   // the first run
            if (h.y() >= 0x163) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
            o.b(0x5b) = 0;
            o.b(0x3c) = uint8_t((ctx.rnd() & 3) + 2);
            o.y += 0x18;
            o.frame = 0xff;
            setState(o, kHidden);
            o.l(0x38) = -0x10000;
            o.behind = 0xff;
            o.b(0x6a) = 0xff;
        }
        // 0x41e4f3: the weapons (DAT_00466782 marks a hit for this tick)
        h.b(0x466782) = 0;
        o.hits = 0;
        if (weaponHits(ctx, o, 0x1e, nullptr)) {
            h.b(0x466782) = 0xff;
            o.w(0x6c) += o.hits;
            if (!ctx.channel0Busy()) {
                if (manForm(o)) ctx.sound0(0x13, 0xe);              // FUN_0042633d(0x13, 0x60, 0xe)
                else if (ctx.rnd() & 1) ctx.sound0(0x2a, 0xe);       // FUN_0042633d(0x2a, 0x60, 0xe)
            }
        }
        const uint16_t hp = uint16_t(uint16_t(o.w(0x6c)) >> 3);
        o.hits = uint8_t(hp);
        if (hp >= 0x3e) {
            ctx.pl.endBoss();                      // DAT_00466507 = 0
            jaguar::bossDeath(ctx, o);
            h_bossDeathBurst(ctx, o);              // 0x415b92 runs straight into the 0x51 handler,
            ctx.pl.strayReturn(0x41e62c);          // so the code after the call runs when the pass ends
            return;
        }
        // 0x41e665: its touch, but while Harry lies dazed or is finished off
        const uint32_t hc = h.cont();
        if (hc != 0x428dce && hc != 0x428cdb && h.invulnerable() == 0) {
            bool touched = false;
            if (spriteIs(o, "jm0swipe") && o.frame == 3) {   // the blow, 0x80 px ahead to 8 behind
                int16_t d = int16_t(h.x() - o.x);
                if (o.facing) d = int16_t(-d);
                if (d < 8 && d >= -0x80) { ctx.pl.hurtHarry(0x1e); touched = true; }   // FUN_00428233
            }
            if (!touched) touched = harryContact(ctx, o, 0x18);
            if (touched) {
                // done for (a signed byte test, as the original's)
                if (int8_t(h.damage() - (ctx.hard ? 0x64 : 0x96)) >= 0) {
                    if (uint32_t(o.l(0x32)) == kSwipe) {
                        h.setUl(0x465b55, ctx.pl.addrOf(&o));
                        ctx.pl.harryCall(0x428d7b);
                        return;
                    }
                    o.b(0x5c) = 0xff;
                    h.setUl(0x465b55, ctx.pl.addrOf(&o));
                    ctx.pl.harryCall(0x428cc5);
                    return;
                }
                if (uint32_t(o.l(0x32)) == kCharge) {
                    h.b(0x465b55) = o.facing;     // shoved the way it charges
                    ctx.pl.harryCall(0x427167);
                }
            }
        }
        if (o.x < 0x1660) o.x = 0x1660;            // the arena
        if (o.x >= 0x17b0) o.x = 0x17b0;
        uint32_t st = uint32_t(o.l(0x32)), next = 0;
        Flow f = kJump;
        for (int hop = 0; hop < 4 && f == kJump; ++hop) { f = body(ctx, o, st, next); st = next; }
        if (f != kRestart) return;
    }
}

// 0x41f24c: type 159, the jaguar man's claw: shown (0x45b514, frames 4..8 of jmzswipe) while the
// swipe holds its raised paw, gone with the blow.
void h_jagManClaw(ObjCtx& ctx, Obj& o) {
    const Obj* boss = ctx.pl.objAt(uint32_t(o.l(0x5a)));
    if (!boss) return;
    if (jagMan::spriteIs(*boss, "jm0swipe")) { ctx.release(o); return; }
    if (!jagMan::spriteIs(*boss, "jmzswipe")) { o.frame = 0xff; return; }
    if (boss->frame != 3) return;
    if (animStep(ctx, o, o.anim(0x50), kAnim_jagManSwipeFx, 5)) ctx.release(o);
}

// ---------------------------------------------------------------- the Warrior Spirit (types 105 / 106 / 176)
//
// Zakelua, the last boss (level 10): a body of 16 parts (type 106) that a pose animator
// (FUN_00422806) moves between the poses of eight tables, 8 steps a pose.  The head (the part
// with a frame below 3, at l64) takes the hits: w6a >> 4, 0x30 kill, three hits within 26 ticks
// and it hits back (b68).  It turns by jumping off the top of the screen, its parts raining
// down and gathering again where it lands; it throws fire (type 0x1d), leaps onto Harry, pounds
// the floor (Harry stunned, Harry's state 0x427316), sucks him in to its hand and throws him
// (0x42c2ff), walks at him and away, and brushes his stones away from its head.  While Harry
// is short of stones (fewer than 10), a pouch of them drops in (type 156 from the template at
// 0x467fe4, its record at [DAT_004504e4] + 0x1ca).  Dead, its parts burst one by one and the
// last letter of PITFALL (type 176) falls; picking it up ends the game.
// State: l46 the continuation address (below), b36 / b37 the pose cursor, b33 the pose shown,
// l3c the first part (each part's l3c the next), l64 the head, w38 the floor y, w3a / b3a the
// walk's start flag, l40 the walking speed, l4a / b4a a vertical speed or a count, w4e the
// screen shake, b51 the opening pause, l54 the pull on Harry (w56 its integer part), b68 "hit
// back", b6d "a pouch is out".  [DAT_004504e4] + 0x1c6: set once it is dead.
namespace spirit {
constexpr uint32_t kIntro = 0x4214d6, kIdle = 0x4214a0, kDecide = 0x42134a, kJumpOff = 0x421b3e, kRain = 0x421e44,
                   kSlam = 0x421ec2, kThrow = 0x42164e, kPound = 0x421800, kSuck = 0x42195b, kPull = 0x421ff9,
                   kPullHit = 0x421fb6, kWalk = 0x422260, kWalkBack = 0x422359, kWalkEnd = 0x422423,
                   kWalkBackEnd = 0x422479;
// The pose tables and scripts by original address.
constexpr uint32_t kPoses = 0x457420, kScripts = 0x460090;
enum Flow { kDone, kJump };                         // return / a jump into another state's code

int16_t rdw(const uint8_t* p) { return int16_t(p[0] | p[1] << 8); }
void setState(Obj& o, uint32_t st) { o.l(0x46) = int32_t(st); }
void start(Obj& o, uint32_t st) { o.w(0x36) = 0x3f00; setState(o, st); }   // the cursor steps at once
Obj* part(ObjCtx& ctx, Obj& o) { return ctx.pl.objAt(uint32_t(o.l(0x3c))); }

int32_t fx(const Obj& o) { return int32_t(uint32_t(uint16_t(o.x)) << 16 | o.xFrac); }
int32_t fy(const Obj& o) { return int32_t(uint32_t(uint16_t(o.y)) << 16 | o.yFrac); }
// A 16.16 value kept as {int word, frac word}, as x / x.frac are.
void putFixed(Obj& o, int off, int32_t v) { o.w(off) = int16_t(uint32_t(v) >> 16); o.w(off + 2) = int16_t(v & 0xffff); }
int32_t getFixed(Obj& o, int off) { return int32_t(uint32_t(uint16_t(o.w(off))) << 16 | uint16_t(o.w(off + 2))); }

// FUN_00422806 (cursor at +0x36): every `ticks` + 1 ticks the next byte of the script: the
// sound opcodes are played, a pose number is shown (b33) and sends every part towards its
// place in the pose (8 steps); any other negative byte ends the script, which starts again
// with its first pose.  True when it ended.
bool pose(ObjCtx& ctx, Obj& o, uint32_t table, uint32_t script, int ticks) {
    const uint8_t* s = kData_spiritScripts + (script - kScripts);
    uint8_t& pos = o.s[0x36];
    uint8_t& tick = o.s[0x37];
    tick = uint8_t(tick + 1);
    if (int8_t(ticks - tick) >= 0) return false;
    pos = uint8_t(pos + 1);
    tick = 0;
    bool ended = false;
    uint8_t b = 0;
    for (int guard = 0; guard < 64; ++guard) {
        b = s[pos];
        if (int8_t(b) >= 0) break;
        if (b == 0xfc) { ctx.sound0(s[pos + 1], s[pos + 2]); pos = uint8_t(pos + 3); tick = 0; continue; }   // FUN_004262d9
        if (b == 0xfd) { ctx.sound1(s[pos + 1], s[pos + 2]); pos = uint8_t(pos + 3); tick = 0; continue; }   // FUN_004261e2
        if (b == 0xfb) { ctx.rnd(); ctx.sound0(s[pos + 1], s[pos + 5]); pos = uint8_t(pos + 6); tick = 0; continue; }   // FUN_0042633d
        pos = 0;
        b = s[0];
        ended = true;
        break;
    }
    o.b(0x33) = b;
    const uint8_t* t = kData_spiritPoses + (table - kPoses);
    const uint8_t* p = t + rdw(t + 2 * b);
    for (Obj* a5 = part(ctx, o); a5; a5 = part(ctx, *a5)) {
        // (with no part at all the original stops with the error "U00151_back_mk")
        if (uint16_t(rdw(p)) == 0x8000) break;
        a5->b(0x46) = 8;
        int32_t d = int32_t(uint32_t(uint16_t(rdw(p))) << 16); p += 2;
        if (o.facing) d = -d;
        const int32_t tx = fx(o) + d;
        putFixed(*a5, 0x48, tx);
        putFixed(*a5, 0x50, (tx - fx(*a5)) >> 3);
        d = int32_t(uint32_t(uint16_t(rdw(p))) << 16); p += 2;
        const int32_t ty = fy(o) + d;
        putFixed(*a5, 0x4c, ty);
        putFixed(*a5, 0x54, (ty - fy(*a5)) >> 3);
        a5->b(0x58) = uint8_t(rdw(p)); p += 2;
    }
    return ended;
}

// FUN_004226e6: the body moves by (d0, d1), d0 the way it faces, inside x 0x48 .. 0x198; the
// parts and their places in the pose with it.
void move(ObjCtx& ctx, Obj& o, int32_t d0, int32_t d1) {
    if (o.facing) d0 = -d0;
    moveX(o, d0);
    if (o.x < 0x47) o.x = 0x48;
    if (o.x >= 0x199) o.x = 0x198;
    moveY(o, d1);
    for (Obj* a5 = part(ctx, o); a5; a5 = part(ctx, *a5)) {
        moveX(*a5, d0);
        putFixed(*a5, 0x48, getFixed(*a5, 0x48) + d0);
        moveY(*a5, d1);
        putFixed(*a5, 0x4c, getFixed(*a5, 0x4c) + d1);
    }
}

// 0x42251d: a stone or the boomerang of Harry's (not the big stone, not one sent back already)
// 0x18 .. 0x70 px under the head and within 10 px of it is batted back.
bool deflect(ObjCtx& ctx, Obj& o) {
    const Obj* head = ctx.pl.objAt(uint32_t(o.l(0x64)));
    if (!head) return false;
    for (uint32_t a = 0x466808; a <= 0x466818; a += 4) {
        Obj* w = ctx.pl.objAt(ctx.harry.ul(a));
        if (!w || w->type == 4 || w->b(0x6a) != 0) continue;
        const int16_t dx = int16_t(head->x - w->x);
        if (dx < -10 || dx >= 10) continue;
        const int16_t dy = int16_t(head->y - w->y);
        if (dy >= -0x18 || dy < -0x70) continue;
        ctx.sound1(0xce, 0xc);
        w->l(0x32) = (w->l(0x32) >> 1) + int32_t(ctx.rnd() & 0xffff) - 0x7fff;
        w->l(0x36) = (w->l(0x36) >> 2) + int32_t(ctx.rnd() & 0x3ffff) - 0x24000;
        w->l(0x32) = -w->l(0x32);
        w->l(0x4a) = 0x6000;
        w->b(0x6a) = 0xff;
        return true;
    }
    return false;
}

// 0x421ae6: a blow: 16 damage and Harry knocked away from it.
void blow(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    ctx.pl.hurtHarry(0x10);
    h.facing() = o.facing;
    h.b(0x465b55) = uint8_t((h.facing() & 1) ^ 1);
    h.facing() = 0;
    ctx.pl.harryCall(0x42715b);
}

// 0x4220f7: Harry sucked towards its hand (0x28 px ahead), faster and faster up to 6 px/tick;
// true once he is there and on the floor (stunned, his state 0x427316).
bool suckHarry(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    if (!ctx.channel1Busy()) ctx.sound1(0xd0, 3);
    const int16_t hand = int16_t(o.x + (o.facing ? 0x28 - 0x50 : 0x28));
    if (o.facing) {
        if (o.w(0x56) < 6) o.l(0x54) += 0x3800;
        ctx.pl.carryHarry(o.l(0x54));
        if (int16_t(hand - h.x()) >= 0) return false;
    } else {
        if (o.w(0x56) >= -6) o.l(0x54) -= 0x3800;
        ctx.pl.carryHarry(o.l(0x54));
        if (int16_t(hand - h.x()) < 0) return false;
    }
    // (0x4221da: tests of DAT_00462695 that change nothing)
    if (!ctx.pl.harryFeetGrounded()) return false;
    if (h.cont() != 0x427316) { h.setCont(0x427316); h.w(0x465b47) = 0x702f; h.w(0x465b45) = 0; }
    return true;
}

// 0x421d88: off the top of the screen, the parts rain down (w6c 0: they bounce about) and the
// count to 0x70 starts.
void rain(ObjCtx& ctx, Obj& o) {
    start(o, kRain);
    o.w(0x4a) = 0;
    ObjectSystem& os = ctx.pl.objectSystem();
    for (Obj* p = os.first(); p; p = os.nextOf(*p)) {
        if (p->type != 0x6a) continue;
        p->b(0x5a) = 0xff;
        p->y = -0x30;
        p->l(0x40) = int32_t(ctx.rnd() & 0x7ffff);
        p->l(0x38) = int32_t(ctx.rnd() & 0x3ffff) - 0x20000;
        p->x = int16_t(p->x + int16_t((ctx.rnd() & 0x7f) - 0x3f));
    }
}

// The code of state `st`; `d1` is what the original's d1 register holds when the decision
// reads it on one path without having set it (see below).
Flow body(ObjCtx& ctx, Obj& o, uint32_t st, uint32_t& next, int32_t d1) {
    Harry& h = ctx.harry;
    auto go = [&](uint32_t s) { next = s; return kJump; };
    switch (st) {
    case kIntro:                                    // 0x4214d6: until Harry comes down
        pose(ctx, o, 0x457420, 0x4600f4, 8);
        if (h.y() >= 0xa2) { start(o, kDecide); return go(kDecide); }
        return kDone;
    case kIdle:                                     // 0x4214a0: Harry is gone
        pose(ctx, o, 0x457420, 0x4600f4, 8);
        return kDone;
    case kDecide: {                                 // 0x42134a
        if (h.b(0x465b72)) { start(o, kIdle); return go(kIdle); }
        o.b(0x3a) = 0;
        auto jumpOff = [&] { start(o, kJumpOff); o.l(0x4a) = int32_t(0xfffb0000); return go(kJumpOff); };   // 0x42152b
        auto pull = [&] { start(o, kPull); h.vx() = 0; o.l(0x54) = 0; return go(kPull); };                    // 0x421f90
        auto suck = [&] { start(o, kSuck); o.l(0x54) = 0; return go(kSuck); };                                // 0x421612
        auto walkBack = [&] { start(o, kWalkBack); return go(kWalkBack); };                                    // 0x421631
        int16_t d0 = int16_t(h.x() - o.x);
        if (d0 < 0) { if (o.facing == 0) return jumpOff(); d0 = int16_t(-d0); }
        else if (o.facing != 0) return jumpOff();   // Harry behind: over him
        if (o.b(0x68) != 0) {                       // hit back: away, or over him when cornered
            o.b(0x68) -= 1;
            if (o.facing ? o.x < 0x169 : o.x >= 0x77) return walkBack();
            return jumpOff();
        }
        if (d0 >= 0xf0) return pull();
        bool walkCheck = false;
        if (d0 >= 0xc0) {
            if ((ctx.rnd() & 3) == 0) return pull();
            walkCheck = true;                       // (with d1 as the original's register has it)
        } else if (d0 < 0x3c) {
            return suck();
        } else {
            d1 = d0;
        }
        for (int guard = 0; guard < 0x400; ++guard) {   // 0x421438
            if (!walkCheck) {
                const uint32_t r = ctx.rnd() & 0x3f;
                if (r < 0x1c) walkCheck = true;
                else if (r < 0x1f) { start(o, kThrow); return go(kThrow); }
                else if (r < 0x24) { start(o, kSlam); return go(kSlam); }
                else if (r < 0x29) { if (int8_t(o.hits - 0x5a) >= 0) return jumpOff(); continue; }   // (the body's own hits: always 0)
                else if (r < 0x2b) { start(o, kPound); o.b(0x4a) = 2; return go(kPound); }
                else if (r < 0x30) return suck();
                else continue;
            }
            walkCheck = false;                      // 0x4215be: at Harry, given room
            if (int16_t(d1) < 0x50) continue;
            if (o.x < 0xe7 && o.facing != 0) continue;
            if (o.x >= 0xf9 && o.facing == 0) continue;
            start(o, kWalk);
            return go(kWalk);
        }
        return kDone;
    }
    case kJumpOff:                                  // 0x421b3e: up off the top of the screen
        if (int8_t(o.b(0x33) - 9) >= 0) {
            o.l(0x4a) -= 0x4800;
            move(ctx, o, 0, o.l(0x4a));
            if (o.y < 0x10) {
                o.x = o.x < 0x100 ? 0xa9 : 0x117;
                rain(ctx, o);
                return go(kRain);
            }
        }
        if (o.b(0x33) == 0xa) return kDone;
        pose(ctx, o, 0x4579fc, 0x46014a, 8);
        return kDone;
    case kRain: {                                   // 0x421e44: 0x70 ticks, then the parts gather on it
        o.w(0x4a) += 1;
        if (o.w(0x4a) != 0x70) return kDone;
        ObjectSystem& os = ctx.pl.objectSystem();
        for (Obj* p = os.first(); p; p = os.nextOf(*p))
            if (p->type == 0x6a) p->b(0x5a) = 0;
        o.y = o.w(0x38);
        o.facing = int16_t(h.x() - o.x) < 0 ? 0xff : 0;
        start(o, kDecide);
        return go(kDecide);
    }
    case kSlam: {                                   // 0x421ec2: a leap that comes down on Harry
        if (o.b(0x33) == 9) {
            o.l(0x4a) += 0x3800;
            move(ctx, o, 0, o.l(0x4a));
            if (o.l(0x4a) < 0) return kDone;
            if (int16_t(o.y - o.w(0x38)) < 0) return kDone;
            o.y = o.w(0x38);
            o.b(0x37) = 0x3f;
            o.w(0x4e) = 0xa;                        // the floor shakes
            if (h.onGround()) blow(ctx, o);
        }
        const bool ended = pose(ctx, o, 0x4579fc, 0x460154, 8);
        if (o.b(0x33) == 9) o.l(0x4a) = int32_t(0xfff80000);
        if (ended) { start(o, kDecide); return go(kDecide); }
        return kDone;
    }
    case kThrow: {                                  // 0x42164e: a fireball (type 0x1d)
        if (pose(ctx, o, 0x45916c, 0x460090, 8)) { start(o, kDecide); return go(kDecide); }
        if (o.w(0x36) == 0x12) {
            ctx.sound0(7, 0xa);                     // FUN_0042633d(7, 0x60, 0xa)
            if (Obj* f = ctx.spawnFrom(o, true, 1)) {   // FUN_0043546e(1)
                f->x += 0x1e; f->y -= 0x30;
                f->behind = 0xff;
                f->sprite = ctx.spr("bosss"); f->frame = 0x19;
                f->b(0x5a) = 0xc; f->type = 0x1d; f->b(0x48) = 0xff;
                f->l(0x32) = 0x80000; f->l(0x36) = 0; f->l(0x3a) = 0; f->l(0x3e) = 0;
                f->flags = 0x4000;
                f->facing = o.facing;
                if (f->facing) { f->l(0x32) = -f->l(0x32); f->x -= 0x3c; }
            }
            return kDone;
        }
        // at the end of the throw, now and then (3 in 8) one more
        if (o.b(0x37) != 0 || o.b(0x36) != 0x17 || o.b(0x68) != 0) return kDone;
        if (int8_t((ctx.rnd() & 7) - 5) < 0) return kDone;
        o.b(0x36) = 0xa; o.b(0x37) = 0x10;
        pose(ctx, o, 0x45916c, 0x460090, 8);
        return kDone;
    }
    case kPound: {                                  // 0x421800: pounds the floor (b4a times): Harry stunned
        for (int guard = 0; guard < 4; ++guard) {
            if (pose(ctx, o, 0x459b94, 0x4600ac, 8)) {
                if (h.cont() == 0x427316) { start(o, kPull); h.vx() = 0; o.l(0x54) = 0; return go(kPull); }
                o.b(0x4a) -= 1;
                if (o.b(0x4a) == 0) { start(o, kDecide); return go(kDecide); }
                return kDone;
            }
            if (o.b(0x37) != 0) return kDone;
            if (o.b(0x36) == 9) {
                ctx.sound0(0xbc, 0xa);              // FUN_004262d9
                o.w(0x4e) = 0xa;
                if (h.onGround()) { h.setCont(0x427316); h.w(0x465b47) = 0x702f; h.w(0x465b45) = 0; }
                return kDone;
            }
            if (o.b(0x36) == 0xf) { o.b(0x36) = 5; o.b(0x37) = 0xa; continue; }
            if (o.b(0x36) != 0xb) return kDone;
            if (o.b(0x68) == 0 && h.cont() != 0x427316 && int8_t((ctx.rnd() & 7) - 5) < 0) return kDone;
            o.b(0x36) = 0xf; o.b(0x37) = 0xa;
            pose(ctx, o, 0x459b94, 0x4600ac, 8);
        }
        return kDone;
    }
    case kSuck: {                                   // 0x42195b: sucks Harry in, lifts him and throws him
        if (pose(ctx, o, 0x458104, 0x4600be, 8)) {
            if (o.b(0x68) != 0) { start(o, kDecide); return go(kDecide); }
            if ((ctx.rnd() & 1) == 0) { start(o, kSlam); return go(kSlam); }
            start(o, kThrow);
            return go(kThrow);
        }
        bool event = false;
        if (int8_t(o.b(0x36) - 0x1c) < 0) {
            if (suckHarry(ctx, o)) {
                if (o.b(0x33) != 4) return kDone;
                o.w(0x36) = 0x20;
                event = true;
            } else if (o.b(0x33) == 4 && h.w(0x466782) != 0) {
                o.w(0x36) = 0x1c;
            }
        } else {
            event = true;
        }
        if (event && ctx.pl.scriptEvent() == 0xd0) { ctx.soundNow(0x2d); ctx.pl.scriptEvent() = 0; }   // the suction's sound stops
        if (int8_t(o.b(0x33) - 6) < 0) return kDone;
        if (int8_t(o.b(0x33) - 0xf) < 0) {          // Harry held in its hand (part 14 at 0x465c64)
            if (h.cont() != 0x42c2ff) { h.setCont(0x42c2ff); h.w(0x465b47) = 0; }
            if (const Obj* hand = ctx.pl.objAt(h.ul(0x465c64))) {
                h.x() = int16_t(hand->x + 0x10);
                h.y() = int16_t(hand->y + 0x30);
                if (hand->facing) h.x() -= 0x20;
            }
            return kDone;
        }
        if (h.cont() != 0x42c2ff) return kDone;
        o.b(0x68) = 1;                              // thrown
        ctx.sound0(0xc7, 0x3e);                     // FUN_004262d9
        blow(ctx, o);
        h.vy() = 0x90000;
        ctx.pl.objectSystem().endPass();            // 0x421ae5: a `ret` ends the object pass
        return kDone;
    }
    case kPullHit:                                  // 0x421fb6: hit while pulling: let go
        if (pose(ctx, o, 0x45899c, 0x4600ea, 8)) { start(o, kDecide); return go(kDecide); }
        return kDone;
    case kPull: {                                   // 0x421ff9: pulls Harry in from afar and strikes
        for (int guard = 0; guard < 4; ++guard) {
            if (pose(ctx, o, 0x45899c, 0x46011b, 8)) { start(o, kDecide); return go(kDecide); }
            if (o.b(0x33) == 0xb && o.b(0x37) == 3) {
                if (h.cont() == 0x427316) h.w(0x465b47) = 0x100;
                blow(ctx, o);
                ctx.sound1(0x5a, 9);
                o.b(0x68) = 1;
            }
            if (o.b(0x33) != 4 && o.b(0x33) != 5) return kDone;
            if (h.b(0x466782) != 0) { start(o, kPullHit); return go(kPullHit); }
            if (!suckHarry(ctx, o)) return kDone;
            if (ctx.pl.scriptEvent() == 0xd0) { ctx.soundNow(0x2d); ctx.pl.scriptEvent() = 0; }
            o.b(0x37) = 0x3f; o.b(0x36) = 0x22;     // on to the strike
        }
        return kDone;
    }
    case kWalk: {                                   // 0x422260: walks at Harry, until within 0x70 px
        if (o.b(0x3a) == 0) {
            if (!pose(ctx, o, 0x457614, 0x4600ee, 8)) return kDone;
            o.b(0x3a) = 0xff;
            o.w(0x36) = 0x3f00;
        }
        move(ctx, o, o.l(0x40), 0);
        if (!pose(ctx, o, 0x457614, 0x4600fd, 8)) return kDone;
        const int16_t d = int16_t(o.x - h.x());
        if (d >= 0x70 || d < -0x70) return kDone;
        start(o, kWalkEnd);
        return go(kWalkEnd);
    }
    case kWalkBack:                                 // 0x422359: steps back
        if (o.b(0x3a) == 0) {
            if (!pose(ctx, o, 0x457614, 0x4600f1, 8)) return kDone;
            o.b(0x3a) = 0xff;
            o.w(0x36) = 0x3f00;
        }
        move(ctx, o, -o.l(0x40), 0);
        if (!pose(ctx, o, 0x457614, 0x46010c, 8)) return kDone;
        start(o, kWalkBackEnd);
        return go(kWalkBackEnd);
    case kWalkEnd:                                  // 0x422423
        if (!pose(ctx, o, 0x457614, 0x4600f1, 8)) return kDone;
        start(o, kDecide);
        return go(kDecide);
    case kWalkBackEnd:                              // 0x422479
        if (!pose(ctx, o, 0x457614, 0x4600ee, 8)) return kDone;
        start(o, kDecide);
        return go(kDecide);
    default:                                        // (0x4217bd, set at 0x42154a, is never reached)
        return kDone;
    }
}

// 0x420bd0 (b34 0): the body, the head, the boss meter.
void setUp(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    ctx.pl.setAlsByte(0x1c6, 0);
    h.b(0x466507) = 0xff;
    o.facing = 0xff;
    ctx.pl.setAlsLong(0x1ca, ctx.pl.addrOf(&o));
    o.w(0x38) = o.y;
    o.b(0x34) = 0xff;
    Obj* prev = &o;
    for (int d7 = 0xf; d7 >= 0; --d7) {
        Obj* p = ctx.pl.objectSystem().allocHead();   // FUN_00435905
        if (!p) break;
        p->w(0x26) = 0x6663;                          // FUN_004263a6
        p->sprite = ctx.spr("bosss");
        p->type = 0x6a;
        p->l(0x64) = int32_t(ctx.pl.addrOf(prev));    // the part before it in the chain
        p->x = o.x;
        p->y = int16_t(o.y - 0x10);
        prev->l(0x3c) = int32_t(ctx.pl.addrOf(p));
        prev = p;
        if (d7 == 1) h.setUl(0x465c64, ctx.pl.addrOf(p));   // the hand that holds Harry
    }
    prev->w(0x36) = 0x3f00;                           // (meant for the boss: written to the last part)
    prev->l(0x46) = int32_t(kIntro);
    o.b(0x51) = 0x60;
    const uint8_t* t = kData_spiritPoses;
    const uint8_t* p = t + rdw(t);                     // pose 0 of 0x457420
    const uint8_t* q = kData_spiritParts;
    for (Obj* a5 = part(ctx, o); a5; a5 = part(ctx, *a5)) {
        if (uint16_t(rdw(p)) == 0x8000) break;
        a5->x = int16_t(o.x - rdw(p)); p += 2;
        a5->y = int16_t(o.y + rdw(p)); p += 2;
        putFixed(*a5, 0x48, fx(*a5));
        putFixed(*a5, 0x4c, fy(*a5));
        a5->frame = uint8_t(rdw(p)); p += 2;
        a5->facing = 1;
        a5->behind = uint8_t(rdw(q)); q += 2;
        a5->flags = uint16_t(rdw(q)); q += 2;
        if (int8_t(a5->frame - 3) < 0) o.l(0x64) = int32_t(ctx.pl.addrOf(a5));   // the head
    }
    // the boss meter (type 0x30): the head's hits while this is a type 0x69 boss
    if (Obj* m = ctx.spawnFrom(o, false, 1)) {
        m->l(0x5a) = o.l(0x64);
        m->l(0x56) = int32_t(ctx.pl.addrOf(&o));
        m->sprite = ctx.spr("scoline"); m->flags = 0xe000; m->type = 0x30;
        Obj** links[3] = {&m->link, &m->link2, &m->link3};
        for (int i = 0; i < 3; ++i) {
            Obj* d = ctx.spawnFrom(o, false, 1);
            if (!d) break;
            d->sprite = ctx.spr(i == 2 ? "mi_prcnt" : "scoline"); d->flags = 0xe000;
            *links[i] = d;
        }
    }
    setState(o, kIntro);
    o.w(0x3a) = 3;
    o.l(0x40) = 0x11c00;
}

// 0x421bd3: dead.
void die(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    ctx.sound0(0x51, 0x56);                           // FUN_0042633d(0x51, 0x60, 0x56)
    ctx.g.stats().score += 25000;
    if (h.spriteIs("hyidazed") || h.cont() == 0x427316 || h.cont() == 0x42c2ff) ctx.pl.harryCall(0x428dfa);
    ctx.g.stats().score += 25000;
    o.w(0x36) = 0x3f00;
    ctx.pl.setAlsByte(0x1c6, 0xff);
    o.w(0x4a) = 0;
    bool odd = false;                                 // the parts fly apart, each to burst a while later
    ObjectSystem& os = ctx.pl.objectSystem();
    for (Obj* p = os.first(); p; p = os.nextOf(*p)) {
        if (p->type != 0x6a) continue;
        p->b(0x5a) = 0xff;
        p->w(0x6c) = int16_t((ctx.rnd() & 0x3f) + 0x20);
        p->l(0x40) = -int32_t((ctx.rnd() & 0x3ffff) + 0x54000);
        p->l(0x38) = int32_t(ctx.rnd() & 0x3ffff);
        odd = !odd;
        if (odd) p->l(0x38) = -p->l(0x38);
    }
    h.b(0x466507) = 0;
    ctx.release(o);
    if (Obj* l = ctx.spawnFrom(o, true, 2)) {         // FUN_0043546e(2): the last letter falls
        l->y = -0x10; l->x = 0xf0;
        l->sprite = ctx.spr("mi_ptfal"); l->frame = 5;
        l->flags = 0x6000;
        l->type = 0xb0;
        return;
    }
    ctx.g.stats().letters = uint8_t(ctx.g.stats().letters + 0x40);   // no room for it: had, and the game ends
    ctx.pl.harryCall(0x430912);
}
} // namespace spirit

void h_warriorSpirit(ObjCtx& ctx, Obj& o) {
    using namespace spirit;
    Harry& h = ctx.harry;
    h.w(0x465c58) = o.x;                              // the camera follows it (0x404a21)
    if (o.b(0x34) == 0) setUp(ctx, o);
    if (o.b(0x51) != 0) { o.b(0x51) -= 1; return; }   // the opening pause
    if (o.w(0x4e) != 0) {                             // the floor shakes
        ctx.pl.setShake(int16_t((ctx.rnd() & 0xf) - 8));
        o.w(0x4e) -= 1;
        if (o.w(0x4e) == 0) ctx.pl.setShake(0);
    }
    // 0x421077: short of stones, a pouch of them drops in (one at a time)
    if (int8_t(ctx.g.stats().ammoCount[0] - 0xa) < 0 && o.b(0x6d) == 0) {
        // FUN_004351c5 with the template 0x467fe4: {type 2, frame 0, flags 0x6000, mi_pouc2, d0 1}
        if (Obj* p = ctx.spawnFrom(o, true, 1)) {
            p->type = 2; p->frame = 0; p->flags = 0x6000; p->sprite = ctx.spr("mi_pouc2");
            o.b(0x6d) = 0xff;
            p->w(0x68) = 8;
            ctx.pl.setAlsLong(0x1ca, ctx.pl.addrOf(p));
            p->b(0x6a) = 0xff;
            p->type = 0x9c;
            p->l(0x32) = 1;                           // falling
            p->x = int16_t(o.x + (o.facing ? 0x20 : -0x20));
            p->y = 0;
            if (p->x < 0x5f) p->x = 0x60;
            if (p->x >= 0x181) p->x = 0x180;
        }
    }
    if (const Obj* p = ctx.pl.objAt(ctx.pl.alsLong(0x1ca)); !p || (p->type != 0x9c && p->type != 2)) o.b(0x6d) = 0;
    // 0x421173: the weapons, on the head
    h.b(0x466782) = 0;
    const uint32_t st0 = uint32_t(o.l(0x46));
    if (st0 != kJumpOff && st0 != kRain) deflect(ctx, o);
    int32_t d1 = 0;                                   // the original's d1, as far as it can be known here
    if (Obj* head = ctx.pl.objAt(uint32_t(o.l(0x64)))) {
        head->b(0x5c) += 1;
        if (head->b(0x5c) == 0x1a) head->w(0x5c) = 0;   // three hits within 26 ticks: it hits back
        // (0x4211c7 compares the head's +0x46, the pose's step count, with two state addresses:
        // never equal)
        head->hits = 0;
        if (weaponHits(ctx, *head, 0x30, nullptr)) {
            h.b(0x466782) = 0xff;
            ctx.sound0(0x51, 0xa);                    // FUN_0042633d(0x51, 0x60, 0xa)
            d1 = 0x60;
            head->b(0x5c) = 0;
            head->b(0x5d) += 1;
            if (head->b(0x5d) == 3) { head->w(0x5c) = 0; h.b(0x466782) = 0x2b; }
            head->w(0x6a) += head->hits;
        }
        const uint16_t hp = uint16_t(uint16_t(head->w(0x6a)) >> 4);
        head->hits = uint8_t(hp);
        if (hp >= 0x30) { die(ctx, o); return; }
    }
    if (h.b(0x466782) == 0x2b) o.b(0x68) = 1;
    // 0x4212b2: Harry cannot walk through it
    const int16_t dy = int16_t(o.y - h.y());
    if (dy >= -8 && dy < 0x90) {
        const int16_t dx = int16_t(o.x - h.x());
        if (dx >= 0) { if (dx < 0x24) h.x() = int16_t(o.x - 0x24); }
        else if (dx >= -0x24) h.x() = int16_t(o.x + 0x24);
    }
    // The decision at 0x4215be reads d1 on its long-range path without setting it: there the
    // register keeps the low byte the weapon test left (0 or 1) or 0x60 after a hit's sound,
    // and whatever came before in its upper bytes, taken as 0 here.
    uint32_t st = uint32_t(o.l(0x46)), next = 0;
    Flow f = kJump;
    for (int hop = 0; hop < 8 && f == kJump; ++hop) { f = body(ctx, o, st, next, d1); st = next; }
}

// 0x422c7a: type 106, a part of the Warrior Spirit's body.  It faces the way the part before
// it does and steps to its place in the pose (8 steps, the frame at the first); scattered
// (b5a), it flies under gravity and bounces off the walls and the floor at y 0xc4, bursting
// into stones when its count w6c runs out (w6c 0: the rain, which goes for Harry).
void h_spiritPart(ObjCtx& ctx, Obj& o) {
    if (const Obj* up = ctx.pl.objAt(uint32_t(o.l(0x64)))) o.facing = up->facing;
    if (o.b(0x5a) == 0) {
        if (o.b(0x46) != 0) {
            o.b(0x46) -= 1;
            moveX(o, spirit::getFixed(o, 0x50));
            moveY(o, spirit::getFixed(o, 0x54));
            if (o.b(0x46) == 7) o.frame = o.b(0x58);
            return;
        }
        o.x = o.w(0x48); o.xFrac = uint16_t(o.w(0x4a));
        o.y = o.w(0x4c); o.yFrac = uint16_t(o.w(0x4e));
        return;
    }
    if (o.w(0x6c) != 0) {
        o.w(0x6c) -= 1;
        if (o.w(0x6c) == 0) {
            ctx.sound0(6, 0x18);                      // FUN_0042633d(6, 0x60, 0x18)
            ctx.spawnEffect(o, ctx.spr("fx_stone"), kAnim_turn7, 3);   // 0x433914 (script 0x4619c7)
            ctx.release(o);
            return;
        }
    } else {
        // (the original compares camY - 0x10 with x: the part goes over Harry when x is that low)
        if (int16_t(int16_t(ctx.harry.w(0x4658b6) - 0x10) - o.x) >= 0)
            o.x = int16_t(ctx.harry.x() + int16_t((ctx.rnd() & 0x7f) - 0x40));
        harryContact(ctx, o, 0);
    }
    if (o.w(0x42) != 0xc) o.l(0x40) += 0x4000;        // gravity up to 12 px/tick
    moveY(o, o.l(0x40));
    moveX(o, o.l(0x38));
    if (o.w(0x6c) == 0) {
        if (o.l(0x38) < 0 ? o.x < 0x47 : o.x >= 0x199) o.l(0x38) = -o.l(0x38);
    }
    if (o.y < 0xc4) return;
    o.l(0x40) = -(o.l(0x40) - 0x40000);
    o.l(0x38) = o.l(0x38) >> 1;
    ctx.sound1(int(ctx.rnd() & 1) + 0x5a, 9);
    o.y = 0xc4;
}

// 0x420abf: type 176, the last letter of PITFALL: 0x60 ticks, then it falls (gravity 0x3000 up
// to 8 px/tick) to the floor at y 0xc5; Harry picks it up (10000 points, the letter) and 0x70
// ticks later the game is won (0x430912).
void h_lastLetter(ObjCtx& ctx, Obj& o) {
    if (o.w(0x5a) != 0x60) { o.w(0x5a) += 1; return; }
    if (o.b(0x3c) != 0) {
        o.w(0x46) += 1;
        if (o.w(0x46) == 0x70) ctx.pl.harryCall(0x430912);
        return;
    }
    if (o.b(0x69) == 0) {
        if (o.w(0x34) != 8) o.l(0x32) += 0x3000;
        moveY(o, o.l(0x32));
        if (o.y >= 0xc5) {
            o.l(0x32) = 0;
            o.y = 0xc5;
            ctx.sound0(0x1e, 4);                      // FUN_0042633d(0x1e, 0x60, 4)
            o.b(0x69) = 0xff;
        }
    }
    if (!ctx.touchesHarry(o)) return;
    o.b(0x3c) = 0xff;
    ctx.g.stats().score += 10000;
    o.frame = 0xff;
    ctx.g.stats().letters = uint8_t(ctx.g.stats().letters + 0x40);
    ctx.sound1(0x24, 0x28);
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
    Obj* b = ctx.spawnFrom(o, true);            // FUN_0043546e(1)
    if (!b) return nullptr;
    b->w(0x26) = 0x6663;                        // (d0 != 0: FUN_004263a6)
    b->sprite = ctx.spr("skzbone"); b->frame = 0;
    b->script = kAnim2_boneTumble; b->l(0x50) = 0;
    b->b(0x5a) = o.b(0x5c); b->type = 0x1d;
    b->flags = o.flags; b->b(0x46) = 0xff;
    return b;
}

// 0x413a70: the READY countdown ran out - pick the next move.
void decide(ObjCtx& ctx, Obj& o) {
    const uint8_t v = o.b(0x50);
    if (v != 2) {
        const int d = harryDx(ctx, o);
        if (!between(d, -0x50, 0x50)) {                               // Harry out of reach
            if (v == 3) { o.b(0x32) = kWalk; o.w(0x34) = 0; walk(ctx, o); return; }
            o.b(0x32) = kMorph; o.w(0x34) = 0; exitDespawnWide(ctx, o); return;
        }
        if (v == 1 || (v == 3 && between(d, -0x30, 0x30))) {         // 0x413b1e: the chop
            startScript(o, kChop, kAnim2_skullChop);
            exitDespawnWide(ctx, o);
            return;
        }
    }
    startScript(o, kThrow, kAnim2_skullThrow);                        // 0x413b3a
    exitDespawnWide(ctx, o);
}

// 0x4132ec: a hit while a sling stone's velocity is on record (DAT_00466788, of which each
// weapon test clears only the low word) knocks a bone loose from where that stone hit
// (DAT_00466782 / 84), unless the skeleton is a skull or dying.
void stoneKnocksBone(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    if (h.l(0x466788) == 0) return;
    if (spriteIs(o, "skiroll") || spriteIs(o, "skimorph") || spriteIs(o, "skijaw") || spriteIs(o, "skidie")) return;
    Obj* b = spawnBone(ctx, o);
    if (!b) return;
    b->b(0x68) = 0xff;
    b->x = h.w(0x466782); b->y = h.w(0x466784);
    const int32_t v = h.l(0x466788);
    moveX(*b, v);
    b->l(0x32) = v >> 1;
    b->l(0x3a) = 0; b->l(0x3e) = 0x6e00;
    b->l(0x36) = int32_t(ctx.rnd() & 0x1ffff) - 0x40000;
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
    ctx.sound0(0x42, 0xa);                                                // FUN_004262d9
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
    if (ctx.frozen()) {                                                     // 0x413065: stop-watch
        // (d5 is not loaded here: the weapon test runs with whatever value the code before
        // left in it.  0 stands in for it, so any hit kills.)
        if (!weaponHit(ctx, o, 0).z) {
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
        const WeaponHit r = weaponHit(ctx, o, o.b(0x5a));
        if (o.b(0x50) == 1) {
            if (r.d1) {
                stoneKnocksBone(ctx, o);
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
        } else {
            if (r.d1) stoneKnocksBone(ctx, o);
            if (r.d0) {                                                     // 0x4134d8
                o.b(0x32) = kDying; o.w(0x34) = 0;
                ctx.sound1(0x42, 0xa);
                dying(ctx, o);
                return;
            }
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
    if (!ctx.frozen()) animStep(ctx, o, o.anim(0x32), kAnim_ballDrop, 4);
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
        if (Obj* c = ctx.spawnFrom(o, true)) {                           // FUN_0043546e(2)
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
    if (!ctx.frozen()) anim2Step(ctx, o, 0x32, 2);
    if (o.frame != 0xff) {
        static const uint8_t kChainFrame[12] = {0xff, 0xff, 0xff, 0, 1, 2, 3, 4, 5, 6, 7, 8};   // 0x45b08a
        static const int16_t kSwing[12][2] = {                                                  // 0x45b05a
            {60, -8}, {63, -8}, {68, -5}, {70, -4}, {70, -2}, {67, 4}, {64, 7}, {58, 10}, {49, 15}, {38, 18}, {25, 21}, {13, 22}};
        const int i = std::min<int>(o.frame, 11);
        if (o.link) { o.link->frame = kChainFrame[i]; o.link->facing = o.facing; }   // l4a
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
    if (beyondWide(ctx, o)) {                                            // 0x425051: both go, the ball first
        ctx.pl.restoreEntry(o);
        ctx.release(o);
        if (o.link) ctx.release(*o.link);
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
    if (o.link) o.link->frame = o.frame;                                 // l50: the twin
    if (done) {
        ctx.release(o);
        if (Obj* t = o.link) {
            // The twin's l6a is the display entry it had when it last ran (0x41562c) - in the
            // last pass, as it runs after the ripple - and that entry of this pass is cancelled.
            ctx.pl.objectSystem().cancelDisplayAt(uint32_t(t->l(0x6a)));
            ctx.release(*t);
        }
    }
}

// 0x41562c: type 0x4f, the ripple's mirrored twin: it only notes its display entry.
void h_rippleTwin(ObjCtx& ctx, Obj& o) {
    o.l(0x6a) = int32_t(ctx.pl.objectSystem().currentDisplayAddr());     // DAT_00465b8e
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
    o.facing = uint8_t(o.x & 1);                    // the parity of the new x
    o.b(0x33) = uint8_t((ctx.rnd() & 0x3f) + 0x30);
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- the rolling wheels (types 67 / 148 / 149)

// Five sprites make a wheel: the object itself and four children (obrwhee1 flipped three ways
// and obrwhee2) that follow it and take their frames from the 0x45bbba table by the roll
// angle (l54 in 1/0xc0000 turns per row, w32 = row * 5).
namespace wheel {

// 0x4146f1: stop the rumble (if it is still the last channel-1 sound) and free the children
// (l3c, l40, l44, l48 up to the first missing one) and the wheel.
void teardown(ObjCtx& ctx, Obj& o) {
    if (ctx.pl.scriptEvent() == 0xd1) { ctx.soundNow(0x89); ctx.pl.scriptEvent() = 0; }
    for (Obj* p : o.parts) { if (!p) break; ctx.release(*p); }
    o.parts.clear();
    ctx.release(o);
}

// 0x414356: first tick (frame 0xff): behind Harry, roll angle 4.0 and the four children.
// 1 = set up now, 0 = already running, -1 = the pool ran out (everything is released).
int init(ObjCtx& ctx, Obj& o) {
    if (o.frame != 0xff) return 0;
    o.behind = 0xff;
    o.l(0x54) = 0x40000;
    o.parts.clear();
    for (int i = 0; i < 4; ++i) {                                        // FUN_0043546e(4) x 4
        Obj* c = ctx.spawnFrom(o, true);
        if (!c) { teardown(ctx, o); return -1; }                         // 0x414482 -> 0x4146f1
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
    if (!ctx.channel1Busy()) ctx.sound1(0xd1, 3);                        // the rumble, whenever channel 1 is free (FUN_004261d1)
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
        if (!c) continue;
        c->frame = kWheelFrames[row + 1 + int(i)];
        c->x = o.x; c->y = o.y; c->flags = o.flags;
    }
    const int16_t dy = int16_t(o.y - ctx.camY);                          // 0x424e73
    if (dy < -0xa0 || dy >= 0x17c) {
        ctx.pl.restoreEntry(o);
        teardown(ctx, o);
    }
}

// The bytes the pit wheel's slope test (0x41db24) reads after FUN_00425d6f: [a0 + (d3 & 0xfff0)]
// and 15 bytes on.  On a hit that is the cell's row of the height table; on a miss d3 / a0 are
// what the probe left: the det header (an empty column) or the entry the column scan stopped
// at, read as if it were a shape row.  (Below row 0x12 they are the handler's own registers:
// no test then.)
bool slopeBytes(const ObjCtx& ctx, int x, int y, int& left, int& right) {
    const ph::FloorHit f = ph::probeNear(ctx.det(), x, y);
    if (f.hit) { left = ph::kCollisionHeights[f.shape * 16]; right = ph::kCollisionHeights[f.shape * 16 + 15]; return true; }
    if (int16_t(y) < 0x12) return false;
    // (the .det's bytes in the load block, past the table too; its column word added signed)
    const ph::ColumnTable& t = ctx.lv.collision;
    const std::vector<uint8_t>& det = ctx.lv.det;
    auto at = [&](int64_t i) {
        if (t.arena()) return int(t.arena()->at(int64_t(t.base()) + i));
        return i >= 0 && size_t(i) < det.size() ? int(det[size_t(i)]) : 0;
    };
    auto inside = [&](int64_t i) { return t.arena() ? t.arena()->contains(int64_t(t.base()) + i) : i < int64_t(det.size()); };
    const int64_t col = uint16_t(((uint16_t(x) >> 3) & 0xfffe) + 4);
    const uint16_t off = uint16_t(at(col) << 8 | at(col + 1));
    int64_t a0 = 0, d3 = 0;
    if (off != 0) {
        const int row = (uint16_t(y) >> 4);
        a0 = int16_t(off);
        for (;;) {
            d3 = at(a0);
            if (int(d3) == row + 1 || int(d3) == row || int(d3) == row - 1) break;   // (a hit: not here)
            if (int16_t((row - 1) - int(d3)) < 0) break;                             // the stop entry
            a0 += 2;
            if (!inside(a0)) break;
        }
    }
    d3 &= 0xfff0;
    left = at(a0 + d3); right = at(a0 + d3 + 15);
    return true;
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
    const int py = o.y + 0x3c;
    int left = 0, right = 0;
    const bool slopeRead = wheel::slopeBytes(ctx, o.x, py, left, right);
    { int y = py; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0x3c); }    // FUN_00425d6f
    if (slopeRead) {
        const int16_t slope = int16_t(left - right);
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
            if (Obj* w = ctx.spawnFrom(o, true)) {                       // FUN_004351c5
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

// A spear (obzspear) that thrusts out of the wall after a wait of 0xf..0x4e ticks; frame 4
// of the thrust stabs Harry for 15.
void h_spear(ObjCtx& ctx, Obj& o) {
    auto wait = [&] { o.w(0x32) = int16_t((ctx.rnd() & 0x3f) + 0x10); o.w(0x3c) = 0; };   // 0x40d32f
    if (o.w(0x32) == 0) wait();
    o.w(0x32) -= 1;
    if (o.w(0x32) == 0) {
        o.w(0x32) = 1;
        if (animStep(ctx, o, o.anim(0x3c), kAnim_spearThrust, 4)) {      // thrust over: back to 0x40d32f
            wait();
            o.w(0x32) -= 1;
        } else if (o.frame == 4) {
            harryContactNow(ctx, o, 0xf);                                // 0x407b0f
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
        Obj* t = ctx.spawnFrom(o, true);                                 // FUN_0043546e(2)
        if (!t) { ctx.pl.restoreEntry(o); ctx.release(o); return; }
        t->w(0x26) = 0x6663;                                             // (d0 != 0: FUN_004263a6)
        t->sprite = ctx.spr("fxifirer"); t->flags = 0x2000;
        o.link = t;                                                      // l50
    }
    if (!ctx.frozen()) {
        o.l(0x5a) -= 0x8800;
        if (o.l(0x5a) < 0) {
            o.l(0x5a) += 0x10000;
            o.w(0x36) += 1;
            if (o.w(0x36) == 15 && !ctx.channel0Busy()) ctx.sound0(8, 5); // 0x45ae48: the roar (FUN_0042633d(8, 0x60))
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
    if (o.link) {
        Obj& t = *o.link;
        t.frame = uint8_t(key(i, 3) - 1);                                // (0x8004 entries read as frame 3)
        t.x = int16_t(o.w(0x46) + key(i, 4));
        tailY = o.w(0x48) + key(i, 5) - 0x3a;
        t.y = int16_t(tailY);
    }
    o.behind = 0;
    const bool secondSheet = o.sprite && o.sprite->name() == "fx0firep";
    if (!secondSheet && between(tailY, 0xd, 0x11)) o.behind = 0xff;    // (sic: compares the tail's absolute y)
    if (!ctx.frozen() && !secondSheet && int8_t(o.frame - 0xa) >= 0) harryContact(ctx, o, 0x28);
    const int dx = int16_t(o.x - ctx.camX), dy = int16_t(o.y - ctx.camY);   // 0x40f584: its own window
    if (dx < -0xc8 || dx >= 0x208 || dy < -0xa0 || dy >= 0x17c) {
        ctx.pl.restoreEntry(o); ctx.release(o);
        if (o.link) ctx.release(*o.link);
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
    if (o.b(0x4b) == 0 && !weaponHit(ctx, o, 0).z) {
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
            if (Obj* s = spawnFromTemplate(ctx, o, 0x74)) {              // FUN_004351c5, template 0x468314
                o.link = s;                                              // (+0x50)
                o.w(0x5a) = int16_t((ctx.rnd() & 0x7f) + 0xd0);
            }
        }
    } else if (o.link->type != 0x6d) {                                   // (a freed record's type is 0)
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
    // 0x40ec3d: on Harry (0x407b0f) or a wall marker it splats (the cursor from tick 1), and the
    // handler starts again at once (the 0x40ec5f facing test changes nothing)
    if (harryContactNow(ctx, o, 0xc) || (ctx.triggerClass(o.x - 0x10, o.y) & 1)) {
        o.w(0x46) = 0x100;
        h_spit(ctx, o);
        return;
    }
    exitDespawnFar(ctx, o);
}

// 0x407bfc: a snake out of its hole (type 12, placed as snix_dot): it rears up when Harry
// comes within 0xc0 x 0xa0, then slithers back and forth within 32 px of home (w58) at
// 1.25 px/tick, turning at edges, wall markers and the leash.  Every 0x10..0x2f ticks it
// decides to lunge when Harry is ahead of it (b38 = 0xfb), but in the PC build the lunge
// and spit code at 0x407db0 can never be reached (both branches of 0x407da1 skip it), so
// it never does.  10 hp, 10 contact damage.
void h_roamingSnake(ObjCtx& ctx, Obj& o) {
    priorityTrigger(ctx, o, o.facing ? -0x20 : 0);
    if (o.b(0x32) == 0) {
        o.w(0x58) = o.x;
        o.w(0x4c) = int16_t((ctx.rnd() & 0x1f) + 0x10);
        o.l(0x54) = -0x14000;
        o.facing = 0; o.b(0x32) = 0xff;
    }
    bool killed = false;
    weaponHits(ctx, o, 10, &killed);
    if (killed) { killObject(ctx, o); return; }
    if (ctx.frozen()) {
        if (o.frame == 0xff) return;                                     // (an unseen frozen snake just waits)
        freezeSparkle(ctx, o);
        return;
    }
    Harry& h = ctx.harry;
    if (!o.sprite || o.sprite->name() != "snimiss") harryContact(ctx, o, 10);
    if (o.b(0x33) == 0) {                                                // still a dot: rear up when Harry nears
        if (o.w(0x34) == 0) {
            const int16_t dx = int16_t(o.x - h.x()), dy = int16_t(h.y() - o.y);
            if (int16_t(dx + 0xc0) < 0 || int16_t(dx - 0xc0) >= 0 || int16_t(dy + 0xa0) < 0 || int16_t(dy - 0xa0) >= 0) {
                exitDespawnFar(ctx, o);
                return;
            }
        }
        o.sprite = ctx.spr("snix_dot");
        if (animStep(ctx, o, o.anim(0x34), kAnim_turn7, 5)) {
            o.sprite = ctx.spr("snislthr"); o.b(0x33) = 0xff; o.frame = 0;
            o.b(0x34) = 2; o.facing = 1;
        }
        exitDespawnFar(ctx, o);
        return;
    }
    if (o.b(0x40)) {                                                     // 0x407ec4: turning round
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
        if (int8_t(o.frame - 6) >= 0) o.frame = 0;
        o.frame += 1;
        if (o.frame == 6) o.frame = 0;
    }
    moveX(o, o.l(0x54));
    o.w(0x4c) -= 1;
    if (o.w(0x4c) < 0) {
        o.w(0x4c) = int16_t((ctx.rnd() & 0x1f) + 0x10);
        const bool ahead = int16_t(h.x() - o.x) >= 0 ? o.l(0x54) >= 0 : o.l(0x54) < 0;
        if (ahead) { o.b(0x38) = 0xfb; o.w(0x3c) = 0; }
    }
    { int y = o.y + 0xe; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0xe); }
    auto turn = [&] {                                                    // 0x408174
        o.b(0x40) = 0xff; moveX(o, -o.l(0x54)); o.w(0x42) = 0;
        o.sprite = ctx.spr("sniturn"); o.frame = 0;
        exitDespawnWide(ctx, o);
    };
    const int16_t leash = int16_t(o.w(0x58) - o.x);
    if (o.facing ? int16_t(leash - 0x20) >= 0 : int16_t(leash + 0x20) < 0) { turn(); return; }
    const int lv = ctx.levelNo;
    if (lv == 3 || lv == 8 || lv == 1 || lv == 6) {                     // these levels also look for the floor's edge
        int y = o.y + 0xe;
        if (!ctx.floorSnap(o.x + (o.facing ? -0x28 : 0x28), y)) { turn(); return; }
    }
    o.b(0x48) -= 1;                                                      // 0x4080ca
    if (int8_t(o.b(0x48)) < 0) {
        o.b(0x48) = 5;
        int y = o.y + 0xb;
        if (!ctx.floorSnap(o.x + 0x20 - (o.facing ? 0x58 : 0), y)) { turn(); return; }
    }
    if (o.b(0x48) == 3 && (ctx.triggerClass(o.x + (o.facing ? -0x10 : 0xc), o.y - 0x10) & 1)) { turn(); return; }
    exitDespawnWide(ctx, o);
}

// ---------------------------------------------------------------- the leaping crocodile (types 84 / 0x65)

// 0x4188e0: type 0x65, the splash of the leaping crocodile, with its mirrored twin (type 0x4f,
// l50): hidden for w64 ticks, then the ring (0x45bd02) at 4 ticks; at its end both go, the
// twin's display entry of this pass cancelled as for the ripples.
void h_crocSplash(ObjCtx& ctx, Obj& o) {
    Obj* t = ctx.pl.objAt(uint32_t(o.l(0x50)));
    if (o.w(0x64)) {
        o.w(0x64) -= 1;
        o.frame = 0xff;
        if (t) t->frame = 0xff;
        return;
    }
    const bool done = animStep(ctx, o, o.anim(0x32), kAnim_splashRing2, 4);
    if (t) t->frame = o.frame;
    if (!done) return;
    ctx.release(o);
    if (t) {
        ctx.pl.objectSystem().cancelDisplayAt(uint32_t(t->l(0x6a)));
        ctx.release(*t);
    }
}

// 0x4160b2: type 84, a crocodile that leaps out of the water (crzjump; Yaxchilan Lagoon, Jaina
// Island Falls).  It waits under water 8..23 ticks (w3c), then comes up somewhere near Harry
// inside the level's stretch of water (0x98e..0xa9f, 0x3f2..0x5fa in level 18) - right under
// him while he swings on a vine (state 0x42bda7) - with a splash (type 0x65 and its twin), leaps
// at -8.25 px a tick (gravity 0x5300 while it rises fast, 0x3000 after) drifting left, and goes
// back under where it started (w3e / w3a).  At the top of a leap it snatches Harry off his vine
// (state 0x42939b; the vine in his hands, DAT_00465b53, is freed).  Its second record (+0x5a)
// shows the jaws (0x45bc66, cursor +0x5e) until their frame 6.
void h_leapingCroc(ObjCtx& ctx, Obj& o) {
    halfMinuteTick(o);                                            // 0x4057b0
    Harry& h = ctx.harry;
    bool arm = false;
    if (o.b(0x50) == 0) {
        o.behind = 0xff; o.w(0x3e) = o.x;
        o.y += 0x23; o.w(0x3a) = o.y;
        Obj* a5 = ctx.spawnFrom(o, true, 2);                       // FUN_0043546e(2)
        if (!a5) return;
        o.b(0x50) = 0xff;
        o.l(0x5a) = int32_t(ctx.pl.addrOf(a5));
        a5->sprite = o.sprite; a5->flags = o.flags;
        arm = true;
    }
    Obj* body = ctx.pl.objAt(uint32_t(o.l(0x5a)));
    for (;;) {
        if (arm) { o.w(0x3c) = int16_t((ctx.rnd() & 0xf) + 8); arm = false; }   // 0x416127
        if (o.w(0x3c) != 0) {                                     // under water
            if (h.cont() == 0x427528) break;                      // (not while Harry drowns)
            o.w(0x3c) -= 1;
            if (o.w(0x3c) != 0) break;
            // 0x416178: up it comes
            Obj* a3 = ctx.spawnFrom(o);                           // FUN_004356bc(1)
            if (!a3) { o.w(0x3c) = 0xa; break; }
            a3->type = 0x65; a3->w(0x64) = 6; a3->behind = 0xff;
            a3->sprite = ctx.spr("fxisplsh"); a3->flags = 0xa000;
            a3->x = o.x; a3->y = int16_t(o.y - 0x54);
            Obj* a4 = ctx.spawnFrom(o, false, 9);                 // FUN_004356bc(9)
            if (!a4) { ctx.release(*a3); o.w(0x3c) = 0xa; break; }
            a3->l(0x50) = int32_t(ctx.pl.addrOf(a4)); a4->l(0x50) = int32_t(ctx.pl.addrOf(a3));
            a4->type = 0x4f;
            std::memcpy(a3->s + 0x12, a4->s + 0x12, 2);
            a3->w(0x26) = 0x6663;
            a4->behind = 0xff; a4->sprite = ctx.spr("fxisplsh"); a4->flags = 0xa000;
            a4->x = o.x; a4->y = int16_t(o.y - 0x54); a4->facing = 0xff;
            o.w(0x38) = 0; o.w(0x40) = 6; o.w(0x4a) = 0x32; o.l(0x46) = int32_t(0xfff7c000);
            o.frame = 0; o.w(0x5e) = 0;
            if (body) body->frame = 0xff;
            const bool l18 = ctx.levelNo == 0x12;
            const int16_t lo = l18 ? 0x3f2 : 0x98e, hi = l18 ? 0x5fa : 0xa9f;
            int16_t x = int16_t(int(ctx.rnd() & 0x7f) - 0x40 + h.x());
            if (int16_t(x - lo) < 0) x = int16_t((ctx.rnd() & 0x7f) + lo);
            else if (int16_t(x - hi) >= 0) x = int16_t(hi - int(ctx.rnd() & 0x7f));
            o.x = x;
            if (h.cont() == 0x42bda7 && int16_t(h.x() - lo) >= 0 && int16_t(h.x() - hi) < 0) o.x = int16_t(h.x() + 0x10);
            a4->x = int16_t(o.x - 2); a3->x = int16_t(o.x - 2);
            break;
        }
        // 0x416446: in the air
        if (h.cont() == 0x42bda7 && o.w(0x48) == 0 && ctx.touchesHarry(o)) {
            if (Obj* v = ctx.pl.objAt(h.ul(0x465b53))) ctx.release(*v);   // the vine in his hands
            h.setCont(0x42939b);
            h.w(0x465b47) = 0;
            h.setUl(0x465b4b, ctx.pl.addrOf(&o));
            h.vy() = 0;
            h.y() += 0x26;
        }
        moveX(o, -0x3000);
        if (body) {
            body->x = o.x; body->y = o.y;
            if (body->frame != 6) animStep(ctx, *body, o.anim(0x5e), kAnim_crocLeap, 4);   // 0x45bc66
        }
        moveY(o, o.l(0x46));
        o.l(0x46) += int16_t(o.w(0x48) + 1) >= 0 ? 0x3000 : 0x5300;
        if (int16_t(o.y - o.w(0x3a)) >= 0) { o.y = o.w(0x3a); o.x = o.w(0x3e); arm = true; continue; }
        break;
    }
    if (beyondWide(ctx, o)) {                                     // 0x425051
        ctx.pl.restoreEntry(o);
        ctx.release(o);
        if (body) ctx.release(*body);
    }
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
    if (o.frame == 0 && ctx.frozen()) { exitDespawnFar(ctx, o); return; }
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
    if (int8_t(o.frame - 4) < 0) {
        Obj* p = o.link;                                                 // l50: the priest
        if (!p || p->type != 45 || p->b(0x32) != 0) { o.w(0x32) = 0x100; return; }
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
    if (!weaponHit(ctx, o, 0).z) { die(); return; }
    if (harryContactNow(ctx, o, 0x11)) o.w(0x32) = 0x100;               // 0x407b0f
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
        Obj* f = o.link2;                                                // l64
        if (f && f->type == 0x2c) { exitDespawnFar(ctx, o); return; }   // its fireball still burns
        o.link2 = nullptr;                                               // (l64 = Harry's record, whose type is 1 or 0xff)
    }
    anim2Step(ctx, o, kAnim2_priestMain, 0x36, 4);
    if (o.b(0x3a) == 0 && o.l(0x36) == 4) {                              // 0x45a833: the fireball appears
        if (Obj* f = ctx.spawn()) {                                      // FUN_004356bc(3)
            f->w(0x26) = 0x6663;                                         // (d0 != 0: FUN_004263a6)
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
    if (harryContactNow(ctx, o, 0xf)) {                                  // 0x407b0f
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
    // 0x412079: stay on the floor; where the ledge ends ahead it turns round (true).
    auto ledgeEnds = [&]() -> bool {
        int y = o.y + 0xe; ctx.floorSnap(o.x, y); o.y = int16_t(y - 0xe);
        int y2 = o.y + 0xe;
        if (ctx.floorSnap(o.x - (o.facing ? 0x20 : 0), y2)) return false;
        o.w(0x38) = int16_t((ctx.rnd() & 0x1f) + 0x30);
        return true;
    };
    for (int pass = 0; pass < 4; ++pass) {                               // (a turn re-enters at the top)
        if (o.b(0x32) == 0) {                                            // still in the wall
            o.w(0x64) = 1; o.w(0x66) = 4;
            o.facing = uint8_t((uint16_t(o.x) >> 4) & 1);
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
        if (!(spriteIs("tgrpeel") && int8_t(o.frame - 7) < 0)) harryContact(ctx, o, 0xd);
        bool killed = false;
        weaponHits(ctx, o, 0x1e, &killed);
        if (killed) { killObject(ctx, o); return; }
        if (o.b(0x33)) { o.b(0x33) -= 1; return; }                       // pausing (no exit test)
        bool turn = false;
        if (o.w(0x46)) {                                                 // shooting
            const bool low = o.l(0x48) == 1;
            o.sprite = ctx.spr(low ? "tgrlarro" : "tgrharro");
            const bool done = animStep(ctx, o, o.anim(0x46), low ? kAnim_guardShotLow : kAnim_guardShotHigh, 6);
            if (o.w(0x46) == (low ? 0xc : 0xb)) {                        // the arrow leaves the bow
                Obj* a = ctx.spawnFrom(o, true);                         // FUN_0043546e(1)
                if (!a) {                                                // (out of records: on to 0x412079)
                    if (!ledgeEnds()) { exitDespawnMid(ctx, o); return; }
                    turn = true;
                } else {
                    a->w(0x26) = 0x6663;
                    a->type = 0x39; a->frame = 1;
                    a->sprite = ctx.spr("tgfarrow");
                    a->l(0x32) = 0x62000;
                    a->x = o.x; a->xFrac = o.xFrac; a->y = o.y; a->yFrac = o.yFrac;
                    a->x += 0x10; a->y -= 0x29;
                    if (low) a->y += 0x14;
                    if (o.facing) { a->facing ^= 1; a->l(0x32) = -a->l(0x32); a->x -= 0x20; }
                    exitDespawnMid(ctx, o);
                    return;
                }
            } else {
                if (!done) { exitDespawnMid(ctx, o); return; }
                o.w(0x3c) = 0;
            }
        }
        if (!turn && o.w(0x3a)) {                                        // turning (tgrturn)
            o.sprite = ctx.spr("tgrturn");
            const bool done = animStep(ctx, o, o.anim(0x3a), kAnim_guardTurn, 4);
            if (o.w(0x3a) == 3) o.facing ^= 1;
            if (!done) { exitDespawnMid(ctx, o); return; }
        }
        if (!turn && o.w(0x34)) {                                        // peeling out of the wall
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
        if (!turn) {                                                     // walking (0x411e99)
            o.w(0x38) -= 1;
            if (o.w(0x38) < 0) {
                o.w(0x38) = int16_t((ctx.rnd() & 0x1f) + 0x30);
                turn = ctx.harry.x() < o.x ? o.facing == 0 : o.facing != 0;
            }
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
            if (!ledgeEnds()) { exitDespawnMid(ctx, o); return; }
        }
        o.l(0x3e) = -o.l(0x3e);                                          // 0x411ef7: turn round, from the top
        o.w(0x3a) = 1;
        o.w(0x3c) = 0;
    }
    exitDespawnMid(ctx, o);
}

// ---------------------------------------------------------------- vortex (type 71, 0x414c58)

// The swirl that takes Harry to a vault, or on level 3 to the 2600 level.  It stays shut once
// its vault has been seen (DAT_0046a884 / 86 / 88 hold the x of the vortices taken) and, but on
// level 3, from the moment Harry goes through it (DAT_00465afb); otherwise Harry standing in
// its box (0x45bbce) is caught: he spins away in Harry's state 0x428780 (the script 0x428a43).
void h_vortex(ObjCtx& ctx, Obj& o) {
    animStep(ctx, o, o.anim(0x3c), kAnim_marker, 4);           // 0x4619cf
    const Game::Warp& wp = ctx.g.warp();
    Harry& h = ctx.harry;
    if (o.x == wp.used[0] || o.x == wp.used[1] || o.x == wp.used[2] ||
        (ctx.levelNo != 3 && h.w(0x465afb) == o.x)) {
        o.frame = 0xff;
        return;
    }
    const uint32_t cont = h.cont();
    if (cont != 0x430e3a && cont != 0x428780 && h.onGround() && cont != 0x42fadf &&
        harryOnBox(ctx, o, -20, 20, -80, 80)) {
        o.b(0x32) = 0xff;
        h.l(0x465b47) = 0x9000;
        h.setUl(0x465b4b, 0x428a43);                            // Harry's script and its tick
        h.w(0x465afb) = o.x;
        h.w(0x465b4f) = 0;
        h.x() = o.x;
        h.y() = int16_t(o.y + 0x25);
        h.onGround() = 0;
        h.setCont(0x428780);
        ctx.sound1(0xa2, 0x12);
        h.setUl(0x465b43, ctx.pl.addrOf(&o));                   // the vortex: its trigger says where to
    }
    exitDespawnFar(ctx, o);
}

// ---------------------------------------------------------------- the continue screen

// 0x40b880: type 24, the letters P-I-T-F-A-L-L the continue screen shows: its handler jumps
// straight to the pass's next object (what follows the jump, a level-10 HUD placement, is dead).
void h_inert(ObjCtx&, Obj&) {}

// 0x41e250: type 157, the continue screen's sign (mi_sign).  The camera stays at y 0x2a.  At its
// first run it writes "continues N" in ltr_sm letters (type 108 at (0x50, 0xbe); N the
// continues, 9 at most) and puts up the P-I-T-F-A-L-L letters found (type 24, mi_ptfal, frames
// 0x4619d8, at y 0xe2 from x 0x92, 0x1d apart; a letter not found is not shown).  Its frame
// follows Harry's: 1 at his frame 0xd, 0 at 0x13.  While the match burns it rotates five words
// of the Genesis palette (0x46266a .. 0x462672) every sixth tick, which the PC does not show.
void h_continueSign(ObjCtx& ctx, Obj& o) {
    Harry& h = ctx.harry;
    o.flags = 0x4000;
    h.w(0x4658b6) = 0x2a;
    if (o.b(0x5a) == 0) {
        o.b(0x5a) = 0xff;
        char text[] = "continues  ";                               // 0x45bfd2, copied to 0x4661b8
        const int c = std::min(ctx.g.stats().continues & 0xff, 9);
        text[10] = char('0' + c);
        ctx.pl.spawnText(text, 0x50, 0xbe, 0x6c);                    // FUN_00434a30
        static const uint8_t kFrames[] = {0, 1, 2, 3, 4, 5, 5};     // 0x4619d8
        int16_t x = 0x92;
        uint8_t bit = 1;
        for (uint8_t f : kFrames) {
            Obj* a5 = ctx.spawnFrom(o, true, 2);                     // FUN_0043546e
            if (!a5) break;
            a5->x = x;
            x = int16_t(x + 0x1d);
            a5->type = 0x18;
            a5->y = 0xe2;
            a5->frame = (ctx.g.stats().letters & bit) ? f : 0xff;   // DAT_00465896
            bit = uint8_t(bit << 1);
            a5->setSprite(ctx.spr("mi_ptfal"));
        }
    }
    if (h.frame() == 0xd) { o.frame = 1; o.setSprite(ctx.spr("mi_sign")); }
    if (h.frame() == 0x13) { o.setSprite(ctx.spr("mi_sign")); o.frame = 0; }
    if (h.cont() != 0x4299ad) return;
    o.behind = 0xff;
    o.b(0x32) -= 1;
    if (int8_t(o.b(0x32)) >= 0) return;
    o.b(0x32) = 5;
    h.b(0x46654f) = 0xff;
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
    case 0x41: return h_dust;
    case 4:    return h_bigStone;
    case 5:    return h_slingStone;
    case 0x89: return h_stonePiece;
    case 88:   return h_limpSnake;
    case 84:   return h_leapingCroc;
    case 0x65: return h_crocSplash;
    case 41:   return h_hollow;
    case 92:   return h_pushLever;
    case 98:   return h_cartSpawner;
    case 0x63: return h_runawayCart;
    case 15:   return h_mineCart;
    case 0x25: return h_cartSpark;
    case 19:   return h_pushCart;
    case 14:   return h_rope;
    case 0xa9: return h_ropeSpawner;
    case 0x6f: return h_floatPlatform;
    case 0x7a: return h_harrySplash;
    case 115:  return h_rideDirector;
    case 0x7e: return h_rideLaneLight;
    case 7:    return h_rideWarning;
    case 0x7f: return h_rideCart;
    case 0x79: return h_rideMarker;
    case 0x80: return h_rideVapor;
    case 0x97: return h_hudText;
    case 0x9b: return h_hudGo;
    case 6:    return h_boomerang;
    case 0x87: return h_boomerangBack;
    case 0x8a: return h_stoneSpark;
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
    case 132:  return h_scorpion;                  // (both types run 0x41baa0)
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
    case 0x4f: return h_rippleTwin;
    case 66:   return h_skullWarrior;
    case 68:   return h_swingBall;
    case 85:   return h_hawk;
    case 1:    return h_static;
    case 113:  return h_hidden1up;
    case 174:  return h_leanLever;
    case 173:  return h_oneWayDoor;
    case 71:   return h_vortex;
    case 103:  return h_vaultLetter;
    case 21:   return h_vaultDoor;
    case 107:  return h_vaultIntro;
    case 108:  return h_textLetter;
    case 166:  return h_vaultReward;
    case 140:  return h_stoneJaguar;
    case 141:  return h_stoneJaguarBase;
    case 153:  return h_jaguarFight;
    case 154:  return h_jaguarRope;
    case 158:  return h_jagMan;
    case 159:  return h_jagManClaw;
    case 105:  return h_warriorSpirit;
    case 106:  return h_spiritPart;
    case 176:  return h_lastLetter;
    case 24:   return h_inert;
    case 157:  return h_continueSign;
    case 0:    return nullptr;
    default:   return h_default;
    }
}

} // namespace game
