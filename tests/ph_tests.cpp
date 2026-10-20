// Round-trip tests against the original data files.
// Usage: ph_tests <dir containing INIT.PH and LEVELxx.PH>
#include "engine/renderer.h"
#include "game/game.h"
#include "game/play_logic.h"
#include "ph/level.h"
#include "ph/manifests.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

static int failures = 0;
int runGameplayTests(const std::string& dir);
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main(int argc, char** argv) {
    const std::string dir = ph::resolveDataDir(argc > 1 ? argv[1] : "original");
    // No CD music but the music test's own (the data folder may hold the disc's tracks, whose
    // fade at each load would put the transitions' timing off)
    const std::string noMusic = (std::filesystem::temp_directory_path() / "pitfall_no_music").string();
    std::filesystem::create_directories(noMusic);
#ifdef _WIN32
    _putenv_s("PITFALL_MUSIC_DIR", noMusic.c_str());
#else
    setenv("PITFALL_MUSIC_DIR", noMusic.c_str(), 1);
#endif
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
namespace { int jaguarTest(const std::string& dir); int bungeeTest(const std::string& dir); int stoneTest(const std::string& dir);
            int vortexTest(const std::string& dir); int deathTest(const std::string& dir); int vaultTest(const std::string& dir);
            int stoneJaguarTest(const std::string& dir); int jaguarManTest(const std::string& dir);
            int warriorSpiritTest(const std::string& dir); int atari2600Test(const std::string& dir);
            int mapScreenTest(const std::string& dir); int continueTest(const std::string& dir);
            int transitionTest(const std::string& dir); int musicTest(const std::string& dir);
            int cutsceneTest(const std::string& dir); int titleTest(const std::string& dir); int earthwormTest(const std::string& dir);
            int cheatTest(const std::string& dir); int menuTest(const std::string& dir); int exitTest(const std::string& dir);
            int leftoversTest(const std::string& dir); int pointersTest(const std::string& dir);
            int slotsTest(const std::string& dir); }
namespace {
// The engine's blocking parts run out: a command's fades and waits, a level load's fade in.
int settle(game::Game& g, int cap = 1000) {
    int n = 0;
    for (engine::HostInput idle; (g.transitionActive() || g.exitPending()) && n < cap; ++n) g.tick(idle);
    return n;
}
int gameplayTests(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    engine::HostInput idle, left, right, jumpRight;
    left.keyDown[0x25] = true; right.keyDown[0x27] = true;
    jumpRight.keyDown[0x27] = true; jumpRight.keyDown['W'] = true;

    // Ceiba Jungle: the pouch left of the start is collected by walking left.
    g.setMode(game::Mode::Playing); g.startLevel(0);
    const int fadeIn = settle(g);
    if (fadeIn != 29) { std::printf("FAIL: level 0's fade in took %d ticks\n", fadeIn); ++fails; }
    const int lives0 = g.stats().lives;
    // The HUD slides in: DAT_00451940 is 0x30 less the start's tick, put back after the 14
    // pre-ticks, then 3 less a tick; the logic's tick count went up by 2 + 14.
    const uint32_t ticks0 = g.logicTicks();
    if (g.state().hudSlide != 0x2d) { std::printf("FAIL: HUD slide %d at the start of level 0\n", g.state().hudSlide); ++fails; }
    for (int i = 0; i < 90; ++i) {
        g.tick(i < 2 ? idle : left);
        if (i == 4 && g.state().hudSlide != 0x2d - 15) { std::printf("FAIL: HUD slide %d after 5 ticks\n", g.state().hudSlide); ++fails; }
    }
    if (g.state().hudSlide != 0 || g.logicTicks() != ticks0 + 90) { std::printf("FAIL: HUD slide %d, logic ticks %u\n", g.state().hudSlide, g.logicTicks() - ticks0); ++fails; }
    if (g.stats().score <= 0) { std::printf("FAIL: no treasure collected on level 0 (score %d)\n", g.stats().score); ++fails; }
    // Xibalba Falls: running off the start ledge into the gorge costs a life and restarts the level.
    g.startLevel(1);
    settle(g);
    for (int i = 0; i < 230; ++i) g.tick(i < 2 ? idle : right);
    if (g.stats().lives != lives0 - 1) { std::printf("FAIL: expected a lost life on level 1, lives=%d\n", g.stats().lives); ++fails; }
    if (g.levelNumber() != 1) { std::printf("FAIL: level should restart after death, now %d\n", g.levelNumber()); ++fails; }
    // Jumping right from the start must land on the ledge above (y decreases, still on ground).
    g.startLevel(1);
    settle(g);
    for (int i = 0; i < 60; ++i) g.tick(i < 2 ? idle : jumpRight);
    const auto& dl = g.state().displayList;
    if (dl.empty()) { std::printf("FAIL: Harry not in the display list\n"); ++fails; }
    std::printf("gameplay: score=%d lives=%d level=%d harry=(%d,%d)\n", g.stats().score, g.stats().lives, g.levelNumber(),
                dl.empty() ? -1 : dl.back().x, dl.empty() ? -1 : dl.back().y);
    fails += jaguarTest(dir);
    fails += bungeeTest(dir);
    fails += stoneTest(dir);
    fails += vortexTest(dir);
    fails += deathTest(dir);
    fails += vaultTest(dir);
    fails += stoneJaguarTest(dir);
    fails += jaguarManTest(dir);
    fails += warriorSpiritTest(dir);
    fails += atari2600Test(dir);
    fails += mapScreenTest(dir);
    fails += continueTest(dir);
    fails += transitionTest(dir);
    fails += musicTest(dir);
    fails += cutsceneTest(dir);
    fails += titleTest(dir);
    fails += earthwormTest(dir);
    fails += cheatTest(dir);
    fails += menuTest(dir);
    fails += exitTest(dir);
    fails += leftoversTest(dir);
    fails += pointersTest(dir);
    fails += slotsTest(dir);
    return fails;
}
}

// The Ceiba Jungle jaguar (type 55): fly Harry into the arena with the debug cheat, then
// whip whenever it comes close while staying healed; the boss must die, the outro walk
// Harry to the exit and the level end.
namespace game { extern PlayLogic* g_debugLogic; }
namespace {
// A level's exit ("*4") goes through the map screen (level 19): let it run to the next level.
int throughMap(game::Game& g) {
    int t = 0;
    for (engine::HostInput idle; (g.levelNumber() != 19 || g.transitionActive()) && t < 1000; ++t) g.tick(idle);   // the fade out
    for (engine::HostInput idle; g.levelNumber() == 19 && t < 2000; ++t) g.tick(idle);
    settle(g);
    return t;
}

int jaguarTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.startLevel(0); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) { std::printf("FAIL: no play logic for level 0\n"); return 1; }
    // Fly over the arena to its right edge, then down along it: the jaguar's .als entry is met
    // again by the camera's bottom-row scan (FUN_004042fe) only while the camera comes down, and
    // it wakes only with Harry against the right edge (x >= 0xbce) on the arena floor (the exit
    // trigger 0x5f to its left ends the level without the fight).
    const int legs[2][2] = {{3030, 400}, {3030, 980}};
    for (const auto& leg : legs) {
        for (int i = 0; i < 3000; ++i) {
            const game::Harry& h = pl->harry();
            if (std::abs(h.x() - leg[0]) <= 7 && std::abs(h.y() - leg[1]) <= 7) break;
            engine::HostInput fly; fly.ctrlDown = true;
            fly.keyDown[h.x() < leg[0] ? 0x27 : 0x25] = std::abs(h.x() - leg[0]) > 7;
            fly.keyDown[h.y() < leg[1] ? 0x28 : 0x26] = std::abs(h.y() - leg[1]) > 7;
            g.tick(fly);
        }
    }
    bool sawMeter = false, sawBoss = false;
    int ticks = 0;
    for (; ticks < 9000 && g.levelNumber() == 0; ++ticks) {
        const game::Obj* jag = nullptr;
        for (const game::Obj* o : pl->objectSystem().list()) {
            if (o->type == 55) jag = o;
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
    const int mapTicks = throughMap(g);
    if (g.levelNumber() != 1) { std::printf("FAIL: level 0 did not lead to level 1 after the jaguar fight (level %d)\n", g.levelNumber()); ++fails; }
    std::printf("jaguar: level %d after %d ticks and %d on the map, score %d\n", g.levelNumber(), ticks, mapTicks, g.stats().score);
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
    g.startLevel(0); g.setMode(game::Mode::Playing); settle(g);
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
        if (grabbed >= 0 && pl->harryBungee() && pl->harryBungee()->l(0x34) < -0x70000 && released < 0) { in.keyDown['W'] = true; released = i; }
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


// A sling stone at the start of Ceiba Jungle: E throws one (type 5, held in one of the five
// in-flight slots at DAT_00466808); it must fly off the way Harry faces, come down or break (on
// the snake hole ahead), and be gone within 200 ticks, freeing its slot and costing one stone.
namespace {
int stoneTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    g.startLevel(0); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) return 1;
    for (int i = 0; i < 60; ++i) g.tick(engine::HostInput{});
    g.stats().ammoCount[0] = 10;
    const int ammo0 = g.stats().ammoCount[0];
    int thrownAt = -1, x0 = 0, maxDx = 0, gone = -1, prevY = 0;
    bool fell = false, broke = false, slotSet = false;
    for (int i = 0; i < 200; ++i) {
        engine::HostInput in;
        if (i < 2) in.keyDown['E'] = true;
        g.tick(in);
        game::Obj* st = nullptr;
        for (game::Obj* o : pl->objectSystem().list()) if (o->type == 5) st = o;
        if (st && thrownAt < 0) { thrownAt = i; x0 = st->x; prevY = st->y; }
        if (st) {
            const int dx = pl->harry().facing() ? x0 - st->x : st->x - x0;
            maxDx = std::max(maxDx, dx);
            if (st->y > prevY) fell = true;
            prevY = st->y;
            if (st->w(0x40)) broke = true;
            for (uint32_t a = 0x466808; a <= 0x466818; a += 4) if (pl->harry().ul(a) == pl->addrOf(st)) slotSet = true;
        }
        if (thrownAt >= 0 && !st && gone < 0) gone = i;
    }
    bool slotsFree = true;
    for (uint32_t a = 0x466808; a <= 0x466818; a += 4) if (pl->harry().ul(a) != 0) slotsFree = false;
    if (thrownAt < 0) { std::printf("FAIL: no sling stone was thrown\n"); ++fails; }
    else {
        if (!slotSet) { std::printf("FAIL: the stone was not in an in-flight slot\n"); ++fails; }
        if (maxDx < 32) { std::printf("FAIL: the stone did not fly (%d px)\n", maxDx); ++fails; }
        if (!fell && !broke) { std::printf("FAIL: the stone neither came down nor broke\n"); ++fails; }
        if (gone < 0) { std::printf("FAIL: the stone was still there after 200 ticks\n"); ++fails; }
        if (!slotsFree) { std::printf("FAIL: the stone's slot was not freed\n"); ++fails; }
        if (g.stats().ammoCount[0] != ammo0 - 1) { std::printf("FAIL: stones %d -> %d\n", ammo0, g.stats().ammoCount[0]); ++fails; }
    }
    std::printf("stone: thrown at tick %d, flew %d px, %s, gone at %d\n", thrownAt, maxDx, broke ? "broke" : "left the screen", gone);

    // The boomerang (type 6) flies its loop, turns into type 0x87 at the end of the path and
    // comes back to Harry, who stands still and gets it back (a new game: nothing about to
    // home in on).
    g.startLevel(0); g.setMode(game::Mode::Playing); settle(g);
    pl = game::g_debugLogic;
    for (int i = 0; i < 60; ++i) g.tick(engine::HostInput{});
    g.stats().weapon = 1; g.stats().ammoCount[1] = 3;
    int back = -1, caught = -1;
    for (int i = 0; i < 300 && caught < 0; ++i) {
        engine::HostInput in;
        if (i < 2) in.keyDown['E'] = true;
        g.tick(in);
        bool out = false, ret = false;
        for (game::Obj* o : pl->objectSystem().list()) { if (o->type == 6) out = true; if (o->type == 0x87) ret = true; }
        if (ret && back < 0) back = i;
        if (back >= 0 && !out && !ret) caught = i;
    }
    if (back < 0) { std::printf("FAIL: the boomerang did not turn back\n"); ++fails; }
    if (caught < 0 || g.stats().ammoCount[1] != 3) { std::printf("FAIL: the boomerang was not caught (boomerangs %d)\n", g.stats().ammoCount[1]); ++fails; }

    // The big stone (type 4) explodes into eight pieces (type 0x89), whose blast flashes the
    // screen (DAT_0046b1b0).
    g.stats().weapon = 2; g.stats().ammoCount[2] = 1;
    int pieces = 0, flash = -1;
    for (int i = 0; i < 120 && flash < 0; ++i) {
        engine::HostInput in;
        if (i < 2) in.keyDown['E'] = true;
        g.tick(in);
        int n = 0;
        for (game::Obj* o : pl->objectSystem().list()) if (o->type == 0x89) ++n;
        pieces = std::max(pieces, n);
        if (pl->fadeCmd() != 0) flash = i;
    }
    if (pieces != 8) { std::printf("FAIL: the big stone made %d pieces\n", pieces); ++fails; }
    if (flash < 0) { std::printf("FAIL: no blast from the big stone\n"); ++fails; }
    std::printf("boomerang: back at tick %d, caught at %d; big stone: %d pieces, blast at %d\n", back, caught, pieces, flash);
    return fails;
}
}

// The vortex of Tikal Ruins (type 71 at (6040, 400)): walking into it spins Harry away to the
// third vault (level 12, "*52"); "*7" brings him back next to it with the level as he left it,
// twirling out (state 0x42a08f), and the vortex stays shut (DAT_0046a888).
namespace {
void flyTo(game::Game& g, game::PlayLogic* pl, int x, int y) {
    for (int i = 0; i < 4000; ++i) {
        const game::Harry& h = pl->harry();
        if (std::abs(h.x() - x) <= 7 && std::abs(h.y() - y) <= 7) return;
        engine::HostInput fly; fly.ctrlDown = true;
        fly.keyDown[h.x() < x ? 0x27 : 0x25] = std::abs(h.x() - x) > 7;
        fly.keyDown[h.y() < y ? 0x28 : 0x26] = std::abs(h.y() - y) > 7;
        g.tick(fly);
    }
}
int vortexTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.newGame();
    g.startLevel(8); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) return 1;
    flyTo(g, pl, 5970, 400);
    int caught = -1, warped = -1;
    for (int i = 0; i < 400 && warped < 0; ++i) {
        engine::HostInput in;
        if (i >= 40 && caught < 0) in.keyDown[0x27] = true;
        g.tick(in);
        if (caught < 0 && g.levelNumber() == 8 && pl->harry().cont() == 0x428780) caught = i;
        if (g.levelNumber() != 8) warped = i;
    }
    if (caught < 0) { std::printf("FAIL: the vortex did not catch Harry\n"); return fails + 1; }
    if (g.levelNumber() != 12) { std::printf("FAIL: the vortex led to level %d, not 12\n", g.levelNumber()); return fails + 1; }
    g.command("*7");
    settle(g);
    pl = game::g_debugLogic;
    const game::Harry& h = pl->harry();
    const int bx = h.x(), by = h.y();
    const uint32_t cont = h.cont();
    bool shut = true;
    for (int i = 0; i < 60; ++i) {
        g.tick(engine::HostInput{});
        for (const game::Obj* o : pl->objectSystem().list()) if (o->type == 71 && o->frame != 0xff) shut = false;
    }
    if (g.levelNumber() != 8 || bx != 6044 || cont != 0x42a08f) { std::printf("FAIL: back on level %d at (%d, %d) in state %#x\n", g.levelNumber(), bx, by, cont); ++fails; }
    if (!shut) { std::printf("FAIL: the vortex opened again\n"); ++fails; }
    std::printf("vortex: caught at tick %d, level 12 at %d, back at (%d, %d)\n", caught, warped, bx, by);
    return fails;
}

