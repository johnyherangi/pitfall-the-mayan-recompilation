// Harry's support code: the joypad decode, the two animation interpreters as they run on
// Harry, the trigger and floor probes of his state code, the sound channels, the hurt
// routine.  The states themselves are in harry_states.cpp.
#include "game/play_logic.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace game {

// ---------------------------------------------------------------- sprite slots

const char* spriteSlotName(uint32_t va) {
    int lo = 0, hi = kSpriteSlotCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (kSpriteSlots[mid].va == va) return kSpriteSlots[mid].name;
        if (kSpriteSlots[mid].va < va) lo = mid + 1; else hi = mid - 1;
    }
    return "";
}

uint32_t spriteSlotOf(const char* name) {
    static std::unordered_map<std::string, uint32_t> map;
    if (map.empty())
        for (int i = 0; i < kSpriteSlotCount; ++i) map.emplace(kSpriteSlots[i].name, kSpriteSlots[i].va);
    const auto it = map.find(name ? name : "");
    return it == map.end() ? 0 : it->second;
}

// ---------------------------------------------------------------- memory helpers

uint8_t PlayLogic::mb(uint32_t va) const {
    const uint8_t* p = harryMem(va);
    if (!p) {
        static uint32_t last = 0;
        if (va != last) { std::fprintf(stderr, "harry: read of unextracted address %#x\n", va); last = va; }
        return 0xf0;                                   // ends any script that runs off its data
    }
    return *p;
}

uint16_t PlayLogic::mw(uint32_t va) const { return uint16_t(mb(va) | mb(va + 1) << 8); }
uint32_t PlayLogic::ml(uint32_t va) const { return uint32_t(mw(va)) | uint32_t(mw(va + 2)) << 16; }

void PlayLogic::addX(int32_t v) {
    // add.l to x.frac (the 16.16 position at +0 / +2)
    const int64_t p = (int64_t(h_.x()) << 16 | h_.xFrac()) + v;
    h_.x() = int16_t(p >> 16); h_.xFrac() = uint16_t(p);
}

void PlayLogic::addY(int32_t v) {
    const int64_t p = (int64_t(h_.y()) << 16 | h_.yFrac()) + v;
    h_.y() = int16_t(p >> 16); h_.yFrac() = uint16_t(p);
}

uint32_t PlayLogic::random() { return game_->random(); }

// ---------------------------------------------------------------- sound channels

bool PlayLogic::sound0(int id, int pri) {
    // FUN_004262d9 / FUN_0042633d (the PC ignores the pitch argument of the latter).
    if (h_.b(0x465b72)) return false;
    if (int8_t(uint8_t(pri) - ch0Pri_) < 0) {
        if (ch0Hold_ != 0 || game_->channelBusy(0)) return false;   // 0x4627c2, FUN_004262c8
    }
    ch0Pri_ = uint8_t(pri);
    ch0Id_ = uint8_t(id);
    game_->playChannel(0, id == 0x8000 ? 0x8000 : (id & 0xff));     // FUN_004262a7
    ch0Hold_ = 8;
    return true;
}

bool PlayLogic::sound1(int id, int pri) {
    // FUN_004261e2
    if (h_.b(0x465b72) || h_.w(0x46652f) != 0 || h_.w(0x46652b) != 0) return false;   // the stop-watch / chili music
    if (int8_t(uint8_t(pri) - ch1Pri_) < 0 && game_->channelBusy(1)) return false;
    ch1Id_ = uint8_t(id);                                           // DAT_004627c1
    ch1Pri_ = uint8_t(pri);
    sound1Now(id);
    return true;
}

void PlayLogic::sound1Now(int id) {
    // FUN_004261b0: channel 1 without the priority test (0x8000 / 0x89 stop it)
    game_->playChannel(1, id == 0x8000 ? 0x8000 : (id & 0xff));
}

void PlayLogic::stopSound0() {
    // FUN_0042624a
    game_->playChannel(0, 0x8000);
    ch0Id_ = 0; ch0Pri_ = 0;                                        // 0x4627bf / 0x4627be (0x4627b8 / ba: unused here)
}

