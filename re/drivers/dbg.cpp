#include "game/game.h"
#include "game/play_logic.h"
#include "ph/tables.h"
#include <cstdio>
int main(int argc, char** argv) {
    engine::Options opts; game::Game g("original", opts);
    g.startLevel(0); g.setMode(game::Mode::Playing);
    engine::HostInput host, idle; host.keyDown[0x25] = true;
    const ph::SpriteHitbox* h = ph::hitbox("hyirun"); std::printf("hyirun box %d %d %d %d\n", h?h->dx:0, h?h->w:0, h?h->dy:0, h?h->h:0);
    h = ph::hitbox("mi_pouc2"); std::printf("mi_pouc2 box %d %d %d %d\n", h?h->dx:0, h?h->w:0, h?h->dy:0, h?h->h:0);
    for (int i = 0; i < 60; ++i) {
        g.tick(i < 2 ? idle : host);
        for (const auto& s : g.state().displayList) if (s.sprite && s.sprite->name() == "mi_pouc2") std::printf("t%d pouch at %d,%d; ", i, s.x, s.y);
        for (const auto& s : g.state().displayList) if (s.sprite && s.sprite->name().rfind("hyi",0)==0) std::printf("harry %s at %d,%d\n", s.sprite->name().c_str(), s.x, s.y);
    }
}
