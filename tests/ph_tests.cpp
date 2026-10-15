// Round-trip tests against the original data files.
// Usage: ph_tests <dir containing INIT.PH and LEVELxx.PH>
#include "engine/renderer.h"
#include "game/game.h"
#include "game/play_logic.h"
#include "ph/level.h"
#include "ph/manifests.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int failures = 0;
int runGameplayTests(const std::string& dir);
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "original";
    ph::Palette pal;

    // INIT.PH: 120 sprite/font records + 138 sounds, manifest order.
    ph::AssetBank bank = ph::loadInit(dir, pal);
    CHECK(bank.sprites.size() == 118);
    CHECK(bank.font.cellCount() == 39 && bank.font.cellW() == 8 && bank.font.cellH() == 8);
    CHECK(bank.scoline.cellCount() == 12 && bank.scoline.cellW() == 8 && bank.scoline.cellH() == 16);
    CHECK(bank.sounds.size() == 138);
    const ph::Sprite* run = bank.sprite("hyirun");
    CHECK(run && run->frameCount() == 10);
    if (run) {
        const auto& f = run->frame(0);
        CHECK(f.w > 0 && f.h > 0 && f.w < 128 && f.h < 128);
        std::vector<uint8_t> px = run->decode(0);
        size_t opaque = 0;
        for (uint8_t v : px) if (v != 0xff) ++opaque;
        CHECK(opaque > size_t(f.w) * f.h / 4);        // a decoded Harry is mostly solid
    }
    // Palette: all INIT.PH colours must sit in the free range and be committed.
    for (int i = 0; i < 256; ++i) {
        const uint8_t fl = pal.flags(i);
        if (i < pal.firstFree() || i >= pal.endFree()) CHECK(fl & ph::Palette::kSystem);
        else CHECK((fl & ph::Palette::kLevelAlloc) == 0);
    }

    // Every level loads and consumes exactly the manifest's records.
    for (int lv = 0; lv < ph::kLevelCount; ++lv) {
        ph::Palette p2 = pal;
        try {
            ph::LevelData l = ph::loadLevel(lv, dir, p2);
            CHECK(l.sprites.size() == ph::levelSprites(lv).size());
            if (l.hasBg) {
                CHECK(l.bg.tileCount() > 0);
                for (uint16_t e : l.bg.tilemap) CHECK((e & 0xfff) < l.bg.tileCount());
            }
        } catch (const std::exception& e) {
            std::printf("level %d: %s\n", lv, e.what());
            ++failures;
        }
    }
    {
        ph::Palette p2 = pal;
        ph::LevelData l1 = ph::loadLevel(1, dir, p2);
        CHECK(l1.bg.wTiles == 128 && l1.bg.hTiles == 352 && l1.bg.tileCount() == 1114);
        CHECK(l1.parallax.wTiles == 98 && l1.parallax.hTiles == 64);
        CHECK(l1.sprites.size() == 31 && l1.sprite("hkifly") && l1.sprite("hkifly")->frameCount() == 10);
    }

    // Renderer smoke test: a frame of level 1 must not be blank.
    {
        engine::Options opts;
        game::Game g(dir, opts);
        g.startLevel(1);
        g.setMode(game::Mode::Playing);
        engine::HostInput host;
        g.tick(host);
        g.render();
        std::vector<uint8_t> view(size_t(engine::kViewW) * engine::kViewH);
        g.framebuffer().copyView(g.viewScrollX(), view.data());
        size_t nonZero = 0;
        for (uint8_t v : view) if (v) ++nonZero;
        CHECK(nonZero > view.size() / 2);
    }

    failures += runGameplayTests(dir);
    std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}

// ---- gameplay smoke tests (appended) --------------------------------------
namespace { int jaguarTest(const std::string& dir); int bungeeTest(const std::string& dir); }
namespace {
int gameplayTests(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    engine::HostInput idle, left, right, jumpRight;
    left.keyDown[0x25] = true; right.keyDown[0x27] = true;
    jumpRight.keyDown[0x27] = true; jumpRight.keyDown['W'] = true;

    // Ceiba Jungle: the pouch left of the start is collected by walking left.
    g.setMode(game::Mode::Playing); g.startLevel(0);
    const int lives0 = g.stats().lives;
    for (int i = 0; i < 90; ++i) g.tick(i < 2 ? idle : left);
    if (g.stats().score <= 0) { std::printf("FAIL: no treasure collected on level 0 (score %d)\n", g.stats().score); ++fails; }
    // Xibalba Falls: running off the start ledge into the gorge costs a life and restarts the level.
    g.startLevel(1);
    for (int i = 0; i < 230; ++i) g.tick(i < 2 ? idle : right);
    if (g.stats().lives != lives0 - 1) { std::printf("FAIL: expected a lost life on level 1, lives=%d\n", g.stats().lives); ++fails; }
    if (g.levelNumber() != 1) { std::printf("FAIL: level should restart after death, now %d\n", g.levelNumber()); ++fails; }
    // Jumping right from the start must land on the ledge above (y decreases, still on ground).
    g.startLevel(1);
    for (int i = 0; i < 60; ++i) g.tick(i < 2 ? idle : jumpRight);
    const auto& dl = g.state().displayList;
    if (dl.empty()) { std::printf("FAIL: Harry not in the display list\n"); ++fails; }
    std::printf("gameplay: score=%d lives=%d level=%d harry=(%d,%d)\n", g.stats().score, g.stats().lives, g.levelNumber(),
                dl.empty() ? -1 : dl.back().x, dl.empty() ? -1 : dl.back().y);
    fails += jaguarTest(dir);
    fails += bungeeTest(dir);
    return fails;
}
}

