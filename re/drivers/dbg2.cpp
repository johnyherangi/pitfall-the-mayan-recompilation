#include "game/game.h"
#include "game/play_logic.h"
#include <cstdio>
#include <cstdlib>
// usage: dbg2 <level> <ticks> [keys]   -- run and report objects of interest every 30 ticks
int main(int argc, char** argv) {
    engine::Options opts; opts.allowCheat = true; game::Game g("original", opts);
    int level = std::atoi(argv[1]), ticks = std::atoi(argv[2]); std::string keys = argc > 3 ? argv[3] : "";
    g.startLevel(level); g.setMode(game::Mode::Playing);
    engine::HostInput host, idle;
    for (char k : keys) { if (k=='L') host.keyDown[0x25]=true; if (k=='R') host.keyDown[0x27]=true; if (k=='F') host.ctrlDown=true; }
    if (argc > 5) {
        // fly Harry (debug mode) to a target x,y at 7 px/tick
        int tx = std::atoi(argv[4]), ty = std::atoi(argv[5]);
        for (int i = 0; i < 2000; ++i) {
            engine::HostInput fly; fly.ctrlDown = true;
            int hx = -1, hy = -1;
            for (const auto& s : g.state().displayList) if (s.sprite && s.sprite->name().rfind("hyi",0)==0) { hx = s.x - 16; hy = s.y - 16; }
            if (hx < 0) { g.tick(fly); continue; }
            if (std::abs(hx - tx) <= 7 && std::abs(hy - ty) <= 7) break;
            fly.keyDown[hx < tx ? 0x27 : 0x25] = std::abs(hx - tx) > 7;
            fly.keyDown[hy < ty ? 0x28 : 0x26] = std::abs(hy - ty) > 7;
            g.tick(fly);
        }
    }
    for (int i = 0; i < ticks; ++i) {
        host.keyDown['W'] = keys.find('j') != std::string::npos && (i % 30) < 2;
        const int holdTicks = argc > 6 ? std::atoi(argv[6]) : 1 << 30;
        g.tick(i < 2 || i >= holdTicks ? idle : host);
        if (i % 30 == 29) {
            std::printf("t%4d scroll %d,%d score %d dmg %d:", i, g.state().scrollX, g.state().scrollY, g.stats().score, g.stats().damage);
            for (const auto& s : g.state().displayList) if (s.sprite) std::printf(" %s#%u(%d,%d)", s.sprite->name().c_str(), s.frame, s.x, s.y);
            std::printf("\n");
        }
    }
}
