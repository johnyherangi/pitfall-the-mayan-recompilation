// Object pool, dispatcher and the helpers shared by the behaviour handlers.
// Each function names the routine of PITFALL.EXE it reproduces.
#include "game/objects.h"

#include "game/game.h"
#include "game/play_logic.h"

#include <algorithm>
#include <cstring>

namespace game {

// ---------------------------------------------------------------- ObjCtx helpers

uint32_t ObjCtx::rnd() { return pl.random(); }

void ObjCtx::sound(int id) { if (id) g.playSound(id); }

void ObjCtx::soundNear(const Obj& o, int id) {
    // FUN_00423134 opcodes 0xee/0xef: within 128 px horizontally, -0x60..0x80 vertically.
    const int dx = o.x - harry.x(), dy = o.y - harry.y();
    if (dx >= -0x80 && dx < 0x80 && dy >= -0x60 && dy < 0x80) sound(id);
}

void ObjCtx::hurtHarry(int amount) { pl.hurt(g, amount); }

const ph::ColumnTable& ObjCtx::det() const { return lv.collision; }
const ph::ColumnTable& ObjCtx::dt2() const { return lv.collision2.empty() ? lv.collision : lv.collision2; }

bool ObjCtx::floorSnap(int x, int& y, bool plane2) const {
    // FUN_00425d6f / FUN_00425f88: the cell of (x, y), then the row above, then below.
    ph::FloorHit f = ph::probeNear(plane2 ? dt2() : det(), x, y);
    if (!f.hit) return false;
    y = f.surfaceY;
    return true;
}

uint8_t ObjCtx::triggerClass(int x, int y, uint8_t* id) const {
    // FUN_004251f9: the lookup column is (x + 0x20) >> 4; class bytes >= 0xf0 read as 0.
    const int tx = x + 0x20;
    if (tx < 0 || y < 0) { if (id) *id = 0; return 0; }
    const ph::ColumnEntry* e = lv.triggers.find(tx >> 4, y >> 4);
    if (!e) { if (id) *id = 0; return 0; }
    if (id) *id = e->id;
    const uint8_t cls = pl.triggerClassOf(e->id);           // DAT_004658c0, the runtime copy
    return cls >= 0xf0 ? 0 : cls;
}

const ph::Sprite* ObjCtx::spr(const char* name) { return g.findSprite(name); }

Obj* ObjCtx::spawn() { return pl.objectSystem().alloc(); }
void ObjCtx::release(Obj& o) { pl.objectSystem().free(o); }

Obj* ObjCtx::spawnEffect(Obj& at, const ph::Sprite* sprite, const uint8_t* script, int ticks) {
    // 0x433914: a type 0x68 effect at the object's position playing `script`.
    Obj* e = spawn();
    if (!e) return nullptr;
    e->x = at.x; e->y = at.y;
    e->sprite = sprite;
    e->script = script;
    e->b(0x46) = uint8_t(ticks);
    e->type = 0x68;
    e->flags = uint16_t((at.flags & 0x8000) + 0x6000);
    return e;
}

void ObjCtx::sparkleBurst(Obj& at, const int16_t* offsets) {
    // 0x433747: one fx_sprk1 (type 0x10) per (dx, dy) pair, mirrored at random,
    // each with a random start delay.
    for (; offsets[0] != int16_t(0x8000); offsets += 2) {
        const uint32_t r = rnd();
        Obj* e = spawn();
        if (!e) return;
        e->x = int16_t(at.x + ((r & 1) ? -offsets[0] : offsets[0]));
        e->y = int16_t(at.y + ((r & 2) ? -offsets[1] : offsets[1]));
        e->type = 0x10;
        e->flags = uint16_t((at.flags & 0x8000) + 0x6000);
        e->sprite = spr("fx_sprk1");
        e->w(0x60) = int16_t(rnd() & 0x1f);
    }
}

bool ObjCtx::boxesOverlap(const Obj& a, const Obj& b) {
    // FUN_00424af1: boxes from the 0x468824 table, relative to each object's position.
    if (!a.sprite || !b.sprite) return false;
    const ph::SpriteHitbox* ha = ph::hitbox(a.sprite->name().c_str());
    const ph::SpriteHitbox* hb = ph::hitbox(b.sprite->name().c_str());
    if (!ha || !hb || ha->w == 0 || hb->w == 0) return false;
    const int ax0 = a.x + ha->dx, ax1 = ax0 + ha->w, bx0 = b.x + hb->dx, bx1 = bx0 + hb->w;
    if (bx1 - ax0 < 0 || bx0 - ax1 >= 0) return false;
    const int ay0 = a.y + ha->dy, ay1 = ay0 + ha->h, by0 = b.y + hb->dy, by1 = by0 + hb->h;
    if (by1 - ay0 < 0 || by0 - ay1 >= 0) return false;
    return true;
}

bool ObjCtx::touchesHarry(Obj& o, bool evenHidden) {
    if (harryUntouchable || !o.sprite || (o.frame == 0xff && !evenHidden)) return false;
    const ph::SpriteHitbox* ho = ph::hitbox(o.sprite->name().c_str());
    const ph::SpriteHitbox* hh = ph::hitbox(harrySprite);
    if (!ho || !hh || ho->w == 0 || hh->w == 0) return false;
    // Harry's box is stored for the right-facing sprite; mirror it when he faces left.
    int hx0 = harry.x() + hh->dx, hx1 = hx0 + hh->w;
    if (harry.facingLeft()) { hx1 = harry.x() - hh->dx; hx0 = hx1 - hh->w; }
    const int ox0 = o.x + ho->dx, ox1 = ox0 + ho->w;
    if (ox1 - hx0 < 0 || ox0 - hx1 >= 0) return false;
    const int hy0 = harry.y() + hh->dy, hy1 = hy0 + hh->h, oy0 = o.y + ho->dy, oy1 = oy0 + ho->h;
    if (oy1 - hy0 < 0 || oy0 - hy1 >= 0) return false;
    return true;
}

// ---------------------------------------------------------------- ObjectSystem

void ObjectSystem::reset() { pool_.clear(); }

Obj* ObjectSystem::alloc() {
    // FUN_00435905: take a free record; the pool holds 0x50 objects.
    for (auto& p : pool_)
        if (p->freed) { *p = Obj{}; return p.get(); }
    if (int(pool_.size()) >= kPoolSize) return nullptr;
    pool_.push_back(std::make_unique<Obj>());
    return pool_.back().get();
}

void ObjectSystem::free(Obj& o) {
    // FUN_0043598f: unlink and return the record.  Objects linking to it are
    // left alone; handlers test `link->freed` before following a link.
    o.freed = true;
    o.sprite = nullptr;
    o.frame = 0xff;
}

void ObjectSystem::tick(ObjCtx& ctx) {
    // 0x405170: walk the list; the handler may release its own object, so the
    // next pointer is taken before dispatching (DAT_00465888).
    const size_t n = pool_.size();        // objects spawned this tick run next tick
    for (size_t i = 0; i < n; ++i) {
        Obj& o = *pool_[i];
        if (o.freed) continue;
        Handler h = handler(o.type);
        ctx.pl.setTriggerClass(2, 3);     // 0x40528a: trigger id 2 is a wall for objects (a few handlers clear it again)
        if (h) h(ctx, o);
        o.fresh = false;
    }
}

// ---------------------------------------------------------------- shared pieces

namespace {

// FUN_00423134 control bytes.
void animControl(ObjCtx& ctx, Obj& o, uint8_t op, const uint8_t* p) {
    switch (op) {
    case 0xfc: ctx.sound(p[1]); break;                      // FUN_004262d9
    case 0xfd: ctx.sound(p[1]); break;                      // FUN_004261e2
    case 0xee: case 0xef: ctx.soundNear(o, p[1]); break;
    case 0xfb: ctx.rnd(); ctx.sound(p[1]); break;          // FUN_0042633d: the random part only varies the pitch
    case 0xed: ctx.rnd(); ctx.soundNear(o, p[1]); break;
    case 0xf7: o.facing ^= 1; break;
    case 0xf9: o.vflip = 0; break;
    default: break;
    }
}

bool isAnimControl(uint8_t op) {
    switch (op) {
    case 0xfc: case 0xfd: case 0xee: case 0xef: case 0xfb: case 0xed: case 0xf7: case 0xf9: case 0xfe: return true;
    default: return false;
    }
}

int animOpLen(uint8_t op) {
    switch (op) {
    case 0xfe: return 2;
    case 0xfc: case 0xfd: case 0xee: case 0xef: return 3;
    case 0xfb: case 0xed: return 6;
    default: return 1;
    }
}

} // namespace

void animPeek(ObjCtx& ctx, Obj& o, AnimCursor& cur, const uint8_t* script) {
    // FUN_00423150: resolve control bytes at the cursor and show the frame there.
    for (int guard = 0; guard < 64; ++guard) {
        const uint8_t b = script[cur.pos];
        if (b < 0x80) { o.frame = b; return; }
        if (b == 0xf0) { cur.pos = 0; cur.tick = 0; if (script[0] == 0xf0) return; continue; }
        if (!isAnimControl(b)) { o.frame = b; return; }
        animControl(ctx, o, b, script + cur.pos);
        cur.pos = uint8_t(cur.pos + animOpLen(b));
        cur.tick = 0;
    }
}

bool animStep(ObjCtx& ctx, Obj& o, AnimCursor& cur, const uint8_t* script, int ticksPerFrame) {
    // FUN_00423134: a frame lasts ticksPerFrame + 1 ticks.
    cur.tick++;
    if (int8_t(ticksPerFrame - cur.tick) < 0) { cur.tick = 0; cur.pos++; }
    for (int guard = 0; guard < 64; ++guard) {
        const uint8_t b = script[cur.pos];
        if (b < 0x80) { o.frame = b; return false; }
        if (b == 0xf0) {
            cur.pos = 0; cur.tick = 0;
            animPeek(ctx, o, cur, script);
            return true;
        }
        if (b == 0xfe) {
            // Level-script event: not ported (the only user is the Loltun vault lever).
            cur.pos = uint8_t(cur.pos + 2); cur.tick = 0;
            continue;
        }
        if (!isAnimControl(b)) { o.frame = b; return false; }   // e.g. 0xff: hidden frame
        animControl(ctx, o, b, script + cur.pos);
        cur.pos = uint8_t(cur.pos + animOpLen(b));
        cur.tick = 0;
    }
    return false;
}

bool anim2Step(ObjCtx& ctx, Obj& o, int pcOff, int ticksPerFrame) {
    return anim2Step(ctx, o, o.script, pcOff, ticksPerFrame);
}

bool anim2Step(ObjCtx& ctx, Obj& o, const uint8_t* script, int pcOff, int ticksPerFrame) {
    // FUN_004237a5 for objects: pc = o.l(pcOff) (byte offset into the script), tick at pcOff + 4.
    // Only the opcodes the object scripts use are implemented; the joypad-driven
    // branches (0xf2/0xf4/0xf5) and the sling-swing opcodes are Harry's.
    if (!script) return true;
    FieldRef<int32_t> pc = o.l(pcOff);
    FieldRef<uint8_t> tick = o.b(pcOff + 4);
    tick += 1;
    if (int8_t(ticksPerFrame - int(tick)) < 0) { tick = 0; pc += 1; }
    for (int guard = 0; guard < 64; ++guard) {
        const uint8_t* p = script + int32_t(pc);
        const uint8_t b = p[0];
        if (b < 0x80) { o.frame = b; return false; }
        switch (b) {
        case 0xf0: return true;
        case 0xfc: case 0xfd: ctx.sound(p[1]); pc += 3; break;
        case 0xfb: ctx.rnd(); ctx.sound(p[1]); pc += 6; break;
        case 0xed: ctx.rnd(); ctx.soundNear(o, p[1]); pc += 6; break;
        case 0xf7: o.facing = o.facing ? 0 : 0xff; pc += 1; break;
        case 0xf9: o.vflip ^= 1; pc += 1; break;
        case 0xf8: return false;                               // waits for DAT_00465b96 (Harry only)
        case 0xf3: { int32_t t; std::memcpy(&t, p + 1, 4); pc = t; break; }   // jump (relocated to an offset)
        case 0xf1: {                                           // set sprite (relocated to a name index)
            int32_t idx; std::memcpy(&idx, p + 1, 4);
            o.sprite = ctx.spr(kAnim2Sprites[idx]);
            pc += 5;
            break;
        }
        case 0xf2: case 0xf4: case 0xf5: pc += 6; break;       // Harry-only joypad branches: skip
        case 0xf6: case 0xfa: pc += 1; break;                  // Harry-only sling opcodes: skip
        default: o.frame = b; return false;                    // any other byte is shown as the frame (0xff = hidden)
        }
    }
    return false;
}

bool weaponHits(ObjCtx& ctx, Obj& o, int hitPoints, bool* killed) {
    // 0x424171: the sling stones / boomerang (0x466808 list, not ported yet) and the
    // whip box (DAT_00465aeb..).  Each hit adds to o.hits; hits >= hitPoints kills.
    bool hit = false;
    if (o.whipCool) {
        --o.whipCool;
    } else if (ctx.whipX1 > ctx.whipX0 && o.sprite && o.type != 0x20) {
        if (const ph::SpriteHitbox* hb = ph::hitbox(o.sprite->name().c_str())) {
            const int x0 = o.x + hb->dx, x1 = x0 + hb->w, y0 = o.y + hb->dy, y1 = y0 + hb->h;
            if (hb->w && x1 >= ctx.whipX0 && x0 < ctx.whipX1 && y1 >= ctx.whipY0 && y0 < ctx.whipY1) {
                o.whipCool = 0x14;
                ctx.rnd(); ctx.sound(0x19);                                  // FUN_0042633d(0x19, 0x60, 9)
                o.hits = uint8_t(o.hits + (ctx.hard ? 9 : 10));
                hit = true;
            }
        }
    }
    // The kill test only runs on the tick of a hit (0x424682): hits + 1 > hitPoints.
    const bool dead = hit && int(o.hits) + 1 > hitPoints;
    if (hit && !dead) ctx.sound(o.type == 0x42 ? 0x8c : 0xcd);    // 0x4246c7: a hit that did not kill
    if (killed) *killed = dead;
    return hit;
}

namespace {
// 0x45b6c3: behaviour class per type (bit 1: the kill is permanent across deaths).
// 0x461aca: score per type.  0x45b814: death-effect kind per type.
struct DeathFx { uint8_t type; int8_t yOff; uint8_t kind; };
const DeathFx kDeathFx[] = {
    {10, 0, 2}, {36, 0, 2}, {12, -4, 1}, {18, 0, 2}, {43, 0, 2}, {23, 0, 1}, {35, -16, 1}, {45, -40, 3},
    {56, -30, 3}, {61, 8, 3}, {66, -45, 3}, {70, 0, 1}, {72, 5, 1}, {80, -25, 2}, {102, 0, 2}, {12, -4, 1},
    {110, 0, 1}, {112, 0, 1}, {133, -4, 2}, {140, -25, 3}, {172, 32, 2},
};
const uint16_t kKillScore[180] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 350, 0, 125, 0, 0, 0, 0, 0, 125, 0, 0, 0, 0, 100, 0, 0, 0, 0, 400, 0, 0, 0, 0, 0,
    999, 150, 0, 0, 100, 0, 0, 0, 0, 125, 0, 2000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6000, 500, 10, 0, 0, 0, 750, 10, 0,
    0, 0, 750, 0, 0, 0, 100, 0, 180, 0, 0, 0, 0, 0, 0, 0, 450, 0, 0, 0, 0, 350, 350, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 125, 0, 0, 0, 25000, 0, 0, 125, 200, 0, 100, 0, 0, 0, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 8000, 100, 1500, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    10000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 225, 0, 0, 0, 0, 0, 0, 0};
const uint8_t kTypeClass[180] = {
    240, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 7, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 7, 0, 0, 0, 0, 1, 1, 0, 0, 5, 0, 0, 7,
    0, 0, 5, 0, 0, 0, 0, 5, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 1, 5, 0, 0, 5, 1, 0, 0, 0, 5, 0, 0, 0, 7, 0,
    5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 7, 7, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 5, 0, 0,
    0, 7, 7, 0, 7, 0, 0, 0, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0};
} // namespace