// A lost life on Ceiba Jungle: the silver at the start (id 0x5b) stays collected, the pouch
// (id 5) comes back, and the level logic starts again in place at the checkpoint.
size_t alsEntry(const std::vector<uint8_t>& als, int col, int row) {
    const size_t off = size_t(als[4 + col * 2] << 8 | als[5 + col * 2]);
    for (size_t e = off; e + 1 < als.size() && als[e] != 0xff; e += 2) if (als[e] == row) return e;
    return 0;
}
int deathTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.newGame();
    g.startLevel(0); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) return 1;
    const size_t silver = alsEntry(g.level().als, 11, 17);
    auto id = [&](size_t e) { return int(pl->alsLong(e + 1) & 0xff); };
    flyTo(g, pl, 200, 250);
    for (int i = 0; i < 60; ++i) { engine::HostInput in; if (i > 20) in.keyDown[i < 40 ? 0x25 : 0x27] = true; g.tick(in); }
    const int score = g.stats().score, lives = g.stats().lives;
    if (id(silver) != 0) { std::printf("FAIL: the silver was not collected (score %d)\n", score); ++fails; }
    pl->harry().b(0x465b72) = 0xfa;                       // fell out of the level
    const int loads = g.levelLoads();
    g.tick(engine::HostInput{});
    settle(g);                                            // the sounds, the fade out, 750 ms, "*3", the fade in
    const game::Harry& h = pl->harry();
    if (g.levelLoads() != loads) { std::printf("FAIL: the level was loaded again\n"); ++fails; }
    if (g.stats().lives != lives - 1) { std::printf("FAIL: lives %d -> %d\n", lives, g.stats().lives); ++fails; }
    if (id(silver) != 0) { std::printf("FAIL: the silver came back after the death\n"); ++fails; }
    if (h.x() != h.w(0x46679e)) { std::printf("FAIL: Harry at x %d, not at the checkpoint %d\n", int(h.x()), int(h.w(0x46679e))); ++fails; }
    std::printf("death: score %d kept, lives %d, Harry at (%d, %d)\n", g.stats().score, g.stats().lives, int(h.x()), int(h.y()));
    return fails;
}
}

// The third Loltun vault (level 12): pressing the keys of the combination on the player's
// turn must win the ten rounds (a coin, silver, gold and a ring fall on the way, then a 1-up),
// the ceiling must go back up and Harry walk out, which ends the vault ("*7").
namespace {
int vaultTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    g.newGame();
    g.startLevel(12); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) return 1;
    const int score0 = g.stats().score, lives0 = g.stats().lives, loads = g.levelLoads();
    int nextKey = 0, solvedAt = -1, outAt = -1, lowest = 1000;
    for (int t = 0; t < 6000; ++t) {
        game::Harry& h = pl->harry();
        game::Obj* lead = pl->objAt(h.ul(0x46617c));
        engine::HostInput in;
        if (lead && h.b(0x46617f) == 0 && uint32_t(lead->l(0x52)) == 0x40be92 && lead->b(0x6a) == 0 && t >= nextKey &&
            h.w(0x465b41) == 0 && h.b(0x465b53) == 0 && h.b(0x465b45) == 0 && h.b(h.ul(0x4661a0)) == 0) {
            in.keyDown['1' + h.b(uint32_t(lead->l(0x58)))] = true;
            nextKey = t + 12;
        }
        lowest = std::min(lowest, int(int16_t(pl->tw(0x1f4))));
        g.tick(in);
        if (g.levelLoads() != loads) { outAt = t; break; }
        if (solvedAt < 0 && pl->tb(0x215)) solvedAt = t;
        if (pl->tb(0x214)) { std::printf("FAIL: the vault's ceiling fell at tick %d\n", t); return fails + 1; }
    }
    if (solvedAt < 0) { std::printf("FAIL: the vault was not solved\n"); ++fails; }
    if (outAt < 0) { std::printf("FAIL: Harry did not leave the vault\n"); ++fails; }
    if (g.stats().score - score0 != 1800 + 7000 || g.stats().lives != lives0 + 1) {
        std::printf("FAIL: vault rewards: score +%d, lives %d -> %d\n", g.stats().score - score0, lives0, g.stats().lives); ++fails;
    }
    std::printf("vault: solved at tick %d, out at %d, ceiling down to %d, score +%d\n", solvedAt, outAt, lowest, g.stats().score - score0);
    return fails;
}
}

