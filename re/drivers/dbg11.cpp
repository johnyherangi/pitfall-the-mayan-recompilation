#include "game/game.h"
#include "game/play_logic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <utility>
namespace game { extern PlayLogic* g_debugLogic; }
using game::g_debugLogic;
// dbg11 <level> <x> <y> <ticks> <type> <whipEvery> [walkDir] [every]: fly to x,y; stand; whip every N ticks (0 = never);
// walkDir -1/0/1 holds left/right; prints Harry and all objects of <type> (+ bones type 29) with state bytes.
int main(int argc, char** argv) {
    engine::Options opts; opts.allowCheat = true; game::Game g("original", opts);
    int level = std::atoi(argv[1]), tx = std::atoi(argv[2]), ty = std::atoi(argv[3]), ticks = std::atoi(argv[4]), type = std::atoi(argv[5]), we = std::atoi(argv[6]);
    int walk = argc > 7 ? std::atoi(argv[7]) : 0, every = argc > 8 ? std::atoi(argv[8]) : 10;
    g.startLevel(level); g.setMode(game::Mode::Playing);
    // VIA=x,y;x,y... optional waypoints flown through first
    std::vector<std::pair<int,int>> legs;
    if (const char* via = std::getenv("VIA")) { int a, b; const char* p = via; while (std::sscanf(p, "%d,%d", &a, &b) == 2) { legs.push_back({a, b}); p = std::strchr(p, ';'); if (!p) break; ++p; } }
    legs.push_back({tx, ty});
    for (auto [lx, ly] : legs) for (int i = 0; i < 3000; ++i) {
        engine::HostInput fly; fly.ctrlDown = true;
        const game::Harry& h = g_debugLogic->harry();
        if (std::abs(h.x - lx) <= 7 && std::abs(h.y - ly) <= 7) break;
        fly.keyDown[h.x < lx ? 0x27 : 0x25] = std::abs(h.x - lx) > 7;
        fly.keyDown[h.y < ly ? 0x28 : 0x26] = std::abs(h.y - ly) > 7;
        g.tick(fly);
        if (std::getenv("FLYLOG") && i % 10 == 0) { std::printf("fly%4d H(%d,%d)", i, h.x, h.y); for (auto& p : g_debugLogic->objectSystem().objects()) if (!p->freed && p->type == type) std::printf(" [%s (%d,%d) s%d]", p->sprite ? p->sprite->name().c_str() : "-", p->x, p->y, int(p->b(0x32))); std::printf("\n"); }
    }
    for (int i = 0; i < ticks; ++i) {
        engine::HostInput in; if (we && i % we == 0) in.keyDown['Q'] = true;
        if (walk) in.keyDown[walk > 0 ? 0x27 : 0x25] = true;
        g.tick(in);
        if (i % every) continue;
        const game::Harry& h = g_debugLogic->harry();
        std::printf("t%4d H(%d,%d %s dmg %d) sc %d:", i, h.x, h.y, h.anim, h.damage, g.stats().score);
        for (auto& p : g_debugLogic->objectSystem().objects()) if (!p->freed && (type < 0 || p->type == type || p->type == 29 || p->type == 0x26))
            std::printf(" [%s t%d (%d,%d) f%d fc%d s%d v%d b36=%d w38=%d hits%d]", p->sprite ? p->sprite->name().c_str() : "-", p->type, p->x, p->y, p->frame, p->facing, int(p->b(0x32)), int(p->b(0x50)), int(p->b(0x36)), int(p->w(0x38)), p->hits);
        std::printf("\n");
    }
}
