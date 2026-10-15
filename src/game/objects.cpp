// Object pool, dispatcher and the helpers shared by the behaviour handlers.
// Each function names the routine of PITFALL.EXE it reproduces.
#include "game/objects.h"
#include "game/harry.h"

#include "game/game.h"
#include "game/play_logic.h"

#include <algorithm>
#include <cstring>

namespace game {

// ---------------------------------------------------------------- ObjCtx helpers

uint32_t ObjCtx::rnd() { return pl.random(); }

bool ObjCtx::hurtHarry(int amount) { return pl.hurtHarry(amount); }
bool ObjCtx::sound0(int id, int pri) { return pl.sound0(id, pri); }
bool ObjCtx::sound1(int id, int pri) { return pl.sound1(id, pri); }
void ObjCtx::soundNow(int id) { pl.sound1Now(id); }
bool ObjCtx::channel0Busy() { return g.channelBusy(0); }
bool ObjCtx::channel1Busy() { return g.channelBusy(1); }

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
    // FUN_004251f9: d2 = the class (0 for classes >= 0xf0), d3 = the id.
    const Trg t = pl.trg(x, y);
    if (id) *id = t.id;
    return t.cls;
}

Trg ObjCtx::trigger(int x, int y) const { return pl.trg(x, y); }

bool ObjCtx::frozen() const { return static_cast<const Harry&>(harry).w(0x46652f) != 0; }

const ph::Sprite* ObjCtx::spr(const char* name) { return g.findSprite(name); }

Obj* ObjCtx::spawn(bool tail) { return tail ? pl.objectSystem().allocTail() : pl.objectSystem().allocHead(); }
Obj* ObjCtx::spawnFrom(Obj& parent, bool tail, int d0) {
    const int16_t x = parent.x, y = parent.y;          // (the parent may be the record just freed)
    Obj* o = spawn(tail);
    if (o) {
        if (uint8_t(d0)) o->w(0x26) = 0x6663;          // FUN_004263a6
        o->x = x; o->y = y;
    }
    return o;
}
void ObjCtx::release(Obj& o) { pl.objectSystem().free(o); }

Obj* ObjCtx::spawnEffect(Obj& at, const ph::Sprite* sprite, const uint8_t* script, int ticks) {
    // 0x433914: a type 0x68 effect at the object's position playing `script`.
    Obj* e = spawn();                                  // FUN_004356bc(2): every caller passes d2 = 2
    if (!e) return nullptr;
    e->w(0x26) = 0x6663;                               // (d0 != 0: FUN_004263a6)
    e->x = at.x; e->y = at.y;
    e->sprite = sprite;
    e->script = script;
    e->b(0x46) = uint8_t(ticks);
    e->type = 0x68;
    e->flags = uint16_t((at.flags & 0x8000) + 0x6000);
    return e;
}

void ObjCtx::sparkleBurst(Obj& at, const int16_t* offsets) {
    // 0x433747: one fx_sprk1 (type 0x10) per (dx, dy) pair, all mirrored the same way at
    // random, each with a random start delay.  Each one is placed from `at` as it is then
    // (when `at` was just freed, the first sparkle is that very record).
    const uint32_t r = rnd();
    const bool mx = (r & 1) != 0, my = (r & 2) != 0;
    do {
        Obj* e = spawn();                              // FUN_00435905
        if (!e) return;
        e->x = at.x; e->y = at.y;
        e->x = int16_t(e->x + (mx ? -offsets[0] : offsets[0]));
        e->y = int16_t(e->y + (my ? -offsets[1] : offsets[1]));
        e->type = 0x10;
        e->flags = uint16_t((at.flags & 0x8000) + 0x6000);
        e->sprite = spr("fx_sprk1");
        e->w(0x60) = int16_t(rnd() & 0x1f);
        offsets += 2;
    } while (offsets[0] != int16_t(0x8000));
}