// The stone jaguars of the Lost City of Copan (types 140 / 141 / 153 / 154): flown in from the
// right (the .als scan meets the statues again only while the camera comes left, with Harry
// beside them), Harry walks into the arena and whips whatever comes near while staying healed.
// Both jaguars must wake, one must leap up onto the ledge, both must die, the rope must come
// down; Harry then climbs it, jumps left onto the walkway and walks out of the level (0x5f).
namespace {
int stoneJaguarTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.startLevel(3); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) { std::printf("FAIL: no play logic for level 3\n"); return 1; }
    const int legs[3][2] = {{2400, 700}, {2400, 140}, {2250, 140}};
    for (const auto& leg : legs) {
        for (int i = 0; i < 3000; ++i) {
            const game::Harry& h = pl->harry();
            if (std::abs(h.x() - leg[0]) <= 7 && std::abs(h.y() - leg[1]) <= 7) break;
            engine::HostInput fly; fly.ctrlDown = true;
            fly.keyDown[h.x() < leg[0] ? 0x27 : 0x25] = std::abs(h.x() - leg[0]) > 7;
            fly.keyDown[h.y() < leg[1] ? 0x28 : 0x26] = std::abs(h.y() - leg[1]) > 7;
            g.tick(fly);
        }
    }
    int fightAt = -1, bothAwakeAt = -1, ledgeAt = -1, deadAt = -1, ropeAt = -1, climbedAt = -1, outAt = -1;
    int phase = 0, segments = 0, ropeY = 0;
    for (int t = 0; t < 12000 && g.levelNumber() == 3; ++t) {
        game::Harry& h = pl->harry();
        int awake = 0;
        const game::Obj* rope = nullptr;
        engine::HostInput in;
        for (game::Obj* o : pl->objectSystem().list()) {
            if (o->type == 0x8c) {
                const uint32_t st = uint32_t(o->l(0x32));
                if (st != 0x41cb3a) awake += 1;
                if (st == 0x41d195 && ledgeAt < 0) ledgeAt = t;
                if (st != 0x41cb3a && std::abs(o->x - h.x()) < 70 && t % 3 == 0) in.keyDown['Q'] = true;
            }
            if (o->type == 0x9a) rope = o;
        }
        if (fightAt < 0 && h.b(0x465c60)) fightAt = t;
        if (bothAwakeAt < 0 && awake == 2) bothAwakeAt = t;
        if (deadAt < 0 && fightAt >= 0 && h.ul(0x465c58) == 0 && h.ul(0x465c5c) == 0) deadAt = t;
        if (rope && ropeAt < 0) {
            ropeAt = t;
            for (const game::Obj* s = rope; s; s = pl->objAt(uint32_t(const_cast<game::Obj*>(s)->l(0x46))))
                if (s != rope) ++segments;
        }
        if (rope) ropeY = rope->y;
        const bool onRope = h.w(0x466508) != 0 && !h.onGround();
        if (rope && phase == 0) phase = 1;
        if (phase == 1 && onRope) phase = 2;
        if (phase == 2 && h.y() <= 4) { phase = 3; climbedAt = t; }
        if (phase == 0) {
            if (std::abs(h.x() - 1990) > 4) in.keyDown[h.x() < 1990 ? 0x27 : 0x25] = true;
        } else {
            in = engine::HostInput{};
            if (phase == 1) {                                          // under the rope, jump up to it
                if (std::abs(h.x() - 1968) > 3) in.keyDown[h.x() < 1968 ? 0x27 : 0x25] = true;
                else { in.keyDown[0x26] = true; if (t % 40 < 6) in.keyDown['W'] = true; }
            } else if (phase == 2) {
                in.keyDown[0x26] = true;                               // climb
            } else {
                in.keyDown[0x25] = true;                               // jump off to the left, walk out
                if (onRope && t % 8 < 4) in.keyDown['W'] = true;
            }
        }
        pl->healHarry();
        g.tick(in);
        if (g.levelNumber() != 3) outAt = t;
    }
    if (fightAt < 0) { std::printf("FAIL: the stone jaguars' fight never started\n"); ++fails; }
    if (bothAwakeAt < 0) { std::printf("FAIL: the second stone jaguar never woke\n"); ++fails; }
    if (ledgeAt < 0) { std::printf("FAIL: no stone jaguar leapt up onto the ledge\n"); ++fails; }
    if (deadAt < 0) { std::printf("FAIL: the stone jaguars did not die\n"); ++fails; }
    if (ropeAt < 0 || segments != 8 || ropeY != -0x20) { std::printf("FAIL: the rope (segments %d, head y %d)\n", segments, ropeY); ++fails; }
    throughMap(g);
    if (outAt < 0 || g.levelNumber() != 4) { std::printf("FAIL: Harry did not leave the level by the rope (level %d)\n", g.levelNumber()); ++fails; }
    std::printf("stone jaguars: fight at tick %d, both awake at %d, ledge at %d, dead at %d, rope at %d, top at %d, level 4 at %d\n",
                fightAt, bothAwakeAt, ledgeAt, deadAt, ropeAt, climbedAt, outAt);
    return fails;
}
}

// The jaguar man of level 8 (types 158 / 159): Harry flies down beside its arena (its .als entry
// is met by the camera coming down) and back up, where it shows itself and walks him in; he
// whips whatever comes near while staying healed.  It must change into its man form and swipe
// (with the claw: an unhurt Harry's damage byte tests high), and die; then the stone face's
// tongue (type 91) throws Harry up to the walkway and the exit.
namespace {
int jaguarManTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.startLevel(8); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) { std::printf("FAIL: no play logic for level 8\n"); return 1; }
    const int legs[2][2] = {{5800, 280}, {5800, 150}};
    for (const auto& leg : legs) {
        for (int i = 0; i < 3000; ++i) {
            const game::Harry& h = pl->harry();
            if (std::abs(h.x() - leg[0]) <= 7 && std::abs(h.y() - leg[1]) <= 7) break;
            engine::HostInput fly; fly.ctrlDown = true;
            fly.keyDown[h.x() < leg[0] ? 0x27 : 0x25] = std::abs(h.x() - leg[0]) > 7;
            fly.keyDown[h.y() < leg[1] ? 0x28 : 0x26] = std::abs(h.y() - leg[1]) > 7;
            g.tick(fly);
        }
    }
    int shownAt = -1, manAt = -1, clawAt = -1, deadAt = -1, outAt = -1;
    for (int t = 0; t < 16000 && g.levelNumber() == 8; ++t) {
        game::Harry& h = pl->harry();
        engine::HostInput in;
        bool boss = false;
        for (game::Obj* o : pl->objectSystem().list()) {
            if (o->type == 0x9e) {
                boss = true;
                const uint32_t st = uint32_t(o->l(0x32));
                if (st != 0x41ecc3 && shownAt < 0) shownAt = t;
                if (st == 0x41eac3 && manAt < 0) manAt = t;
                if (st != 0x41ecc3 && std::abs(o->x - h.x()) < 70 && t % 3 == 0) in.keyDown['Q'] = true;
                // on the arena floor, keep near it
                if (st != 0x41ecc3 && std::abs(o->x - h.x()) >= 60 && h.y() > 280) in.keyDown[o->x < h.x() ? 0x25 : 0x27] = true;
            }
            if (o->type == 0x9f && clawAt < 0) clawAt = t;
        }
        if (shownAt >= 0 && !boss && deadAt < 0) deadAt = t;
        if (deadAt >= 0) {                                         // into the stone face's mouth, then right
            in = engine::HostInput{};
            if (h.y() < 200) in.keyDown[0x27] = true;
            else if (std::abs(h.x() - 6008) > 4) in.keyDown[h.x() < 6008 ? 0x27 : 0x25] = true;
            else if (t % 30 < 4) in.keyDown['W'] = true;
        }
        pl->healHarry();
        g.tick(in);
        if (g.levelNumber() != 8) outAt = t;
    }
    if (shownAt < 0) { std::printf("FAIL: the jaguar man never showed itself\n"); ++fails; }
    if (manAt < 0) { std::printf("FAIL: the jaguar man never swiped in its man form\n"); ++fails; }
    if (clawAt < 0) { std::printf("FAIL: no claw with the swipe\n"); ++fails; }
    if (deadAt < 0) { std::printf("FAIL: the jaguar man did not die\n"); ++fails; }
    throughMap(g);
    if (outAt < 0 || g.levelNumber() != 9) { std::printf("FAIL: Harry did not leave level 8 by the tongue (level %d)\n", g.levelNumber()); ++fails; }
    std::printf("jaguar man: shown at tick %d, swipe at %d, claw at %d, dead at %d, level 9 at %d\n", shownAt, manAt, clawAt, deadAt, outAt);
    return fails;
}
}

