#include "ph/level.h"
#include "ph/palette.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    ph::Palette pal; ph::loadInit("original", pal);
    int lv = std::atoi(argv[1]);
    ph::LevelData l = ph::loadLevel(lv, "original", pal);
    for (auto& s : l.sprites) std::printf("%s(%d) ", s->name().c_str(), s->frameCount());
    std::printf("\n");
}
