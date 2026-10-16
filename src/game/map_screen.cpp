// Level 19: the map screen between two levels.  "*4" (a level's exit, 0x430912, which keeps the
// level in DAT_0046b1ac) loads it; FUN_00435fc0 starts it as any level, and the end of the restart
// (0x432cd6) runs its set-up (0x431c99), which runs on into a first FUN_00432038; FUN_0043628c then
// runs the joypad and FUN_00432038 in place of the usual tick.  The camera pans along the path from the level left to the next one,
// a peg every 16 ticks, then the next level's name rises from the bottom; a button or a key
// (after the first 30 ticks) or 180 ticks at the end go on ("*1", the level DAT_0045050c).
//
// The tables: 0x46aa34 the path by the level left, 0x46aa4e the next level by the path, 0x461c44
// the paths ({x, y, frame | 0x8000 h-flip | 0x4000 v-flip} words, -1 ending one), 0x46a88e the
// names by the level left.  The camera is kept 0x200 up and left of its position at
// 0x4661e0 / 0x4661e4 (16.16, int word first), its step (a sixteenth of the way to the next
// point) at 0x4661d6 / 0x4661da; 0x4661ea / 0x4661ee run at twice the speed (the spawner's last
// column and row, which nothing scans here).
#include "game/play_logic.h"

#include <algorithm>
#include <cstring>

namespace game {

namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(le16(p)) | uint32_t(le16(p + 2)) << 16; }
const uint8_t* mapPath(uint32_t va) {                    // 0x461c44 ..
    const uint32_t off = va - 0x461c44;
    return off < 0x348 ? kData_mapPaths + off : nullptr;
}
}

uint32_t PlayLogic::fixed(uint32_t a) const { return uint32_t(h_.uw(a)) << 16 | h_.uw(a + 2); }
void PlayLogic::setFixed(uint32_t a, uint32_t v) { h_.uw(a) = uint16_t(v >> 16); h_.uw(a + 2) = uint16_t(v); }

void PlayLogic::listAll() {
    // FUN_00435174: Harry's display entry, then every object's from the tail of the list.
    h_.w(0x465886) = 0x7000;
    displayEntry(h_.x(), h_.y(), h_.frame(), h_.facing(), h_.b(0x465b38), h_.flags(), h_.spriteRecord());
    const std::vector<Obj*> objs = objects_.list();
    for (auto it = objs.rbegin(); it != objs.rend(); ++it) displayEntry(**it);
}

void PlayLogic::mapPeg(Obj& o, uint32_t va) {
    // A peg of the path at va (the set-up and 0x432152), in a record FUN_004356bc gave: the
    // sprite pegs at (x - 0x10, y - 0x10), its frame and flips from the third word; type 1 does
    // nothing.
    o.setSprite(game_->findSprite("pegs"));
    const uint8_t* p = mapPath(va);
    if (!p) return;
    o.x = int16_t(le16(p) - 0x200 + 0x1f0);
    o.y = int16_t(le16(p + 2) - 0x200 + 0x1f0);
    const uint16_t f = le16(p + 4);
    o.frame = uint8_t(f);
    if (f & 0x8000) o.facing = 0xff;
    if (f & 0x4000) o.vflip = 0xff;
    o.type = 1;
    o.flags = 0x2000;
}