int scoreForType(uint8_t type) { return type < 180 ? kKillScore[type] : 0; }   // 0x4081ae

void killObject(ObjCtx& ctx, Obj& o) {
    // 0x4081fc
    if (o.type < 180 && (kTypeClass[o.type] & 2)) ctx.pl.markKilled(o);     // permanent (0x465a40 list)
    if (o.type < 180) ctx.g.stats().score += kKillScore[o.type];
    int kind = 0, yOff = 0;
    for (const DeathFx& d : kDeathFx) if (d.type == o.type) { kind = d.kind; yOff = d.yOff; break; }
    if (kind) { ctx.rnd(); ctx.sound(6); }                                   // FUN_0042633d(6, 0x60, 0xe)
    if (kind == 1) {
        // A puff of debris (0x433725 + fx_stone script 0x4619c7 at 3 ticks).
        ctx.sparkleBurst(o, (ctx.rnd() & 1) ? kSparkleOffsetsB : kSparkleOffsetsA);
        ctx.spawnEffect(o, ctx.spr("fx_stone"), kAnim_turn7, 3);
    } else if (kind == 2 || kind == 3) {
        // The death cloud (type 0x82) over the dying enemy's picture.
        if (Obj* e = ctx.spawn()) {
            e->x = o.x; e->y = int16_t(o.y + yOff);
            e->type = 0x82;
            e->behind = 0xff;
            e->flags = uint16_t((o.flags & 0x8000) + 0x6000);
            e->sprite = ctx.spr(kind == 2 ? "fx_deat2" : "fxzdeath");
            e->b(0x3c) = 1;
            e->link = nullptr;
            if (kind == 3) {
                // fxzdeath keeps the enemy's sprite under the cloud.
                if (Obj* body = ctx.spawn()) {
                    body->x = o.x; body->y = o.y;
                    body->type = 0x82;
                    body->sprite = o.sprite; body->frame = o.frame; body->facing = o.facing;
                    body->flags = o.flags; body->behind = 0xff;
                    body->w(0x26) = 0x6663;
                    body->link = e;
                    e->link = body;
                }
            }
        }
    }
    ctx.release(o);
}