// The Warrior Spirit of level 10 (types 105 / 106 / 176): Harry whips at it while staying
// healed; it must go through its attacks (the suction that lifts and throws Harry among them)
// and turn by leaping off the top of the screen; then its head is given the killing damage, its
// parts must fly apart and burst, and the last letter of PITFALL must fall for Harry to pick
// up, which ends the game.
namespace {
int warriorSpiritTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    g.startLevel(10); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) { std::printf("FAIL: no play logic for level 10\n"); return 1; }
    std::set<uint32_t> states;
    bool thrown = false, behind = false;
    int t = 0;
    for (; t < 4000; ++t) {
        game::Harry& h = pl->harry();
        engine::HostInput in;
        for (game::Obj* o : pl->objectSystem().list())
            if (o->type == 0x69) {
                states.insert(uint32_t(o->l(0x46)));
                if (std::abs(o->x - h.x()) < 70 && t % 3 == 0) in.keyDown['Q'] = true;
                // once, while it walks and Harry stands, Harry is put behind it (as a throw may
                // leave him) and goes after it: it must leap off the screen and come down facing him
                if (!behind && t >= 1500 && (o->l(0x46) == 0x422260 || o->l(0x46) == 0x422359) && h.cont() == 0x428e50) {
                    h.x() = int16_t(std::clamp(o->x + (o->facing ? 0x60 : -0x60), 0x50, 0x190));
                    behind = true;
                }
                if (behind && !states.count(0x421b3e) && std::abs(o->x - h.x()) > 0x40) {
                    in.keyDown[h.x() < o->x ? 0x27 : 0x25] = true;
                    in.keyDown['Q'] = false;
                }
            }
        if (h.cont() == 0x42c2ff) thrown = true;
        pl->healHarry();
        g.tick(in);
    }
    for (uint32_t st : {0x42195bu, 0x421b3eu, 0x421e44u})
        if (!states.count(st)) { std::printf("FAIL: the Warrior Spirit never went into its state %#x\n", st); ++fails; }
    if (!thrown) { std::printf("FAIL: the Warrior Spirit never lifted Harry\n"); ++fails; }
    // the killing blow
    game::Obj* head = nullptr;
    for (game::Obj* o : pl->objectSystem().list()) if (o->type == 0x69) head = pl->objAt(uint32_t(o->l(0x64)));
    if (!head) { std::printf("FAIL: no Warrior Spirit\n"); return fails + 1; }
    head->w(0x6a) = 0x300;
    const int score0 = g.stats().score;
    int burstAt = -1, landedAt = -1, endAt = -1, letterX = -1;
    for (int i = 0; i < 1500 && g.levelNumber() == 10; ++i, ++t) {
        game::Harry& h = pl->harry();
        int parts = 0;
        const game::Obj* letter = nullptr;
        for (const game::Obj* o : pl->objectSystem().list()) {
            if (o->type == 0x6a) ++parts;
            if (o->type == 0xb0) letter = o;
        }
        if (parts == 0 && burstAt < 0) burstAt = i;
        engine::HostInput in;
        if (letter) {
            letterX = letter->x;
            if (letter->y == 0xc5 && landedAt < 0) landedAt = i;
            if (landedAt >= 0 && std::abs(h.x() - letter->x) > 4) in.keyDown[h.x() < letter->x ? 0x27 : 0x25] = true;
        }
        pl->healHarry();
        g.tick(in);
        if (g.levelNumber() != 10) endAt = i;
    }
    if (burstAt < 0) { std::printf("FAIL: the Warrior Spirit's parts did not burst\n"); ++fails; }
    if (landedAt < 0 || letterX != 0xf0) { std::printf("FAIL: the last letter did not fall (x %d)\n", letterX); ++fails; }
    if (endAt < 0 || !(g.stats().letters & 0x40) || g.stats().score - score0 != 60000) {
        std::printf("FAIL: the game did not end with the letter (level %d, letters %#x, score +%d)\n", g.levelNumber(), g.stats().letters, g.stats().score - score0);
        ++fails;
    }
    std::printf("warrior spirit: %zu states, thrown %d; dead: parts gone after %d ticks, letter down at %d, game over at %d\n",
                states.size(), int(thrown), burstAt, landedAt, endAt);
    return fails;
}
}

// The Atari 2600 level (20).  The attract mode's third demo, recorded on the original, must play
// as it was recorded: through the vine over the tar pit of screen 2 to the crocodile pond of
// screen 3, where the input stops 2 px short of the water.  Then scripted runs: the hole over
// the ladder of screen 0 and the ladder; screen 6's tar pit opening under Harry (a life, and he
// drops back in); its ring (+4000, gone for good); a crocodile's head (safe while the mouth is
// shut); the scorpion below screen 3 until the game is over; and a button out of it ("*6", which
// starts the level again when it was not entered from the Lost City); the clock running out.
namespace {
engine::HostInput a26Pad(uint32_t pad) {
    engine::HostInput in;
    if (pad & 1) in.keyDown[0x26] = true;
    if (pad & 2) in.keyDown[0x28] = true;
    if (pad & 4) in.keyDown[0x25] = true;
    if (pad & 8) in.keyDown[0x27] = true;
    if (pad & 0x40) in.keyDown['E'] = true;
    return in;
}

int atari2600Test(const std::string& dir) {
    int fails = 0;
    auto fail = [&](const char* what) { std::printf("FAIL: 2600: %s\n", what); ++fails; };
    // --- the attract demo
    struct Sample { int screen = -1, x = 0, y = 0, score = 0, lives = 0; } last;
    bool vine = false, flew = false;
    int demoTicks = 0;
    {
        engine::Options opts;
        game::Game g(dir, opts);
        engine::HostInput idle;
        int t = 0;
        // On the way, demo 1 throws a sling stone off Xibalba Falls' left edge: past x 8 its floor
        // probe (x - 8, FUN_0042571f) reads 8 KB past the .det's start, in the parallax map, whose
        // bytes make a "floor" the stone breaks on, as in the original's demo (ph::Arena).
        int brokeAt = -1, brokeX = 0;
        while (!(g.levelNumber() == 20 && g.mode() == game::Mode::Attract && !g.transitionActive()) && t < 16000) {
            g.tick(idle); ++t;
            if (brokeAt < 0 && g.levelNumber() == 1 && game::g_debugLogic)
                for (game::Obj* o : game::g_debugLogic->objectSystem().list())
                    if (o->type == 5 && o->w(0x40) != 0 && o->x < 16) { brokeAt = t; brokeX = o->x; }
        }
        if (g.levelNumber() != 20) { fail("the attract mode never reached the 2600 demo"); return fails; }
        if (brokeAt < 0 || brokeX >= 8) fail("demo 1's sling stone did not break at Xibalba Falls' left edge");
        std::printf("2600: (on the way, demo 1's stone broke at x %d, tick %d)\n", brokeX, brokeAt);
        while (g.levelNumber() == 20 && demoTicks < 3000) {
            const game::Harry& h = game::g_debugLogic->harry();
            last = Sample{h.uw(0x465ba8), h.x(), h.y(), h.l(0x466172), h.b(0x465e85)};
            vine |= h.b(0x465ea4) != 0 && h.uw(0x465ba8) == 2;
            flew |= h.b(0x465ea5) != 0;
            g.tick(idle);
            ++demoTicks;
        }
    }
    if (!vine || !flew) fail("the demo never swung on the vine of screen 2");
    // (The level's start sets only the whole parts of Harry's x and y, 0x401f23: their fractions
    // are what demo 1 left, so the demo ends at x 343, as the original's does in its attract run
    // captured under Wine.)
    if (last.screen != 3 || last.x != 343 || last.y != 0x18a || last.score != 1993 || last.lives != 2) {
        std::printf("FAIL: 2600: the demo ended at screen %d (%d, %d), score %d, lives %d\n", last.screen, last.x, last.y, last.score, last.lives);
        ++fails;
    }
    // --- scripted runs
    engine::Options opts;
    game::Game g(dir, opts);
    g.setMode(game::Mode::Playing);
    g.startLevel(20);
    settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    game::Harry& h = pl->harry();
    auto objects = [&](uint8_t type) { int n = 0; for (const game::Obj* o : pl->objectSystem().list()) n += o->type == type; return n; };
    auto run = [&](int ticks, uint32_t pad, auto until) {
        for (int i = 0; i < ticks; ++i) { if (until()) return i; g.tick(a26Pad(pad)); }
        return -1;
    };
    run(20, 0, [] { return false; });
    if (h.x() != 0x118 || h.b(0x465e83) != 0 || h.b(0x465e82) != 0x14 || g.state().displayList.empty()) fail("the screen did not wait for the first input");
    // the hole over the ladder: 100 points, down to the tunnel; up the ladder and off it
    const int holeAt = run(400, 8, [&] { return h.b(0x465e97) != 0; });
    if (holeAt < 0 || h.x() != 0x191 || h.l(0x466172) != 1900) { std::printf("FAIL: 2600: no fall into the ladder's hole (x %d, score %d)\n", int(h.x()), int(h.l(0x466172))); ++fails; }
    run(100, 0, [&] { return h.y() == 0x1c1; });
    const int climbAt = run(10, 1, [&] { return h.b(0x465e96) != 0; });
    const int topAt = run(200, 1, [&] { return h.y() == 0x197; });
    run(10, 8, [&] { return h.b(0x465e96) == 0; });
    const int groundAt = run(100, 8, [&] { return h.onGround() && h.y() == 0x18a; });
    const int tripAt = run(300, 8, [&] { return h.b(0x465e87) != 0; });
    if (climbAt < 0 || topAt < 0 || groundAt < 0 || tripAt < 0 || h.l(0x466172) >= 1900) {
        std::printf("FAIL: 2600: ladder %d, top %d, off it %d, the log %d (score %d)\n", climbAt, topAt, groundAt, tripAt, int(h.l(0x466172)));
        ++fails;
    }
    // screen 6: in from screen 5; its tar pit opens under Harry
    h.uw(0x465ba8) = 5; h.x() = 0x21f; h.y() = 0x18a;
    const int sinkAt = run(400, 8, [&] { return h.b(0x465e8e) != 0; });
    const int sinkX = h.x();
    const int backAt = run(400, 0, [&] { return h.b(0x465ea2) != 0; });
    const int backY = h.y();
    const int landAt = run(400, 0, [&] { return h.b(0x465ea2) == 0; });   // ("*3": the fades and the wait first)
    if (sinkAt < 0 || h.uw(0x465ba8) != 6 || backAt < 0 || backY != 0x168 || landAt < 0 || h.y() != 0x18a || h.x() != 0x116 || h.b(0x465e85) != 1) {
        std::printf("FAIL: 2600: the tar pit (sank %d at x %d, back %d at y %d, landed %d, lives %d)\n", sinkAt, sinkX, backAt, backY, landAt, int(h.b(0x465e85)));
        ++fails;
    }
    // its ring, past the pit
    h.x() = 0x1e0;
    const int score0 = h.l(0x466172);
    const int ringAt = run(100, 8, [&] { return objects(2) == 0; });
    if (ringAt < 0 || h.l(0x466172) != score0 + 4000 || h.b(0x46617c + 6) != 0xff) { std::printf("FAIL: 2600: the ring (score +%d)\n", int(h.l(0x466172)) - score0); ++fails; }
    h.x() = 0x10c;                                           // out left and back in: no ring
    run(10, 4, [&] { return h.uw(0x465ba8) == 5; });
    run(10, 8, [&] { return h.uw(0x465ba8) == 6; });
    if (h.uw(0x465ba8) != 6 || objects(2) != 0 || objects(3) != 1) fail("the ring came back");
    // screen 3: onto the first crocodile while its mouth is shut
    h.uw(0x465ba8) = 2; h.x() = 0x21f; h.y() = 0x18a;
    run(200, 8, [&] { return h.uw(0x465ba8) == 3 && h.x() >= 338; });
    auto croc = [&]() -> const game::Obj* { for (const game::Obj* o : pl->objectSystem().list()) if (o->type == 7) return o; return nullptr; };
    run(300, 0, [&] { return croc() && croc()->frame == 4; });
    g.tick(a26Pad(0x48));
    const int onAt = run(60, 0, [&] { return h.onGround(); });
    const int onX = h.x();
    const int drownAt = run(300, 0, [&] { return h.b(0x465e8e) != 0; });
    if (onAt < 0 || onX != 371 || drownAt < 30 || !croc() || croc()->frame != 3) {
        std::printf("FAIL: 2600: the crocodile (on at %d, x %d, sank after %d)\n", onAt, onX, drownAt);
        ++fails;
    }
    // the scorpion below screen 3 takes the last life; a button leaves the game over
    run(400, 0, [&] { return h.b(0x465ea2) != 0; });
    h.uw(0x465ba8) = 0; h.x() = 0x21f; h.y() = 0x1c1; h.b(0x465ea2) = 0;
    const int stungAt = run(400, 8, [&] { return h.b(0x465e86) != 0; });
    const int livesStung = h.b(0x465e85), screenStung = h.uw(0x465ba8);
    int exitAt = -1;
    const int loads = g.levelLoads();
    for (int i = 0; i < 600 && exitAt < 0; ++i) {
        g.tick(a26Pad(i >= 300 && (i & 1) ? 0x40 : 0));
        if (g.levelLoads() != loads) exitAt = i;
    }
    if (stungAt < 0 || livesStung != 0 || screenStung != 3 || exitAt < 300) {               // (h is gone with the old logic)
        std::printf("FAIL: 2600: the scorpion (stung %d, lives %d, screen %d), out at %d\n", stungAt, livesStung, screenStung, exitAt);
        ++fails;
    }
    settle(g);
    pl = game::g_debugLogic;
    if (g.levelNumber() != 20 || !pl || pl->harry().b(0x465e85) != 2 || pl->harry().l(0x466172) != 2000) fail("\"*6\" did not start the level again");
    // the clock: from 0:01 it runs out in a second; the game is over
    game::Harry& h2 = pl->harry();
    g.tick(a26Pad(8));
    h2.b(0x465e82) = 0; h2.b(0x465e83) = 1;
    int upAt = -1;
    for (int i = 0; i < 200 && upAt < 0; ++i) { g.tick(a26Pad(0)); if (h2.b(0x465e83) == 0 && h2.b(0x465e85) == 0) upAt = i; }
    if (upAt < 0 || upAt > 120) { std::printf("FAIL: 2600: the clock did not run out (%d)\n", upAt); ++fails; }
    std::printf("2600: demo %d ticks, at screen %d x %d score %d; hole at %d, top %d, log %d; pit %d, back %d; ring %d; on the crocodile at x %d for %d; stung %d, out at %d; time up %d\n",
                demoTicks, last.screen, last.x, last.score, holeAt, topAt, tripAt, sinkAt, backAt, ringAt, onX, drownAt, stungAt, exitAt, upAt);
    return fails;
}
}

