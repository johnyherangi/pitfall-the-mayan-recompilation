#include "ph/tables.h"

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
// The column-table walk of the three probes: entries are (row, id) pairs in row order.
// The original indexes the column with (x >> 3) & 0xfffe as an unsigned word, so a negative
// x reads past the table; here it simply finds nothing.
int surfaceAt(uint8_t id, int x, int row) {
    return row * 16 + kCollisionHeights[id * 16 + (x & 15)] - 1;
}
}

FloorHit probeCell(const ColumnTable& t, int x, int y) {
    FloorHit r;
    if (y < 2 || x < 0) return r;                    // cmp d1.w,2 / js
    const int row = (y & 0xffff) >> 4;
    for (const ColumnEntry& e : t.column(x >> 4)) {
        if (e.row == row) { r.hit = true; r.surfaceY = surfaceAt(e.id, x, row); r.shape = e.id; return r; }
        if (row < e.row) break;
    }
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
    if (y < 0x12 || x < 0) return r;                 // cmp d1.w,0x12 / js
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

} // namespace ph

namespace ph {
const SpriteHitbox* hitbox(const char* sprite) {
    if (!sprite || !*sprite) return nullptr;
    for (int i = 0; i < kHitboxCount; ++i)
        if (std::strcmp(kHitboxes[i].sprite, sprite) == 0) return &kHitboxes[i];
    return nullptr;
}
} // namespace ph
