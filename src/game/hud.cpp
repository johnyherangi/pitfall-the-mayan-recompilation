// Heads-up display and title overlay.
//   renderHud   - port of FUN_00442fa0 (score, lives, weapon, ammo)
//   renderTitle - simplified FUN_004426e0 title animation
#include "game/game.h"

#include <algorithm>
#include <cstdio>

namespace game {

void Game::renderHud() {
    const int sx = snap_.scrollX;
    const bool gameplayLevel =
        levelNo_ != kTitleScreen && levelNo_ < kMapScreen && (levelNo_ < kLoltunVault2 || levelNo_ > kLoltunVault1);
    if (gameplayLevel && mode_ != Mode::Attract) {
        const ph::RawImage& digits = assets_->scoline;        // DAT_004504d8
        const int topY = 0x10 - 4;                            // DAT_004518e4 - 4 (HUD slide offset 0)
        // Score: six digits right-aligned, cells 0..9 of scoline, x = 0x10 .. 0x38.
        char buf[16];
        std::snprintf(buf, sizeof buf, "%6d", stats_.score);
        for (int i = 0; i < 6; ++i)
            if (buf[i] >= '0' && buf[i] <= '9') renderer_.drawCell(digits, buf[i] - '0', 0x10 + i * 8, topY, sx);
        if (levelNo_ < 0xc || levelNo_ > 0xf) {
            // Health (FUN_00435c4e / FUN_00435d45): the crocodile's jaws open as the
            // damage meter rises - meter = 1 + 2*damage/22 (/15 on hard), frame = meter/2 + 1,
            // and Harry's life icon (mi_life) sits at the top right.
            const int step = stats_.difficulty ? 0xf : 0x16;
            int meter = 1;
            for (int t = step; std::min(stats_.damage, stats_.difficulty ? 100 : 0x96) * 2 - t >= 0; t += step) ++meter;
            engine::SpriteInstance croc;
            croc.sprite = findSprite("mi_croc");
            croc.x = sx + 0xd8 + meter; croc.y = snap_.scrollY + 0x10 + 0x10;
            croc.frame = stats_.harryDead ? 0u : uint32_t(std::min(7, meter / 2 + 1));
            croc.flags = engine::kSprVisible;
            renderer_.drawSprite(croc, engine::View{snap_.scrollX, snap_.scrollY});
            engine::SpriteInstance life;
            life.sprite = findSprite("mi_life");
            life.x = sx + 0x107; life.y = snap_.scrollY + 0x10 + 2;
            // Idle blink animation (frames 0..0xb, blink on frame & 0xa0 == 0 -> 0xb).
            life.frame = ((snap_.globalFrame & 0xa0) == 0 && meter >= 10) ? 0xbu : 0u;
            life.flags = engine::kSprVisible;
            renderer_.drawSprite(life, engine::View{snap_.scrollX, snap_.scrollY});
            // Lives in reserve: "x N" (DAT_00451804 = DAT_00466527 - 1).
            renderer_.drawCell(digits, 10, 0x11b, topY, sx);
            renderer_.drawCell(digits, std::clamp(stats_.lives - 1, 0, 9), 0x127, topY, sx);
            // Treasures: coin icon + "x" cell + two digits (DAT_00451a28).
            engine::SpriteInstance coin;
            coin.sprite = findSprite("mi_coin");
            coin.x = sx + 0x107; coin.y = snap_.scrollY + 0xc4 + 0x10;
            coin.flags = engine::kSprVisible;
            renderer_.drawSprite(coin, engine::View{snap_.scrollX, snap_.scrollY});
            const int bottomY = 0x10 + 0xb6;
            renderer_.drawCell(digits, 10, 0x112, bottomY, sx);
            renderer_.drawCell(digits, stats_.treasures / 10, 0x11d, bottomY, sx);
            renderer_.drawCell(digits, stats_.treasures % 10, 0x125, bottomY, sx);
            if (levelNo_ != kBalankancheMine) {
                // Weapon icon (DAT_00451938) + ammo count (DAT_00451908).
                const char* icon = stats_.weapon == 1 ? "mirbmrng" : stats_.weapon == 2 ? "mi_stonb" : "ob_pouch";
                engine::SpriteInstance w;
                w.sprite = findSprite(icon);
                w.x = sx + 0x1e; w.y = snap_.scrollY + 0xbe + 0x10;
                w.flags = engine::kSprVisible;
                renderer_.drawSprite(w, engine::View{snap_.scrollX, snap_.scrollY});
                renderer_.drawCell(digits, 10, 0x28, bottomY, sx);
                renderer_.drawCell(digits, stats_.ammo() / 10, 0x32, bottomY, sx);
                renderer_.drawCell(digits, stats_.ammo() % 10, 0x3a, bottomY, sx);
            }
        }
    }
    if (mode_ == Mode::Attract && (snap_.frame & 0x30) != 0)
        renderer_.drawText(assets_->font, 0x70, 0xe, "DEMONSTRATION", sx);     // s_DEMONSTRATION
    if (options_.frameRate) {
        char fps[16];
        std::snprintf(fps, sizeof fps, "%3d", snap_.frame);
        renderer_.drawText(assets_->font, 0x78, 0x10, fps, sx);
    }
}

void Game::renderTitle() {
    // FUN_004426e0 (level 0xf branch): the logo sprites drop in, then the
    // "PRESS ANY KEY TO START" prompt blinks.  Only the final composition is
    // reproduced here; the drop-in timings are listed in the ledger.
    const int t = snap_.frame;
    engine::View view{snap_.scrollX, snap_.scrollY};
    if (t > 0xd2) {
        engine::SpriteInstance logo;
        logo.sprite = findSprite("pitfall");
        logo.x = snap_.scrollX + 0xa0;
        logo.y = (t < 0xe1) ? t * 8 - 0x698 : snap_.scrollY + 0x68;
        logo.flags = engine::kSprVisible;
        renderer_.drawSprite(logo, view);
    }
    if (t > 0x172 && (t & 0x20)) renderer_.drawText(assets_->font, 0x48, 0xbe, "PRESS ANY KEY TO START", snap_.scrollX);
}

} // namespace game