// The map screen (level 19) after each level of the tour: the path's pegs, the next level's name,
// and on to the next level of the original's order (6 -> 16 -> 17 -> 7 -> 18 -> 8); a button after
// the first 30 ticks goes on at once.
namespace {
int mapScreenTest(const std::string& dir) {
    int fails = 0;
    static const int kTour[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 16}, {16, 17}, {17, 7}, {7, 18}, {18, 8}, {8, 9}, {9, 10}};
    static const int kPegs[] = {7, 9, 9, 10, 5, 10, 12, 6, 7, 17, 10, 9, 16};
    static const char* const kNames[] = {"[XIBALBA FALLS]", "[TAZAMUL MINES]", "[LOST CITY OF COPAN]", "[COPAN TEMPLE]", "[LAKAMUL RAINFOREST]",
                                         "[YAXCHILAN LAGOON]", "[PALENQUE RUINS]", "[TOMB OF PALENQUE]", "[RUNAWAY MINECAR]", "[JAINA ISLAND FALLS]",
                                         "[TIKAL RUINS]", "[TEMPLE OF TIKAL]", "[WARRIOR SPIRIT]"};
    engine::Options opts;
    game::Game g(dir, opts);
    engine::HostInput idle;
    int total = 0;
    for (size_t i = 0; i < sizeof kTour / sizeof kTour[0]; ++i) {
        g.setMode(game::Mode::Playing);
        g.startLevel(kTour[i][0]);
        g.completeLevel();
        settle(g);
        if (g.levelNumber() != 19) { std::printf("FAIL: map: level %d did not lead to the map (%d)\n", kTour[i][0], g.levelNumber()); ++fails; continue; }
        if (i == 0) {
            // FUN_004331d8 reads the map's missing trigger record where it would be in the load block:
            // after level 0 it finds a '^' there in column 12, Harry's start and checkpoint at
            // (0xb8, 0x620), the facing 0xc7 from past the table's end (as the original's run shows)
            auto w = [&](uint32_t a) { return int(uint16_t(*g.logicMemory().at(a) | *g.logicMemory().at(a + 1) << 8)); };
            if (w(0x46679e) != 0xb8 || w(0x4667a0) != 0x620 || *g.logicMemory().at(0x4667a4) != 0xc7) {
                std::printf("FAIL: map: the checkpoint at (%#x, %#x) facing %#x\n", w(0x46679e), w(0x4667a0), unsigned(*g.logicMemory().at(0x4667a4)));
                ++fails;
            }
        }
        game::PlayLogic* pl = game::g_debugLogic;
        int t = 0, pegs = 0, letters = 0;
        for (; g.levelNumber() == 19 && t < 1000; ++t) {
            int p = 0, l = 0;
            for (const game::Obj* o : pl->objectSystem().list()) { p += o->type == 1; l += o->type == 2; }
            pegs = std::max(pegs, p);
            letters = std::max(letters, l);
            g.tick(idle);
        }
        // the pegs of up to three paths before stay on the map
        int expect = kPegs[i];
        for (size_t k = i >= 3 ? i - 3 : 0; k < i; ++k) expect += kPegs[k];
        if (g.levelNumber() != kTour[i][1] || pegs != expect || letters != int(std::strlen(kNames[i]))) {
            std::printf("FAIL: map after level %d: level %d (want %d), %d pegs (want %d), %d letters\n", kTour[i][0], g.levelNumber(), kTour[i][1], pegs, expect, letters);
            ++fails;
        }
        total += t;
    }
    // a button skips it, but not in its first 30 ticks (FUN_00432038's calls: the set-up's own, the
    // level start's and the first tick's come before this loop's first)
    g.startLevel(0);
    g.completeLevel();
    settle(g);
    engine::HostInput jump; jump.keyDown['W'] = true;
    int skipAt = -1;
    for (int t = 0; t < 100 && skipAt < 0; ++t) { g.tick(t % 2 ? jump : idle); if (g.transitionActive()) skipAt = t; }
    settle(g);
    if (skipAt < 27 || skipAt > 30 || g.levelNumber() != 1) { std::printf("FAIL: map: a button went on at %d (level %d)\n", skipAt, g.levelNumber()); ++fails; }
    std::printf("map: 13 paths in %d ticks; a button goes on at %d\n", total, skipAt);
    return fails;
}
}

// The last life lost: the continue screen (level 14) with "continues N" and the letters found;
// a button there walks Harry off to the right and takes a continue (four lives, no score, back
// to the level); "down" first walks him off to the left: "[GAME OVER]" for three seconds, then
// the title.  Without continues the game is over at once.
namespace {
int continueTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    engine::HostInput idle, button, down;
    button.keyDown['W'] = true;
    down.keyDown[0x28] = true;
    auto loseAll = [&](int level, int continues) {
        g.setMode(game::Mode::Playing);
        g.newGame();
        g.startLevel(level);
        g.stats().continues = continues;
        g.stats().letters = 0x05;                                       // P and T found
        g.stats().score = 1234;
        g.stats().lives = 1;
        g.loseLife();
        for (int i = 0; i < 500 && g.levelNumber() == level && g.mode() != game::Mode::GameOver; ++i) g.tick(idle);
        if (g.levelNumber() == 14) settle(g);
    };
    // yes
    loseAll(2, 3);
    if (g.levelNumber() != 14) { std::printf("FAIL: continue: no continue screen (level %d)\n", g.levelNumber()); return fails + 1; }
    game::PlayLogic* pl = game::g_debugLogic;
    int t = 0;
    for (; t < 120; ++t) g.tick(idle);
    int letters = 0, shown = 0, digit = -1;
    for (const game::Obj* o : pl->objectSystem().list()) {
        if (o->type == 0x18) { ++letters; shown += o->frame != 0xff; }
        if (o->type == 108 && o->sprite && o->sprite->name() == "ltr_sm" && o->frame >= 0x1a && o->frame < 0x24) digit = o->frame - 0x1a;
    }
    if (letters != 7 || shown != 2 || digit != 3) { std::printf("FAIL: continue: %d letters, %d shown, digit %d\n", letters, shown, digit); ++fails; }
    int out = -1;
    for (; t < 1200 && out < 0; ++t) { g.tick(t == 150 ? button : idle); if (g.levelNumber() != 14) out = t; }
    if (out < 0 || g.levelNumber() != 2 || g.stats().lives != 4 || g.stats().continues != 2 || g.stats().score != 0) {
        std::printf("FAIL: continue: yes led to level %d (lives %d, continues %d, score %d)\n", g.levelNumber(), g.stats().lives, g.stats().continues, g.stats().score);
        ++fails;
    }
    // no
    loseAll(5, 2);
    int over = -1, title = -1;
    for (int i = 0; i < 1500 && title < 0; ++i) {
        g.tick(i > 30 && i < 40 ? down : i == 150 ? button : idle);
        if (over < 0 && g.mode() == game::Mode::GameOver) over = i;
        if (g.levelNumber() == 15) title = i;
    }
    if (over < 0 || title - over < 170 || title - over > 190) { std::printf("FAIL: continue: no led to the game over at %d, the title at %d\n", over, title); ++fails; }
    // no continues left
    loseAll(5, 0);
    if (g.mode() != game::Mode::GameOver) { std::printf("FAIL: continue: without continues the game is not over (level %d)\n", g.levelNumber()); ++fails; }
    std::printf("continue: yes out at %d; no: game over at %d, title at %d\n", out, over, title);
    return fails;
}
}

