// The game-logic camera (FUN_00404714) and the object spawner it drives (FUN_00403fa0 ..
// FUN_0040454e), the full spawn of the level start (FUN_00405114) and the objects the level
// start places by itself (FUN_00433d0b).
//
// The camera DAT_004658b4 / b6 runs after Harry with a lead (DAT_004658ba, -0x44 .. 0x44,
// towards where he faces) at 1/8 of the distance a tick sideways and 1/4 up and down, at most
// 7 px, inside the locks DAT_00465b09 / 0b / 0d.  When it crosses into another 16-px cell it
// flags that edge (DAT_00465892 top, 93 bottom, 94 left, 95 right) and the next tick that
// moves it the same way without crossing spawns the column or row coming into view, from the
// level's .als data ([DAT_004504e4]: a 4-byte header, a big-endian word per column with the
// offset of its (row, id) byte pairs, 0xff ending them).  A spawned object keeps a pointer to
// its entry (+0x1c) and the id (+0x20), and the entry's id is cleared while it lives; the
// handlers' exits put the id back.
#include "game/play_logic.h"

#include <algorithm>
#include <cstring>

namespace game {

namespace {
inline bool neg16(int v) { return int16_t(v) < 0; }
inline bool present(const ph::ObjectTemplate& t) { return t.sprite[0] || t.type != 0; }   // the 0x4678c4 slot is set
}

// [DAT_004504e4] + off, off taken as signed (a list's offset is a signed word): the load block
// past the 4 KB too.
uint16_t PlayLogic::alsWord(size_t off) const {
    return game_->arena().be16(int64_t(game_->arenaLayout().als) + int64_t(off));
}

uint8_t PlayLogic::alsByte(size_t off) const { return game_->arena().at(int64_t(game_->arenaLayout().als) + int64_t(off)); }

uint32_t PlayLogic::alsLong(size_t off) const {
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = v << 8 | alsByte(off + size_t(i));   // (little-endian, as the PC stores it)
    return v;
}

void PlayLogic::setAlsLong(size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        const uint8_t b = uint8_t(v >> (8 * i));
        game_->arena().put(int64_t(game_->arenaLayout().als) + int64_t(off) + i, &b, 1);
    }
}

// ---------------------------------------------------------------- FUN_00404714