bool harryContact(ObjCtx& ctx, Obj& o, int damage) {
    // 0x407aff: every 6th tick, if Harry is touchable and the boxes overlap.
    if (int8_t(o.touchTick) - 1 < 0) {
        o.touchTick = 5;
        if (ctx.harryUntouchable) return false;
        if (ctx.hstate == HarryState::Swinging) return false;       // hyisswng: handled with an offset table
        if (ctx.touchesHarry(o)) { ctx.hurtHarry(damage); return true; }
        return false;
    }
    --o.touchTick;
    return false;
}

void priorityTrigger(ObjCtx& ctx, Obj& o, int dx) {
    // 0x4301ce
    uint8_t id = 0;
    const ph::ColumnEntry* e = nullptr;
    const int tx = o.x + dx + 0x20;
    if (tx >= 0 && o.y >= 0) e = ctx.lv.triggers.find(tx >> 4, o.y >> 4);
    if (!e) return;
    id = e->id;
    if (ctx.pl.triggerClassOf(id) != 0xf2) return;
    if (id == 0x30) o.flags = uint16_t((o.flags & 0x7fff) + 0x8000);
    else if (id == 0x31) o.flags &= 0x7fff;
}

void exitDespawnFar(ObjCtx& ctx, Obj& o) {
    // 0x424d6a / 0x424ec4
    const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
    if (dx < -0x78 || dx >= 0x1b8 || dy < -0x82 || dy >= 0x15e) { ctx.pl.restoreEntry(o); ctx.release(o); }
}

