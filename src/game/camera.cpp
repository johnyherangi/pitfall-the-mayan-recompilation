// Per-level parallax camera: port of the switch in FUN_00443370.
//
//   DAT_00450504/508 = scrollX/scrollY     DAT_00450f94/f98 = parX/parY
//   DAT_004517ec     = vertical bob         DAT_0046fb74     = global frame
#include "game/game.h"
#include "game/objects.h"

#include <cstring>

namespace game {

void Game::computeParallax() {
    FrameState& s = fs_;
    const int sx = s.scrollX, sy = s.scrollY, bob = s.bob;
    s.lineScroll.clear();
    s.waterColumns.clear();
    switch (levelNo_) {
    case kCeibaJungle: {                             // case 0
        s.parX = sx >> 1;
        s.parY = bob + (sy << 2) / 10;
        // Below parY 0x6f the parallax's bottom (parY - 0x6f) lines shimmer: each takes the low
        // byte of a long of the table at 0x455010 as its offset, from a phase (1 .. 32) that steps
        // every fourth tick while the band shows.
        if (s.parY > 0x6f) {
            const int band = s.parY - 0x6f;
            s.lineScroll.assign(224, 0);
            if (--leafCount_ < 0) { leafCount_ = 3; leafPhase_ = (leafPhase_ & 0x1f) + 1; }   // DAT_0046fb48 / 4c
            for (int i = 0; i < band && i < 224; ++i)
                s.lineScroll[size_t(224 - band + i)] = int8_t(kData_leafWave[size_t((leafPhase_ + i) & 0x1f) * 4]);
        }
        break;
    }
    case kXibalbaFalls: case kYaxchilanLagoon: case kJainaIslandFalls: {   // 1, 6, 0x12
        s.parX = sx;
        s.parY = sy + bob;
        // The waterfalls: six phases fall at their own speeds (16.16, 512 px round) but while the
        // stop-watch runs; every 16-px column of the parallax map takes one of them by its x
        // (blocks of eight columns at 0x450028: 0x450024, 0x4500e8, 0x45001c, 0x450014, 0x450018,
        // 0x450020, 0x45001c, 0x450018) plus half the camera's y.  On level 1 only the columns from
        // x 0x80 to 0x37f have water (its map's column 0 at x 0x80); on 6 and 18 the map repeats
        // every 256 px from its column 2.
        if (s.stopWatch == 0) {
            water_[4] -= 0x84000; water_[5] -= 0x6c800; water_[2] -= 0x5f000;
            water_[0] -= 0x72000; water_[1] -= 0x8c000; water_[3] -= 0x65000;
        }
        static const int kBlock[8] = {4, 5, 2, 0, 1, 3, 2, 1};
        const uint32_t ux = uint32_t(sx);
        s.waterColumns.clear();
        if (levelNo_ == kXibalbaFalls) s.waterColumns.push_back(ux < 0x80 || ux > 0x38f ? 0 : int32_t((((ux - 0x80) & 0xfffffff3u) >> 2) / 2));
        else s.waterColumns.push_back(int32_t(((ux & 0xf0) >> 3) + 2));
        for (uint32_t x = ux & ~0xfu; int32_t(((ux + 0xf) & ~0xfu) - x) != -0x140; x += 0x10) {
            if (levelNo_ == kXibalbaFalls && (x < 0x80 || x >= 0x380)) { s.waterColumns.push_back(-1); continue; }
            const uint32_t v = uint32_t(int32_t(uint32_t(sy) * 0x8000u) + water_[kBlock[(x >> 4) & 7]]);
            s.waterColumns.push_back(int32_t((v & 0x1ff0000) >> 16));
        }
        s.waterColumns.push_back(-1);
        // levels 6 and 18: the water's wobble moves on a line every fifth tick
        if (--waterCount_ < 0) {
            if (levelNo_ != kXibalbaFalls) {
                ++waveIndex_;
                int32_t w; std::memcpy(&w, kData_waterWave + size_t(waveIndex_) * 4, 4);
                if (w == 0x8000) waveIndex_ = 0;
            }
            waterCount_ = 4;
        }
        s.waveStart = waveIndex_;
        break;
    }
    case kTazamulMines:                              // 2
        s.parX = sx >> 2;
        s.parY = bob;
        break;
    case kLostCityOfCopan: case kTikalRuins: case kPalenqueRuins:         // 3, 8, 0x10 (clouds drift)
        if ((s.globalFrame & 3) == 0) ++cloudDrift_;
        s.parX = cloudDrift_;
        s.parY = bob;
        break;
    case kLakamulRainForest:                         // 5
        s.parX = sx >> 1;
        s.parY = (sy << 2) / 7 + bob;
        break;
    case kBalankancheMine:                           // 7
        s.parX = sx >> 1;
        s.parY = bob;
        break;
    case kTempleOfTikal:                             // 9
        s.parX = sx - ruinsCounter_;
        ++ruinsCounter_;
        s.parY = (sy + bob) & 0xff;
        break;
    case kWarriorSpirit:                             // 10
        cloudDrift_ += 4;
        s.parX = cloudDrift_;
        s.parY = bob;
        break;
    case kLoltunVault2: case kLoltunVault3: case kLoltunVault1:            // 0xb..0xd
        // The ceiling of the vault is the parallax layer, at the height the game logic keeps
        // in the trigger buffer; the room is drawn 0x20 (vault 1: 0x30) px higher.
        s.scrollY = sy + bob - (levelNo_ == kLoltunVault1 ? 0x30 : 0x20);
        s.parY = s.vaultCeiling + bob + (levelNo_ == kLoltunVault3 ? 0x20 : 0);
        s.parX = -0x10;
        s.scrollX = 0;
        return;
    case kTitleScreen:                               // 0xf
        s.parX = sx >> 1;
        s.parY = (sy + bob) >> 1;
        break;
    case kAtari2600Level: case kEnding:              // 0x14, 0x18
        s.parX = 0; s.parY = 0;
        return;
    case kIntro:                                     // 0x15
        s.parX = (sx << 3) / 7;
        s.parY = bob;
        break;
    case kSimonRoom:                                 // 0x16
        if ((s.globalFrame & 3) == 0) ++cloudDrift_;
        s.parX = cloudDrift_;
        s.parY = 0; s.scrollY = 0;
        return;
    default:
        s.parX = 0;
        s.parY = bob;
        break;
    }
    s.scrollY = sy + bob;                            // the shake moves the main layer and the sprites too
}

} // namespace game
