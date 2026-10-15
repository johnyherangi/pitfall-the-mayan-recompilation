#include "ph/level.h"
#include "ph/tables.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    ph::Palette pal; ph::loadInit("original", pal);
    ph::LevelData l = ph::loadLevel(std::atoi(argv[1]), "original", pal);
    int r0 = std::atoi(argv[2]), r1 = std::atoi(argv[3]);
    for (int r = r0; r <= r1; ++r) {
        std::printf("row %3d:", r);
        for (int c = 0; c < l.collision.columns(); ++c) { const ph::ColumnEntry* e = l.collision.find(c, r); std::putchar(e ? (e->id < 17 ? '#' : '/') : '.'); }
        std::printf("\n");
    }
}
