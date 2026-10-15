#include "game/game.h"
#include "game/play_logic.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "engine/renderer.h"
static void snap(game::Game& g, const char* out) {
    g.render();
    std::vector<uint8_t> view(size_t(engine::kViewW) * engine::kViewH);
    g.framebuffer().copyView(g.viewScrollX(), view.data());
    FILE* f = std::fopen(out, "wb"); std::fprintf(f, "P6\n%d %d\n255\n", engine::kViewW, engine::kViewH);
    for (uint8_t px : view) { const ph::Rgb c = g.palette().colour(px); uint8_t rgb[3] = {c.r, c.g, c.b}; std::fwrite(rgb, 1, 3, f); }
    std::fclose(f);
}
namespace game { extern PlayLogic* g_debugLogic; }
using game::g_debugLogic;
// dbg8 <level> <x> <y> <ticks> <type>: fly to x,y, then hold right; print objects of <type> and any mi_pouc2 each 10 ticks
int main(int argc, char** argv) {
    engine::Options opts; opts.allowCheat = true; game::Game g("original", opts);
    int level = std::atoi(argv[1]), tx = std::atoi(argv[2]), ty = std::atoi(argv[3]), ticks = std::atoi(argv[4]), type = std::atoi(argv[5]);
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
        engine::HostInput in; in.keyDown[0x27] = true;
        g.tick(in);
        if (getenv("SNAP") && i == std::atoi(getenv("SNAP"))) snap(g, "snap8.ppm");
        if (i % 10) continue;
        const game::Harry& h = g_debugLogic->harry();
        std::printf("t%3d H(%d,%d) cam %d ammo %d:", i, h.x, h.y, g.state().scrollX, g.stats().ammoCount[0]);
        for (auto& p : g_debugLogic->objectSystem().objects()) if (!p->freed && p->type == type)
            std::printf(" [%s t%d (%d,%d) f%d link %d]", p->sprite ? p->sprite->name().c_str() : "-", p->type, p->x, p->y, p->frame, p->link ? 1 : 0);
        std::printf("\n");
    }
}
