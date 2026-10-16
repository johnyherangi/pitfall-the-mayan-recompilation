// Level data tables: collision (.det / .dt2), object list (.als), triggers (.trg).
//
// All four are "column lists" in big-endian Genesis layout (the game logic
// half of PITFALL.EXE is translated 68000 code and keeps its data as-is):
//
//   u16be offset[columns]      one per 16-pixel column; 0 = empty column
//   lists: (u8 row, u8 id) pairs sorted by row, terminated by row 0xff
//          .trg only: (0xfe, rowFrom, rowTo, id) = a vertical range entry
//
// .det / .dt2 / .als have a 4-byte header before the offset table; .trg has
// none (FUN_0042571f, FUN_00433d0b vs FUN_004331d8 / FUN_004251f9).
//
// Collision: id selects a 16-byte row of kCollisionHeights; the byte for
// (x & 15) is the surface height h inside the 16x16 cell (0 = no surface),
// the surface pixel row being  row*16 + h - 1  (FUN_0042571f).
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace ph {

class Arena;

extern const uint8_t kCollisionHeights[256 * 16];   // 0x4668c4
extern const uint8_t kTriggerClass[255];             // 0x461f8e
extern const uint8_t kStartFacing[25];               // 0x433334 (0 right, 1 left)

struct ColumnEntry {
    uint8_t row;      // 16-pixel row
    uint8_t id;       // shape / object type / trigger type
    uint8_t rowTo;    // last row covered (inclusive); == row except for .trg range entries
};

class ColumnTable {
public:
    ColumnTable() = default;
    // `headerBytes` is 4 for det/dt2/als and 0 for trg; `columns` = level width / 16.
    ColumnTable(std::span<const uint8_t> blob, int headerBytes, int columns, bool ranges);

    int columns() const { return int(cols_.size()); }
    std::span<const ColumnEntry> column(int c) const {
        if (c < 0 || c >= columns()) return {};
        return {entries_.data() + cols_[size_t(c)].first, size_t(cols_[size_t(c)].count)};
    }
    // Entry whose row range contains `row` in column `c`, or nullptr.
    const ColumnEntry* find(int c, int row) const {
        for (const ColumnEntry& e : column(c)) {
            if (row < e.row) break;
            if (row <= e.rowTo) return &e;
        }
        return nullptr;
    }
    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }
    size_t indexOf(const ColumnEntry& e) const { return size_t(&e - entries_.data()); }
    const ColumnEntry& entry(size_t i) const { return entries_[i]; }

    // The table's place in the load block (ph::Arena): the probes then walk the block's bytes as
    // the original does, past the table too, and see what the game logic writes there.  An
    // unplaced table (no block) is walked from its entries.
    void place(const Arena* arena, uint32_t base) { arena_ = arena; base_ = base; }
    const Arena* arena() const { return arena_; }
    uint32_t base() const { return base_; }

private:
    struct Col { uint32_t first = 0, count = 0; };
    std::vector<Col> cols_;
    std::vector<ColumnEntry> entries_;
    const Arena* arena_ = nullptr;
    uint32_t base_ = 0;
};

// Object template (0x4678c4 table, one per .als id; FUN_0040454e copies it into a new object).
struct ObjectTemplate {
    uint8_t type;        // behaviour class (obj+8), selects the per-type handler
    uint8_t sub;         // initial frame / variant (obj+9), 0xff = hidden until the handler shows it
    uint16_t flags;      // obj+0xc
    uint8_t soundSlot;   // obj template +8: Genesis sound-channel class handed to FUN_004263a6 (a stub in the PC build that marks obj+0x26 = 0x6663); no gameplay effect
    uint8_t head;        // template +9: the spawner puts the object at the head of the list (FUN_00435905), else at the tail (FUN_00435780)
    int16_t xOff, yOff;  // added to col*16 + 0x18 and row*16 - 0x10
    int16_t snap;        // != 0: snap to the floor probed at y + snap
    const char* sprite;  // sprite name, "" when the type draws nothing itself
};
extern const ObjectTemplate kObjectTemplates[256];

