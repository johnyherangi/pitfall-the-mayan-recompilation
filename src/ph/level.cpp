#include "ph/level.h"

#include "ph/ph_stream.h"

#include <cctype>
#include <cstdio>
#include <filesystem>

namespace ph {

Background Background::parse(const std::string& name, std::span<const uint8_t> body) {
    if (body.size() < 20) throw Error("Bad background record: " + name);
    Background bg;
    bg.name = name;
    bg.wTiles = int(rd32(&body[0]));
    bg.hTiles = int(rd32(&body[4]));
    const uint32_t tilesOff = rd32(&body[8]);
    bg.tilesOffset = tilesOff;
    const uint32_t palOff = rd32(&body[12]);
    const uint32_t ncm1 = rd32(&body[16]);
    const size_t mapBytes = size_t(bg.wTiles) * bg.hTiles * 2;
    if (20 + mapBytes > body.size() || tilesOff > palOff || palOff + ncm1 * 3 > body.size())
        throw Error("Bad background record: " + name);
    bg.tilemap.resize(size_t(bg.wTiles) * bg.hTiles);
    for (size_t i = 0; i < bg.tilemap.size(); ++i) bg.tilemap[i] = rd16(&body[20 + i * 2]);
    bg.tiles.assign(body.begin() + tilesOff, body.begin() + palOff);
    bg.colourCount = int(ncm1) + 1;
    bg.localPalette.assign(body.begin() + palOff, body.begin() + palOff + ncm1 * 3);
    return bg;
}

void Background::bindPalette(Palette& pal) {
    // FUN_004460e0: DAT_004503c0[0] = 0; allocate(pal, ncolors, 1); remap tile bytes.
    // Local colour i (1-based) lives at localPalette[(i-1)*3], so pass pointer-3 for local 1.
    uint8_t remap[256] = {0};
    // localPalette[0..2] is the triplet of local colour 1 (allocate() starts at local 1).
    pal.allocate(localPalette.data(), colourCount, 1);
    for (int i = 0; i < 256; ++i) remap[i] = pal.remap()[i];
    remap[0] = 0;
    for (auto& px : tiles) px = remap[px];
}

LinearBitmap LinearBitmap::expand(const Background& bg) {
    LinearBitmap out;
    out.w = bg.wTiles * 8;
    out.h = bg.hTiles * 8;
    out.pixels.assign(size_t(out.w) * out.h, 0);
    const int W = out.w;
    for (int ty = 0; ty < bg.hTiles; ++ty) {
        for (int tx = 0; tx < bg.wTiles; ++tx) {
            const uint16_t e = bg.tilemap[size_t(ty) * bg.wTiles + tx];
            const int t = e & 0xfff;
            if (t >= bg.tileCount()) continue;
            const uint8_t* src = &bg.tiles[size_t(t) * 64];
            uint8_t* dst = &out.pixels[(size_t(ty) * 8) * W + tx * 8];
            int dir = 1;
            if (e & 0x4000) { dir = -1; dst += 7; }
            int stride = W;
            if (e & 0x2000) { dst += W * 7; stride = -W; }
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) { *dst = *src++; dst += dir; }
                dst += stride - dir * 8;
            }
        }
    }
    return out;
}

static std::string levelPath(const std::string& dir, int level) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "level%2.2d.ph", level);      // format string at 0x47112c
    return dir.empty() ? std::string(buf) : dir + "/" + buf;
}

namespace {
uint32_t align16(uint32_t n) { return (n + 15) & ~15u; }

// A background record as FUN_004460e0 leaves it: the bytes as read, the tiles in the palette's
// colours.
void putBackground(Arena* a, uint32_t at, std::span<const uint8_t> body, const Background& bg) {
    if (!a) return;
    a->put(at, body.data(), body.size());
    a->put(int64_t(at) + bg.tilesOffset, bg.tiles.data(), bg.tiles.size());
}

// A sprite record as FUN_004453a0 leaves it, up to its palette (where the next record goes):
// the pixels in the palette's colours, +4 and +8 replaced.  Returns where the next one goes.
uint32_t putSprite(Arena* a, uint32_t at, const Sprite& spr, const ArenaSprite* info) {
    const std::vector<uint8_t>& b = spr.bytes();
    const uint32_t palOff = rd32(&b[4]);
    if (a) {
        a->put(at, b.data(), palOff);
        if (info) {
            uint8_t name[4] = {uint8_t(info->name), uint8_t(info->name >> 8), uint8_t(info->name >> 16), uint8_t(info->name >> 24)};
            uint8_t box[2] = {uint8_t(info->box), uint8_t(info->box >> 8)};
            a->put(int64_t(at) + 4, name, 4);
            a->put(int64_t(at) + 8, box, 2);
        }
    }
    return at + palOff;
}
} // namespace

