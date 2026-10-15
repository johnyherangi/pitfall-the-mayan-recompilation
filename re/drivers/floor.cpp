#include "ph/level.h"
#include "ph/palette.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    ph::Palette pal; ph::loadInit("original", pal);
    int lv = std::atoi(argv[1]), x0 = std::atoi(argv[2]), x1 = std::atoi(argv[3]), y = std::atoi(argv[4]);
    ph::LevelData l = ph::loadLevel(lv, "original", pal);
    for (int x = x0; x <= x1; x += 8) {
        ph::FloorHit f = ph::probeNear(l.collision, x, y);
        std::printf("x %d: %s %d\n", x, f.hit ? "floor" : "none ", f.hit ? f.surfaceY : 0);
    }
    for (int c = (x0 >> 4); c <= (x1 >> 4); ++c) { std::printf("det col %d:", c); for (const ph::ColumnEntry& e : l.collision.column(c)) std::printf(" r%d-%d:%02x", e.row, e.rowTo, e.id); std::printf("\n"); }
}