// The engine's transitions: a key on the title (after its first 61 ticks) fades out (19 steps of
// 30 ms), waits 500 ms and loads level 0, which fades in (30 ms of black, 15 steps); "*3" waits
// for the sounds, fades out, waits 750 ms and starts the logic again; the free colours come back
// exactly.  The big stone's flash (DAT_0046b1b0) brightens them and gives them back.
namespace {
int transitionTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    engine::HostInput idle, key;
    key.keyDown['A'] = true;
    int t = 0;
    while (g.levelNumber() != 15 && t < 3000) { g.tick(idle); ++t; }
    settle(g);
    for (int i = 0; i < 70; ++i) g.tick(idle);
    const ph::Rgb before = g.palette().colour(100);
    g.tick(key);
    int faded = 0, black = -1, loaded = -1;
    for (int i = 0; i < 200 && loaded < 0; ++i) {
        const ph::Rgb c = g.palette().shown(100);
        if (c.r < before.r) ++faded;
        if (black < 0 && c.r == 0 && c.g == 0 && c.b == 0) black = i;
        g.tick(idle);
        if (g.levelNumber() == 0) loaded = i;
    }
    const int fadeIn = settle(g);
    const ph::Rgb after = g.palette().shown(100), truth = g.palette().colour(100);
    if (loaded < 60 || loaded > 66 || fadeIn != 29 || black < 0 || after.r != truth.r || after.g != truth.g || after.b != truth.b || g.mode() != game::Mode::Playing) {
        std::printf("FAIL: transition: the title's start (faded %d, black at %d, level 0 at %d, fade in %d)\n", faded, black, loaded, fadeIn);
        ++fails;
    }
    // "*3"
    game::PlayLogic* pl = game::g_debugLogic;
    for (int i = 0; i < 20; ++i) g.tick(idle);
    pl->harry().b(0x465b72) = 0xfa;
    const int loads = g.levelLoads();
    g.tick(idle);
    int restarted = -1;
    const uint32_t ticks0 = g.logicTicks();
    for (int i = 0; i < 300 && restarted < 0; ++i) { g.tick(idle); if (g.logicTicks() != ticks0) restarted = i; }
    if (restarted < 76 || restarted > 80 || g.levelLoads() != loads) { std::printf("FAIL: transition: \"*3\" restarted at %d\n", restarted); ++fails; }
    std::printf("transition: title -> level 0 at %d (fade in %d), \"*3\" after %d\n", loaded, fadeIn, restarted);
    return fails;
}
}

// CD audio from ripped tracks: TrackNN.wav in $PITFALL_MUSIC_DIR is decoded to 44.1 kHz stereo;
// the level's music track (level 0: track 5) plays after its load, a pause stops it and its end
// plays it again, the stop-watch turns it down to a quarter, and a level's exit fades it out over
// two seconds before the next level loads (the map's track 4 is missing: silence).
namespace {
void writeWav(const std::string& path, int rate, int seconds10) {
    const int n = rate * seconds10 / 10;
    std::vector<uint8_t> d(44 + size_t(n) * 2);
    auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + size_t(i)] = uint8_t(v >> (8 * i)); };
    auto put16 = [&](size_t o, uint32_t v) { d[o] = uint8_t(v); d[o + 1] = uint8_t(v >> 8); };
    std::memcpy(d.data(), "RIFF", 4); put32(4, uint32_t(d.size() - 8)); std::memcpy(d.data() + 8, "WAVEfmt ", 8);
    put32(16, 16); put16(20, 1); put16(22, 1); put32(24, uint32_t(rate)); put32(28, uint32_t(rate * 2)); put16(32, 2); put16(34, 16);
    std::memcpy(d.data() + 36, "data", 4); put32(40, uint32_t(n * 2));
    for (int i = 0; i < n; ++i) put16(44 + size_t(i) * 2, uint16_t(int16_t((i % 50) * 400 - 10000)));
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}
int musicTest(const std::string& dir) {
    int fails = 0;
    const std::string noMusic = (std::filesystem::temp_directory_path() / "pitfall_no_music").string();
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "pitfall_music_test";
    std::filesystem::create_directories(tmp);
    writeWav((tmp / "Track05.wav").string(), 22050, 10);
    writeWav((tmp / "06 - unused.wav").string(), 44100, 5);
    engine::CdTracks probe(tmp.string() + "/none");
    std::vector<int16_t> pcm;
    if (!engine::CdTracks::decodeWavFile((tmp / "Track05.wav").string(), pcm) || pcm.size() < 2 * 44090 || pcm.size() > 2 * 44110) {
        std::printf("FAIL: music: the 22050 Hz track decoded to %zu samples\n", pcm.size()); ++fails;
    }
#ifdef _WIN32
    _putenv_s("PITFALL_MUSIC_DIR", tmp.string().c_str());
#else
    setenv("PITFALL_MUSIC_DIR", tmp.string().c_str(), 1);
#endif
    engine::Options opts;
    game::Game g(dir, opts);
#ifdef _WIN32
    _putenv_s("PITFALL_MUSIC_DIR", noMusic.c_str());
#else
    setenv("PITFALL_MUSIC_DIR", noMusic.c_str(), 1);
#endif
    g.setMode(game::Mode::Playing);
    g.newGame();
    g.startLevel(0);
    const bool playing = g.mixer().musicPlaying();
    settle(g);
    g.togglePause();
    const bool stopped = !g.mixer().musicPlaying() && g.paused();
    engine::HostInput key; key.keyDown['W'] = true;
    g.tick(engine::HostInput{});
    g.tick(key);                                          // a key ends the pause
    const bool again = g.mixer().musicPlaying() && !g.paused();
    g.setCdVolumeCmd(1);
    g.render();
    const int quarter = g.mixer().musicVolume();
    g.setCdVolumeCmd(2);
    g.render();
    const int full = g.mixer().musicVolume();
    g.completeLevel();
    int mapAt = -1, quieter = 0;
    for (int i = 0; i < 400 && mapAt < 0; ++i) {
        const int v = g.mixer().musicVolume();
        g.tick(engine::HostInput{});
        if (g.mixer().musicVolume() < v) ++quieter;
        if (g.levelNumber() == 19) mapAt = i;
    }
    if (!playing || !stopped || !again || quarter != (0x50 >> 2) * 0x28f || full != 0x50 * 0x28f || mapAt < 190 || mapAt > 200 ||
        quieter < 19 || g.mixer().musicPlaying() || g.mixer().musicVolume() != full) {
        std::printf("FAIL: music: playing %d, paused %d, again %d, volumes %#x / %#x, the map at %d after %d fade steps\n",
                    playing, stopped, again, quarter, full, mapAt, quieter);
        ++fails;
    }
    std::printf("music: track 5 at the load, the map at %d after a fade of %d steps\n", mapAt, quieter);
    std::filesystem::remove_all(tmp);
    return fails;
}
}

