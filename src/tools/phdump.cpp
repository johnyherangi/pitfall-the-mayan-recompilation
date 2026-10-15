// phdump - command line asset extractor and frame renderer.
//
//   phdump info   <datadir>                       list INIT.PH / level contents
//   phdump sprite <datadir> <name> <out.ppm>      render all frames of a sprite
//   phdump level  <datadir> <n> <out.ppm>         render a level's full background
//   phdump frame  <datadir> <n> <sx> <sy> <out.ppm>  render one 320x224 game frame
//   phdump wav    <datadir> <id> <out.wav>        extract a sound effect
//   phdump objects <datadir> [n]                  .als object ids / behaviour types per level
//
// Output images are binary PPM (P6); any image viewer / converter reads them.
#include "engine/renderer.h"
#include "game/game.h"
#include "ph/level.h"
#include "ph/manifests.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

void writePpm(const std::string& path, int w, int h, const uint8_t* idx, const ph::Palette& pal,
              int transparent = -1) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (int i = 0; i < w * h; ++i) {
        const int v = idx[i];
        if (v == transparent) { f.put(char(255)); f.put(char(0)); f.put(char(255)); continue; }
        const ph::Rgb c = pal.colour(v);
        f.put(char(c.r)); f.put(char(c.g)); f.put(char(c.b));
    }
}

int cmdInfo(const std::string& dir) {
    ph::Palette pal;
    ph::AssetBank bank = ph::loadInit(dir, pal);
    std::printf("INIT.PH: font %d cells, scoline %d cells, %zu sprites, %zu sounds\n",
                bank.font.cellCount(), bank.scoline.cellCount(), bank.sprites.size(), bank.sounds.size());
    for (const auto& s : bank.sprites) std::printf("  %-10s %3d frames %3d colours\n", s->name().c_str(), s->frameCount(), s->colourCount());
    for (int lv = 0; lv < ph::kLevelCount; ++lv) {
        try {
            ph::Palette p2 = pal;
            ph::LevelData l = ph::loadLevel(lv, dir, p2);
            std::printf("LEVEL%02d %-20s bg %dx%d tiles (%d), parallax %s, %zu sprites, trg %zu det %zu dt2 %zu als %zu\n",
                        lv, ph::levelName(lv), l.bg.wTiles, l.bg.hTiles, l.bg.tileCount(),
                        l.hasParallax ? l.parallax.name.c_str() : "-", l.sprites.size(),
                        l.trg.size(), l.det.size(), l.dt2.size(), l.als.size());
        } catch (const std::exception& e) {
            std::printf("LEVEL%02d: %s\n", lv, e.what());
        }
    }
    return 0;
}

int cmdSprite(const std::string& dir, const std::string& name, const std::string& out) {
    ph::Palette pal;
    ph::AssetBank bank = ph::loadInit(dir, pal);
    const ph::Sprite* spr = bank.sprite(name);
    std::unique_ptr<ph::LevelData> lv;
    for (int l = 0; !spr && l < ph::kLevelCount; ++l) {
        try { lv = std::make_unique<ph::LevelData>(ph::loadLevel(l, dir, pal)); spr = lv->sprite(name); } catch (...) {}
    }
    if (!spr) { std::fprintf(stderr, "Missing Sprite: %s\n", name.c_str()); return 1; }
    int W = 0, H = 1;
    for (int i = 0; i < spr->frameCount(); ++i) { W += spr->frame(i).w + 1; H = std::max(H, spr->frame(i).h); }
    std::vector<uint8_t> sheet(size_t(W) * H, 0xff);
    int x0 = 0;
    for (int i = 0; i < spr->frameCount(); ++i) {
        const auto& f = spr->frame(i);
        std::vector<uint8_t> px = spr->decode(i);
        for (int y = 0; y < f.h; ++y) std::memcpy(&sheet[size_t(y) * W + x0], &px[size_t(y) * f.w], size_t(f.w));
        x0 += f.w + 1;
        std::printf("frame %2d: %3dx%-3d hot (%d,%d)\n", i, f.w, f.h, f.hotX, f.hotY);
    }
    writePpm(out, W, H, sheet.data(), pal, 0xff);
    return 0;
}

int cmdLevel(const std::string& dir, int level, const std::string& out) {
    ph::Palette pal;
    ph::loadInit(dir, pal);
    ph::LevelData lv = ph::loadLevel(level, dir, pal);
    if (!lv.hasBg) { std::fprintf(stderr, "level %d has no background\n", level); return 1; }
    ph::LinearBitmap bmp = ph::LinearBitmap::expand(lv.bg);
    writePpm(out, bmp.w, bmp.h, bmp.pixels.data(), pal, 0);
    std::printf("%s: %dx%d\n", out.c_str(), bmp.w, bmp.h);
    return 0;
}

