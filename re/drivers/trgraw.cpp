#include "ph/level.h"
#include "ph/palette.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    ph::Palette pal; ph::loadInit("original", pal);
    ph::LevelData l = ph::loadLevel(std::atoi(argv[1]), "original", pal);
    const std::vector<uint8_t>& t = l.trg;
    int col = std::atoi(argv[2]);
    // header-less: offset table of big-endian words at col*2
    int off = (t[col*2] << 8) | t[col*2+1];
    std::printf("col %d offset %d:", col, off);
    for (int i = 0; i < 40 && off + i < int(t.size()); ++i) std::printf(" %02x", t[off+i]);
    std::printf("\n");
}