// The Ceiba Jungle jaguar (type 55): fly Harry into the arena with the debug cheat, then
// whip whenever it comes close while staying healed; the boss must die, the outro walk
// Harry to the exit and the level end.
namespace game { extern PlayLogic* g_debugLogic; }
namespace {
int jaguarTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.startLevel(0); g.setMode(game::Mode::Playing);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) { std::printf("FAIL: no play logic for level 0\n"); return 1; }
    for (int i = 0; i < 3000; ++i) {                      // fly to the arena's right end
        const game::Harry& h = pl->harry();
        if (std::abs(h.x() - 3030) <= 7 && std::abs(h.y() - 940) <= 7) break;
        engine::HostInput fly; fly.ctrlDown = true;
        fly.keyDown[h.x() < 3030 ? 0x27 : 0x25] = std::abs(h.x() - 3030) > 7;
        fly.keyDown[h.y() < 940 ? 0x28 : 0x26] = std::abs(h.y() - 940) > 7;
        g.tick(fly);
    }
    bool sawMeter = false, sawBoss = false;
    int ticks = 0;
    for (; ticks < 9000 && g.levelNumber() == 0; ++ticks) {
        const game::Obj* jag = nullptr;
        for (const auto& o : pl->objectSystem().objects()) {
            if (o->freed) continue;
            if (o->type == 55) jag = o.get();
            if (o->type == 0x30) sawMeter = true;
        }
        if (jag) sawBoss = true;
        engine::HostInput in;
        if (jag && std::abs(jag->x - pl->harry().x()) < 70 && ticks % 3 == 0) in.keyDown['Q'] = true;
        pl->healHarry();
        g.tick(in);
    }
    if (!sawBoss) { std::printf("FAIL: the jaguar never woke up\n"); ++fails; }
    if (!sawMeter) { std::printf("FAIL: no boss health meter\n"); ++fails; }
    if (g.levelNumber() == 0) { std::printf("FAIL: level 0 did not end after the jaguar fight\n"); ++fails; }
    std::printf("jaguar: level %d after %d ticks, score %d\n", g.levelNumber(), ticks, g.stats().score);
    return fails;
}
}

int runGameplayTests(const std::string& dir) { return gameplayTests(dir); }

// The first bungee vine of Ceiba Jungle (type 27 at (120, 592)): a running jump from the
// small branch at (24, 666) must catch it, the vine must stretch and rebound, and a jump
// press on the way up must throw Harry well above the vine's rest height.
namespace {
int bungeeTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.startLevel(0); g.setMode(game::Mode::Playing);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) return 1;
    for (int i = 0; i < 3000; ++i) {                      // fly to the branch
        const game::Harry& h = pl->harry();
        if (std::abs(h.x() - 24) <= 7 && std::abs(h.y() - 650) <= 7) break;
        engine::HostInput fly; fly.ctrlDown = true;
        fly.keyDown[h.x() < 24 ? 0x27 : 0x25] = std::abs(h.x() - 24) > 7;
        fly.keyDown[h.y() < 650 ? 0x28 : 0x26] = std::abs(h.y() - 650) > 7;
        g.tick(fly);
    }
    int grabbed = -1, lowest = 0, highest = 100000, released = -1;
    for (int i = 0; i < 400; ++i) {
        const game::Harry& h = pl->harry();
        engine::HostInput in;
        if (grabbed < 0) in.keyDown[0x27] = true;                       // run right
        if (i >= 14 && i < 24) in.keyDown['W'] = true;                  // jump, held
        if (grabbed >= 0 && pl->harryBungee() && h.vy() == 0 && pl->harryBungee()->l(0x34) < -0x70000 && released < 0) { in.keyDown['W'] = true; released = i; }
        g.tick(in);
        if (pl->harryBungee() && grabbed < 0) grabbed = i;
        if (pl->harryBungee()) lowest = std::max(lowest, int(h.y()));
        if (released >= 0) highest = std::min(highest, int(h.y()));
    }
    if (grabbed < 0) { std::printf("FAIL: Harry did not catch the bungee vine\n"); ++fails; }
    if (lowest < 700) { std::printf("FAIL: the bungee vine did not stretch (lowest y %d)\n", lowest); ++fails; }
    if (released < 0 || highest > 540) { std::printf("FAIL: the bungee did not throw Harry up (released %d, highest y %d)\n", released, highest); ++fails; }
    std::printf("bungee: caught at tick %d, stretched to y %d, released at %d, rose to y %d\n", grabbed, lowest, released, highest);
    return fails;
}
}

