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

FloorHit probeCell(const ColumnTable& t, int x, int y) {
    FloorHit r;
    if (x < 0 || y < 0) return r;
    const int col = x >> 4, row = y >> 4;
    const ColumnEntry* e = t.find(col, row);
    if (!e || e->row != row) return r;
    const uint8_t h = kCollisionHeights[(e->id & 0x7f) * 16 + (x & 15)];
    if (h == 0) return r;
    r.hit = true; r.surfaceY = row * 16 + h - 1; r.shape = e->id;
    return r;
}

FloorHit probeNear(const ColumnTable& t, int x, int y) {
    // FUN_00425d6f: same row first, then the row above, then the row below.
    FloorHit r = probeCell(t, x, y);
    if (!r.hit && y >= 16) r = probeCell(t, x, y - 16);
    if (!r.hit) r = probeCell(t, x, y + 16);
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