bool PlayLogic::channel0Busy() const { return game_->channelBusy(0); }   // FUN_004262c8
bool PlayLogic::channel1Busy() const { return game_->channelBusy(1); }   // FUN_004261d1

// ---------------------------------------------------------------- the joypad

void PlayLogic::readPad() {
    // FUN_004263f3
    Harry& p = h_;
    p.l(0x466765) = 0;
    p.l(0x466761) = 0;
    const uint32_t joy = game_->state().joypad;
    uint32_t d0 = joy ^ 0xffffu;                                   // FUN_004263e5: not ax
    if (d0 != 0) {
        d0 ^= 0xffffffffu;
        if ((d0 & 0x80000000u) == 0 && h_.b(0x466501) == 0) {
            // Weapon keys (never set by the PC's joypad builder, kept for completeness).
            const uint32_t k = d0 & 0x7000;
            PlayerStats& st = game_->stats();
            if (k == 0x4000) { st.weapon = 0; if (st.ammoCount[0]) p.b(0x466761) = 0xff; }
            if (k == 0x2000) { st.weapon = 1; if (st.ammoCount[1]) p.b(0x466761) = 0xff; }
            if (k == 0x1000) { st.weapon = 2; if (st.ammoCount[2]) p.b(0x466761) = 0xff; }
        }
        p.b(0x466764) = uint8_t(d0 & 0x80);
        if (p.b(0x466761) == 0) p.b(0x466761) = uint8_t(d0 & 0x40);
        p.b(0x466763) = uint8_t(d0 & 0x20);
        p.b(0x466762) = uint8_t(d0 & 0x10);
        p.b(0x466768) = uint8_t(d0 & 0x08);
        p.b(0x466767) = uint8_t(d0 & 0x04);
        p.b(0x466766) = uint8_t(d0 & 0x02);
        p.b(0x466765) = uint8_t(d0 & 0x01);
        if (p.b(0x466765) == 0) p.b(0x466760) = 0;
        if (p.b(0x466765) && p.b(0x466766)) p.w(0x466765) = 0;
        if (p.b(0x466767) && p.b(0x466768)) p.w(0x466767) = 0;
    }
    scriptedPad();                                                 // FUN_004346a7
    padOverrides();                                                // FUN_0043426d
    // (The button layout remap of DAT_00462695 follows here; the PC never sets it.)
    if (h_.b(0x465b38)) { const uint8_t u = p.b(0x466765); p.b(0x466765) = p.b(0x466766); p.b(0x466766) = u; }   // upside down
    // Edges: 1 on the press, kept (the state code marks a used press 0x80) until released.
    auto edge = [&](uint32_t held, uint32_t e) { if (p.b(held)) { if (p.b(e) == 0) p.b(e) = 1; } else p.b(e) = 0; };
    edge(0x466761, 0x466769);
    edge(0x466762, 0x46676a);
    edge(0x466763, 0x46676b);
    auto edge2 = [&](uint32_t held, uint32_t e) { if (p.b(held)) p.b(e) = p.b(e) ? 0x80 : 1; else p.b(e) = 0; };
    edge2(0x466764, 0x46676f);
    edge2(0x466761, 0x46676c);
    edge2(0x466762, 0x46676d);
    edge2(0x466763, 0x46676e);
}

void PlayLogic::scriptedPad() {
    // FUN_004346a7: DAT_00466501 == 0xff plays the input script at DAT_00466549 (the
    // cutscenes); not used by the ported levels yet.
    if (h_.b(0x466501) != 0xff) return;
}