void PlayLogic::camera() {
    Harry& h = h_;
    h.w(0x465c54) = 0;
    if (level_ == 0xe || h.cont() == 0x42756d) return;   // the map; the vaults' Simon game
    if (h.b(0x465c56) != 0) {
        // The Balankanche ride: the scroll speed to the left (not while an obstacle has Harry),
        // the vertical follow at 1 px a tick inside 0x202 .. 0x21e.
        const int16_t d0 = h.w(0x466182);
        if (h.b(0x46618a) != 0) return;
        scrollLeft(d0);
        cameraRideVertical();
        if (neg16(h.w(0x4658b6) - 0x202)) { h.w(0x4658b6) = 0x202; return; }
        if (!neg16(h.w(0x4658b6) - 0x21e)) h.w(0x4658b6) = 0x21e;
        return;
    }
    h.b(0x465e64) = 0;                                   // (its bits 2 / 3 lead to the stubs FUN_00405111 / 2)

    // Sideways, to Harry less the lead (level 10: half way to DAT_00465c58).
    int16_t d0;
    if (level_ == 10) d0 = int16_t(h.x() - (int16_t(h.x() - h.w(0x465c58)) >> 1) - 0x87 - h.w(0x4658b4));
    else d0 = int16_t(h.x() - h.w(0x4658ba) - 0x87 - h.w(0x4658b4));
    if (d0 < 0) scrollLeft(int16_t(std::min(uint16_t(-d0) >> 3, 7)));
    else scrollRight(int16_t(std::min(uint16_t(d0) >> 3, 7)));

    // The lead (0x40493f).
    auto ahead = [&](int step) {
        h.w(0x4658ba) += int16_t(step);
        if (!neg16(h.w(0x4658ba) - 0x44)) h.w(0x4658ba) = 0x44;
    };
    auto behind = [&](int step) {
        h.w(0x4658ba) -= int16_t(step);
        if (neg16(h.w(0x4658ba) + 0x44)) h.w(0x4658ba) = int16_t(0xffbc);
    };
    auto toward0 = [&] {
        if (h.w(0x4658ba) == 0) return;
        if (h.w(0x4658ba) > 0) behind(2); else ahead(2);
    };
    auto byFacing = [&] { if (h.facing() == 0) behind(2); else ahead(2); };
    const uint32_t c = h.cont();
    if (c == 0x429fe2 || c == 0x42de4f || c == 0x42d781) {
        // swinging or climbing: the pad leads
        h.w(0x4658ba) &= int16_t(0xfffe);
        if (h.b(0x466767)) ahead(2);
        else if (h.b(0x466768)) behind(2);
        else toward0();
    } else if (c == 0x42a048) {
        // on a rope: by the swing
        if (h.b(0x465b47) != 0) ahead(5); else behind(5);
    } else if (h.b(0x466507) != 0) {
        // a boss fight: towards the boss
        if (level_ == 3) {
            Obj* a0 = objAt(h.ul(0x465c58));
            Obj* a1 = objAt(h.ul(0x465c5c));
            int k = 0;
            if (h.l(0x465c58) != 0 && a0 && !neg16(a0->y - 0x6d)) k += 1;
            if (h.l(0x465c5c) != 0 && a1 && !neg16(a1->y - 0x6d)) k += 2;
            if (k == 0 || k == 3) toward0();
            else if (neg16(h.x() - (k == 1 ? a0 : a1)->x)) behind(2);
            else ahead(2);
        } else if (level_ == 8 || level_ == 0) {
            const Obj* b = bossObject();                    // the record at the address trg+0x1f4 holds
            if (b && neg16(h.x() - b->x)) behind(2); else ahead(2);
        } else {
            byFacing();
        }
    } else {
        byFacing();
    }

    // The vertical offset DAT_004658a6 (looking up or down) eases back (0x404b67).
    if (c != 0x42dd10 && c != 0x42bda7 && c != 0x42ddc8 && c != 0x42a048 && c != 0x429fe2 && h.b(0x465b71) != 0x16
        && h.w(0x4658a6) != 0) {
        if (h.w(0x4658a6) > 0) {
            h.w(0x4658a6) -= 3;
            if (neg16(h.w(0x4658a6))) h.w(0x4658a6) = 0;
        } else if (h.b(0x465ae2) != 0x2d) {
            h.w(0x4658a6) += 3;
            if (!neg16(h.w(0x4658a6))) h.w(0x4658a6) = 0;
        }
    }
    // Up and down (0x404bfd).
    int16_t up = int16_t(h.y() + h.w(0x4658a6) - 0x82);
    if (h.b(0x465b38)) up = int16_t(up + 0x48);
    up = int16_t(up - h.w(0x4658b6));
    if (up < 0) scrollUp(int16_t(std::min(uint16_t(-up) >> 2, 7)));
    int16_t dn = int16_t(h.y() + h.w(0x4658a6) - 0x8a);
    if (level_ == 7 && (h.b(0x465c56) != 0 || c == 0x42aa3b)) dn = int16_t(dn + 0x1c);
    if (h.b(0x465b38)) dn = int16_t(dn + 0x48);
    dn = int16_t(dn - h.w(0x4658b6));
    if (dn >= 0) scrollDown(int16_t(std::min(uint16_t(dn) >> 2, 7)));
}

void PlayLogic::cameraRideVertical() {
    // 0x404cf1: as the vertical follow above, without the offset and at 1 px a tick
    Harry& h = h_;
    int16_t up = int16_t(h.y() - 0x82);
    if (h.b(0x465b38)) up = int16_t(up + 0x48);
    up = int16_t(up - h.w(0x4658b6));
    if (up < 0) scrollUp(int16_t(std::min(uint16_t(-up) >> 2, 1)));
    int16_t dn = int16_t(h.y() - 0x8a);
    if (level_ == 7 && (h.b(0x465c56) != 0 || h.cont() == 0x42aa3b)) dn = int16_t(dn + 0x1c);
    if (h.b(0x465b38)) dn = int16_t(dn + 0x48);
    dn = int16_t(dn - h.w(0x4658b6));
    if (dn >= 0) scrollDown(int16_t(std::min(uint16_t(dn) >> 2, 1)));
}

