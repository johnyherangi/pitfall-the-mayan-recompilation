// memdump <ticks>: the port's run as cmpmem.py reads it, a line per engine tick (the driver's loop
// step): the step, the engine's tick count, the logic's, the level, the random generator, the logic's
// memory 0x465800 .. 0x4668ff (with the player's numbers the port keeps apart put at their
// addresses) and the 80 object records in the original's layout (what the port keeps as pointers,
// +0x0e .. +0x1b, left 0; the .als entry at +0x1c as the original's address).
//
//   MENU=<level>@<step>  the level picked from the menu before that step (the original's start + 40)
//   PAD=<file>           mkpad.py's joypad, replayed tick by tick from the level's start (Game::joypadHook)
//   KEYS=vk@from-to,...  or keys held (Windows key codes, from one step to before another, -1: on)
//
// Build it as re/drivers' and run it from the repository root (it loads original/).
#include "game/game.h"
#include "game/play_logic.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace game { extern PlayLogic* g_debugLogic; }
int main(int argc, char** argv) {
    const int t1 = std::atoi(argv[1]);
    engine::Options opts; opts.soundEnabled = false; opts.slow = true;
    game::Game g("original", opts);
    int menuLevel = -1, menuTick = -1;
    if (const char* m = std::getenv("MENU")) std::sscanf(m, "%d@%d", &menuLevel, &menuTick);
    std::vector<std::array<int, 3>> keys;
    if (const char* ks = std::getenv("KEYS")) {
        std::string s(ks); size_t p = 0;
        while (p < s.size()) {
            size_t e = s.find(',', p); if (e == std::string::npos) e = s.size();
            std::array<int, 3> k{}; std::sscanf(s.substr(p, e - p).c_str(), "%i@%d-%d", &k[0], &k[1], &k[2]);
            keys.push_back(k); p = e + 1;
        }
    }
    // PAD=<file from mkpad.py>: the original's joypad replayed by engine tick from the level's start
    // (the port's first tick on the menu's level against the original's), through the default keys
    std::vector<int> pad; int oStart = -1, pStart = -1;
    if (const char* pf = std::getenv("PAD")) {
        FILE* f = std::fopen(pf, "r");
        char line[128];
        while (f && std::fgets(line, sizeof line, f)) {
            int a = 0, b = 0;
            if (std::sscanf(line, "start %d", &a) == 1) { oStart = a; continue; }
            if (std::sscanf(line, "%d %d", &a, &b) == 2 && a >= 0) {
                if (size_t(a) >= pad.size()) pad.resize(size_t(a) + 1, 0);
                pad[size_t(a)] = b;
            }
        }
        if (f) std::fclose(f);
    }
    if (!pad.empty())
        g.joypadHook = [&](uint32_t gf) -> int {          // each engine tick's, the load's ticks too
            if (pStart < 0) return -1;
            const long og = long(gf) - pStart + oStart;
            return og >= 0 && size_t(og) < pad.size() ? pad[size_t(og)] : 0;
        };
    for (int t = 0; t <= t1; ++t) {
        if (t == menuTick) g.menuStart(menuLevel);
        engine::HostInput hi;
        for (const auto& k : keys) if (t >= k[1] && (k[2] < 0 || t < k[2])) hi.keyDown[size_t(k[0])] = true;
        if (!pad.empty() && pStart < 0 && t > menuTick && menuTick >= 0 && g.levelNumber() == menuLevel) pStart = int(g.state().globalFrame);
        g.tick(hi);
        g.render();
        std::printf("%d %d %u %d ", t, g.state().globalFrame, g.logicTicks(), g.levelNumber());
        for (int i = 0; i < 12; ++i) std::printf("%02x", *g.logicMemory().at(0x466772 + uint32_t(i)));
        std::printf(" ");
        uint8_t mem[game::Harry::kSize];
        std::memcpy(mem, g.logicMemory().mem, sizeof mem);
        const uint32_t alsAddr = ph::Arena::kAddress + g.arenaLayout().als;
        auto entryAddr = [&](int als) { return als < 0 ? 0x466770u : alsAddr + uint32_t(als); };
        {
            // the player's numbers at their addresses
            auto put = [&](uint32_t a, const void* v, size_t n) { std::memcpy(mem + (a - game::Harry::kBase), v, n); };
            const auto& st = g.stats();
            const int32_t score = st.score; put(0x46653b, &score, 4);
            const uint8_t b[] = {uint8_t(st.treasures), uint8_t(st.lives), uint8_t(st.ammoCount[0]), uint8_t(st.ammoCount[1]), uint8_t(st.ammoCount[2])};
            put(0x466526, b, 5);
            const uint8_t c = uint8_t(st.continues), w = uint8_t(st.weapon), l = uint8_t(st.letters);
            put(0x465ae3, &c, 1); put(0x46650b, &w, 1); put(0x465896, &l, 1);
            put(0x466510, g.levelFlags(), 0x16);              // the level-script flags (Game::levelFlags_)
        }
        for (uint32_t i = 0; i < game::Harry::kSize; ++i) std::printf("%02x", mem[i]);
        std::printf(" ");
        if (game::g_debugLogic) {
            auto& os = game::g_debugLogic->objectSystem();
            for (int r = 0; r < 0x50; ++r) {
                const game::Obj& o = os.record(r);
                uint8_t b[0x6e] = {};
                std::memcpy(b + 0, &o.x, 2); std::memcpy(b + 2, &o.xFrac, 2); std::memcpy(b + 4, &o.y, 2); std::memcpy(b + 6, &o.yFrac, 2);
                b[8] = o.type; b[9] = o.frame; b[0xa] = o.hits; b[0xb] = o.facing; std::memcpy(b + 0xc, &o.flags, 2);
                const uint32_t ent = entryAddr(o.als); std::memcpy(b + 0x1c, &ent, 4); b[0x20] = o.id;
                b[0x28] = o.behind; b[0x29] = o.vflip; b[0x2d] = o.whipCool; b[0x2e] = o.touchTick;
                std::memcpy(b + 0x30, o.s + 0x30, 0x6e - 0x30);
                for (int i = 0; i < 0x6e; ++i) std::printf("%02x", b[i]);
            }
        }
        std::printf("\n");
    }
}