void PlayLogic::padOverrides() {
    // FUN_0043426d
    Harry& p = h_;
    if (h_.b(0x465b3c)) {
        // Walked out by force (a door, the end of a level): keep going the way he faces.
        h_.b(0x465b3c) -= 1;
        p.l(0x466765) = 0;
        p.l(0x466761) = 0;
        p.uw(0x466767) = h_.facing() ? 0x0001 : 0x0100;
        return;
    }
    if (h_.b(0x465d49) == 0 || h_.b(0x466795) != 0) return;       // DAT_00465d49 / DAT_00466795
    if (h_.cont() != 0x4299ad) {
        p.l(0x466765) = 0;
        p.w(0x466761) = 0;
        p.b(0x466763) = 0;
    }
    if (h_.b(0x466547)) return;                                    // DAT_00466547
    const int lv = game_->levelNumber();
    if (lv == 10) {
        h_.b(0x465d49) += 1;
        if (int8_t(h_.b(0x465d49) - 0x30) < 0) return;
        if (int8_t(h_.b(0x465d49) - 0x50) >= 0) { h_.b(0x465d49) = 0; return; }
        if (h_.b(0x465d49) == 0x40) sound1(0x4f, 0x12);
        p.b(0x466768) = 0xff;
        return;
    }
    if (h_.b(0x466507) == 0 && lv == 0) {
        // The Ceiba Jungle outro: 0x50 ticks without input, then walk left to the exit.
        if (h_.b(0x465d49) != 0x50) { h_.b(0x465d49) += 1; return; }
        p.b(0x466767) = 0xff;
        return;
    }
    if (lv == 5) { p.b(0x466768) = 0xff; return; }
    if (lv == 8) {
        p.b(0x466768) = 0xff;
        if (h_.y() == 0x132) h_.b(0x465d49) = 0;
        return;
    }
    // Levels 14 and 7 (the continue screen and the mine-cart ride) script more; not ported yet.
}

// ---------------------------------------------------------------- triggers and floors

Trg PlayLogic::trg(int x, int y) const {
    // FUN_004251f9
    Trg r;
    const int tx = int16_t(x + 0x20);
    if (tx < 0) return r;
    if (int16_t(y) < 4) y = 4;
    const ph::ColumnEntry* e = game_->level().triggers.find(tx >> 4, (y & 0xffff) >> 4 & 0xff);
    if (!e) return r;
    r.id = e->id;
    r.raw = h_.trgClass()[e->id];
    r.found = true;
    r.cls = r.raw >= 0xf0 ? 0 : r.raw;
    return r;
}

bool PlayLogic::floorAt(bool plane2, bool ground, int x, int& y) const {
    // FUN_0042571f / FUN_004258a2 (ground) and FUN_00425a25 / FUN_00425bca (air)
    const ph::ColumnTable& t = plane2 ? dt2() : det();
    const ph::FloorHit f = ground ? ph::probeCell(t, x, y) : ph::probeLanding(t, x, y);
    if (!f.hit) return false;
    y = f.surfaceY;
    return true;
}

bool PlayLogic::feet() { return feetAt(h_.x() - 0x10, h_.y() + 0xe); }   // FUN_00425455

bool PlayLogic::feetAt(int d0, int d1) {
    // FUN_0042547d: the floor under the probe point; sets y from it and returns d4.
    if (h_.b(0x465b73)) {                                          // standing on an object
        if (h_.cont() == 0x428e50) h_.w(0x465b41) = 0x1e;
        return true;
    }
    if (h_.y() < 0) return false;
    const bool plane2 = h_.b(0x465bb3) != 0;
    bool found;
    if (h_.onGround()) {
        int y = d1;
        found = floorAt(plane2, true, d0, y);
        if (!found) { y = d1 + 0x10; found = floorAt(plane2, true, d0, y); }
        if (!found) { y = d1 - 0x10; found = floorAt(plane2, true, d0, y); }
        if (!found) y = d1;
        h_.y() = int16_t(y - 0xe);
        return found;
    }
    int y = d1 - 3;
    found = floorAt(plane2, false, d0, y);
    if (!found) { y = d1 + 3; found = floorAt(plane2, false, d0, y); }
    if (!found) {
        y = d1;
        if (h_.w(0x465baa) >= 3) {                                 // falling 3 px/tick or faster
            y = d1 - 0xc;
            found = floorAt(plane2, true, d0, y);
            if (!found) y = d1;
        }
    }
    h_.y() = int16_t(y - 0xe);
    return found;
}

bool PlayLogic::snapFeet(bool plane2) {
    // FUN_0042562b / FUN_004256a5: FUN_00425d6f / FUN_00425f88 at the feet.
    if (h_.b(0x465b73)) return true;
    if (h_.y() < 0) return false;
    const ph::FloorHit f = ph::probeNear(plane2 ? dt2() : det(), h_.x() - 0x10, h_.y() + 0xe);
    if (!f.hit) return false;
    h_.y() = int16_t(f.surfaceY - 0xe);
    return true;
}