void PlayLogic::scrollLeft(int16_t d0) {
    // 0x404dcb
    Harry& h = h_;
    if (h.cont() == 0x42bda7 && !neg16(d0 - 2)) d0 = 2;
    h.w(0x465d4c) = d0;
    const int16_t d1 = int16_t(h.w(0x4658b4) & 0xfff0);
    h.w(0x4658b4) -= d0;
    if (neg16(h.w(0x4658b4) - h.w(0x465b09))) { h.w(0x4658b4) = h.w(0x465b09); h.w(0x465d4c) = 0; return; }
    if (int16_t(d1 - (h.w(0x4658b4) & 0xfff0)) != 0) { h.b(0x465894) = 0xff; h.b(0x465e64) |= 2; return; }
    if (h.b(0x465894)) { spawnLeftColumn(); h.b(0x465894) = 0; }
}

void PlayLogic::scrollRight(int16_t d0) {
    // 0x404ea2: the right lock is tested before the move, so the camera can pass it for a tick
    Harry& h = h_;
    if (h.cont() == 0x42bda7 && !neg16(d0 - 2)) d0 = 2;
    h.w(0x465d4c) = int16_t(-d0);
    if (!(h.w(0x465b0b) > h.w(0x4658b4))) { h.w(0x4658b4) = h.w(0x465b0b); h.w(0x465d4c) = 0; return; }
    const int16_t d1 = int16_t(h.w(0x4658b4) & 0xfff0);
    h.w(0x4658b4) += d0;
    if (int16_t(d1 - (h.w(0x4658b4) & 0xfff0)) != 0) { h.b(0x465895) = 0xff; h.b(0x465e64) |= 2; return; }
    if (h.b(0x465895)) { spawnRightColumn(); h.b(0x465895) = 0; }
}

void PlayLogic::scrollUp(int16_t d0) {
    // 0x404f75 (2 px a tick on a ladder, states 0x42df5b / 0x42dea6)
    Harry& h = h_;
    if (h.cont() == 0x42df5b || h.cont() == 0x42dea6) d0 = 2;
    const int16_t d1 = int16_t(h.w(0x4658b6) & 0xfff0);
    h.w(0x4658b6) -= d0;
    h.w(0x465c54) -= d0;
    if (!(h.w(0x4658b6) > 0)) { h.w(0x4658b6) = 0; h.w(0x465c54) = 0; return; }
    if (int16_t((h.w(0x4658b6) & 0xfff0) - d1) != 0) {
        h.b(0x465892) = 0xff;
        if (h.b(0x465e64) != 0) h.b(0x465e64) |= 8;
        return;
    }
    if (h.b(0x465892)) { spawnTopRow(); h.b(0x465892) = 0; }
}

void PlayLogic::scrollDown(int16_t d0) {
    // 0x40503e: held a pixel above the bottom lock
    Harry& h = h_;
    const int16_t d1 = int16_t(h.w(0x4658b6) & 0xfff0);
    h.w(0x4658b6) += d0;
    h.w(0x465c54) += d0;
    if (!(h.w(0x465b0d) > h.w(0x4658b6))) {
        h.w(0x4658b6) = h.w(0x465b0d);
        h.w(0x465c54) = 0;
        h.w(0x4658b6) -= 1;
        if (neg16(h.w(0x4658b6))) h.w(0x4658b6) = 0;
        return;
    }
    if (int16_t((h.w(0x4658b6) & 0xfff0) - d1) != 0) {
        h.b(0x465893) = 0xff;
        if (h.b(0x465e64) != 0) h.b(0x465e64) |= 4;
        return;
    }
    if (h.b(0x465893)) { spawnBottomRow(); h.b(0x465893) = 0; }
}

// ---------------------------------------------------------------- the spawner

void PlayLogic::spawnLeftColumn() {
    // FUN_00403fa0: the column 0x40 px left of the camera
    int16_t d0 = int16_t(h_.w(0x4658b4) - 0x40);
    if (d0 < 0) d0 = 1;
    spawnColumn(uint16_t(d0) >> 4);
}

