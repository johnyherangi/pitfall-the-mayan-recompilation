#include "game/game.h"
#include "game/play_logic.h"
#include <cstdio>
#include <cstdlib>
namespace game { extern PlayLogic* g_debugLogic; }
using game::g_debugLogic;
// dbg10 <level> <x> <y> <ticks> <type> <upAt>: fly to x,y, stand; press UP at tick upAt (hold 2); print objects of <type> + nearby
int main(int argc, char** argv) {
    engine::Options opts; opts.allowCheat = true; game::Game g("original", opts);
    int level = std::atoi(argv[1]), tx = std::atoi(argv[2]), ty = std::atoi(argv[3]), ticks = std::atoi(argv[4]), type = std::atoi(argv[5]), upAt = std::atoi(argv[6]);
    int walk = argc > 7 ? std::atoi(argv[7]) : 0;
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
        engine::HostInput in; if (i >= upAt && i < upAt + 2) in.keyDown[0x26] = true;
        if (walk && i > upAt + 60) in.keyDown[walk > 0 ? 0x27 : 0x25] = true;
        g.tick(in);
        if (i % 10) continue;
        const game::Harry& h = g_debugLogic->harry();
        std::printf("t%3d H(%d,%d %s f%d) flags", i, h.x, h.y, h.anim, h.frame);
        for (int k = 4; k < 0x16; ++k) std::printf("%d", g.levelFlags()[k] ? 1 : 0);
        std::printf(" cls53=%d:", g_debugLogic->triggerClassOf(0x53));
        for (auto& p : g_debugLogic->objectSystem().objects()) if (!p->freed && (p->type == type || p->type == 31 || p->type == 33 || p->type == 26))
            std::printf(" [%s t%d (%d,%d) f%d w34=%d b36=%d]", p->sprite ? p->sprite->name().c_str() : "-", p->type, p->x, p->y, p->frame, int(p->w(0x34)), int(p->b(0x36)));
        std::printf("\n");
    }
}