bool PlayLogic::climbableHere() {
    // FUN_00422ee0: a climbing object (DAT_00466508) or class bit 3 at y-0x38 and y-0x20.
    if (h_.w(0x466508)) { h_.x() = h_.w(0x466508); return true; }
    if (!(trg(h_.x() - 0x10, h_.y() - 0x38).cls & 8)) return false;
    return (trg(h_.x() - 0x10, h_.y() - 0x20).cls & 8) != 0;
}

bool PlayLogic::ropeHere() {
    // FUN_00422f93: ids 0x19 / 0x1a / 0xf9 at y-0x38 and y-0x20.
    if (h_.w(0x466508)) { h_.x() = h_.w(0x466508); return true; }
    auto rope = [&](uint8_t id) { return id == 0x19 || id == 0x1a || id == 0xf9; };
    if (!rope(trg(h_.x() - 0x10, h_.y() - 0x38).id)) return false;
    return rope(trg(h_.x() - 0x10, h_.y() - 0x20).id);
}

bool PlayLogic::grabClimbable() {
    // FUN_004253a8: not right where he was hurt; then FUN_00422ee0.
    if (h_.b(0x465b77)) {
        h_.b(0x465b77) -= 1;
        if (h_.b(0x465b77) == 0) h_.w(0x4658bc) = 0;
        const int d = int16_t(h_.w(0x4658bc) - h_.x());
        if (d >= -0x10 && d < 0x10) return false;
        h_.b(0x465b77) = 0;
    }
    if (!climbableHere()) return false;
    h_.b(0x465b76) = 0xb;
    h_.b(0x465b47) = 1;
    h_.x() = int16_t((h_.x() & 0xfff0) + 4);
    h_.vx() = 0; h_.vy() = 0;
    return true;
}

// ---------------------------------------------------------------- animation interpreters

bool PlayLogic::hAnim(uint32_t cursor, uint32_t script, int ticks) {
    // FUN_00423134 with a6 = Harry: cursor = position byte, tick byte.
    FieldRef<uint8_t> pos = h_.b(cursor), tick = h_.b(cursor + 1);
    tick += 1;
    if (int8_t(uint8_t(ticks) - tick) < 0) { tick = 0; pos += 1; }
    return hAnimPeek(cursor, script);
}

bool PlayLogic::hAnimPeek(uint32_t cursor, uint32_t script) {
    // FUN_00423150
    FieldRef<uint8_t> pos = h_.b(cursor), tick = h_.b(cursor + 1);
    for (int guard = 0; guard < 64; ++guard) {
        const uint32_t p = script + pos;
        const uint8_t b = mb(p);
        if (b < 0x80) { h_.frame() = b; return false; }
        switch (b) {
        case 0xf0:
            h_.w(cursor) = 0;
            hAnimPeek(cursor, script);
            return true;
        case 0xfe:
            if (mb(p + 1) == ch1Id_) { sound1Now(0x89); ch1Id_ = 0; }   // stop my channel-1 sound
            pos += 2; tick = 0; continue;
        case 0xfc: sound0(mb(p + 1), mb(p + 2)); pos += 3; tick = 0; continue;
        case 0xfd: sound1(mb(p + 1), mb(p + 2)); pos += 3; tick = 0; continue;
        case 0xee: case 0xef: pos += 3; tick = 0; continue;       // near-Harry sounds: Harry is always near himself
        case 0xfb: case 0xed: {
            random();
            sound0(mb(p + 1), mb(p + 5));
            pos += 6; tick = 0; continue;
        }
        case 0xf7: h_.b(0x465b1a) ^= 1; pos += 1; tick = 0; continue;
        case 0xf9: h_.b(0x465b38) = 0; pos += 1; tick = 0; continue;
        default: h_.frame() = b; return false;
        }
    }
    return false;
}

