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
namespace { int jaguarTest(const std::string& dir); int bungeeTest(const std::string& dir); int stoneTest(const std::string& dir);
            int vortexTest(const std::string& dir); int deathTest(const std::string& dir); int vaultTest(const std::string& dir);
            int stoneJaguarTest(const std::string& dir); }
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
    fails += stoneTest(dir);
    fails += vortexTest(dir);
    fails += deathTest(dir);
    fails += vaultTest(dir);
    fails += stoneJaguarTest(dir);
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
    g.startLevel(0); g.setMode(game::Mode::Playing);
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
    g.startLevel(0); g.setMode(game::Mode::Playing);
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
    g.startLevel(8); g.setMode(game::Mode::Playing);
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
    g.tick(engine::HostInput{});
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
    g.startLevel(0); g.setMode(game::Mode::Playing);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) return 1;
    const size_t silver = alsEntry(g.level().als, 11, 17);
    auto id = [&](size_t e) { return int(pl->alsLong(e + 1) & 0xff); };
    flyTo(g, pl, 200, 250);
    for (int i = 0; i < 60; ++i) { engine::HostInput in; if (i > 20) in.keyDown[i < 40 ? 0x25 : 0x27] = true; g.tick(in); }
    const int score = g.stats().score, lives = g.stats().lives;
    if (id(silver) != 0) { std::printf("FAIL: the silver was not collected (score %d)\n", score); ++fails; }
    pl->harry().b(0x465b72) = 0xfa;                       // fell out of the level
    g.tick(engine::HostInput{});
    const game::Harry& h = pl->harry();
    if (game::g_debugLogic != pl) { std::printf("FAIL: the level was loaded again\n"); ++fails; }
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
    g.startLevel(12); g.setMode(game::Mode::Playing);
    game::PlayLogic* pl = game::g_debugLogic;
    if (!pl) return 1;
    const int score0 = g.stats().score, lives0 = g.stats().lives;
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
        if (game::g_debugLogic != pl) { outAt = t; break; }
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
    g.startLevel(3); g.setMode(game::Mode::Playing);
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
    if (outAt < 0 || g.levelNumber() != 4) { std::printf("FAIL: Harry did not leave the level by the rope (level %d)\n", g.levelNumber()); ++fails; }
    std::printf("stone jaguars: fight at tick %d, both awake at %d, ledge at %d, dead at %d, rope at %d, top at %d, level 4 at %d\n",
                fightAt, bothAwakeAt, ledgeAt, deadAt, ropeAt, climbedAt, outAt);
    return fails;
}
}