void PlayLogic::mapSetup() {
    // 0x431c99
    mapEnd_ = 0xb4;                                           // DAT_004601c0
    mapNamed_ = 0;                                            // DAT_0046a87c
    mapKeysOff_ = 0x1e;                                       // DAT_0046a88a
    h_.b(0x466761) = 0; h_.b(0x466762) = 0; h_.b(0x466763) = 0; h_.b(0x466764) = 0;
    h_.b(0x466189) = 0;
    h_.w(0x4658b4) = 0x200;
    h_.w(0x4658b6) = 0x200;
    h_.uw(0x465ae0) = 0x8b00;
    levelVars();                                              // FUN_00433116
    resetObjects();                                           // FUN_00435274
    h_.frame() = 0xff;                                        // (Harry is not shown)
    h_.b(0x466554) = 0xff;
    h_.b(0x465d48) = 0;
    // FUN_004263a2(0xf3) is a stub; DAT_004627bd = 0 the Genesis music byte
    const uint8_t idx = kData_mapLevels[std::min(game_->tourLevel(), 0x19)];   // 0x46aa34
    game_->setNextLevel(kData_mapLevels[0x1a + std::min<int>(idx, 0x22)]);        // 0x46aa4e: DAT_0045050c
    h_.l(0x46618a) = idx * 4;
    // the pegs of the paths before this one, three at most
    const int from = idx >= 3 ? idx - 3 : 0;
    bool full = false;
    for (int k = from; k < idx && !full; ++k) {
        for (uint32_t va = le32(kData_mapPaths + k * 4); ; va += 6) {
            const uint8_t* p = mapPath(va);
            if (!p || le16(p) == 0xffff) break;
            Obj* o = newObjAtHarry(1);                        // FUN_004356bc
            if (!o) { full = true; break; }
            mapPeg(*o, va);
        }
    }
    h_.l(0x4661b8) = idx;
    const uint32_t path = le32(kData_mapPaths + idx * 4);
    h_.setUl(0x4661cc, path);
    h_.w(0x4661d0) = 0x28;
    h_.l(0x4661d6) = 0;
    h_.l(0x4661da) = 0;
    const uint8_t* p = mapPath(path);
    const uint16_t px = p ? le16(p) : 0, py = p ? le16(p + 2) : 0;
    h_.w(0x4658b4) = int16_t(px - 0x200 + 0x15a);
    h_.w(0x4661e0) = int16_t(h_.w(0x4658b4) + 0x200);
    h_.w(0x4661e2) = 0;
    h_.w(0x4661ea) = h_.w(0x4661e0);
    h_.w(0x4661ec) = 0;
    h_.w(0x4658b6) = int16_t(py - 0x200 + 0x196);
    h_.w(0x4661e4) = int16_t(h_.w(0x4658b6) + 0x200);
    h_.w(0x4661e6) = 0;
    h_.w(0x4661ee) = h_.w(0x4661e4);
    h_.w(0x4661f0) = 0;
    h_.uw(0x465ae0) = 0x8b00;
    h_.w(0x46616a) = int16_t(-(h_.w(0x4658b4) + 0x200));
    h_.w(0x466168) = int16_t(-h_.w(0x4661ea));
    h_.w(0x465cf6) = int16_t(h_.w(0x4658b6) + 0x200);
    h_.w(0x465cf4) = h_.w(0x4661ee);
    h_.w(0x466550) = 2;
    mapTick();                                                // 0x432033: FUN_004263a5 (a stub), then on into FUN_00432038
}

void PlayLogic::mapAmbient() {
    // FUN_00431a67: every 0x80 .. 0xff ticks a jungle sound (0x9b .. 0xa1) on channel 1 when it is free.
    h_.w(0x465b04) -= 1;
    if (h_.w(0x465b04) >= 0) return;
    h_.w(0x465b04) = int16_t((random() & 0x7f) + 0x80);
    for (;;) {
        if (channel1Busy()) return;
        const uint32_t d0 = random() & 7;
        if (d0 == 7) continue;
        sound1(int(d0 + 0x9b), 0);
        return;
    }
}

void PlayLogic::mapText(const char* s, int y) {
    // FUN_00431ad5: the string in ltr_lg letters (type 2 objects at the head of the list, drawn in
    // front), centred on x 0x98 of the screen, at y.
    int total = 0;
    for (const char* c = s; *c; ++c) { int w; game_->bigGlyph(uint8_t(*c), w); total += w; }
    int x = 0x98 - (total >> 1) + h_.w(0x4658b4);             // DAT_0046a870 / 74
    const int yy = y + h_.w(0x4658b6);
    for (const char* c = s; *c; ++c) {
        int w;
        const uint8_t g = game_->bigGlyph(uint8_t(*c), w);
        Obj* o = newObjAtHarry(1);                            // FUN_004356bc (a6 = Harry here)
        if (o) {
            o->setSprite(game_->findSprite("ltr_lg"));
            o->x = int16_t(x);
            o->y = int16_t(yy);
            o->frame = g;
            o->type = 2;
            o->flags = 0x8000;
        }
        x += w;
    }
}