namespace {
// FUN_00424af1 takes the box from the sprite record (+8: offset into the 0x468824 table,
// 0xffff = none); three objects point +0xe at a box record of their own instead (Obj::rawBox).
const ph::SpriteHitbox* boxOf(const Obj& o) {
    if (o.rawBox) return o.rawBox;
    return o.sprite ? ph::hitbox(o.sprite->name().c_str()) : nullptr;
}
// The overlap test of FUN_00424af1 for a5 at (ax, ay) with box ha and a6 at (bx, by) with box hb
// (16-bit compares, as the listing does them; no mirroring by facing).
bool overlap(int ax, int ay, const ph::SpriteHitbox* ha, int bx, int by, const ph::SpriteHitbox* hb, int16_t* d1Out = nullptr) {
    if (!ha || !hb || ha->w == 0 || hb->w == 0) return false;
    int16_t d0 = int16_t(ax + ha->dx), d1 = int16_t(bx + hb->dx + hb->w);
    auto done = [&](bool r) { if (d1Out) *d1Out = d1; return r; };
    if (int16_t(d1 - d0) < 0) return done(false);
    d1 = int16_t(d1 - hb->w); d0 = int16_t(d0 + ha->w);
    if (int16_t(d1 - d0) >= 0) return done(false);
    d0 = int16_t(ay + ha->dy); d1 = int16_t(by + hb->dy + hb->h);
    if (int16_t(d1 - d0) < 0) return done(false);
    d1 = int16_t(d1 - hb->h); d0 = int16_t(d0 + ha->h);
    if (int16_t(d1 - d0) >= 0) return done(false);
    return done(true);
}
} // namespace

bool ObjCtx::boxesOverlap(const Obj& a, const Obj& b) {
    // FUN_00424af1 with a5 = a, a6 = b.
    return overlap(a.x, a.y, boxOf(a), b.x, b.y, boxOf(b));
}

bool ObjCtx::touchesHarry(Obj& o, int16_t* d1) {
    // FUN_00424af1 with a5 = Harry (DAT_00465b0f), a6 = o: Harry's box from his sprite slot;
    // never while DAT_00465b72 is set (dying / leaving the level).
    const char* hs = harry.sprite();
    if (!*hs || (!o.sprite && !o.rawBox)) return false;
    if (!overlap(harry.x(), harry.y(), ph::hitbox(hs), o.x, o.y, boxOf(o), d1)) return false;
    return harry.b(0x465b72) == 0;
}

// ---------------------------------------------------------------- ObjectSystem

void ObjectSystem::reset() {
    // FUN_00435274 (0x4352b4): record i on the stack at 0x464ab4 + 4i, record 0x4f on top.
    for (int i = 0; i < kPoolSize; ++i) {
        stack_[i] = i;
        Obj& o = recs_[size_t(i)];
        o.next = -1; o.prev = -1;
        o.type = 0;
        o.freed = true;
    }
    top_ = kPoolSize - 1;
    head_ = tail_ = -1;
    disp_.fill(-1);
    behindN_ = 0; frontN_ = kFront;
    curSlot_ = 0; hasSlot_ = false;
}

void ObjectSystem::init(Obj& o) {
    // 0x43582b: the fields a new record starts with (x, y and their fractions are left as they were)
    o.type = 0; o.frame = 0xff; o.hits = 0; o.facing = 0; o.flags = 0;
    o.sprite = nullptr; o.rawBox = nullptr;
    std::memset(o.s + 0x12, 0, 2);
    o.col = o.row = -1; o.als = -1;                    // +0x1c = 0x466770, a dummy .als entry
    o.id = 0x10;
    o.s[0x21] = 0xff;
    std::memset(o.s + 0x22, 0, 6);                     // +0x22 .. +0x27
    o.behind = 0; o.vflip = 0;
    std::memset(o.s + 0x2a, 0, 3);
    o.whipCool = 0;
    o.touchTick = 0xa;
    o.s[0x2f] = 0xff;
    std::memset(o.s + 0x30, 0, 0x3e);                  // +0x30 .. +0x6d
    o.link = o.link2 = o.link3 = nullptr;
    o.parts.clear();
    o.script = nullptr;
    o.freed = false;
}

