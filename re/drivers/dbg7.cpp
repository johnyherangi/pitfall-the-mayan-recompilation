#include "game/game.h"
#include "game/play_logic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
// dbg7 <level> <x> <y> <ticks> <jumpAt> [<every>]: fly Harry to x,y, drop him, press jump once at tick jumpAt; dump the bungee (type 27)
int main(int argc, char** argv) {
    engine::Options opts; opts.allowCheat = true; game::Game g("original", opts);
    int level = std::atoi(argv[1]), tx = std::atoi(argv[2]), ty = std::atoi(argv[3]), ticks = std::atoi(argv[4]), jumpAt = std::atoi(argv[5]);
    int every = argc > 6 ? std::atoi(argv[6]) : 1;
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
        engine::HostInput in;
        int runDir = argc > 7 ? std::atoi(argv[7]) : 0;      // 1 = hold right until the grab, -1 = left
        int hold = argc > 8 ? std::atoi(argv[8]) : 1;         // ticks to hold the jump key
        int jump2 = argc > 9 ? std::atoi(argv[9]) : -1;       // a second jump press (let go of the vine)
        const game::Harry& hh = g_debugLogic->harry();
        if (runDir && !hh.bungee) in.keyDown[runDir > 0 ? 0x27 : 0x25] = true;
        if ((i >= jumpAt && i < jumpAt + hold) || i == jump2) in.keyDown['W'] = true;
        g.tick(in);
        if (getenv("SNAP") && i == std::atoi(getenv("SNAP"))) snap(g, "bungee.ppm");
        const game::Harry& h = g_debugLogic->harry();
        if (i % every) continue;
        std::printf("t%4d H(%d,%d %s f%d vy %.2f %s%s)", i, h.x, h.y, h.anim, h.frame, h.vy / 65536.0, h.onGround ? "G" : "air", h.bungee ? " VINE" : "");
        for (auto& p : g_debugLogic->objectSystem().objects()) if (!p->freed && p->type == 27) {
            std::printf(" | V (%d,%d) f%d rest %d ph %d v %.2f launch %.2f cool %d b66 %d scr %d/%d cnt %d parts %zu", p->x, p->y, p->frame, int(p->w(0x38)), int(p->b(0x32)), p->l(0x34) / 65536.0, p->l(0x5a) / 65536.0, int(p->b(0x3a)), int(p->b(0x66)), int(p->w(0x42)), int(p->w(0x4c)), int(p->b(0x48)), p->link2 ? p->link2->parts.size() : 0);
            if (p->link2) std::printf(" body f%d", p->link2->frame);
            if (p->link3) std::printf(" stub f%d x%d", p->link3->frame, p->link3->x);
        }
        std::printf("\n");
    }
}