int cmdFrame(const std::string& dir, int level, int sx, int sy, const std::string& out,
             const std::string& keys = "", int ticks = 12) {
    // keys: letters held during the ticks: L R U D J(ump) W(hip) S(hoot)
    engine::Options opts;
    game::Game g(dir, opts);
    g.startLevel(level);
    g.setMode(game::Mode::Playing);
    engine::HostInput host, idle;
    for (char k : keys) {
        switch (k) {
        case 'L': host.keyDown[0x25] = true; break; case 'R': host.keyDown[0x27] = true; break;
        case 'U': host.keyDown[0x26] = true; break; case 'D': host.keyDown[0x28] = true; break;
        case 'J': host.keyDown['W'] = true; break;  case 'W': host.keyDown['Q'] = true; break;
        case 'S': host.keyDown['E'] = true; break;  default: break;
        }
    }
    const bool tapJump = keys.find('j') != std::string::npos;    // 'j' = tap jump every 30 ticks
    for (int i = 0; i < ticks; ++i) {
        host.keyDown['W'] = keys.find('J') != std::string::npos || (tapJump && (i % 30) < 2);
        g.tick(i < 2 ? idle : host);           // keys are pressed from tick 2 (edge-triggered jump)
        if (sx >= 0) { g.state().scrollX = sx; g.state().scrollY = sy; }
    }
    g.render();
    std::vector<uint8_t> view(size_t(engine::kViewW) * engine::kViewH);
    g.framebuffer().copyView(g.viewScrollX(), view.data());
    writePpm(out, engine::kViewW, engine::kViewH, view.data(), g.palette());
    std::printf("%s: level %d scroll (%d,%d) after %d ticks\n", out.c_str(), level, g.state().scrollX, g.state().scrollY, ticks);
    if (std::getenv("PHDUMP_VERBOSE"))
        for (const auto& s : g.state().displayList)
            std::printf("  %-10s frame %2u at (%d,%d) flags %02x\n", s.sprite ? s.sprite->name().c_str() : "?", s.frame, s.x, s.y, s.flags);
    return 0;
}

int cmdObjects(const std::string& dir, int level) {
    // Histogram of the .als object ids / behaviour types of a level (or all levels).
    ph::Palette pal;
    ph::loadInit(dir, pal);
    for (int lv = (level < 0 ? 0 : level); lv < (level < 0 ? ph::kLevelCount : level + 1); ++lv) {
        ph::LevelData l;
        try { l = ph::loadLevel(lv, dir, pal); } catch (...) { continue; }
        if (l.objects.empty()) continue;
        std::map<int, int> byId;
        for (int c = 0; c < l.objects.columns(); ++c)
            for (const ph::ColumnEntry& e : l.objects.column(c)) ++byId[e.id];
        std::printf("LEVEL%02d %s\n", lv, ph::levelName(lv));
        for (auto [id, n] : byId) {
            const ph::ObjectTemplate& t = ph::kObjectTemplates[id];
            std::printf("  id %3d (0x%02x) x%-3d type %3d sub %3d flags %04x sprite %s\n", id, id, n, t.type, t.sub,
                        t.flags, t.sprite);
        }
    }
    return 0;
}

int cmdWav(const std::string& dir, int id, const std::string& out) {
    ph::Palette pal;
    ph::AssetBank bank = ph::loadInit(dir, pal);
    const ph::SoundSample* s = bank.sound(id);
    if (!s) { std::fprintf(stderr, "no sound with id %d\n", id); return 1; }
    std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(s->riff.data()), std::streamsize(s->riff.size()));
    std::printf("%s: %s (%zu bytes)\n", out.c_str(), s->name.c_str(), s->riff.size());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "info" && argc >= 3) return cmdInfo(argv[2]);
        if (cmd == "sprite" && argc >= 5) return cmdSprite(argv[2], argv[3], argv[4]);
        if (cmd == "level" && argc >= 5) return cmdLevel(argv[2], std::atoi(argv[3]), argv[4]);
        if (cmd == "frame" && argc >= 7)
            return cmdFrame(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), argv[6],
                            argc > 7 ? argv[7] : "", argc > 8 ? std::atoi(argv[8]) : 12);
        if (cmd == "wav" && argc >= 5) return cmdWav(argv[2], std::atoi(argv[3]), argv[4]);
        if (cmd == "objects" && argc >= 3) return cmdObjects(argv[2], argc > 3 ? std::atoi(argv[3]) : -1);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
    std::fprintf(stderr,
        "usage:\n  phdump info <datadir>\n  phdump sprite <datadir> <name> <out.ppm>\n"
        "  phdump level <datadir> <n> <out.ppm>\n  phdump frame <datadir> <n> <scrollX|-1> <scrollY> <out.ppm> [keys LRUDJWS] [ticks]\n"
        "  phdump wav <datadir> <id> <out.wav>\n  phdump objects <datadir> [n]\n");
    return 1;
}