void ObjectSystem::unlink(Obj& o) {
    // FUN_0043598f / 0x435532 (0x435b55): out of the list by its own prev / next.
    if (o.next == -1) {
        if (o.prev == -1) { head_ = tail_ = -1; return; }
        recs_[size_t(o.prev)].next = -1;
        tail_ = o.prev;
        return;
    }
    if (o.prev == -1) {
        recs_[size_t(o.next)].prev = -1;
        head_ = o.next;
        return;
    }
    recs_[size_t(o.prev)].next = o.next;
    recs_[size_t(o.next)].prev = o.prev;
}

void ObjectSystem::linkTail(Obj& o) {
    const int i = indexOf(o);
    if (tail_ == -1) { head_ = tail_ = i; o.prev = o.next = -1; return; }
    recs_[size_t(tail_)].next = i;
    o.prev = tail_;
    o.next = -1;
    tail_ = i;
}

Obj* ObjectSystem::allocHead() {
    // FUN_00435905: pop a free record and put it at the head of the list.
    if (top_ < kReserve) return nullptr;
    Obj& o = recs_[size_t(stack_[top_--])];
    const int i = indexOf(o);
    if (head_ == -1) {
        head_ = tail_ = i; o.prev = o.next = -1;
    } else {
        recs_[size_t(head_)].prev = i;
        o.next = head_;
        o.prev = -1;
        head_ = i;
    }
    init(o);
    return &o;
}

Obj* ObjectSystem::allocTail() {
    // FUN_00435780: the same at the tail.
    if (top_ < kReserve) return nullptr;
    Obj& o = recs_[size_t(stack_[top_--])];
    linkTail(o);
    init(o);
    return &o;
}

void ObjectSystem::free(Obj& o) {
    // FUN_0043598f.  (The original stops the game with an error for a stone still in its
    // slot, for Harry's record and for bad pointers.)  The display entry of the object that
    // is running is cancelled when it frees itself (DAT_00465b8e); the record keeps its fields.
    if (o.freed) return;   // (a second free would push the record twice; the handlers never do it)
    if (hasSlot_ && disp_[curSlot_] == indexOf(o)) disp_[curSlot_] = -2;
    stack_[++top_] = indexOf(o);
    o.type = 0;
    o.freed = true;
    unlink(o);
}

bool ObjectSystem::moveToTail(Obj& o) {
    // 0x435532: pushed onto the free stack and popped straight back, at the tail.
    stack_[++top_] = indexOf(o);
    unlink(o);
    if (top_ < kReserve) {
        // no record to spare: FUN_0043598f again (its second unlink changes nothing), and the
        // handler is abandoned
        if (hasSlot_ && disp_[curSlot_] == indexOf(o)) disp_[curSlot_] = -2;
        stack_[++top_] = indexOf(o);
        o.type = 0; o.freed = true;
        return false;
    }
    Obj& r = recs_[size_t(stack_[top_--])];
    linkTail(r);
    return true;
}

std::vector<Obj*> ObjectSystem::list() {
    std::vector<Obj*> v;
    for (int i = head_; i != -1 && v.size() <= size_t(kPoolSize); i = recs_[size_t(i)].next) v.push_back(&recs_[size_t(i)]);
    return v;
}

void ObjectSystem::tick(ObjCtx& ctx) {
    // 0x405170: the display lists start again; the list is walked from the head, the next
    // record is taken before the handler runs (DAT_00465888).
    behindN_ = 0; frontN_ = kFront;
    disp_[0] = -1; disp_[kFront] = -1;                       // 0x405183
    int guard = 0;
    for (int i = head_; i != -1 && guard < 0x1000; ++guard) {
        Obj& o = recs_[size_t(i)];
        const int pending = o.next;
        size_t& n = o.behind ? behindN_ : frontN_;            // 0x405236 (+0x28)
        if (n + 1 < disp_.size()) {                          // (the original would write on past 0x464e7c)
            disp_[n] = i; disp_[n + 1] = -1;
            curSlot_ = n; hasSlot_ = true;
            ++n;
        }
        ctx.pl.setTriggerClass(2, 3);     // 0x40528a: trigger id 2 is a wall for objects (a few handlers clear it again)
        if (o.type != 0) {
            Handler h = handler(o.type);
            if (h) h(ctx, o);
        }
        i = pending;
    }
}