LevelData loadLevel(int level, const std::string& dataDir, Palette& pal, Arena* arena) {
    const LevelFiles& files = levelFiles().at(size_t(level));
    LevelData lv;
    lv.level = level;
    pal.releaseLevelAllocations();

    PhStream s = PhStream::fromFile(levelPath(dataDir, level));

    // The load block (FUN_004460e0): each record at the last one's end rounded up to 16 bytes
    // (`last` is what the original keeps in puVar19, the size of the last record read: a record
    // that is not there leaves it as it was), the objects' 4 KB cleared first, the parallax map
    // from the next 32-byte boundary past them (32 bytes on from one).
    ArenaLayout& lay = lv.arena;
    lay.base = arena ? arena->levelBase() : 0;           // DAT_004504e0
    uint32_t last = 0;
    auto loadBg = [&](const std::string& name, uint32_t at) {
        auto body = s.next();
        Background bg = Background::parse(name, body);
        bg.bindPalette(pal);
        putBackground(arena, at, body, bg);
        last = uint32_t(body.size());
        return bg;
    };
    auto loadBlob = [&](std::vector<uint8_t>& dst, uint32_t at) {
        auto body = s.next();
        dst.assign(body.begin(), body.end());
        if (arena) arena->put(at, body.data(), body.size());
        last = uint32_t(body.size());
    };

    if (!files.bg.empty()) { lv.bg = loadBg(files.bg, lay.base); lv.hasBg = true; }
    lay.trg = lay.base + align16(last);
    if (!files.trg.empty()) {
        loadBlob(lv.trg, lay.trg);
        lay.trgSize = last;
        if (arena) arena->put(int64_t(lay.trg) + last, lv.trg.data(), lv.trg.size());   // DAT_0045052c
        last *= 2;
    }
    lay.det = lay.trg + align16(last);
    if (!files.det.empty()) loadBlob(lv.det, lay.det);
    lay.dt2 = lay.det + align16(last);
    if (!files.dt2.empty()) loadBlob(lv.dt2, lay.dt2);
    lay.als = lay.dt2 + align16(last);
    if (arena) arena->fill(lay.als, 0, 0x1000);
    if (!files.als.empty()) {
        loadBlob(lv.als, lay.als);
        if (arena) {
            uint8_t copy[0x800];
            for (uint32_t i = 0; i < 0x800; ++i) copy[i] = arena->at(int64_t(lay.als) + i);
            arena->put(int64_t(lay.als) + 0x800, copy, 0x800);
        }
        last = 0x1000;
    }
    lay.parallax = lay.als + last + (0x20 - ((lay.als + last) & 0x1f));
    uint32_t next = lay.parallax;                        // DAT_00450fa0
    if (!files.parallax.empty()) {
        lv.parallax = loadBg(files.parallax, lay.parallax); lv.hasParallax = true;
        next = lay.parallax + align16(last);
    }
    if (!files.extra.empty()) {
        lv.extra = loadBg(files.extra, next); lv.hasExtra = true;
        next += align16(last);
    }
    if ((level == 0 || level == 5) && lv.hasParallax) {
        lv.parallaxLinear = LinearBitmap::expand(lv.parallax);
        if (arena) arena->put(next, lv.parallaxLinear.pixels.data(), lv.parallaxLinear.pixels.size());
        next += uint32_t(lv.parallaxLinear.pixels.size());
    }

    // Column tables (FUN_004331d8 / FUN_00433d0b iterate DAT_004658ac = wTiles/2 columns; a level
    // without a background, the continue screen, has 0x28: FUN_004460e0 at 0x446538).
    lv.columns = lv.hasBg ? lv.bg.wTiles / 2 : 0x28;
    if (lv.columns > 0) {
        if (!lv.det.empty()) lv.collision = ColumnTable(lv.det, 4, lv.columns, false);
        if (!lv.dt2.empty()) lv.collision2 = ColumnTable(lv.dt2, 4, lv.columns, false);
        if (!lv.als.empty()) lv.objects = ColumnTable(lv.als, 4, lv.columns, false);
        if (!lv.trg.empty()) lv.triggers = ColumnTable(lv.trg, 0, lv.columns, true);
    }
    if (arena) {
        lv.collision.place(arena, lay.det);              // DAT_0046615c
        if (!lv.dt2.empty()) lv.collision2.place(arena, lay.dt2);   // DAT_00466160
        lv.triggers.place(arena, lay.trg);               // DAT_00450510
    }

    // FUN_00447190: per-level sprites.
    const std::span<const ArenaSprite> infos = levelArenaSprites(level);
    size_t k = 0;
    for (const std::string& name : levelSprites(level)) {
        auto spr = std::make_unique<Sprite>(name, s.next());
        pal.allocate(spr->localPalette(), spr->colourCount(), 0);
        spr->remapInPlace(pal.remap().data());
        if (arena) spr->setArenaAddr(Arena::kAddress + next);
        next = putSprite(arena, next, *spr, k < infos.size() ? &infos[k] : nullptr);
        if (k < infos.size()) spr->setBoxWord(infos[k].box);
        ++k;
        lv.spriteByName[name] = spr.get();
        lv.sprites.push_back(std::move(spr));
    }
    lay.end = next;
    // FUN_00447190: slot names that differ from the level's sprite names.
    for (const SlotBinding* b = kSlotBindings; b->level >= 0; ++b)
        if (b->level == level)
            if (Sprite* target = lv.sprite(b->sprite)) lv.spriteByName[b->slot] = target;
    return lv;
}

