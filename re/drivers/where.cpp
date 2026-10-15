#include "ph/level.h"
#include "ph/palette.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    ph::Palette pal; ph::loadInit("original", pal);
    int lv = std::atoi(argv[1]), id = std::strtol(argv[2], nullptr, 0);
    ph::LevelData l = ph::loadLevel(lv, "original", pal);
    std::printf("level %d: %d columns\n", lv, l.objects.columns());
    for (int c = 0; c < l.objects.columns(); ++c) for (const ph::ColumnEntry& e : l.objects.column(c))
        if (e.id == id) std::printf("id %#x at col %d row %d -> x %d y %d\n", id, c, e.row, c * 16 + 0x18, e.row * 16 - 0x10);
}