void PlayLogic::mapTick() {
    // FUN_00432038
    mapAmbient();                                             // FUN_00431a67
    h_.w(0x4661d0) -= 1;
    if (h_.w(0x4661d0) == 0) {
        const uint32_t va = h_.ul(0x4661cc);
        const uint8_t* p = mapPath(va);
        if (!p || le16(p) == 0xffff) { mapArrived(); return; }
        // the next point: a sixteenth of the way a tick, and its peg with a plink
        h_.l(0x4661d6) = int32_t((uint32_t(uint16_t(le16(p) + 0x15a)) << 16) - fixed(0x4661e0)) >> 4;
        h_.l(0x4661da) = int32_t((uint32_t(uint16_t(le16(p + 2) + 0x196)) << 16) - fixed(0x4661e4)) >> 4;
        h_.setUl(0x4661cc, va + 6);
        if (Obj* o = newObjAtHarry(1)) {                      // FUN_004356bc (none left: no peg, and no next point for a long while)
            h_.b(0x466189) ^= 1;
            sound0(h_.b(0x466189) ? 0xa : 9, 0xa);            // FUN_0042633d (the pitch 0x60 is ignored)
            mapPeg(*o, va);
            h_.w(0x4661d0) = 0x10;
        }
    }
    // 0x432213: the camera
    const uint32_t dx = uint32_t(h_.l(0x4661d6)), dy = uint32_t(h_.l(0x4661da));
    setFixed(0x4661e0, fixed(0x4661e0) + dx);
    setFixed(0x4661ea, fixed(0x4661ea) + dx + dx);
    h_.w(0x4658b4) = int16_t(h_.w(0x4661e0) - 0x200);
    setFixed(0x4661e4, fixed(0x4661e4) + dy);
    setFixed(0x4661ee, fixed(0x4661ee) + dy + dy);
    h_.w(0x4658b6) = int16_t(h_.w(0x4661e4) - 0x200);
    h_.uw(0x465ae0) = 0x8b00;
    h_.w(0x46616a) = int16_t(-(h_.w(0x4658b4) + 0x200));
    h_.w(0x466168) = int16_t(-h_.w(0x4661ea));
    h_.w(0x465cf6) = int16_t(h_.w(0x4658b6) + 0x200);
    h_.w(0x465cf4) = h_.w(0x4661ee);
    h_.w(0x466550) = 2;
    listAll();                                                // FUN_0043524d, FUN_00435174
    h_.b(0x466554) = 0xff;
    // the buttons and the keys (DAT_0046fb88) count after 30 ticks (the ones held then are forgotten)
    if (mapKeysOff_ != 0) {
        if (--mapKeysOff_ != 0) return;
        h_.b(0x466761) = 0; h_.b(0x466762) = 0; h_.b(0x466763) = 0; h_.b(0x466764) = 0;
        game_->startKey() = 0;
    }
    if (game_->startKey() || h_.b(0x466761) || h_.b(0x466762) || h_.b(0x466763) || h_.b(0x466764)) mapExit();
}

void PlayLogic::mapArrived() {
    // 0x4323b4: the end of the path: the next level's name rises to camY + 0xb4; a button or 180
    // ticks go on.
    if (mapNamed_ == 0) {
        mapNamed_ = 1;
        const uint32_t va = le32(kData_mapNames + std::min(game_->tourLevel(), 0x19) * 4);
        const uint32_t off = va - 0x46a88e;
        mapText(off < 0x150 ? reinterpret_cast<const char*>(kData_mapNames + off) : "", 0xfa);
    }
    if (h_.b(0x466764) || h_.b(0x466761) || h_.b(0x466762) || h_.b(0x466763)) { mapExit(); return; }
    h_.w(0x4661d0) = 1;
    // FUN_004324c5 (with the words at 0x46208e + path * 4 + 4 / + 6) is a stub
    mapAmbient();                                             // FUN_00431a67
    if (h_.b(0x466764)) { mapExit(); return; }
    for (Obj* o = objects_.first(); o; o = objects_.nextOf(*o))
        if (o->type == 2 && uint16_t(o->y) > uint16_t(h_.w(0x4658b6) + 0xb4)) o->y -= 3;
    listAll();                                                // FUN_0043524d, FUN_00435174
    h_.b(0x466554) = 0xff;
    if (--mapEnd_ < 0) mapExit();
}

void PlayLogic::mapExit() {
    // "*1": the engine loads DAT_0045050c (engineExit empties the display list first)
    game_->state().displayList.clear();
    specialExited_ = true;
    game_->command("*1");
}

} // namespace game
