// hstate <level> <ticks> [keys] [every]: run a level from its start and print Harry's state.
// keys: a string of per-tick key groups separated by ',' ("R" right, "L" left, "U" up, "D" down,
// "J" jump, "W" whip, "S" shoot, "." nothing), each optionally followed by *N to repeat it.
#include "game/game.h"
#include "game/play_logic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace game { extern PlayLogic* g_debugLogic; }
int main(int argc, char** argv) {
    engine::Options opts; opts.allowCheat = true; game::Game g("original", opts);
    const int level = std::atoi(argv[1]), ticks = std::atoi(argv[2]);
    std::vector<std::string> seq;
    if (argc > 3) {
        std::string s = argv[3];
        size_t p = 0;
        while (p <= s.size()) {
            size_t q = s.find(',', p); if (q == std::string::npos) q = s.size();
            std::string t = s.substr(p, q - p);
            int n = 1; size_t st = t.find('*'); if (st != std::string::npos) { n = std::atoi(t.c_str() + st + 1); t = t.substr(0, st); }
            for (int i = 0; i < n; ++i) seq.push_back(t);
            p = q + 1;
        }
    }
    const int every = argc > 4 ? std::atoi(argv[4]) : 1;
    g.startLevel(level); g.setMode(game::Mode::Playing);
    for (int i = 0; i < ticks; ++i) {
        engine::HostInput in;
        const std::string k = i < int(seq.size()) ? seq[size_t(i)] : std::string();
        for (char c : k) {
            if (c == 'R') in.keyDown[0x27] = true; if (c == 'L') in.keyDown[0x25] = true;
            if (c == 'U') in.keyDown[0x26] = true; if (c == 'D') in.keyDown[0x28] = true;
            if (c == 'J') in.keyDown['W'] = true; if (c == 'W') in.keyDown['Q'] = true; if (c == 'S') in.keyDown['E'] = true;
        }
        g.tick(in);
        if (i % every) continue;
        game::PlayLogic* pl = game::g_debugLogic;
        const game::Harry& h = pl->harry();
        std::printf("t%4d lv%d %-2s (%d.%04x,%d.%04x) v(%x,%x) %06x %-9s f%-3d g%d fc%d dmg%d b72=%x cam(%d,%d) sc%d\n", i, g.levelNumber(), k.c_str(),
                    h.x(), h.xFrac(), h.y(), h.yFrac(), uint32_t(h.vx()), uint32_t(h.vy()), h.cont(), h.sprite(), h.frame(), h.onGround() ? 1 : 0,
                    h.facing(), h.damage(), h.b(0x465b72), g.state().scrollX, g.state().scrollY, g.stats().score);
    }
}