// The cutscenes (levels 21 .. 23, game/cutscene.cpp).  The boot's intro runs its script to "*8":
// its texts, Harry Jr. running in with the camera after him, the 1-up, the shadow's stamps shaking
// the screen, "DAAAD!" and his run off to the left; a key once the first 30 ticks and the fade in
// are over skips to its end.  The reunion runs to "*9" through its second room; the end's words
// (by the difficulty and the letters found) wait for a key, then the hand rises and the credits
// scroll ("[THE END]" first), which a key skips.
namespace {
// the ten actors at 0x45623f in the scenes' data
const uint8_t* actorAt(game::Game& g, int i) { return g.cutsceneData().data() + (0x45623f - 0x456000) + i * 0x40; }
std::string actorText(game::Game& g, int i) {
    const uint8_t* a = actorAt(g, i);
    uint32_t r, s;
    std::memcpy(&r, a + 0x1c, 4);
    std::memcpy(&s, a + 0x24, 4);
    return r == 0x4014e4 || r == 0x40157f ? game::cutsceneText(s) : "";
}
uint32_t actorLong(game::Game& g, int i, int off) {
    uint32_t v;
    std::memcpy(&v, actorAt(g, i) + off, 4);
    return v;
}
int cutsceneTest(const std::string& dir) {
    int fails = 0;
    engine::HostInput idle, key;
    key.keyDown[' '] = true;
    {
        engine::Options opts;
        game::Game g(dir, opts);                          // WinMain loads the intro
        int deep = -1, temple = -1, oneUp = -1, dad = -1, shake = -1, daaad = -1, title = -1, camMax = 0, shakes = 0;
        for (int t = 0; t < 2000 && title < 0; ++t) {
            g.tick(idle);
            if (deep < 0 && actorText(g, 0) == "DEEP IN THE MAYAN JUNGLE") deep = t;
            if (temple < 0 && actorText(g, 1) == "OF UAXACTUN") temple = t;
            if (oneUp < 0 && actorLong(g, 1, 8) == g.slotSprite(0x470248).addr) oneUp = t;   // mi_1up's record
            if (dad < 0 && actorText(g, 2) == "DAD, CHECK IT OUT") dad = t;
            if (g.state().bob != 0) { if (shake < 0) shake = t; ++shakes; }
            if (daaad < 0 && actorText(g, 2) == "DAAAD!") daaad = t;
            camMax = std::max(camMax, g.state().camX);
            if (g.levelNumber() == 15) title = t;
        }
        // (the script: 1 + 32 ticks of fade in, wait 100, the jungle's sounds, then the first text)
        if (deep != 133 || temple != 355 || oneUp != 667 || dad != 768 || camMax != 63 || shakes != 32 || shake < 1000 || daaad < shake ||
            title < 1420 || title > 1432) {
            std::printf("FAIL: intro: texts at %d / %d / %d / %d, 1-up at %d, camera to %d, %d shakes from %d, title at %d\n",
                        deep, temple, dad, daaad, oneUp, camMax, shakes, shake, title);
            ++fails;
        }
        std::printf("intro: texts at %d / %d / %d / %d, 1-up at %d, camera to %d, %d shakes from %d, title at %d\n",
                    deep, temple, dad, daaad, oneUp, camMax, shakes, shake, title);
    }
    {
        engine::Options opts;
        game::Game g(dir, opts);
        for (int t = 0; t < 40; ++t) g.tick(idle);
        g.tick(key);
        int title = -1;
        for (int t = 0; t < 20 && title < 0; ++t) { g.tick(idle); if (g.levelNumber() == 15) title = t; }
        if (title != 0) { std::printf("FAIL: intro: a key at tick 43 skipped it to the title at %d\n", title); ++fails; }
    }
    {
        engine::Options opts;
        game::Game g(dir, opts);
        g.setMode(game::Mode::Playing);
        g.startLevel(22);
        int dad = -1, reunion = -1, son = -1, credits = -1, camMax = 0;
        for (int t = 0; t < 2000 && credits < 0; ++t) {
            g.tick(idle);
            if (dad < 0 && actorText(g, 1) == "DAD!") dad = t;
            if (reunion < 0 && g.state().reunion) reunion = t;
            if (son < 0 && actorText(g, 1) == "SON, WHAT TOOK YOU") son = t;
            camMax = std::max(camMax, g.state().camX);
            if (g.levelNumber() == 23) credits = t;
        }
        if (dad < 385 || dad > 400 || reunion <= dad || son <= reunion || credits - son != 302 || camMax != 440) {
            std::printf("FAIL: reunion: \"DAD!\" at %d, the second room at %d, \"SON\" at %d, credits at %d, camera to %d\n", dad, reunion, son, credits, camMax);
            ++fails;
        }
        std::printf("reunion: \"DAD!\" at %d, the second room at %d, \"SON\" at %d, credits at %d\n", dad, reunion, son, credits);
    }
    {
        engine::Options opts;
        opts.difficulty = 1;
        game::Game g(dir, opts);
        g.setMode(game::Mode::Playing);
        g.stats().letters = 0x7f;
        g.startLevel(23);
        for (int t = 0; t < 400; ++t) g.tick(idle);
        int letters = 0;
        for (int i = 3; i < 10; ++i) letters += actorLong(g, i, 8) == g.slotSprite(0x470270).addr;   // mi_ptfal's
        if (actorText(g, 0) != "you have survived hard mode..." || actorText(g, 1) != "time for a photo to" ||
            actorText(g, 2) != "document your triumph." || letters != 7 || g.levelNumber() != 23 || g.state().camY != 0x20) {
            std::printf("FAIL: end: \"%s\" / \"%s\" / \"%s\", %d letters, level %d, camera y %d\n", actorText(g, 0).c_str(), actorText(g, 1).c_str(),
                        actorText(g, 2).c_str(), letters, g.levelNumber(), g.state().camY);
            ++fails;
        }
        g.tick(key);
        int handUp = -1, handCam = -1, theEnd = -1, title = -1;
        for (int t = 0; t < 2000 && title < 0; ++t) {
            g.tick(t == 700 ? key : idle);
            if (handUp < 0 && g.cutsceneData()[0x22f] == 1) { handUp = t; handCam = g.state().camY; }
            for (int i = 0; i < 10; ++i) if (theEnd < 0 && actorText(g, i) == "[THE END]") theEnd = t;
            if (g.levelNumber() == 15) title = t;
        }
        // (clear all, wait 30, the hand rises 0.375 px a tick for 200 ticks, the camera 0.125 px)
        if (handUp != 229 || handCam != 7 || theEnd != 291 || title != 701) {
            std::printf("FAIL: end: the hand up at %d (camera y %d), \"[THE END]\" at %d, skipped to the title at %d\n", handUp, handCam, theEnd, title);
            ++fails;
        }
        std::printf("end: the words and the letters, the hand up at %d, \"[THE END]\" at %d, skipped at %d\n", handUp, theEnd, title);
    }
    return fails;
}

// The title (level 15): the camera starts at (0, 0x10) and pans 2 px a tick from the runner's x
// 0x1f to the map's edge; the logo drops in at its tick 0xd3; no key counts before tick 0x3d, nor
// while the stones break away (0x146 .. 0x162); the jaguar runs at 0x1f5.
int titleTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    g.setMode(game::Mode::Playing);
    g.startLevel(15);
    settle(g);
    engine::HostInput idle, key;
    key.keyDown[' '] = true;
    int panStart = -1, panEnd = -1, started = -1;
    const int y0 = g.state().scrollY;
    for (int t = 0; t < 0x200 && started < 0; ++t) {
        const int f = g.state().frame + 1;
        const bool press = f == 0x30 || f == 0x150;   // too early, and during the stones
        const int x = g.state().scrollX;
        g.tick(press ? key : idle);
        if (g.transitionActive()) { started = f; break; }
        g.render();
        if (panStart < 0 && g.state().scrollX != x) panStart = g.state().frame;
        if (panEnd < 0 && g.state().scrollX == g.state().maxScrollX) panEnd = g.state().frame;
    }
    g.tick(key);
    const bool startsAfter = g.transitionActive();
    if (y0 != 0x10 || panStart != 58 || panEnd != 217 || started != -1 || !startsAfter) {
        std::printf("FAIL: title: camera y %d, pans from tick %d to %d, started by a key at %d, a key later starts %d\n", y0, panStart, panEnd, started, startsAfter);
        ++fails;
    }
    std::printf("title: the camera pans from tick %d to %d; no start at ticks 0x30 and 0x150\n", panStart, panEnd);
    return fails;
}

// Level 24, the secret level "*50" leads to with the cheat: its crate is Earthworm Jim (ew_ready,
// 0x40e18d).  Whipped from his right he blows up like a crate (999 points, Harry thrown right,
// still on the ledge) and 200 ticks later FUN_0043628c leaves with "*6": back to the Lost City
// of Copan as from the 2600 level.
int earthwormTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.setMode(game::Mode::Playing);
    g.newGame();
    g.warp().returnLevel = 0xb;                           // as "*50" leaves it
    g.startLevel(24);
    settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    flyTo(g, pl, 1390, 862);
    engine::HostInput idle, left, whip;
    left.keyDown[0x25] = true; whip.keyDown['Q'] = true;
    for (int t = 0; t < 33; ++t) g.tick(t < 30 ? idle : left);   // land, face left
    const int score0 = g.stats().score;
    int hit = -1, left24 = -1;
    for (int t = 0; t < 400 && left24 < 0; ++t) {
        g.tick(hit < 0 && t % 20 < 2 ? whip : idle);
        if (hit < 0 && g.stats().score == score0 + 999) hit = t;
        if (left24 < 0 && g.exitPending()) left24 = t;
    }
    settle(g);
    if (hit < 0 || left24 - hit != 200 || g.levelNumber() != 3 || g.stats().lives != 4) {
        std::printf("FAIL: earthworm: hit at %d, left at %d, level %d, lives %d\n", hit, left24, g.levelNumber(), g.stats().lives);
        ++fails;
    }
    std::printf("earthworm: whipped at %d, \"*6\" at %d, back to level %d\n", hit, left24, g.levelNumber());
    return fails;
}

// The typed cheat codes (WM_CHAR, 0x439868): the last characters typed spell a code, in any case;
// a code toggles or sets its cheat each time it is completed.  The menu's level items come on as
// the levels are reached, but in the demos.
int cheatTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    auto type = [&](const char* s) { for (; *s; ++s) g.typeChar(uint8_t(*s)); };
    const uint32_t boot = g.menuUnlocks();
    type("vampir");
    const bool partial = g.invincible();
    type("e");
    const bool on = g.invincible();
    type("xvampire");
    const bool off = !g.invincible();
    type("MijmrowhTRAE");
    const bool jim = g.warp().cheat;
    type("eatmorebranmeowmeowlikemeowmanpumpyouup");
    const bool stats = g.stats().continues == 99 && g.stats().lives == 99 && g.stats().ammoCount[0] == 99 &&
                       g.stats().ammoCount[1] == 99 && g.stats().ammoCount[2] == 99;
    type("iddqd");
    const int doom = g.takeCheatDialog();
    type("paidtoplaygames");
    const int testers = g.takeCheatDialog();
    int hats[3];
    for (int& h : hats) { type("hatman"); h = g.hatman(); }
    type("framerate beammeup");
    const bool shown = opts.frameRate && g.showPosition();
    type("simonsaysplaysimon");
    const uint32_t vaults = g.menuUnlocks();
    type("idbuythatforadollar");
    const uint32_t all = g.menuUnlocks();
    if (boot != (1u | 1u << 21) || partial || !on || !off || !jim || !stats || doom != 0x7f || testers != 0x100 ||
        hats[0] != 1 || hats[1] != 2 || hats[2] != 0 || !shown || vaults != (boot | 0x3800) || all != 0xffffffffu) {
        std::printf("FAIL: cheats: menu %#x, vampire %d/%d/%d, jim %d, stats %d, dialogs %#x %#x, hatman %d %d %d, shown %d, menu %#x %#x\n",
                    boot, partial, on, off, jim, stats, doom, testers, hats[0], hats[1], hats[2], shown, vaults, all);
        ++fails;
    }
    // a demo's level does not come on in the menu, a level started does (the bits are the options')
    opts.levelBits = 1;
    game::Game g2(dir, opts);
    engine::HostInput idle;
    for (int t = 0; t < 9000 && !(g2.levelNumber() == 1 && !g2.transitionActive()); ++t) g2.tick(idle);   // the intro, the title, demo 0, the title, demo 1 on level 1
    for (int t = 0; t < 60; ++t) g2.tick(idle);
    const bool demoOff = g2.levelNumber() == 1 && g2.mode() == game::Mode::Attract && !(g2.menuUnlocks() & 1u << 1) && (g2.menuUnlocks() & 1u << 15);
    g2.setMode(game::Mode::Playing);
    g2.startLevel(5);
    if (!demoOff || !(g2.menuUnlocks() & 1u << 5)) {
        std::printf("FAIL: cheats: the menu after a demo %#x (level %d), after level 5 %#x\n", g2.menuUnlocks(), g2.levelNumber(), g2.menuUnlocks());
        ++fails;
    }
    std::printf("cheats: vampire, earthworm jim, 99s, the two dialogs, hatman, the frame rate and position, the menu's levels\n");
    return fails;
}

