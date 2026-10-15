// Heads-up display and title overlay.
//   renderHud   - port of FUN_00442fa0 (score, lives, weapon, ammo)
//   renderTitle - simplified FUN_004426e0 title animation
#include "game/game.h"

#include <algorithm>
#include <cstdio>

namespace game {

void Game::hudMeter(engine::SpriteInstance& croc, engine::SpriteInstance& life, int top) {
    // FUN_00435d45 with FUN_00435c4e: the damage meter (1 + 2 * damage / 22, or / 15 on hard,
    // the damage taken as at most 0x96 / 100) moves the crocodile along and opens its jaws;
    // mi_life plays its own frames.  (FUN_00435c4e's full-meter death is in the logic's tick.)
    const bool hard = hardMode();                                   // DAT_00462694
    const int dmg = std::min(stats_.damage & 0xff, hard ? 0x64 : 0x96) * 2;
    const int step = hard ? 0xf : 0x16;
    uint32_t meter = 1;                                             // DAT_0046b18c
    for (int t = step; dmg - t >= 0; t += step) ++meter;
    if (meter < hudAnim_.prevMeter) { hudAnim_.healing = true; hudAnim_.healStart = logicTicks_; }
    hudAnim_.prevMeter = meter;
    const int sx = snap_.scrollX, sy = snap_.scrollY;               // DAT_0046fba0 / DAT_0046fba4
    croc.sprite = findSprite("mi_croc");
    croc.flags = engine::kSprVisible;
    if (stats_.harryDead) {                                         // DAT_00465b72 == 0xff: the jaws shut on the life icon
        croc.x = sx + 0xfc; croc.y = sy + top + 8; croc.frame = 0;
    } else {
        croc.x = sx + 0xd8 + int(meter); croc.y = sy + top + 0x10;
        croc.frame = std::min(meter / 2 + 1, 7u);
    }
    life.sprite = findSprite("mi_life");
    life.x = sx + 0x107; life.y = sy + top + 2;
    uint32_t frame = 0;
    bool visible = true;
    const uint32_t now = logicTicks_;                               // DAT_004601d9
    if (stats_.harryDead) {
        visible = false;
    } else if (hudAnim_.oneUpGlow && now - hudAnim_.oneUpStart < 0x48) {
        hudAnim_.healing = false;
        frame = (now - hudAnim_.oneUpStart) / 8 + 0xc;              // the glow, 0xc .. 0x14
    } else {
        if (hudAnim_.oneUpGlow) { hudAnim_.healing = false; hudAnim_.oneUpGlow = false; }
        if (hudAnim_.oneUp) {
            hudAnim_.healing = false;
            const uint32_t d = now - hudAnim_.oneUpStart;
            frame = d < 0xa ? 0x15 : 0x16;
            if (d >= 0x14) {
                hudAnim_.oneUpGlow = true;
                hudAnim_.oneUpStart = now;
                hudAnim_.oneUp = false;
                frame = 0xc;
            }
        } else if (hudAnim_.healing) {
            frame = uint16_t((now - hudAnim_.healStart + 1) / 7);   // frames 0 .. 10, a step every 7 ticks
            if (frame >= 0xb) { frame = 0; hudAnim_.healing = false; }
        } else if (meter >= 0xa) {
            if ((now & 0xa0) == 0) frame = 0xb;                     // a blink now and then
            if (meter >= 0xd && (now & 0x10)) visible = false;      // and near the end it flashes
        }
    }
    life.frame = frame;
    life.flags = visible ? engine::kSprVisible : 0u;
}

void Game::renderHud() {
    // FUN_00442fa0.  The top row sits at DAT_004518e4 = 0x10 - slide, the bottom at
    // DAT_00451904 = 0x10 + slide, the slide being DAT_00451940 (0x30 at a restart, 3 less a tick:
    // the HUD slides in from the edges).  (In a 200-line display mode, DAT_0046e02c without
    // DAT_00453058, the bases are 0x1c and 4.)
    const int sx = snap_.scrollX;
    const int top = 0x10 - snap_.hudSlide;                          // DAT_004518e4
    const int bottom = 0x10 + snap_.hudSlide;                       // DAT_00451904
    const engine::View view{snap_.scrollX, snap_.scrollY};
    if (levelNo_ != kTitleScreen && levelNo_ < kMapScreen && (levelNo_ < kLoltunVault2 || levelNo_ > kLoltunVault1)) {
        const ph::RawImage& digits = assets_->scoline;              // DAT_004504d8
        // The score: "%06d" of DAT_00451914 (at most 999999), cells 0..9 of scoline at x 0x10 .. 0x38.
        char buf[16];
        std::snprintf(buf, sizeof buf, "%06d", std::min(std::max(stats_.score, 0), 999999));
        for (int i = 0; i < 6; ++i) renderer_.drawCell(digits, buf[i] - '0', 0x10 + i * 8, top - 4, sx);
        if (levelNo_ < kLoltunVault3 || levelNo_ > kTitleScreen) {
            engine::SpriteInstance croc, life;                      // DAT_004518f0 / DAT_00451920
            hudMeter(croc, life, top);
            renderer_.drawSprite(croc, view);
            renderer_.drawSprite(life, view);
            // Lives in reserve: "x N" (DAT_00451804 = DAT_00466527 - 1).
            renderer_.drawCell(digits, 10, 0x11b, top - 4, sx);
            renderer_.drawCell(digits, std::clamp(stats_.lives - 1, 0, 9), 0x127, top - 4, sx);
            // Treasures: the coin and "x NN" (DAT_00451a28).
            engine::SpriteInstance coin;                            // DAT_00451970
            coin.sprite = findSprite("mi_coin");
            coin.x = sx + 0x107; coin.y = snap_.scrollY + bottom + 0xc4;
            coin.flags = engine::kSprVisible;
            renderer_.drawSprite(coin, view);
            renderer_.drawCell(digits, 10, 0x112, bottom + 0xb6, sx);
            renderer_.drawCell(digits, stats_.treasures / 10, 0x11d, bottom + 0xb6, sx);
            renderer_.drawCell(digits, stats_.treasures % 10, 0x125, bottom + 0xb6, sx);
            if (levelNo_ != kBalankancheMine) {
                // The weapon (DAT_00451938) and its ammunition (DAT_00451908).
                const char* icon = stats_.weapon == 1 ? "mirbmrng" : stats_.weapon == 2 ? "mi_stonb" : "ob_pouch";
                engine::SpriteInstance w;                           // DAT_00451950
                w.sprite = findSprite(icon);
                w.x = sx + 0x1e; w.y = snap_.scrollY + bottom + 0xbe;
                w.flags = engine::kSprVisible;
                renderer_.drawSprite(w, view);
                renderer_.drawCell(digits, 10, 0x28, bottom + 0xb6, sx);
                renderer_.drawCell(digits, stats_.ammo() / 10, 0x32, bottom + 0xb6, sx);
                renderer_.drawCell(digits, stats_.ammo() % 10, 0x3a, bottom + 0xb6, sx);
            }
        }
    }
    if (demoPlaying() && (timerTicks_ & 0x30) != 0)
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