bool PlayLogic::hAnim2(uint32_t cursor, int ticks) {
    // FUN_004237a5 with a6 = Harry: pc = the long at cursor, tick = the byte at cursor + 4.
    // Left and right are swapped while he faces left, so the scripts' pad tests mean
    // "forward" and "back".
    auto swapLR = [&] { const uint8_t l = h_.b(0x466767); h_.b(0x466767) = h_.b(0x466768); h_.b(0x466768) = l; };
    if (h_.b(0x465b1a)) swapLR();
    FieldRef<uint8_t> tick = h_.b(cursor + 4);
    tick += 1;
    if (int8_t(uint8_t(ticks) - tick) < 0) { tick = 0; h_.setUl(cursor, h_.ul(cursor) + 1); }
    for (int guard = 0; guard < 64; ++guard) {
        const uint32_t pc = h_.ul(cursor);
        const uint8_t b = mb(pc);
        if (b < 0x80) break;
        switch (b) {
        case 0xf0:
            if (h_.b(0x465b1a)) swapLR();
            return true;
        case 0xfc:
            if (mb(pc + 1) == 0xb9 && h_.b(0x4667af)) sound0(0x17, 0x1d);
            else sound0(mb(pc + 1), mb(pc + 2));
            h_.setUl(cursor, pc + 3); continue;
        case 0xfd: sound1(mb(pc + 1), mb(pc + 2)); h_.setUl(cursor, pc + 3); continue;
        case 0xfb: case 0xed:
            random();
            sound0(mb(pc + 1), mb(pc + 5));
            h_.setUl(cursor, pc + 6); continue;
        case 0xf8:
            if (h_.b(0x465b96) == 0) return false;                 // (sic: the pad stays swapped)
            h_.b(0x465b96) = 0;
            h_.setUl(cursor, pc + 1); continue;
        case 0xf9: h_.b(0x465b38) ^= 1; h_.setUl(cursor, pc + 1); continue;
        case 0xf7:
            h_.b(0x465b1a) = h_.b(0x465b1a) ? 0 : 0xff;
            h_.setUl(cursor, pc + 1);
            swapLR();
            continue;
        case 0xfa: case 0xf6: {
            // Throw a stone: FUN_0043124c spawns it, the 12 bytes after the opcode place it
            // {dx.w, dy.w, vx.l, vy.l}; 0xfa after spinning the sling is the power shot.
            Obj* a5 = h43124c();
            if (a5 && b == 0xfa && h_.b(0x465b4c)) { sound0(0x44, 9); a5->b(0x54) = 0xff; }
            if (a5) {
                const uint32_t a1 = pc + 1;
                int16_t d2 = int16_t(mw(a1));
                if (h_.b(0x4667af)) { d2 = 6; a5->y += 5; }
                a5->x += d2;
                if (h_.facing()) a5->x -= int16_t(d2 * 2);
                a5->y += int16_t(mw(a1 + 2));
                a5->l(0x32) += int32_t(ml(a1 + 4));
                if (h_.facing()) a5->l(0x32) = -a5->l(0x32);
                int32_t vy = int32_t(ml(a1 + 8));
                vy += int32_t(random() & 0x7fff) - 0x4000;
                a5->l(0x36) += vy;
                if (h_.b(0x4667af)) {
                    a5->l(0x36) = 0;
                    a5->l(0x32) = 0x40000;
                    a5->l(0x5c) = h_.onGround() ? int32_t(0xfffd4000) : 0;
                    a5->x += 0x14;
                    if (h_.facing()) { a5->l(0x32) = -a5->l(0x32); a5->x -= 0x28; }
                    a5->l(0x32) += h_.vx();
                }
            }
            h_.setUl(cursor, pc + 13);
            continue;
        }
        case 0xf4: case 0xf5: case 0xf2: {
            const uint8_t bits = padBits(), m = mb(pc + 1);
            const bool jump = b == 0xf4 ? (bits & m) != 0 : b == 0xf5 ? (bits & m) == 0 : bits == m;
            h_.setUl(cursor, jump ? ml(pc + 2) : pc + 6);
            continue;
        }
        case 0xf3: h_.setUl(cursor, ml(pc + 1)); continue;
        case 0xf1: h_.setSpriteSlot(ml(pc + 1)); h_.setUl(cursor, pc + 5); continue;
        default: goto frame;
        }
    }
frame:
    h_.frame() = mb(h_.ul(cursor));
    if (h_.b(0x465b1a)) swapLR();
    return false;
}

