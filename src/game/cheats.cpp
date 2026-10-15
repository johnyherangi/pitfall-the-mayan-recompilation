// The typed cheat codes (FUN_004388c0's WM_CHAR, 0x439868 ..) and the frame rate / position text
// some of them turn on (FUN_00443c90).
//
// Every character typed goes into a ring of 128 (DAT_004533a0, DAT_00453384); then each code at
// 0x4614c2 .. 0x4615a1 (every byte 0x2d higher than its letter) is compared, ignoring case
// (FUN_00449744, tolower), with the last characters typed.  A match plays sound 0x24 on channel
// 0 (MessageBeep without the sound) and:
//   paidtoplaygames      the testers' dialog (0x100, bitmap 0x81), paused
//   iddqd                "Nice try! This isn't Doom!" (0x7f, bitmap 0x80), paused
//   mijmrowhtrae         DAT_0046d05c: "*50" leads to level 24 (Earthworm Jim) instead of the 2600 level
//   framerate            DAT_0046e054 (Option_FrameRate): the frame rate at the top of the screen
//   beammeup             DAT_0046e058: the camera's position there (the 2600 level's screen)
//   flylikeaneagle       DAT_0046e064 (Debug_AllowCheat): Ctrl and the arrows fly Harry
//   eighttracksrule      DAT_0046ed5c (Option_Slow): no CD audio
//   pumpyouup            99 of each weapon (FUN_00435c30)
//   eatmorebran          99 continues
//   meowmeowlikemeowman  99 lives (FUN_00435c46)
//   vampire              DAT_0046d054: no damage
//   hatman               DAT_0046d058, 0 .. 2: Harry, then everything else, drawn as the hat man
//   fiveeasypieces / idbuythatforadollar / simonsaysplaysimon / letsdothetimewarp
//                        DAT_0046d38c |= 0x1f / = all / |= 0x3800 / |= 0x100000: the menu's levels
// ("alllevels", at 0x461575 among them, is never tested.)
#include "game/game.h"
#include "game/objects.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace game {

namespace {
constexpr uint32_t kCodesBase = 0x4614c2;
enum Cheat { kTesters, kDoom, kEarthworm, kFrameRate, kPosition, kFly, kSlow, kAmmo, kContinues, kLives, kInvincible,
             kHatman, kUnlockFive, kUnlockAll, kUnlockVaults, kUnlockAtari };
// In the order WM_CHAR tests them.
const struct { uint32_t va; Cheat cheat; } kCodes[] = {
    {0x46157f, kTesters}, {0x46159c, kDoom}, {0x46158f, kEarthworm}, {0x4614c2, kFrameRate}, {0x4614cc, kPosition},
    {0x4614d5, kFly}, {0x4614e4, kSlow}, {0x4614f4, kAmmo}, {0x4614fe, kContinues}, {0x46150a, kLives},
    {0x46151e, kInvincible}, {0x461526, kHatman}, {0x46152d, kUnlockFive}, {0x46153c, kUnlockAll},
    {0x461550, kUnlockVaults}, {0x461563, kUnlockAtari},
};
uint8_t lowerC(uint8_t c) { return c >= 'A' && c <= 'Z' ? uint8_t(c + 0x20) : c; }   // FUN_00449744 ("C" locale)
}

void Game::typeChar(uint8_t c) {
    typed_[typedPos_] = c;
    typedPos_ = (typedPos_ + 1) & 0x7f;
    for (const auto& code : kCodes) {
        const uint8_t* s = kData_cheatCodes + (code.va - kCodesBase);
        const int n = int(std::strlen(reinterpret_cast<const char*>(s)));
        bool match = true;
        for (int i = 0; i < n; ++i)
            if (lowerC(uint8_t(s[i] - 0x2d)) != lowerC(typed_[(typedPos_ - n + i) & 0x7f])) match = false;
        if (!match) continue;
        if (options_.soundEnabled) playChannel(0, 0x24);  // (else MessageBeep(0))
        PlayerStats& st = stats_;
        switch (code.cheat) {
        case kTesters: cheatDialog_ = 0x100; break;
        case kDoom: cheatDialog_ = 0x7f; break;
        case kEarthworm: warp_.cheat = !warp_.cheat; break;
        case kFrameRate: options_.frameRate = !options_.frameRate; break;
        case kPosition: showPosition_ = !showPosition_; break;
        case kFly: options_.allowCheat = !options_.allowCheat; break;
        case kSlow: options_.slow = !options_.slow; break;
        case kAmmo: st.ammoCount[0] = st.ammoCount[1] = st.ammoCount[2] = 0x63; break;
        case kContinues: st.continues = 99; break;
        case kLives: st.lives = 0x63; break;
        case kInvincible: invincible_ = !invincible_; break;
        case kHatman: if (++hatman_ == 3) hatman_ = 0; break;
        case kUnlockFive: options_.levelBits |= 0x1f; menuChanged_ = true; break;
        case kUnlockAll: options_.levelBits = 0xffffffff; menuChanged_ = true; break;
        case kUnlockVaults: options_.levelBits |= 0x3800; menuChanged_ = true; break;
        case kUnlockAtari: options_.levelBits |= 0x100000; menuChanged_ = true; break;
        }
    }
}

void Game::renderFrameRate() {
    // FUN_00443c90 after "PAUSED": every frame counts (DAT_00453390); with the frame rate
    // (DAT_0046e054) or the position (DAT_0046e058) on, on a level with a HUD or the 2600 level,
    // not paused nor in a demo, every 500 ms the text is made again (the frames a second, the
    // camera, "%3d" / "%3.3d %3.3d" / "%3d %3.3d %3.3d"; on the 2600 level the screen) and the
    // global tick count starts again from 0; TextOutA writes it at (0x78, 0x10) of the buffer
    // (here in the 8x8 font, 9 lines up as the view is).
    ++fpsFrames_;
    if (!(options_.frameRate || showPosition_)) return;
    if ((levelNo_ >= kLoltunVault2 && levelNo_ <= kTitleScreen) || levelNo_ == kMapScreen || levelNo_ >= kIntro) return;
    if (paused_ || demoPlaying()) return;
    const uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const uint64_t elapsed = now - fpsTime_;
    if (elapsed >= 500) {
        char buf[48];
        const unsigned fps = unsigned(uint64_t(fpsFrames_) * 1000 / elapsed);
        bool made = true;
        if (levelNo_ == kAtari2600Level) {
            if (showPosition_) std::snprintf(buf, sizeof buf, "%3d", snap_.atariScreen & 0xff);   // DAT_0046fb7c
            else made = false;
        } else if (showPosition_ && options_.frameRate) {
            std::snprintf(buf, sizeof buf, "%3d %3.3d %3.3d", fps, snap_.scrollX, snap_.scrollY);
        } else if (options_.frameRate) {
            std::snprintf(buf, sizeof buf, "%3d", fps);
        } else {
            std::snprintf(buf, sizeof buf, "%3.3d %3.3d", snap_.scrollX, snap_.scrollY);
        }
        if (made) fpsText_ = buf;
        fpsFrames_ = 0;
        fs_.globalFrame = 0;                                  // DAT_0046fb74
        fpsTime_ = now;
    }
    if (hostText_) hostTexts_.push_back(HostText{0x78, 0x10 - 9, fpsText_});
    else renderer_.drawText(assets_->font, 0x78, 0x10 - 9, fpsText_.c_str(), snap_.scrollX);
}

} // namespace game
