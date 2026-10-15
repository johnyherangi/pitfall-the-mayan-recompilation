// Per-level parallax camera: port of the switch in FUN_00443370.
//
//   DAT_00450504/508 = scrollX/scrollY     DAT_00450f94/f98 = parX/parY
//   DAT_004517ec     = vertical bob         DAT_0046fb74     = global frame
#include "game/game.h"

namespace game {

void Game::computeParallax() {
    FrameState& s = fs_;
    const int sx = s.scrollX, sy = s.scrollY, bob = s.bob;
    s.lineScroll.clear();
    switch (levelNo_) {
    case kCeibaJungle:                               // case 0
        s.parX = sx >> 1;
        s.parY = bob + (sy << 2) / 10;
        // The original also animates a leaf-shaped line-scroll band when the
        // camera is low (DAT_00450f98 > 0x6f); the table (DAT_00455010) is a
        // 32-entry sine ramp stepped every 4 frames.
        if (s.parY > 0x6f) {
            static const int8_t kWave[32] = {0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 3, 3, 3,
                                             2, 2, 1, 1, 0, -1, -1, -2, -2, -3, -3, -3, -4, -3, -2, -1};
            const int band = s.parY - 0x6f;
            s.lineScroll.assign(224, 0);
            const int phase = (s.globalFrame / 4) & 31;
            for (int i = 0; i < band && i < 224; ++i) s.lineScroll[size_t(224 - band + i)] = kWave[(phase + i) & 31];
        }
        break;
    case kXibalbaFalls: case kYaxchilanLagoon: case kJainaIslandFalls:   // 1, 6, 0x12
        s.parX = sx;
        s.parY = sy + bob;
        break;
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