std::string resolveDataDir(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto hasInit = [&](const fs::path& d) {
        for (const auto& e : fs::directory_iterator(d, ec)) {
            std::string n = e.path().filename().string();
            for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (n == "INIT.PH" && e.is_regular_file(ec)) return true;
        }
        return false;
    };
    const fs::path base = dir.empty() ? fs::path(".") : fs::path(dir);
    if (hasInit(base)) return dir;
    for (const auto& e : fs::directory_iterator(base, ec)) {
        std::string n = e.path().filename().string();
        for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (n == "ASSETS" && e.is_directory(ec) && hasInit(e.path())) return e.path().string();
    }
    return dir;
}

AssetBank loadInit(const std::string& dataDir, Palette& pal, Arena* arena) {
    AssetBank bank;
    PhStream s = PhStream::fromFile(dataDir.empty() ? "init.ph" : dataDir + "/init.ph");
    // The load block from its start (DAT_004504dc): each record up to its palette, where the
    // next one goes (FUN_004455f0 / FUN_004453a0).
    uint32_t next = 0;
    const std::span<const ArenaSprite> infos = initArenaSprites();
    size_t k = 0;
    for (const ManifestEntry& e : initManifest()) {
        auto body = s.next();
        if (e.kind == RecordKind::Raw) {
            // FUN_004455f0: DAT_004503c0[0] = 0, then FUN_004451a0(its palette, its colour count, 1):
            // the record's first triplet is local colour 1 (local 0 stays 0, see-through), so its
            // last triplet goes unused and a pixel value past the colours takes what the remap
            // table held before.
            RawImage img(e.name, body);
            pal.setRemap(0, 0);
            pal.allocate(img.localPalette(), img.colourCount(), 1);
            uint8_t remap[256];
            for (int i = 0; i < 256; ++i) remap[i] = pal.remap()[i];
            img.remapInPlace(remap);
            const uint32_t palOff = rd32(&body[4]);
            if (arena) {
                arena->put(next, body.data(), 16);
                arena->put(int64_t(next) + 16, img.pixels().data(), img.pixels().size());
            }
            next += palOff;
            if (e.name == "font") bank.font = std::move(img);
            else bank.scoline = std::move(img);
        } else {
            auto spr = std::make_unique<Sprite>(e.name, body);
            pal.allocate(spr->localPalette(), spr->colourCount(), 0);
            spr->remapInPlace(pal.remap().data());
            if (arena) spr->setArenaAddr(Arena::kAddress + next);
            next = putSprite(arena, next, *spr, k < infos.size() ? &infos[k] : nullptr);
            if (k < infos.size()) spr->setBoxWord(infos[k].box);
            ++k;
            bank.spriteByName[e.name] = spr.get();
            bank.sprites.push_back(std::move(spr));
        }
    }
    if (arena) arena->initEnd = next;                    // DAT_00450fa0 (and DAT_004504e0 from it)
    pal.commitInitAllocations();
    // FUN_0043ce10: sound effects follow the sprites.
    for (const SoundEntry& e : soundManifest()) {
        if (s.atEnd()) break;
        auto body = s.next();
        bank.sounds.push_back(SoundSample{e.name, e.id, std::vector<uint8_t>(body.begin(), body.end())});
    }
    for (const auto& snd : bank.sounds) bank.soundById[snd.id] = &snd;
    return bank;
}

} // namespace ph