// The original's menu (FUN_0043b920 / FUN_0043a370), its Option_Level bits (FUN_0043a2b0 /
// FUN_0043a230), the status bar's text (FUN_00440bc0) and the windowed load's dialog hook.
int menuTest(const std::string& dir) {
    int fails = 0;
    bool codec = true;
    for (uint32_t bits : {1u, 0x1fu, 0x123457u, 0x3800u | 0x100000u, 0xffffffffu, 0x80000001u})
        if (engine::levelBitsDecode(engine::levelBitsEncode(bits)) != bits) codec = false;
    std::array<uint8_t, 64> b = engine::levelBitsEncode(0x1234);
    b[1] ^= 1;                                                   // a random byte changed
    const bool tamper = engine::levelBitsDecode(b) == 1;
    b = engine::levelBitsEncode(0x1234);
    b[63] ^= 0x80;                                               // the sum wrong
    const bool sum = engine::levelBitsDecode(b) == 1;
    engine::Options opts;
    game::Game g(dir, opts);
    std::vector<int> hook;
    g.loadingHook = [&](int phase) { hook.push_back(phase); };
    const bool bootAsk = g.menuNeedsConfirm();                   // the intro
    const std::string bootText = g.statusText();
    g.stats().score = 1234;
    g.menuStart(3);
    engine::HostInput idle;
    const bool fresh = g.levelNumber() == 3 && g.stats().score == 0 && g.mode() == game::Mode::Playing &&
                       g.statusText() == "\tLost City of Copan" && (g.menuUnlocks() & 1u << 3) && hook == std::vector<int>{1, 0};
    const bool noPoints = !g.menuNeedsConfirm();
    g.stats().score = 50;
    settle(g);
    for (int t = 0; t < 2; ++t) g.tick(idle);
    const bool ask = g.menuNeedsConfirm();
    g.specialKey();
    const bool idleKey = g.specialIdle();
    hook.clear();
    g.startLevel(game::kTitleScreen);
    const bool titleNoDialog = hook == std::vector<int>{2, 0} && g.statusText() == " " && !g.menuNeedsConfirm();
    g.menuStart(game::kLoltunVault1);
    const bool vault = g.statusText() == "\tLoltun Vault 1";
    g.command("*2");
    int t = 0;
    for (; t < 400 && g.mode() != game::Mode::GameOver; ++t) g.tick(idle);
    const bool over = g.mode() == game::Mode::GameOver && g.statusText() == " ";
    if (!codec || !tamper || !sum || bootAsk || bootText != " " || !fresh || !noPoints || !ask || !idleKey || !titleNoDialog || !vault || !over) {
        std::printf("FAIL: menu: codec %d tamper %d sum %d, boot ask %d text '%s', fresh %d, no points %d, ask %d, idle %d, title %d, vault %d, game over %d\n",
                    codec, tamper, sum, bootAsk, bootText.c_str(), fresh, noPoints, ask, idleKey, titleNoDialog, vault, over);
        ++fails;
    } else {
        std::printf("menu: Option_Level's bytes, the question only with points, a new game on the level, its name in the status bar, the loading dialog\n");
    }
    return fails;
}

// An engine exit unwinds the logic's tick at once (engineExit puts the bridge's stack back): the
// level's exit Harry's triggers take ("*4", 0x430923) leaves that tick's object pass, its timers
// (FUN_00433a09) and its end (0x4363bd, the tick count) undone.
int exitTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    g.setMode(game::Mode::Playing);
    g.startLevel(0);
    settle(g);
    engine::HostInput idle;
    for (int t = 0; t < 30; ++t) g.tick(idle);
    game::Harry& m = g.logicMemory();
    uint8_t timer = m.b(0x465d4b);
    uint32_t ticks = g.logicTicks();
    g.tick(idle);
    const bool usual = m.b(0x465d4b) != timer && g.logicTicks() == ticks + 1 && !g.exitPending();
    timer = m.b(0x465d4b);
    ticks = g.logicTicks();
    m.x() = 0xa18;                                               // Harry on the level's exit (trigger 0x5f)
    m.y() = 0x3c0;
    g.tick(idle);
    const bool unwound = m.b(0x465d4b) == timer && g.logicTicks() == ticks && g.transitionActive();
    settle(g);
    const bool map = g.levelNumber() == game::kMapScreen;
    if (!usual || !unwound || !map) {
        std::printf("FAIL: exit: a usual tick %d, the exit's tick unwound %d, then the map %d (level %d)\n", usual, unwound, map, g.levelNumber());
        ++fails;
    } else {
        std::printf("exit: an engine exit leaves the rest of the tick undone (the objects, the timers, the end)\n");
    }
    return fails;
}

// What the logic leaves in its memory and reads again: the spawners keep the .als working copy's
// address (the load block where GlobalAlloc put it) in DAT_00466788, of which each weapon test
// clears only the low word, so a whip's hit on a gargoyle chips a piece off that flies at the
// address's high word (0x412987).
int leftoversTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.newGame();
    g.startLevel(3); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) return 1;
    const uint32_t als = ph::Arena::kAddress + g.arenaLayout().als, high = als & 0xffff0000u;
    const game::Harry& h = pl->harry();
    const bool kept = (h.ul(0x466788) & 0xffff0000u) == high && h.ul(0x46678c) == 0x4678c4;
    flyTo(g, pl, 600, 414);                                // below the gargoyle at (616, 336)
    engine::HostInput idle, right, whip;
    right.keyDown[0x27] = true; whip.keyDown['Q'] = true;
    for (int t = 0; t < 33; ++t) g.tick(t < 30 ? idle : right);
    int chipAt = -1;
    for (int t = 0; t < 40 && chipAt < 0; ++t) {
        g.tick(t < 2 ? whip : idle);
        for (int r = 0; r < 0x50; ++r) {
            game::Obj& o = pl->objectSystem().record(r);   // (freed in its tick, the record keeps it)
            if (o.sprite && o.sprite->name() == "gargrok" && uint32_t(o.l(0x32)) == high) chipAt = t;
        }
    }
    if (!kept || chipAt < 0) {
        std::printf("FAIL: leftovers: the .als address kept %d (%#x for %#x), a chip at %d\n", kept, h.ul(0x466788), als, chipAt);
        ++fails;
    } else {
        std::printf("leftovers: the spawners' .als address (%#x) kept in DAT_00466788; a whip's hit chips the gargoyle at %d, the chip flying at %#x\n", als, chipAt, high);
    }
    return fails;
}

// The pointers in the records and the logic's memory hold the original's addresses: Harry's sprite
// word the sprite's place in the load block, the bungee vine its pieces' records (l50 / l54 / l6a)
// and the stretch piece its rope segments (a word each at +0x32.., the record's address less
// 0x46284a).
int pointersTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts; opts.allowCheat = true;
    game::Game g(dir, opts);
    g.startLevel(0); g.setMode(game::Mode::Playing); settle(g);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) return 1;
    const game::Harry& h = pl->harry();
    const ph::Sprite* hs = h.spriteRecord();
    const bool harry = hs && h.ul(0x465b1d) == hs->arenaAddr() && hs->arenaAddr() > ph::Arena::kAddress;
    for (int i = 0; i < 3000; ++i) {                      // fly to the branch with the vine
        if (std::abs(h.x() - 24) <= 7 && std::abs(h.y() - 650) <= 7) break;
        engine::HostInput fly; fly.ctrlDown = true;
        fly.keyDown[h.x() < 24 ? 0x27 : 0x25] = std::abs(h.x() - 24) > 7;
        fly.keyDown[h.y() < 650 ? 0x28 : 0x26] = std::abs(h.y() - 650) > 7;
        g.tick(fly);
    }
    engine::HostInput idle;
    for (int i = 0; i < 4; ++i) g.tick(idle);
    int pieces = 0, segments = -1;
    for (game::Obj* o = pl->objectSystem().first(); o; o = pl->objectSystem().nextOf(*o)) {
        if (o->type != 27) continue;
        const game::Obj* top = pl->objAt(uint32_t(o->l(0x50)));
        const game::Obj* body = pl->objAt(uint32_t(o->l(0x54)));
        const game::Obj* stub = pl->objAt(uint32_t(o->l(0x6a)));
        auto named = [](const game::Obj* r, const char* n) { return r && r->sprite && r->sprite->name() == n; };
        pieces = named(top, "ob_bunge") + named(body, "ob_bunge") + named(stub, "ob_bungs");
        if (!body) break;
        segments = 0;
        for (int k = 0; k < 23; ++k) {
            uint16_t w; std::memcpy(&w, body->s + 0x32 + 2 * k, 2);
            if (w == 0) break;
            if (!named(pl->objAt(0x46284au + w), "ob_bunge")) { segments = -1; break; }
            ++segments;
        }
        break;
    }
    if (!harry || pieces != 3 || segments <= 0) {
        std::printf("FAIL: pointers: Harry's sprite word %#x (%s), the vine's pieces %d, its segments %d\n",
                    h.ul(0x465b1d), hs ? hs->name().c_str() : "-", pieces, segments);
        ++fails;
    } else {
        std::printf("pointers: Harry's sprite word %#x (%s in the load block); the bungee vine's 3 pieces and %d rope segment%s by their records' addresses\n",
                    h.ul(0x465b1d), hs->name().c_str(), segments, segments == 1 ? "" : "s");
    }
    return fails;
}

// The sprite slots as the original's loads leave them (FUN_004453a0, a call a sprite): a slot no
// load has filled holds its name's address (jgustill's, which the jaguar man's hidden state takes
// on level 8); level 17 loads ob_door3 twice, for its own slot and for ob_door's, at two
// addresses; a sprite collides with the box of the slot it was loaded for (level 3's bat,
// btrfly, with btzfly's).
int slotsTest(const std::string& dir) {
    int fails = 0;
    engine::Options opts;
    game::Game g(dir, opts);
    const game::SpriteSlots& sl = g.spriteSlots();
    g.startLevel(8);
    const uint32_t still = sl.of(game::spriteSlotOf("jgustill"));
    g.startLevel(17);
    const uint32_t d3 = sl.of(game::spriteSlotOf("ob_door3")), d = sl.of(game::spriteSlotOf("ob_door"));
    const ph::Sprite* a = sl.at(d3);
    const ph::Sprite* b = sl.at(d);
    const bool doors = a && b && a != b && a->name() == "ob_door3" && b->name() == "ob_door3";
    g.startLevel(3);
    const ph::Sprite* bat = sl.at(sl.of(game::spriteSlotOf("btzfly")));
    const ph::SpriteHitbox* box = ph::hitboxOf(bat);
    const bool bats = bat && bat->name() == "btrfly" && box && std::strcmp(box->sprite, "btzfly") == 0;
    if (still != 0x470acc || !doors || !bats) {
        std::printf("FAIL: slots: jgustill's %#x, level 17's doors %#x / %#x, level 3's bat %s with %s's box\n",
                    still, d3, d, bat ? bat->name().c_str() : "-", box ? box->sprite : "no");
        ++fails;
    } else {
        std::printf("slots: jgustill's slot holds its name's address (%#x) on level 8; level 17's two ob_door3 at %#x / %#x; level 3's btrfly with btzfly's box\n",
                    still, d3, d);
    }
    return fails;
}
}