void PlayLogic::spawnRightColumn() {
    // FUN_00404016: the column 0x160 px right of it
    spawnColumn(uint16_t(h_.w(0x4658b4) + 0x160) >> 4);
}

void PlayLogic::keepAlsBase() {
    // Each spawner keeps the .als working copy's address (DAT_004504e4) in DAT_00466788 and the
    // templates' (0x4678c4) in DAT_0046678c; nothing clears them after it.
    h_.setUl(0x466788, ph::Arena::kAddress + game_->arenaLayout().als);
    h_.setUl(0x46678c, 0x4678c4);
}

void PlayLogic::spawnColumn(uint16_t col) {
    // 0x40407f: the column's entries in the rows (camY >> 4) - 3 < row <= (camY >> 4) + 0x10
    h_.w(0x466782) = int16_t(col);
    keepAlsBase();
    const uint16_t off = alsWord(size_t(int16_t(col * 2 + 4)));
    if (off == 0) return;
    const int16_t cy = int16_t(uint16_t(h_.w(0x4658b6)) >> 4);
    const int16_t d5 = int16_t(cy - 3), d4 = int16_t(cy + 0x10);
    for (size_t e = size_t(int16_t(off)); ; e += 2) {
        const uint8_t row = alsByte(e);
        if (!(d5 >= row)) {
            if (d4 < row) return;                        // the rows run down the column
            const uint8_t id = alsByte(e + 1);
            if (id != 0 && present(ph::kObjectTemplates[id])) {
                const ph::ObjectTemplate& t = ph::kObjectTemplates[id];
                Obj* o = t.head ? objects_.allocHead() : objects_.allocTail();   // FUN_00435905 / FUN_00435780
                if (!o) return;
                spawnEntry(*o, e, t);
            }
        }
        if (alsByte(e + 2) == 0xff) return;
    }
}

void PlayLogic::spawnTopRow() {
    // FUN_00404269: the row two above the camera
    spawnRow(int16_t((uint16_t(h_.w(0x4658b6)) >> 4) - 2));
}

void PlayLogic::spawnBottomRow() {
    // FUN_004042fe: the row 16 below its top
    spawnRow(int16_t((uint16_t(h_.w(0x4658b6)) >> 4) + 0x10));
}

void PlayLogic::spawnRow(int16_t row) {
    // 0x40438e: 29 columns from 0x40 px left of the camera (DAT_00466782 counts them), the first
    // entry of each in this row
    int16_t d0 = int16_t(h_.w(0x4658b4) - 0x40);
    if (d0 < 0) d0 = 1;
    const uint16_t col0 = uint16_t(d0) >> 4;
    h_.w(0x466782) = int16_t(col0);
    keepAlsBase();
    size_t a0 = size_t(int16_t(col0 * 2 + 4));
    for (int d7 = 0x1c; d7 >= 0; --d7, a0 += 2, h_.w(0x466782) += 1) {
        const uint16_t off = alsWord(a0);
        if (off == 0) continue;
        for (size_t e = size_t(int16_t(off)); ; e += 2) {
            const uint8_t r = alsByte(e);
            if (int16_t(row - r) < 0) break;
            if (int16_t(row - r) == 0 && alsByte(e + 1) != 0) {
                const ph::ObjectTemplate& t = ph::kObjectTemplates[alsByte(e + 1)];
                if (!present(t)) break;
                Obj* o = t.head ? objects_.allocHead() : objects_.allocTail();
                if (!o) return;
                spawnEntry(*o, e, t);
                break;
            }
            if (alsByte(e + 2) == 0xff) break;
        }
    }
}

