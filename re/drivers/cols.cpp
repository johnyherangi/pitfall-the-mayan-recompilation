#include "ph/level.h"
#include "ph/palette.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    ph::Palette pal; ph::loadInit("original", pal);
    ph::LevelData l = ph::loadLevel(std::atoi(argv[1]), "original", pal);
    std::printf("det %d cols, dt2 %d cols, als %d, trg %d; bg %dx%d tiles\n", l.collision.columns(), l.collision2.columns(), l.objects.columns(), l.triggers.columns(), l.bg.wTiles, l.bg.hTiles);
    for (int c = std::atoi(argv[2]); c <= std::atoi(argv[3]); ++c) { std::printf("dt2 col %d:", c); for (const ph::ColumnEntry& e : l.collision2.column(c)) std::printf(" r%d-%d:%02x", e.row, e.rowTo, e.id); std::printf("\n"); }
}
