// The title (level 15) and the cutscenes' drawing passes (levels 21 .. 23).
//
// The title has no game logic: FUN_00442410 sets it up at the level's load, FUN_00443370 steps
// the runner's frames and pans the camera, and the frame composition (FUN_00443c90) runs the
// rest as it draws: FUN_004426e0's title branch (the logo drops in, its dust, the four stones
// under it break away, "PRESS ANY KEY TO START", the jungle's sounds) and FUN_00442bf0 (Harry
// runs in, later a jaguar runs out); its records are {x, y, sprite, frame, flags} at 0x451810 ..
// 0x4518d0 and 0x451a10.
//
// The cutscenes' actors (game/cutscene.cpp) are drawn from the frame's copy of them (0x4564bf):
// FUN_00401346 the sprites behind, FUN_004012d8 the scrolling lines, FUN_004013f8 the texts and
// the sprites in front.
#include "game/game.h"
#include "game/harry.h"
#include "game/objects.h"

#include <cstring>

namespace game {

namespace {
constexpr uint32_t kHyirun = 0x47017c, kJgurun = 0x470210, kPitfallLogo = 0x470440, kDust = 0x470020, kStones = 0x470070;
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
}

// ---------------------------------------------------------------- the title

void Game::titleSetup() {
    // FUN_00442410 on the title (DAT_00451880 = the level is 15): the logo hidden, its two dust
    // clouds hidden, the four stones under it (the second and the fourth upside down), the runner
    // 200 px left of the screen with his frame script from its start, the camera at (0, 0x10).
    TitleSprite& logo = titleLogo_[0];
    logo.slot = kPitfallLogo; logo.frame = 0; logo.flags = 0;
    titleLogo_[1].x = 0x170; titleLogo_[1].y = 0x78; titleLogo_[1].slot = kDust; titleLogo_[1].flags = 0;
    titleLogo_[2].x = 0x24f; titleLogo_[2].y = 0x78; titleLogo_[2].slot = kDust; titleLogo_[2].flags = 0;
    static const int32_t kStoneX[4] = {0x1a4, 0x1cb, 0x1f2, 0x219};
    for (int i = 0; i < 4; ++i) {
        TitleSprite& s = titleLogo_[3 + i];
        s.x = kStoneX[i]; s.y = 0x92; s.slot = kStones; s.frame = 0;
        s.flags = (i & 1) ? 0x81 : 1;
    }
    titleRunner_ = TitleSprite{-200, 0x78, kHyirun, 0, 1};
    titleFrame_ = 0;                                      // DAT_0046fa80 / 84
    titleFrameShown_ = 0;
    titleSlot_ = titleSlotShown_ = kHyirun;               // DAT_00451800 / DAT_00451934
    titleScript_ = 0;                                     // DAT_00451984 = 0x46fa50
    fs_.scrollX = 0;
    fs_.scrollY = 0x10;
}

void Game::titleTick() {
    // FUN_00443370's tail, every engine tick: every fifth tick (DAT_0046fa7c, counted on every
    // level) the title's runner takes the next frame of the script at 0x46fa50 (-2: a sprite and a
    // frame follow; -1: back to its start); for the title's first 0xe6 ticks he runs 4 px a tick
    // and from x 0x1f the camera pans 2 px a tick, up to the map's edge.
    if (++titleCount_ > 4) {
        if (levelNo_ != kTitleScreen) return;
        auto at = [](int i) { int32_t v; std::memcpy(&v, kData_titleRunner + 4 * (i < 0 ? 0 : i > 10 ? 10 : i), 4); return v; };
        titleFrame_ = at(titleScript_++);
        titleCount_ = 0;
        if (titleFrame_ == -2) { titleSlot_ = uint32_t(at(titleScript_++)); titleFrame_ = at(titleScript_++); }
        if (titleFrame_ == -1) { titleFrame_ = at(0); titleScript_ = 1; }
    }
    if (levelNo_ == kTitleScreen && fs_.frame < 0xe6) {
        titleRunner_.x += 4;
        const uint32_t x = uint32_t(titleRunner_.x);
        if (x > 0x1e && x < 0x8000) fs_.scrollX += 2;
        if (uint32_t(fs_.maxScrollX) < uint32_t(fs_.scrollX)) fs_.scrollX = fs_.maxScrollX;
    }
}

void Game::titleAmbient() {
    // FUN_00431a67: every 0x80 .. 0xff ticks a jungle sound (0x9b .. 0xa1) on channel 1 when it is
    // free.  Its count is the logic's word DAT_00465b04, and FUN_004261e2 plays it only while the
    // logic's DAT_00465b72 (Harry dead), DAT_0046652f (the stop-watch) and DAT_0046652b are clear:
    // the title runs no logic, so they are what the last level left (after a game over, Harry
    // dead: the title stays quiet).
    Harry& m = logicMem_;
    m.w(0x465b04) -= 1;
    if (m.w(0x465b04) >= 0) return;
    m.w(0x465b04) = int16_t((random() & 0x7f) + 0x80);
    for (;;) {
        if (mixer_.channelBusy(1)) return;               // FUN_004261d1
        const uint32_t d0 = random() & 7;
        if (d0 == 7) continue;
        // FUN_004261e2 at priority 0: the channel is free, so it plays
        if (m.b(0x465b72) == 0 && m.w(0x46652f) == 0 && m.w(0x46652b) == 0) playChannel(1, int(d0 + 0x9b));
        return;
    }
}

void Game::titleFrame() {
    // What FUN_00443c90 does on the title as it composes a frame, before it draws: the runner's
    // frame and sprite for the frame (DAT_0046fa84 / DAT_00451934), FUN_004426e0's title branch
    // (from the level's tick 0x3d) and FUN_00442bf0 set their records, sounds and flags.  (The port
    // runs them with the tick, before the title's start key is looked at as the original does
    // after its drawing; Game::renderTitle and Game::renderTitleRunner draw the records.)
    titleFrameShown_ = titleFrame_;
    titleSlotShown_ = titleSlot_;
    const int t = fs_.frame;                              // DAT_0045190c
    const int sx = fs_.scrollX, sy = fs_.scrollY;         // DAT_0046fba0 / 4
    if (t > 0x3c) {
        if (!paused_ && t > 0x244) titleAmbient();
        if (!mixer_.channelBusy(1)) {                     // FUN_0043cbc0(1)
            // the logo's two sounds, once (DAT_0046fa40 / 44: the jaguar's sounds set them again)
            if (t > 0xb4 && t < 200 && !titleSoundA_ && !paused_) { titleShine_ = false; titleSoundA_ = true; playChannel(1, 0x25); }
            if (t > 0xe1 && t < 0x14a && !titleSoundB_ && !paused_) { titleShine_ = false; titleSoundB_ = true; playChannel(1, 0x29); }
        }
        TitleSprite& logo = titleLogo_[0];
        if (t > 0xd2) {                                   // the logo drops in from the top, 8 px a tick
            logo.y = t <= 0xe0 ? t * 8 - 0x698 : sy + 0x68;
            logo.flags = 1;
            logo.x = sx + 0xa0;
        }
        if (t >= 0xe1 && t < 0x104) {                     // its dust, a frame every 5 ticks
            titleLogo_[1].frame = titleLogo_[2].frame = uint32_t((t - 0xe1) / 5);
            titleLogo_[1].flags = 0x41;
            titleLogo_[2].flags = 1;
        } else {
            titleLogo_[1].flags = 0;
            titleLogo_[2].flags = 0;
        }
        // the stones crack, one after the other, a frame every 4 ticks
        if (t >= 0xff && t < 0x113) titleLogo_[3].frame = uint32_t((t - 0xff) >> 2);
        if (t >= 0x10e && t < 0x122) titleLogo_[4].frame = uint32_t((t - 0x10e) >> 2);
        if (t >= 0x11d && t < 0x131) titleLogo_[5].frame = uint32_t((t - 0x11d) >> 2);
        if (t >= 0x12c && t < 0x140) titleLogo_[6].frame = uint32_t((t - 0x12c) >> 2);
        if ((t > 0x145 && t < 0x163) || (t > 0x163 && !titleShine_)) {
            // then all four break away (frames 5 .. 9, one every 6 ticks, each 26 px lower) and the
            // words under them show; the start key does not count meanwhile (DAT_0046fb88 = 0)
            const int u = (t > 0x163 && !titleShine_) ? 0x162 : t;
            const int f = (u - 0x145) / 6;
            startKey_ = 0;
            for (int i = 3; i < 7; ++i) { titleLogo_[i].frame = uint32_t(f + 5); titleLogo_[i].y = 0x92 + 26 * f; }
            if (f + 5 == 9) titleShine_ = true;
        }
    }
    // FUN_00442bf0: from tick 0x15 to 0xdb Harry runs, his steps on channel 0 on frames 0 and 6;
    // from tick 0x1f5 to 0x257 a jaguar runs back the other way, its growls on channel 1.
    if (t > 0x14 && t < 0xdc) {
        titleRunner_.slot = titleSlotShown_;
        titleRunner_.frame = uint32_t(titleFrameShown_);
        titleRunner_.flags = 1;
        if (!paused_) {
            if (titleFrameShown_ == 0 && !mixer_.channelBusy(0) && t < 0xe6) playChannel(0, 9);
            if (titleFrameShown_ == 6 && !mixer_.channelBusy(0) && t < 0xe6) playChannel(0, 0xa);
        }
    }
    if (t > 0x1f4 && t < 0x258) {
        titleRunner_.slot = kJgurun;
        titleRunner_.y = 0x7a;
        titleRunner_.flags = 0x41;
        titleRunner_.frame = uint32_t(((t - 0x1f3) >> 2) & 7);
        titleRunner_.x = t * 4 - 0x6ae;
        if (t < 0x1fe) titleSoundA_ = titleSoundB_ = false;
        if (!paused_) {
            if (t > 0x1fe && t < 0x208 && !titleSoundA_) { titleSoundA_ = true; playChannel(1, 0x10); }
            if (t > 0x226 && t < 0x230 && !titleSoundB_) { titleSoundB_ = true; playChannel(1, 0x2a); }
        }
    }
}

void Game::renderTitle() {
    // FUN_004426e0's title branch (pass 5 of FUN_00443c90): "PRESS ANY KEY TO START" blinking
    // from tick 0x173, then the logo, its dust and the stones (FUN_00438140).
    const int t = fs_.frame;
    if (t <= 0x3c) return;
    if (t > 0x172 && (t & 0x20)) renderer_.drawText(assets_->font, 0x48, 0xbe, "PRESS ANY KEY TO START", snap_.scrollX);   // FUN_00442600
    const engine::View view{snap_.scrollX, snap_.scrollY};
    for (const TitleSprite& s : titleLogo_) {
        engine::SpriteInstance inst;
        inst.x = s.x; inst.y = s.y; inst.frame = s.frame; inst.flags = s.flags;
        inst.sprite = findSprite(spriteSlotName(s.slot));
        renderer_.drawSprite(inst, view);
    }
}

void Game::renderTitleRunner() {
    // FUN_00442bf0 (pass 6): the runner, later the jaguar.
    const int t = fs_.frame;
    if (!((t > 0x14 && t < 0xdc) || (t > 0x1f4 && t < 0x258))) return;
    engine::SpriteInstance s;
    s.x = titleRunner_.x; s.y = titleRunner_.y; s.frame = titleRunner_.frame; s.flags = titleRunner_.flags;
    s.sprite = findSprite(spriteSlotName(titleRunner_.slot));
    renderer_.drawSprite(s, engine::View{snap_.scrollX, snap_.scrollY});
    // The hatman cheat takes its 0x12 px off the record itself, before and after the drawing:
    // the runner's record keeps them (its y is set only at the title's start), so he rises.
    if (renderer_.hatmanLifts(s.sprite)) titleRunner_.y -= 0x24;
}

// ---------------------------------------------------------------- the cutscenes' passes

void Game::drawBigText(int y, const char* s) {
    // FUN_00431bfb: the line in ltr_lg letters (FUN_00431b73's glyphs and widths), centred on x
    // 0xa0 of the logic's camera, at y below it; FUN_00438140 draws them at the frame's camera.
    int total = 0;
    for (const char* c = s; *c; ++c) { int w; bigGlyph(uint8_t(*c), w); total += w; }
    int x = 0xa0 - (total >> 1) + snap_.camX;              // DAT_0046a870
    const int yy = y + snap_.camY;                         // DAT_0046a874
    const engine::View view{snap_.scrollX, snap_.scrollY};
    const ph::Sprite* font = assets_->sprite("ltr_lg");
    for (const char* c = s; *c; ++c) {
        int w;
        engine::SpriteInstance g;
        g.frame = bigGlyph(uint8_t(*c), w);
        g.sprite = font;
        g.x = x; g.y = yy; g.flags = engine::kSprVisible;   // (the record's flags byte, DAT_0046aa81, is 1)
        if (font) renderer_.drawSprite(g, view);
        x += w;
    }
}

void Game::renderScrollText() {
    // FUN_004012d8: on level 21 after the demos and on level 23, the lines the scroller put in the
    // actors (routine 0x40157f), from the frame's copy: one that starts with '[' in ltr_lg letters
    // (FUN_00431bfb), the others in the small font at their x (FUN_00442600).  The y is the whole
    // part of the actor's, unsigned: a line above the screen is not drawn at all.
    if (!((levelNo_ == kIntro && introAfterDemos_) || levelNo_ == kCredits)) return;
    for (int i = 0; i < 10; ++i) {
        const uint8_t* a = snap_.cutActors.data() + i * 0x40;
        if (le32(a + 0x1c) != 0x40157f) continue;
        const char* s = cutsceneText(le32(a + 0x24));
        const int y = le16(a + 6);
        if (s[0] == '[') drawBigText(y, s);
        else renderer_.drawText(assets_->font, int32_t(le32(a)), y, s, snap_.scrollX);
    }
}

void Game::renderActors(bool front) {
    // FUN_00401346 (behind: the actors with a sprite and no 0x20 in their frame) and FUN_004013f8
    // (in front: the texts, routine 0x4014e4, in the small font at their x and y, and the actors
    // with 0x20 in their frame), on levels 21 .. 23.  The scrolling lines are FUN_004012d8's.  An
    // actor's position is the whole part of its 16.16 x and y taken unsigned (shr 16): one left
    // of the map's 0 is far to its right.
    if (levelNo_ < kIntro || levelNo_ > kCredits) return;
    const engine::View view{snap_.scrollX, snap_.scrollY};
    for (int i = 0; i < 10; ++i) {
        const uint8_t* a = snap_.cutActors.data() + i * 0x40;
        const uint32_t routine = le32(a + 0x1c);
        if (routine == 0 || routine == 0x40157f) continue;
        if (routine == 0x4014e4) {
            if (front) renderer_.drawText(assets_->font, int32_t(le32(a)), int32_t(le32(a + 4)), cutsceneText(le32(a + 0x24)), snap_.scrollX);
            continue;
        }
        const uint32_t slot = le32(a + 8);
        if (slot == 0 || ((le32(a + 0xc) & 0x20) != 0) != front) continue;
        engine::SpriteInstance s;
        s.x = int32_t(le32(a) >> 16);
        s.y = int32_t(le32(a + 4) >> 16);
        s.sprite = findSprite(spriteSlotName(slot));
        s.frame = le32(a + 0xc);
        s.flags = le32(a + 0x10);
        renderer_.drawSprite(s, view);
    }
}

} // namespace game
