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
// dbg6 <level> <x> <y> <ticks> <mode>: fly Harry to x,y, drop him, then play `ticks` with a scripted input:
//   mode 0 idle, 1 whip every 20 ticks, 2 whip when the jaguar is near, 3 run away from the jaguar + jump
static const char* stateName(int s) { static const char* n[] = {"?", "INTRO", "GOWAIT", "POUNCE", "REVERSE", "RUN", "CROUCH", "ROAR", "STOP"}; return s >= 0 && s < 9 ? n[s] : "?"; }
int main(int argc, char** argv) {
    engine::Options opts; opts.allowCheat = true; game::Game g("original", opts);
    int level = std::atoi(argv[1]), tx = std::atoi(argv[2]), ty = std::atoi(argv[3]), ticks = std::atoi(argv[4]), mode = std::atoi(argv[5]);
    int every = argc > 6 ? std::atoi(argv[6]) : 1;
    int snapAt = argc > 7 ? std::atoi(argv[7]) : -1;
    g.startLevel(level); g.setMode(game::Mode::Playing);
    for (int i = 0; i < 3000; ++i) {
        engine::HostInput fly; fly.ctrlDown = true;
        const game::Harry& h = g_debugLogic->harry();
        if (std::abs(h.x - tx) <= 7 && std::abs(h.y - ty) <= 7) break;
        fly.keyDown[h.x < tx ? 0x27 : 0x25] = std::abs(h.x - tx) > 7;
        fly.keyDown[h.y < ty ? 0x28 : 0x26] = std::abs(h.y - ty) > 7;
        g.tick(fly);
    }
    int lastState = -1;
    for (int i = 0; i < ticks; ++i) {
        engine::HostInput in;
        game::Obj* jag = nullptr;
        for (auto& p : g_debugLogic->objectSystem().objects()) if (!p->freed && p->type == 55) jag = p.get();
        const game::Harry& h = g_debugLogic->harry();
        if (mode == 1 && i % 20 == 0) in.keyDown['Q'] = true;
        if (mode == 2 && jag && std::abs(jag->x - h.x) < 70 && i % 4 == 0) in.keyDown['Q'] = true;
        if (mode == 3 && jag) {   // keep away from the jaguar inside the arena, jump over it, whip when it is close
            const bool jagLeft = jag->x < h.x;
            if (jagLeft && h.x < 3000) in.keyDown[0x27] = true;
            if (!jagLeft && h.x > 2680) in.keyDown[0x25] = true;
            if (std::abs(jag->x - h.x) < 100 && h.onGround && jag->y > 940) in.keyDown['W'] = true;
            if (std::abs(jag->x - h.x) < 60 && h.onGround && i % 6 == 0) in.keyDown['Q'] = true;
        }
        if (mode >= 4 && jag && std::abs(jag->x - h.x) < 70 && i % 3 == 0) in.keyDown['Q'] = true;
        if (mode == 5) g_debugLogic->healHarry();
        g.tick(in);
        if (i == snapAt) { char name[256]; std::snprintf(name, sizeof name, "snap%d.ppm", i); snap(g, name); }
        bool print = (i % every) == 0;
        if (jag && jag->b(0x32) != lastState) { lastState = jag->b(0x32); print = true; }
        if (!print) continue;
        std::printf("t%4d H(%d,%d %s%s dmg %d inv %d) cam %d", i, h.x, h.y, h.anim, h.facingLeft ? " L" : " R", h.damage, h.invulnerable, g.state().scrollX);
        if (jag) std::printf(" | J %s (%d,%d) %s f%d fac%d vx %.2f vy %.2f hits %d dmg %d t58 %d w4c %d b5b %d", stateName(jag->b(0x32)), jag->x, jag->y,
            jag->sprite ? jag->sprite->name().c_str() : "-", jag->frame, jag->facing, jag->l(0x38) / 65536.0, jag->l(0x50) / 65536.0, int(jag->hits), int(jag->w(0x6a)), int(jag->w(0x58)), int(jag->w(0x4c)), int(jag->b(0x5b)));
        for (auto& p : g_debugLogic->objectSystem().objects()) if (!p->freed && (p->type == 0x30 || p->type == 0x51 || p->type == 0x82 || p->type == 0x68 || (p->sprite && p->sprite->name() == "mi_go")))
            std::printf(" | %s t%02x (%d,%d) f%d", p->sprite ? p->sprite->name().c_str() : "-", p->type, p->x, p->y, p->frame);
        std::printf("\n");
        if (g.levelNumber() != level) { std::printf("level changed to %d\n", g.levelNumber()); break; }
    }
}