bool farFromCamera(const ObjCtx& ctx, const Obj& o) {
    const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
    return dx < -0x78 || dx >= 0x1b8 || dy < -0x82 || dy >= 0x15e;
}

void exitDespawnTall(ObjCtx& ctx, Obj& o) {
    // 0x4250ce / 0x425100: the far window sideways, but 0x14a above / 0x226 below the camera
    // (for things that hang well above the view, such as the drippers).
    const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
    if (dx < -0x78 || dx >= 0x1b8 || dy < -0x14a || dy >= 0x226) { ctx.pl.restoreEntry(o); ctx.release(o); }
}

void exitDespawnVertical(ObjCtx& ctx, Obj& o) {
    // 0x42501f / 0x424e73: only the vertical distance counts (0xa0 above .. 0x17c below the camera).
    const int dy = o.y - ctx.camY;
    if (dy < -0xa0 || dy >= 0x17c) { ctx.pl.restoreEntry(o); ctx.release(o); }
}

void exitDespawnMid(ObjCtx& ctx, Obj& o) {
    // 0x424d9c
    const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
    if (dx < -0xc8 || dx >= 0x208 || dy < -0x82 || dy >= 0x15e) { ctx.pl.restoreEntry(o); ctx.release(o); }
}

bool beyondWide(const ObjCtx& ctx, const Obj& o) {
    // 0x425051: a full screen beyond the camera in any direction.
    const int dx = o.x - ctx.camX, dy = o.y - ctx.camY;
    return dx < -0x140 || dx >= 0x280 || dy < -0x82 || dy >= 0x15e;
}

void exitDespawnWide(ObjCtx& ctx, Obj& o) {
    // 0x424fbb / 0x425051: a wider window (a full screen either side) used by most enemies.
    if (beyondWide(ctx, o)) { ctx.pl.restoreEntry(o); ctx.release(o); }
}

void freezeSparkle(ObjCtx& ctx, Obj& o) {
    // 0x412629: while the stop-watch runs, enemies only shimmer.
    if (ctx.pl.freezeSparkleBlocked()) { exitDespawnFar(ctx, o); return; }
    uint32_t r;
    do { r = ctx.rnd() & 7; } while (r >= 5);
    if (Obj* e = ctx.spawn()) {
        e->x = int16_t(o.x + kSparkleOffsetsA[r * 2]);
        e->y = int16_t(o.y + kSparkleOffsetsA[r * 2 + 1]);
        e->type = 0x10;
        e->flags = 0x6000;
        e->sprite = ctx.spr("fx_sprk1");
    }
    exitDespawnFar(ctx, o);
}

} // namespace game
