#include "ph/level.h"

#include "ph/ph_stream.h"

#include <cstdio>

namespace ph {

Background Background::parse(const std::string& name, std::span<const uint8_t> body) {
    if (body.size() < 20) throw Error("Bad background record: " + name);
    Background bg;
    bg.name = name;
    bg.wTiles = int(rd32(&body[0]));
    bg.hTiles = int(rd32(&body[4]));
    const uint32_t tilesOff = rd32(&body[8]);
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

LevelData loadLevel(int level, const std::string& dataDir, Palette& pal) {
    const LevelFiles& files = levelFiles().at(size_t(level));
    LevelData lv;
    lv.level = level;
    pal.releaseLevelAllocations();

    PhStream s = PhStream::fromFile(levelPath(dataDir, level));

    auto loadBg = [&](const std::string& name) {
        Background bg = Background::parse(name, s.next());
        bg.bindPalette(pal);
        return bg;
    };
    auto loadBlob = [&](std::vector<uint8_t>& dst) {
        auto body = s.next();
        dst.assign(body.begin(), body.end());
    };

    if (!files.bg.empty()) { lv.bg = loadBg(files.bg); lv.hasBg = true; }
    if (!files.trg.empty()) loadBlob(lv.trg);
    if (!files.det.empty()) loadBlob(lv.det);
    if (!files.dt2.empty()) loadBlob(lv.dt2);
    if (!files.als.empty()) loadBlob(lv.als);
    if (!files.parallax.empty()) { lv.parallax = loadBg(files.parallax); lv.hasParallax = true; }
    if (!files.extra.empty()) { lv.extra = loadBg(files.extra); lv.hasExtra = true; }
    if ((level == 0 || level == 5) && lv.hasParallax) lv.parallaxLinear = LinearBitmap::expand(lv.parallax);

    // Column tables (FUN_004331d8 / FUN_00433d0b iterate DAT_004658ac = wTiles/2 columns; a level
    // without a background, the continue screen, has 0x28: FUN_004460e0 at 0x446538).
    lv.columns = lv.hasBg ? lv.bg.wTiles / 2 : 0x28;
    if (lv.columns > 0) {
        if (!lv.det.empty()) lv.collision = ColumnTable(lv.det, 4, lv.columns, false);
        if (!lv.dt2.empty()) lv.collision2 = ColumnTable(lv.dt2, 4, lv.columns, false);
        if (!lv.als.empty()) lv.objects = ColumnTable(lv.als, 4, lv.columns, false);
        if (!lv.trg.empty()) lv.triggers = ColumnTable(lv.trg, 0, lv.columns, true);
    }

    // FUN_00447190: per-level sprites.
    for (const std::string& name : levelSprites(level)) {
        auto spr = std::make_unique<Sprite>(name, s.next());
        pal.allocate(spr->localPalette(), spr->colourCount(), 0);
        spr->remapInPlace(pal.remap().data());
        lv.spriteByName[name] = spr.get();
        lv.sprites.push_back(std::move(spr));
    }
    // FUN_00447190: slot names that differ from the level's sprite names.
    for (const SlotBinding* b = kSlotBindings; b->level >= 0; ++b)
        if (b->level == level)
            if (Sprite* target = lv.sprite(b->sprite)) lv.spriteByName[b->slot] = target;
    return lv;
}

AssetBank loadInit(const std::string& dataDir, Palette& pal) {
    AssetBank bank;
    PhStream s = PhStream::fromFile(dataDir.empty() ? "init.ph" : dataDir + "/init.ph");
    for (const ManifestEntry& e : initManifest()) {
        auto body = s.next();
        if (e.kind == RecordKind::Raw) {
            RawImage img(e.name, body);
            uint8_t remap[256] = {0};
            pal.allocate(img.localPalette(), img.colourCount(), 0);
            for (int i = 0; i < 256; ++i) remap[i] = pal.remap()[i];
            remap[0] = 0;                               // FUN_004455f0: DAT_004503c0[0] = 0
            img.remapInPlace(remap);
            if (e.name == "font") bank.font = std::move(img);
            else bank.scoline = std::move(img);
        } else {
            auto spr = std::make_unique<Sprite>(e.name, body);
            pal.allocate(spr->localPalette(), spr->colourCount(), 0);
            spr->remapInPlace(pal.remap().data());
            bank.spriteByName[e.name] = spr.get();
            bank.sprites.push_back(std::move(spr));
        }
    }
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
