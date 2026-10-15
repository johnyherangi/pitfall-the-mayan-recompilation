#include "ph/level.h"
#include "ph/tables.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    ph::Palette pal; ph::loadInit("original", pal);
    ph::LevelData l = ph::loadLevel(std::atoi(argv[1]), "original", pal);
    int want = std::atoi(argv[2]);
    for (int c = 0; c < l.objects.columns(); ++c)
        for (const ph::ColumnEntry& e : l.objects.column(c))
            if (e.id == want) std::printf("id %d at col %d row %d -> x %d y %d (%s)\n", e.id, c, e.row, c*16+0x18+ph::kObjectTemplates[e.id].xOff, e.row*16-0x10+ph::kObjectTemplates[e.id].yOff, ph::kObjectTemplates[e.id].sprite);
}