// FUN_00447190 binds the Genesis sprite slots to a level's sprites by name; where the
// level's sprite is named differently from the slot (slot "obzpit2a" -> "obrpit2a" in
// level 3), the loader aliases the slot name to it.  Terminated by level -1.
struct SlotBinding { int level; const char* slot; const char* sprite; };
extern const SlotBinding kSlotBindings[];

// Collision box of a sprite relative to its object's (x, y) (table at 0x468824; FUN_00424af1
// tests two objects by overlapping their boxes).  A box with w == 0 never collides.
struct SpriteHitbox {
    const char* sprite;
    int16_t dx, w, dy, h;
    uint32_t va = 0;           // a box record of an object's own: its address in PITFALL.EXE
};
extern const SpriteHitbox kHitboxes[];
extern const int kHitboxCount;
const SpriteHitbox* hitbox(const char* sprite);  // nullptr when the sprite has none
// The box a sprite record's +8 word leads to (0x468824 + word: entry (word - 4) / 12); nullptr for
// 0xffff.  A sprite not loaded through FUN_004453a0 (word -1): its box by name.
class Sprite;
const SpriteHitbox* hitboxOf(const Sprite* s);

// Trigger ids with known meaning.
enum TriggerId : uint8_t {
    kTrgStart     = 0x5e,   // '^' Harry's start position: x = col*16 - 8, y = (row + 2) * 16
    kTrgExit      = 0x5f,   // '_' level complete -> "*4" (map screen)            FUN_00430229
    kTrgPlane1    = 0x10,   // collision plane .det (DAT_00465bb3 = 0)             FUN_00430229
    kTrgPlane2    = 0x11,   // collision plane .dt2 (DAT_00465bb3 = 0xff)
    kTrgFlip      = 0x18,   // toggles DAT_00465b38 (inverted gravity section)
    kTrgSwing     = 0x1d,   // sling / rope swing anchor                          FUN_0042babc
    kTrgBossExit  = 0x89,   // "*50": after the Warrior Spirit -> 2600 level / ending
    kTrgVault1    = 0x8c, kTrgVault2 = 0x8a, kTrgVault3 = 0x8b,   // "*51".."*53" Loltun vaults
    kTrgWater     = 0xd0,   // splash / drowning                                  FUN_00426810
};

// Object (.als) ids with known meaning (FUN_00433d0b pre-spawns these).
enum ObjectId : uint8_t {
    kObjMinecar = 0x10, kObjFloatA = 0x15, kObjFloatB = 0x16,
};

// Collision probe results (FUN_0042571f / FUN_00425d6f).
struct FloorHit {
    bool hit = false;
    int surfaceY = 0;     // pixel row of the surface
    uint8_t shape = 0;    // collision shape id
};

// Surface directly in the 16x16 cell containing (x, y) (FUN_0042571f / FUN_004258a2).
FloorHit probeCell(const ColumnTable& t, int x, int y);
// The topmost surface among the rows of y - 16, y and y + 16, for y >= 0x12
// (FUN_00425d6f / FUN_00425f88: the column's entries are walked in row order).
FloorHit probeNear(const ColumnTable& t, int x, int y);
// FUN_00425a25 / FUN_00425bca: like probeCell, but a surface in the lower half of the
// cell only counts while y is in the lower half too (Harry falling onto a floor).
FloorHit probeLanding(const ColumnTable& t, int x, int y);

// FUN_004251f9's walk of the trigger table: the id of the trigger under (x, y) (none when
// x + 0x20 is negative); found is false without one.
struct TriggerHit { bool found = false; uint8_t id = 0; };
TriggerHit triggerAt(const ColumnTable& t, int x, int y);

} // namespace ph
