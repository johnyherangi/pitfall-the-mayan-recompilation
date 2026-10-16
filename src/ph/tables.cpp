#include "ph/tables.h"

#include "ph/arena.h"
#include "ph/sprite.h"

#include <cstring>

namespace ph {

ColumnTable::ColumnTable(std::span<const uint8_t> blob, int headerBytes, int columns, bool ranges) {
    cols_.resize(size_t(columns));
    for (int c = 0; c < columns; ++c) {
        const size_t o = size_t(headerBytes) + size_t(c) * 2;
        if (o + 2 > blob.size()) break;
        size_t p = (size_t(blob[o]) << 8) | blob[o + 1];       // big-endian
        if (p == 0) continue;
        Col& col = cols_[size_t(c)];
        col.first = uint32_t(entries_.size());
        while (p < blob.size() && blob[p] != 0xff) {
            ColumnEntry e;
            if (ranges && blob[p] == 0xfe && p + 3 < blob.size()) {
                // FUN_004251f9: {0xfe, from, to, id} matches from <= row < to.
                e.row = blob[p + 1]; e.rowTo = uint8_t(blob[p + 2] ? blob[p + 2] - 1 : 0); e.id = blob[p + 3];
                p += 4;
            } else {
                if (p + 1 >= blob.size()) break;
                e.row = blob[p]; e.rowTo = e.row; e.id = blob[p + 1];
                p += 2;
            }
            entries_.push_back(e);
        }
        col.count = uint32_t(entries_.size()) - col.first;
    }
}

namespace {
int surfaceAt(uint8_t id, int x, int row) {
    return row * 16 + kCollisionHeights[id * 16 + (x & 15)] - 1;
}

// The probes of FUN_0042571f's family find a column's list in the load block as the original:
// the big-endian word at the table + ((x >> 3) & 0xfffe) + 4, 16-bit arithmetic (so a negative x
// reads 8 KB past the table's start, wherever that falls), then the table + that word taken as
// signed; a zero word is no list.  Their walks read the block on until a row decides, and give up
// where it ends (the original would read on past it).
uint32_t detColumn(int x) { return uint16_t(((uint16_t(x) >> 3) & 0xfffe) + 4); }

bool listStart(const ColumnTable& t, uint32_t off, int64_t& p) {
    const uint16_t w = t.arena()->be16(int64_t(t.base()) + off);
    if (w == 0) return false;
    p = int64_t(t.base()) + int16_t(w);
    return true;
}

// The entries' walk, for a table outside the block.
FloorHit cellFromEntries(const ColumnTable& t, int x, int y) {
    FloorHit r;
    if (x < 0) return r;
    const int row = (y & 0xffff) >> 4;
    for (const ColumnEntry& e : t.column(x >> 4)) {
        if (e.row == row) { r.hit = true; r.surfaceY = surfaceAt(e.id, x, row); r.shape = e.id; return r; }
        if (row < e.row) break;
    }
    return r;
}

FloorHit nearFromEntries(const ColumnTable& t, int x, int y) {
    FloorHit r;
    if (x < 0) return r;
    const int row = (y & 0xffff) >> 4;
    for (const ColumnEntry& e : t.column(x >> 4)) {
        int d;
        if (e.row == row + 1) d = 16;
        else if (e.row == row) d = 0;
        else if (e.row == row - 1) d = -16;
        else if (row - 1 < e.row) break;
        else continue;
        r.hit = true; r.surfaceY = surfaceAt(e.id, x, row) + d; r.shape = e.id;
        return r;
    }
    return r;
}
}

FloorHit probeCell(const ColumnTable& t, int x, int y) {
    FloorHit r;
    if (int16_t(y) < 2) return r;                    // cmp d1.w,2 / js
    if (!t.arena()) return cellFromEntries(t, x, y);
    const Arena& a = *t.arena();
    int64_t p;
    if (!listStart(t, detColumn(x), p)) return r;
    const uint16_t row = uint16_t(y) >> 4;
    for (;; p += 2) {
        if (!a.contains(p)) return r;
        const uint8_t b = a.at(p);
        if (row == b) break;
        if (row < b) return r;
    }
    const uint8_t id = a.at(p + 1);
    r.hit = true; r.shape = id;
    r.surfaceY = int16_t((uint16_t(y) & 0xfff0) + kCollisionHeights[id * 16 + (x & 15)] - 1);
    return r;
}

FloorHit probeLanding(const ColumnTable& t, int x, int y) {
    FloorHit r = probeCell(t, x, y);
    if (!r.hit) return r;
    const int h1 = kCollisionHeights[r.shape * 16 + (x & 15)] - 1;
    if ((h1 & 8) && !(y & 8)) r.hit = false;         // bt d4,3 / bt d1,3
    return r;
}

FloorHit probeNear(const ColumnTable& t, int x, int y) {
    FloorHit r;
    if (int16_t(y) < 0x12) return r;                 // cmp d1.w,0x12 / js
    if (!t.arena()) return nearFromEntries(t, x, y);
    const Arena& a = *t.arena();
    int64_t p;
    if (!listStart(t, detColumn(x), p)) return r;
    const uint16_t row = uint16_t(y) >> 4, above = uint16_t(row - 1), below = uint16_t(row + 1);
    int d = 0;
    for (;; p += 2) {
        if (!a.contains(p)) return r;
        const uint8_t b = a.at(p);
        if (below == b) { d = 16; break; }
        if (row == b) { d = 0; break; }
        if (above == b) { d = -16; break; }
        if (int16_t(above - b) < 0) return r;
    }
    const uint8_t id = a.at(p + 1);
    const uint8_t h = kCollisionHeights[id * 16 + (x & 15)];
    if (int16_t(h - uint16_t(y)) >= 0) return r;     // cmp d4.w,d2 (y, at least 0x12): never
    r.hit = true; r.shape = id;
    r.surfaceY = int16_t((uint16_t(y) & 0xfff0) + h - 1 + d);
    return r;
}

TriggerHit triggerAt(const ColumnTable& t, int x, int y) {
    TriggerHit r;
    const int16_t tx = int16_t(x + 0x20);
    if (tx < 0) return r;
    if (int16_t(y) < 4) y = 4;
    const uint16_t row = (uint16_t(y) >> 4) & 0xff;
    if (!t.arena()) {
        if (const ColumnEntry* e = t.find(tx >> 4, row)) { r.found = true; r.id = e->id; }
        return r;
    }
    // (no header: the column's word at the table + ((x + 0x20) >> 3) & 0xfffe)
    const Arena& a = *t.arena();
    int64_t p;
    if (!listStart(t, uint32_t(uint16_t(tx) >> 3) & 0xfffe, p)) return r;
    for (;;) {
        if (!a.contains(p)) return r;
        const uint8_t b = a.at(p);
        if (row == b) { r.found = true; r.id = a.at(p + 1); return r; }
        if (row < b) {
            // {0xfe, from, to, id}: from <= row < to
            if (b != 0xfe || row < a.at(p + 1)) return r;
            if (row < a.at(p + 2)) { r.found = true; r.id = a.at(p + 3); return r; }
            p += 4;
        } else {
            p += 2;
        }
    }
}

} // namespace ph

namespace ph {
const SpriteHitbox* hitboxOf(const Sprite* s) {
    if (!s) return nullptr;
    const int w = s->boxWord();
    if (w < 0) return hitbox(s->name().c_str());
    if (w == 0xffff || w < 4 || (w - 4) % 12 != 0 || (w - 4) / 12 >= kHitboxCount) return nullptr;
    return &kHitboxes[(w - 4) / 12];
}

const SpriteHitbox* hitbox(const char* sprite) {
    if (!sprite || !*sprite) return nullptr;
    for (int i = 0; i < kHitboxCount; ++i)
        if (std::strcmp(kHitboxes[i].sprite, sprite) == 0) return &kHitboxes[i];
    return nullptr;
}
} // namespace ph