void PlayLogic::spawnEntry(Obj& o, size_t e, const ph::ObjectTemplate& t) {
    // FUN_0040454e: the object at the entry's cell (column DAT_00466782), from its template
    o.x = int16_t((h_.w(0x466782) << 4) + 0x18 + t.xOff);
    o.y = int16_t((alsByte(e) << 4) - 0x10 + t.yOff);
    if (t.snap != 0) {
        const ph::FloorHit f = ph::probeNear(det(), o.x, int16_t(o.y + t.snap));   // FUN_00425d6f
        if (f.hit) o.y = int16_t(f.surfaceY - t.snap);
    }
    std::memset(o.s + 0x32, 0, 0x10);                    // +0x32 .. +0x41
    o.type = t.type;
    o.frame = t.sub;
    o.flags = t.flags;
    if (t.sprite[0]) o.setSprite(game_->slotSprite(t.sprite));    // [template + 4]: the slot, whose content goes to +0xe
    else o.setSprite(nullptr);
    o.entry = ph::Arena::kAddress + game_->arenaLayout().als + uint32_t(e);   // +0x1c
    o.id = alsByte(e + 1);                               // +0x20
    alsMem_[e + 1] = 0;
    if (t.soundSlot) o.w(0x26) = 0x6663;                 // FUN_004263a6 (it returns 0: the release after it never runs)
}

void PlayLogic::spawnAll() {
    // FUN_00405114: 33 columns from 0x70 px left of the camera, at the level start
    h_.b(0x465e64) = 0;
    h_.b(0x465892) = 0;
    h_.w(0x465d4e) = 0;
    const int16_t keep = h_.w(0x4658b4);
    h_.w(0x4658b4) = int16_t((keep - 0x30) & 0xfff0);
    for (int d7 = 0x20; d7 >= 0; --d7) {
        spawnLeftColumn();
        h_.w(0x4658b4) += 0x10;
    }
    h_.w(0x4658b4) = keep;
}

void PlayLogic::preSpawn() {
    // FUN_00433d0b: the .als ids without a template that the level start places itself:
    // 0xc9 the drifting platforms (type 0x6f), 0x15 / 0x16 the rope spawners (type 0xa9), and
    // 0x10 a pushed mine cart (type 19) where it was left (DAT_0046589c / 0046589e, when set).
    h_.w(0x466782) = 0;
    int d7 = h_.w(0x4658ac);
    if (level_ >= 0xb && level_ < 0xe) d7 = 0x1c;
    for (size_t a0 = 4; ; a0 += 2) {
        const uint16_t off = alsWord(a0);
        if (off != 0) {
            for (size_t e = off; alsByte(e) != 0xff; e += 2) {
                const uint8_t id = alsByte(e + 1);
                const int16_t col = h_.w(0x466782);
                if (id == 0xc9) {
                    if (Obj* o = objects_.allocTail()) {
                        o->frame = 0;
                        o->setSprite(game_->slotSprite("ob_float"));
                        o->flags = 0;
                        o->type = 0x6f;
                        o->x = int16_t(col << 4);
                        o->w(0x64) = o->x;
                        o->y = int16_t((alsByte(e) << 4) - 0x10);
                        o->w(0x32) = int16_t(h_.w(0x4658ac) << 4);
                        o->w(0x34) = int16_t(random() & 0x1e);
                    }
                }
                // (DAT_00466786 is left at 1 by every entry but a 0x15)
                h_.b(0x466786) = 0;
                bool rope = id == 0x15;
                if (!rope) { h_.b(0x466786) = 1; rope = id == 0x16; }
                if (rope) {
                    if (Obj* o = objects_.allocTail()) {
                        o->type = 0xa9;
                        o->b(0x32) = h_.b(0x466786);
                        o->x = int16_t((col << 4) + 0x20);
                        o->y = int16_t((alsByte(e) << 4) + 0x10);
                    }
                } else if (id == 0x10) {
                    const bool right = !neg16((col << 4) - 0x500);
                    if ((right ? h_.w(0x46589e) : h_.w(0x46589c)) != 0) {
                        if (Obj* o = objects_.allocTail()) {
                            // (it clears the entry's row byte, not its id: the spawner meets the
                            // entry again only when row 0 comes into view)
                            alsMem_[e] = 0;
                            o->y = 0;
                            o->type = 0x13;
                            o->setSprite(game_->slotSprite("ob_mcar"));
                            o->x = h_.w(0x46589c); o->y = 0xc9;
                            if (right) { o->x = h_.w(0x46589e); o->y = 0x151; }
                            o->frame = 0xff;
                            o->flags = 0x2000;
                        }
                    }
                }
            }
        }
        h_.w(0x466782) += 1;
        if (--d7 == 0) return;
    }
}

} // namespace game
