#include "game/game.h"
#include "game/play_logic.h"
#include <cstdio>
#include <cstdlib>
namespace game { extern PlayLogic* g_debugLogic; }
using game::g_debugLogic;
// dbg5 <level> <ticks> <x> <y> <type>: hover Harry at x,y (debug fly), dump the state bytes of objects of <type> each tick
int main(int argc, char** argv) {
    engine::Options opts; opts.allowCheat = true; game::Game g("original", opts);
    int level = std::atoi(argv[1]), ticks = std::atoi(argv[2]), tx = std::atoi(argv[3]), ty = std::atoi(argv[4]), type = std::atoi(argv[5]);
    g.startLevel(level); g.setMode(game::Mode::Playing);
    for (int i = 0; i < 2000 && tx >= 0; ++i) {
        engine::HostInput fly; fly.ctrlDown = true;
        int hx = -1, hy = -1;
        for (const auto& s : g.state().displayList) if (s.sprite && s.sprite->name().rfind("hyi",0)==0) { hx = s.x - 16; hy = s.y - 16; }
        if (hx < 0) { g.tick(fly); continue; }
        if (std::abs(hx - tx) <= 7 && std::abs(hy - ty) <= 7) break;
        fly.keyDown[hx < tx ? 0x27 : 0x25] = std::abs(hx - tx) > 7;
        fly.keyDown[hy < ty ? 0x28 : 0x26] = std::abs(hy - ty) > 7;
        g.tick(fly);
    }
    engine::HostInput fly; fly.ctrlDown = tx >= 0; if (tx < 0) fly.keyDown[0x27] = true;
    for (int i = 0; i < ticks; ++i) {
        fly.keyDown['W'] = tx < 0 && (i % 30) < 2;
        engine::HostInput idle; g.tick(i < 2 && tx < 0 ? idle : fly);
        // reach into the logic through the display list is not enough; use the object system via a friend accessor

        if (g_debugLogic) for (auto& p : g_debugLogic->objectSystem().objects()) if (!p->freed && p->type == type) {
            std::printf("t%3d frame %3d x %d y %d:", i, p->frame, p->x, p->y);
            for (int k = 0x30; k < 0x6e; k += 2) std::printf(" %02x%02x", p->s[k+1], p->s[k]);
            std::printf("\n");
        }
    }
}