uint8_t PlayLogic::padBits() const {
    // FUN_00423f46: the held bytes keep the raw joypad bits on the PC, so these masks
    // (written for the Genesis 0xff bytes) only see a weapon key's 0xff.
    const Harry& p = h_;
    return uint8_t((p.b(0x466761) & 1) + (p.b(0x466762) & 2) + (p.b(0x466763) & 4) + (p.b(0x466765) & 8) +
                   (p.b(0x466766) & 0x10) + (p.b(0x466767) & 0x40) + (p.b(0x466768) & 0x20));
}

Obj* PlayLogic::newObjAtHarry(int d0, bool tail) {
    // FUN_004356bc / FUN_0043546e with a6 = Harry: a new record at the head / the tail of the
    // object list, at Harry's x / y.
    Obj* o = tail ? objects_.allocTail() : objects_.allocHead();
    if (!o) return nullptr;
    if (d0 != 0) o->w(0x26) = 0x6663;                              // FUN_004263a6
    o->x = h_.x();
    o->y = h_.y();
    return o;
}

Obj* PlayLogic::spawnTemplateAtHarry(uint32_t t) {
    // FUN_004351c5: {type, frame, flags.w, sprite slot.l, d0} at a3
    Obj* o = newObjAtHarry(mb(t + 8), true);
    if (!o) return nullptr;
    o->type = mb(t);
    o->frame = mb(t + 1);
    o->flags = mw(t + 2);
    o->sprite = game_->findSprite(spriteSlotName(ml(t + 4)));
    return o;
}

const ph::ColumnTable& PlayLogic::det() const { return game_->level().collision; }
const ph::ColumnTable& PlayLogic::dt2() const {
    const ph::LevelData& lv = game_->level();
    return lv.collision2.empty() ? lv.collision : lv.collision2;
}

// ---------------------------------------------------------------- hurt

bool PlayLogic::hurtHarry(int d5) {
    // FUN_00428233
    if (h_.invulnerable() != 0 || h_.b(0x465b72) || h_.b(0x465b01)) return false;
    const uint32_t c = h_.cont();
    static const uint32_t kKeep[] = {0x42aa3b, 0x42a048, 0x42a017, 0x42acc2, 0x429fe2, 0x42fa1a, 0x42fadf, 0x428aaa,
                                     0x42dea6, 0x42df5b, 0x42dd10, 0x42bda7, 0x42ddc8, 0x42d781, 0x42cc7a, 0x42de4f, 0x42dd7e};
    static const uint32_t kImmune[] = {0x42a217, 0x42a174, 0x430e3a, 0x42a36f, 0x426eaa, 0x426fee};
    if (std::find(std::begin(kImmune), std::end(kImmune), c) != std::end(kImmune)) return false;
    if (std::find(std::begin(kKeep), std::end(kKeep), c) == std::end(kKeep)) {
        if (h_.onGround()) {
            if (h_.b(0x465b71) >= 4) {
                h_.setCont(0x428588);                              // crouched
                h_.w(0x465b47) = 0;
            } else if (c == 0x42e297 || c == 0x428e50 || c == 0x42ea8b) {
                h_.setUl(0x465b49, c);
                h_.setCont(0x428505);                              // flinch (hyirn2sn)
                h_.w(0x465b47) = 0;
            }
        } else {
            h_.setCont(0x42f88c);                                  // knocked out of the air
            h_.w(0x465b47) = 0;
            h_.b(0x465b77) = 0x1e;
            h_.w(0x4658bc) = h_.x();
        }
    }
    const uint32_t r = random() & 3;
    static const int kSnd[4] = {0x17, 0x2f, 0x30, 0x31};
    sound0(kSnd[r], 0xc);
    int d = int(h_.damage()) + int16_t(d5);
    if (int16_t(d - 0xa0) >= 0) d = 0xa0;
    h_.damage() = uint8_t(d);
    h_.invulnerable() = 0x46;
    if (int16_t(d5) >= 0x46) h_.invulnerable() = int16_t(int16_t(d5) + 0x28);
    return true;
}

} // namespace game
