#include "game/game.h"
#include "game/play_logic.h"
#include <cstdio>
#include <cstdlib>
namespace game { extern PlayLogic* g_debugLogic; }
using game::g_debugLogic;
// dbg9 <level> <x> <y> <ticks> <type> <whipEvery>: fly to x,y, stand, whip periodically; list objects of <type> and anything near Harry
int main(int argc, char** argv) {
    engine::Options opts; opts.allowCheat = true; game::Game g("original", opts);
    int level = std::atoi(argv[1]), tx = std::atoi(argv[2]), ty = std::atoi(argv[3]), ticks = std::atoi(argv[4]), type = std::atoi(argv[5]), we = std::atoi(argv[6]);
    g.startLevel(level); g.setMode(game::Mode::Playing);
    for (int i = 0; i < 3000; ++i) {
        engine::HostInput fly; fly.ctrlDown = true;
        const game::Harry& h = g_debugLogic->harry();
        if (std::abs(h.x - tx) <= 7 && std::abs(h.y - ty) <= 7) break;
        fly.keyDown[h.x < tx ? 0x27 : 0x25] = std::abs(h.x - tx) > 7;
        fly.keyDown[h.y < ty ? 0x28 : 0x26] = std::abs(h.y - ty) > 7;
        g.tick(fly);
    }
    for (int i = 0; i < ticks; ++i) {
        engine::HostInput in; if (we && i % we == 0) in.keyDown['Q'] = true;
        g.tick(in);
        if (i % 10) continue;
        const game::Harry& h = g_debugLogic->harry();
        std::printf("t%3d H(%d,%d %s dmg %d) score %d:", i, h.x, h.y, h.anim, h.damage, g.stats().score);
        for (auto& p : g_debugLogic->objectSystem().objects()) if (!p->freed && (p->type == type || (std::abs(p->x - h.x) < 120 && std::abs(p->y - h.y) < 120)))
            std::printf(" [%s t%d (%d,%d) f%d]", p->sprite ? p->sprite->name().c_str() : "-", p->type, p->x, p->y, p->frame);
        std::printf("\n");
    }
}
