#include "ph/level.h"
#include "ph/palette.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    ph::Palette pal; ph::loadInit("original", pal);
    int lv = std::atoi(argv[1]), c0 = std::atoi(argv[2]), c1 = std::atoi(argv[3]);
    ph::LevelData l = ph::loadLevel(lv, "original", pal);
    for (int c = c0; c <= c1; ++c) for (const ph::ColumnEntry& e : l.triggers.column(c))
        std::printf("col %d (x %d): rows %d..%d id %#x\n", c, c * 16, e.row, e.rowTo, e.id);
}