// ---------------------------------------------------------------- shared pieces

namespace {

// FUN_00423134 control bytes.
// Harry within the sound range of the object (0x423310: dx -0x80..0x7f, dy from dyMin to 0x7f).
bool soundRange(ObjCtx& ctx, const Obj& o, int dyMin) {
    const int16_t dx = int16_t(o.x - ctx.harry.x()), dy = int16_t(o.y - ctx.harry.y());
    return int16_t(dx + 0x80) >= 0 && int16_t(dx - 0x80) < 0 && int16_t(dy - dyMin) >= 0 && int16_t(dy - 0x80) < 0;
}

void animControl(ObjCtx& ctx, Obj& o, uint8_t op, const uint8_t* p) {
    switch (op) {
    case 0xfe:                                              // stop the channel-1 sound `ev` if it is the last one
        if (p[1] == ctx.pl.scriptEvent()) { ctx.soundNow(0x89); ctx.pl.scriptEvent() = 0; }
        break;
    case 0xfc: ctx.sound0(p[1], p[2]); break;               // FUN_004262d9
    case 0xfd: ctx.sound1(p[1], p[2]); break;               // FUN_004261e2
    case 0xee: if (soundRange(ctx, o, -0x60)) ctx.sound0(p[1], p[2]); break;
    case 0xef: if (soundRange(ctx, o, -0x60)) ctx.sound1(p[1], p[2]); break;
    case 0xfb: ctx.rnd(); ctx.sound0(p[1], p[5]); break;     // FUN_0042633d: the random part only varies the pitch
    case 0xed: if (soundRange(ctx, o, -0x80)) { ctx.rnd(); ctx.sound0(p[1], p[5]); } break;
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
        case 0xfc:                                             // FUN_004262d9 (0xb9 with the big stones: 0x17)
            if (p[1] == 0xb9 && ctx.harry.b(0x4667af)) ctx.sound0(0x17, 0x1d);
            else ctx.sound0(p[1], p[2]);
            pc += 3; break;
        case 0xfd: ctx.sound1(p[1], p[2]); pc += 3; break;     // FUN_004261e2
        case 0xfb: ctx.rnd(); ctx.sound0(p[1], p[5]); pc += 6; break;   // FUN_0042633d
        case 0xed: if (soundRange(ctx, o, -0x60)) { ctx.rnd(); ctx.sound0(p[1], p[5]); } pc += 6; break;
        // (the original also swaps the joypad's left / right bytes while a left-facing object's
        // script runs; they are swapped back before it returns, so nothing comes of it here)
        case 0xf7: o.facing = o.facing ? 0 : 0xff; pc += 1; break;
        case 0xf9: o.vflip ^= 1; pc += 1; break;
        case 0xf8:                                             // waits for DAT_00465b96
            if (ctx.harry.b(0x465b96) == 0) return false;
            ctx.harry.b(0x465b96) = 0;
            pc += 1; break;
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

namespace {
// The whip box (DAT_00465aeb x0, 0x465aed x1, 0x465aef y0, 0x465af1 y1; x0 == 0 while the whip
// is not out) against the object's box (0x424525 / 0x424762: no test of the box's width).
bool whipTouches(ObjCtx& ctx, const Obj& o) {
    const Harry& h = ctx.harry;
    if (h.w(0x465aeb) == 0) return false;
    const ph::SpriteHitbox* b = boxOf(o);
    if (!b) return false;
    int16_t d1 = int16_t(o.x + b->dx + b->w);
    if (int16_t(d1 - h.w(0x465aeb)) < 0) return false;
    d1 = int16_t(d1 - b->w);
    if (int16_t(d1 - h.w(0x465aed)) >= 0) return false;
    d1 = int16_t(o.y + b->dy + b->h);
    if (int16_t(d1 - h.w(0x465aef)) < 0) return false;
    d1 = int16_t(d1 - b->h);
    if (int16_t(d1 - h.w(0x465af1)) >= 0) return false;
    return true;
}
} // namespace

void debrisPuff(ObjCtx& ctx, Obj& at) {
    // 0x433725: the sparkles of a breaking thing, one of two patterns at random.
    ctx.sparkleBurst(at, (ctx.rnd() & 1) ? kSparkleOffsetsB : kSparkleOffsetsA);
}

WeaponHit weaponHit(ObjCtx& ctx, Obj& o, int hitPoints) {
    // 0x424171 (d5 = hitPoints): a stone in flight (DAT_00466808, five slots), else the
    // boomerang (DAT_0046681c), else the whip box hits the object; each adds to o.hits (+0xa).
    // d1: something hit; d0: hits + 1 > d5 (killed); z: the flag a caller's je tests (= !d0 but
    // for the big stone, which leaves d0 = 1 and the flag of its type compare).
    Harry& h = ctx.harry;
    h.uw(0x466788) = 0;
    auto done = [&](bool hit, bool k) { WeaponHit r; r.d1 = hit; r.d0 = k; r.z = !k; return r; };
    auto killTest = [&] {                                       // 0x424682
        if (int16_t(hitPoints - (int(o.hits) + 1)) >= 0) {     // 0x4246c7: a hit that does not kill
            ctx.sound1(o.type == 0x42 ? 0x8c : 0xcd, 9);
            return done(true, false);
        }
        return done(true, true);
    };
    for (uint32_t a = 0x466808; a <= 0x466818; a += 4) {
        Obj* st = ctx.pl.objAt(h.ul(a));
        if (!st || !ctx.boxesOverlap(*st, o)) continue;
        if (st->type == 4) {
            // The big stone goes on through and only marks itself; the routine returns with the
            // flags of the type compare (je taken) and d1 left by FUN_00424af1 (the top of o's box).
            st->b(0x37) = 0xff;
            const ph::SpriteHitbox* b = boxOf(o);
            WeaponHit r; r.d0 = true; r.z = true; r.d1 = b && uint8_t(o.y + b->dy) != 0;
            return r;
        }
        h.setUl(a, 0);
        st->w(0x26) = 0x6663;                                   // FUN_004263a6(2): a stub that never fails
        if (st->b(0x54) != 0) {                                 // a power shot (d3 = 0xff)
            st->w(0x40) = 1;
            o.hits = uint8_t(o.hits + 0x15);
            if (h.b(0x466507) != 0) o.hits = uint8_t(o.hits + 0x15);   // twice against a boss
        } else {
            h.l(0x466788) = st->l(0x32);                       // where it came from, for the knock-backs
            h.w(0x466782) = st->x;
            h.w(0x466784) = st->y;
            st->w(0x40) = 1;
            o.hits = uint8_t(o.hits + ((ctx.hard && h.b(0x466501) == 0) ? 9 : 0xa));
        }
        return killTest();
    }
    if (Obj* bm = ctx.pl.objAt(h.ul(0x46681c))) {
        // While the boomerang is out the whip does not count.
        if (!ctx.boxesOverlap(*bm, o)) return done(false, false);
        if (h.b(0x466507) != 0 && ctx.levelNo != 5) {
            // against a boss (but in level 5) it breaks
            bm->b(0x5b) = 2;
            h.setUl(0x46681c, 0);
            ctx.release(*bm);
            debrisPuff(ctx, *bm);
            ctx.spawnEffect(*bm, ctx.spr("fx_stone"), kAnim_turn7, 3);   // 0x433914, script 0x4619c7
            o.hits = uint8_t(o.hits + (ctx.hard ? 0x14 : 0x19));
        } else {
            o.hits = uint8_t(o.hits + 0x64);
        }
        if (int16_t(hitPoints - (int(o.hits) + 1)) >= 0) {
            ctx.sound1(o.type == 0x42 ? 0x8c : 0xcd, 9);
            return done(true, false);
        }
        bm->b(0x5b) = 1;
        return done(true, true);
    }
    if (o.whipCool) { o.whipCool -= 1; return done(false, false); }
    if (!whipTouches(ctx, o) || o.type == 0x20) return done(false, false);
    o.whipCool = 0x14;
    ctx.sound0(0x19, 9);                                        // FUN_0042633d(0x19, 0x60)
    o.hits = uint8_t(o.hits + (ctx.hard ? 9 : 0xa));
    return killTest();
}

bool deflectStones(ObjCtx& ctx, Obj& o) {
    // 0x42401c: a sling stone in flight (not the big stone, not one that bounced already, b6a)
    // that touches the object bounces back at a random angle.
    Harry& h = ctx.harry;
    for (uint32_t a = 0x466808; a <= 0x466818; a += 4) {
        Obj* st = ctx.pl.objAt(h.ul(a));
        if (!st || st->type == 4 || st->b(0x6a) != 0 || !ctx.boxesOverlap(*st, o)) continue;
        ctx.sound1(0xce, 0xc);
        st->l(0x32) = st->l(0x32) >> 1;
        st->l(0x32) += int32_t(ctx.rnd() & 0xffff) - 0x7fff;
        st->l(0x36) = st->l(0x36) >> 2;
        st->l(0x36) += int32_t(ctx.rnd() & 0x3ffff) - 0x24000;
        st->l(0x32) = -st->l(0x32);
        st->l(0x4a) = 0x6000;
        st->b(0x6a) = 0xff;
        return true;
    }
    return false;
}

bool webHit(ObjCtx& ctx, Obj& o, bool* cut) {
    // 0x424733: the angled webs (obiaweb) and the vertical web (type 0x20) give under the whip
    // and throw Harry's stones back.  *cut is d3 for the vertical web: cut through.
    Harry& h = ctx.harry;
    if (o.whipCool) { o.whipCool -= 1; return false; }
    auto snd = [&] { ctx.sound1((ctx.rnd() & 0x80) ? 0xcb : 0x47, 9); };
    const bool angled = o.sprite && o.sprite->name() == "obiaweb";
    if ((angled || o.type == 0x20) && whipTouches(ctx, o)) {
        o.w(0x36) = 1;
        o.whipCool = 0x1c;
        snd();
        if (!angled) {
            o.b(0x53) += 1;
            if (cut) *cut = int8_t(o.b(0x53) - 6) >= 0;
        }
        return true;
    }
    if (o.type == 0x20) {
        if (Obj* bm = ctx.pl.objAt(h.ul(0x46681c)); bm && ctx.boxesOverlap(*bm, o)) {
            o.whipCool = 5;
            bm->b(0x6d) = 0xff;
            snd();
            if (cut) *cut = true;
            return true;
        }
    }
    for (uint32_t a = 0x466808; a <= 0x466818; a += 4) {
        Obj* st = ctx.pl.objAt(h.ul(a));
        if (!st || st->type == 4 || !ctx.boxesOverlap(*st, o)) continue;
        o.whipCool = 5;
        if (cut) *cut = st->b(0x54) != 0;
        if (o.type == 0x20) { st->l(0x32) = -st->l(0x32); snd(); return true; }
        o.w(0x36) = 1;
        if (o.b(0x47)) { st->l(0x32) = int32_t(0xfffb8000); st->l(0x36) = int32_t(0xfffc0000); snd(); return true; }
        if (o.b(0x46)) { st->l(0x32) = 0x48000; st->l(0x36) = int32_t(0xfffc0000); snd(); return true; }
    }
    return false;
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
    // 0x4081fc: the 0x465a40 list (class bit 1: it comes back 15 s later; on easy the monkeys
    // do not), the score, then the record is freed and the death effect (0x40855e: by type,
    // which also moves y; unlisted types get the debris puff) is made from it.
    if (o.type != 0 && o.type < 180 && (kTypeClass[o.type] & 2) && (ctx.hard || o.type != 0x23)) ctx.pl.markKilled(o);
    if (o.type < 180) ctx.g.stats().score += kKillScore[o.type];
    int kind = 1;
    for (const DeathFx& d : kDeathFx)
        if (d.type == o.type) { kind = d.kind; o.y = int16_t(o.y + d.yOff); break; }
    ctx.release(o);
    if (kind == 0) return;
    ctx.sound0(6, 0xe);                                         // FUN_0042633d(6, 0x60)
    auto puff = [&] {                                           // 0x408347
        debrisPuff(ctx, o);
        if (Obj* e = ctx.spawnEffect(o, ctx.spr("fx_stone"), kAnim_turn7, 3))   // 0x433914, script 0x4619c7
            e->flags = uint16_t((o.flags & 0x8000) + 0x6000);
    };
    if (kind == 1) { puff(); return; }
    Obj* e = ctx.spawnFrom(o);                                  // FUN_004356bc (d0 = 3 / 4)
    if (!e) { puff(); return; }
    if (kind == 2) {
        // fx_deat2, then fxzdeath: the pointer script 0x45b323 at +0x64
        e->script = kAnim2_deathFade; e->l(0x64) = 0; e->b(0x68) = 0;
    }
    e->link = &o;
    e->b(0x3c) = 1;
    e->type = 0x82;
    e->behind = 0xff;
    e->x = o.x; e->y = o.y;
    e->flags = uint16_t((o.flags & 0x8000) + 0x6000);
    e->sprite = ctx.spr(kind == 2 ? "fx_deat2" : "fxzdeath");
    if (kind == 2) return;
    // 0x4084d0: fxzdeath gets a mirrored second half (type 0) for its frames 1 and 2
    Obj* a3 = e;
    Obj* half = ctx.spawnFrom(o, true, 0);                      // FUN_0043546e(0)
    if (!half) { ctx.release(*a3); return; }
    a3->link = half;
    half->link = a3;
    std::memcpy(half->s + 0x12, a3->s + 0x12, 2);
    half->w(0x26) = 0x6663;
    half->sprite = ctx.spr("fxzdeath");
    half->flags = a3->flags;
    half->facing = 0xff;
}

bool harryContact(ObjCtx& ctx, Obj& o, int damage, int16_t* d1) {
    // 0x407aff: every 6th call (o+0x2e counts down 5..0) the test of 0x407b0f.
    o.touchTick = uint8_t(o.touchTick - 1);
    if (int8_t(o.touchTick) >= 0) return false;
    o.touchTick = 5;
    return harryContactNow(ctx, o, damage, d1);
}

bool harryContactNow(ObjCtx& ctx, Obj& o, int damage, int16_t* d1) {
    // 0x407b0f: the box test against Harry; a hit hurts him by `damage` (FUN_00428233, d5).
    Harry& h = ctx.harry;
    if (h.b(0x465b72) != 0) return false;
    auto test = [&] {                                   // 0x407b25
        if (!ctx.touchesHarry(o, d1)) return false;
        if (ctx.hurtHarry(damage) && d1) *d1 = 0x60;    // (its sound call's d1)
        return true;
    };
    if (!h.spriteIs("hyisswng")) return test();
    // 0x407b59: swinging on a vine his picture swings away from x; the box is moved with it by
    // the word for his frame in the table at 0x45ba4e (towards his facing) for the test.
    const uint8_t* t = harryMem(0x45ba4e + uint32_t(h.frame()) * 2);
    const int16_t d = t ? int16_t(t[0] | t[1] << 8) : 0;
    const int16_t saved = h.x();
    h.x() = int16_t(h.facing() ? saved - d : saved + d);
    const bool hit = test();
    if (h.cont() == 0x42f88c) return true;             // knocked off the vine: he keeps the moved x
    h.x() = saved;
    return hit;
}

void priorityTrigger(ObjCtx& ctx, Obj& o, int dx) {
    // 0x4301ce: class 0xf2 triggers 0x30 / 0x31 at (x + dx, y) put the object in front of /
    // behind the high-priority scenery.
    const Trg t = ctx.trigger(o.x + dx, o.y);
    if (t.raw != 0xf2) return;
    if (t.id == 0x30) o.flags = uint16_t((o.flags & 0x7fff) + 0x8000);
    else if (t.id == 0x31) o.flags &= 0x7fff;
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
