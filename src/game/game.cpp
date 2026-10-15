#include "game/game.h"
#include "game/play_logic.h"

#include "ph/manifests.h"

#include <algorithm>
#include <cstring>
#include <cstdio>

namespace game {

namespace {
constexpr int kTitleAttractFrames = 0x5a0;    // FUN_00443c90: title -> demo after 1440 ticks
constexpr int kTitleAcceptInputAfter = 0x3d;  // keys ignored for the first 61 ticks
constexpr int kDemoNotActive = 0x800;         // DAT_0046fb8c sentinel
}

// --------------------------------------------------------------------------
// Default level logic: free camera + Harry animation preview.  This is the
// stand-in for the Genesis-derived gameplay code (FUN_0043628c and callees),
// which is not ported yet; see docs/RECONSTRUCTION_LEDGER.md.
class ExplorerLogic : public LevelLogic {
public:
    void enter(Game& g) override {
        // Free-flight preview: Harry starts near the top-left of the map.  The
        // real start positions come from the level's .als/.trg tables, which
        // are consumed by the unported Genesis logic.
        harryX_ = 160; harryY_ = 160; anim_ = 0; frame_ = 0; facingLeft_ = false;
        FrameState& fs = g.state();
        fs.scrollX = std::clamp(harryX_ - 160, 0, fs.maxScrollX);
        fs.scrollY = std::clamp(harryY_ - 150, 0, fs.maxScrollY);
    }
    void tick(Game& g) override {
        FrameState& fs = g.state();
        const uint32_t j = fs.joypad;
        const int speed = (j & engine::kJoyJump) ? 4 : 2;
        bool moving = false;
        if (j & engine::kJoyLeft)  { harryX_ -= speed; facingLeft_ = true;  moving = true; }
        if (j & engine::kJoyRight) { harryX_ += speed; facingLeft_ = false; moving = true; }
        if (j & engine::kJoyUp)    { harryY_ -= speed; moving = true; }
        if (j & engine::kJoyDown)  { harryY_ += speed; moving = true; }
        const auto& lv = g.level();
        if (lv.hasBg) {
            harryX_ = std::clamp(harryX_, 0, lv.bg.wTiles * 8 - 1);
            harryY_ = std::clamp(harryY_, 0, lv.bg.hTiles * 8 - 1);
        }
        // Animation: ready (idle) / run / whip.
        const char* animName = "hyiready";
        if (j & engine::kJoyWhip) animName = "hyiwhip";
        else if (moving) animName = "hyirun";
        if (animName != animName_) { animName_ = animName; frame_ = 0; anim_ = 0; }
        if (++anim_ >= 4) { anim_ = 0; ++frame_; }

        // Camera follows Harry (clamped like FUN_004460e0's limits).
        fs.scrollX = std::clamp(harryX_ - 160, 0, fs.maxScrollX);
        fs.scrollY = std::clamp(harryY_ - 150, 0, fs.maxScrollY);

        fs.displayList.clear();
        if (const ph::Sprite* spr = g.findSprite(animName_)) {
            engine::SpriteInstance s;
            s.x = harryX_; s.y = harryY_; s.sprite = spr;
            s.frame = uint32_t(frame_);
            s.flags = engine::kSprVisible | (facingLeft_ ? engine::kSprHFlip : 0u);
            fs.displayList.push_back(s);
        }
    }
private:
    int harryX_ = 0, harryY_ = 0, anim_ = 0, frame_ = 0;
    bool facingLeft_ = false;
    const char* animName_ = "hyiready";
};

// --------------------------------------------------------------------------

Game::Game(std::string dataDir, engine::Options& options)
    : dataDir_(std::move(dataDir)), options_(options) {
    assets_ = std::make_unique<ph::AssetBank>(ph::loadInit(dataDir_, palette_));   // FUN_004457d0
    mixer_.loadBank(*assets_);                                                      // FUN_0043ce10
    mixer_.setMasterVolume(options_.wavVolume);
    mixer_.setEnabled(options_.soundEnabled);
    startLevel(kIntro);                                                             // WinMain: FUN_004460e0(0x15)
}

void Game::setLogic(std::unique_ptr<LevelLogic> logic) { logic_ = std::move(logic); autoLogic_ = false; }

const ph::Sprite* Game::findSprite(const std::string& name) const {
    if (level_) if (const ph::Sprite* s = level_->sprite(name)) return s;
    return assets_->sprite(name);
}

void Game::startLevel(int level) {
    // FUN_004460e0
    levelNo_ = level;
    mixer_.stopAll();
    level_ = std::make_unique<ph::LevelData>(ph::loadLevel(level, dataDir_, palette_));
    fs_ = FrameState{};
    deferred_.clear();
    if (goneLevel_ != level) {
        gone_.clear(); checkpoint_ = Checkpoint{}; goneLevel_ = level; std::memset(levelFlags_, 0, sizeof levelFlags_);
        stats_.secret1up = false;               // 0x43091e: cleared when the exit trigger is taken
    }
    if (level_->hasBg) {
        // DAT_00450fb8 = (w/2 - 20) * 16, DAT_00450fbc = (h/2 - 14) * 16
        fs_.maxScrollX = std::max(0, (level_->bg.wTiles / 2 - 0x14) * 16);
        fs_.maxScrollY = std::max(0, (level_->bg.hTiles / 2 - 0xe) * 16);
        // FUN_00427e30: the game logic clamps its own camera a little tighter on two levels.
        if (level == 0) fs_.maxScrollY = 0x32e & 0xfff0;
        if (level == 6) fs_.maxScrollX = 0x13c3;
    }
    fb_.clear();
    if (level == kTitleScreen || level == kIntro || level == kCredits) {
        mode_ = Mode::Title;
        demoPos_ = kDemoNotActive;
    } else if (mode_ == Mode::Attract) {
        demoPos_ = 0; demoCountdown_ = 1; demoButtons_ = 0;
    } else {
        demoPos_ = kDemoNotActive;
        mode_ = Mode::Playing;
    }
    if (autoLogic_) {
        // Levels with collision data get the playable logic, the rest the explorer.
        if (!level_->collision.empty()) logic_ = std::make_unique<PlayLogic>();
        else logic_ = std::make_unique<ExplorerLogic>();
    }
    if (logic_) logic_->enter(*this);
}

void Game::togglePause() {
    if (mode_ == Mode::Playing) mode_ = Mode::Paused;
    else if (mode_ == Mode::Paused) mode_ = Mode::Playing;
}

void Game::tick(const engine::HostInput& host) {
    // ---- FUN_00444940: joypad -------------------------------------------
    fs_.joypadPrev = fs_.joypad;
    uint32_t pad;
    if (levelNo_ >= kLoltunVault2 && levelNo_ <= kLoltunVault1) pad = engine::Input::pollVaultKeys(host);
    else pad = input_.poll(host, options_.joystick, options_.allowCheat);
    input_.bindings = options_.keys;

    if (demoPos_ != kDemoNotActive && mode_ == Mode::Attract) {
        // Demo playback: any real key press ends the demo (DAT_004517f8 = 0xff).
        if (pad != 0) { mode_ = Mode::Title; demoPos_ = kDemoNotActive; startLevel(kTitleScreen); return; }
        if (--demoCountdown_ <= 0) {
            const DemoScript& d = demoScript(demoIndex_);
            const uint8_t frames = d.pairs[demoPos_], buttons = d.pairs[demoPos_ + 1];
            if (frames == 0xff && buttons == 0xff) {            // script exhausted
                fs_.frame = kTitleAttractFrames;                // force the sequencer on
            } else {
                demoCountdown_ = frames; demoButtons_ = buttons; demoPos_ += 2;
            }
        }
        pad = demoButtons_;
    }
    fs_.joypad = pad;

    if (mode_ == Mode::Paused) return;

    // ---- FUN_00443370 ---------------------------------------------------
    ++fs_.globalFrame;
    ++fs_.frame;
    if (levelNo_ != kTitleScreen && logic_) logic_->tick(*this);   // FUN_0043628c(level)
    computeParallax();
    tickAttract();

    if (pendingLevel_ >= 0) { const int l = pendingLevel_; pendingLevel_ = -1; startLevel(l); }
}

void Game::tickAttract() {
    // Title screen sequencing from the tail of FUN_00443c90.
    if (levelNo_ == kTitleScreen) {
        const bool start = fs_.frame >= kTitleAcceptInputAfter && (fs_.joypad & ~fs_.joypadPrev) != 0;
        if (start) {
            mode_ = Mode::Playing;
            stats_ = PlayerStats{};
            stats_.difficulty = options_.difficulty;
            seedRandom();                                    // FUN_00434815
            playSound(0x25);
            requestLevel(kCeibaJungle);                      // FUN_004324c6 + FUN_004460e0(0)
            return;
        }
        if (fs_.frame > kTitleAttractFrames) {
            ++demoIndex_;
            if (demoIndex_ == 3) { demoIndex_ = -1; requestLevel(kIntro); return; }
            mode_ = Mode::Attract;
            requestLevel(demoScript(demoIndex_).level);
        }
    } else if (mode_ == Mode::Attract && fs_.frame > kTitleAttractFrames) {
        requestLevel(kTitleScreen);
    } else if (levelNo_ == kIntro && fs_.frame > kTitleAttractFrames) {
        demoIndex_ = 0;
        requestLevel(kTitleScreen);
    }
}

void Game::render() {
    // ---- FUN_00443c90: snapshot the logic state --------------------------
    snap_ = fs_;
    engine::View view{snap_.scrollX, snap_.scrollY};
    deferred_.clear();

    // Parallax layer first (FUN_00436534).
    if (level_->hasParallax) {
        if (levelNo_ == kCeibaJungle || levelNo_ == kLakamulRainForest)
            renderer_.drawParallaxLinear(level_->parallaxLinear, snap_.parX, snap_.parY,
                                         snap_.lineScroll.empty() ? nullptr : snap_.lineScroll.data(),
                                         int(snap_.lineScroll.size()));
        else
            renderer_.drawParallaxTiles(level_->parallax, snap_.parX, snap_.parY, snap_.scrollX & 7);
    } else {
        renderer_.clearView(snap_.scrollX);
    }
    // Main tile layer (FUN_00436fb4).
    if (level_->hasBg) renderer_.drawTileLayer(level_->bg, view, deferred_);
    // Sprites, back to front (FUN_004426e0 walks the list from the end).
    for (auto it = snap_.displayList.rbegin(); it != snap_.displayList.rend(); ++it)
        if (!(it->flags & engine::kSprFront)) renderer_.drawSprite(*it, view);
    // High-priority tiles over the sprites, then the "front" sprites (FUN_00442dc0).
    if (level_->hasBg) renderer_.drawDeferredTiles(level_->bg, deferred_);
    for (auto it = snap_.displayList.rbegin(); it != snap_.displayList.rend(); ++it)
        if (it->flags & engine::kSprFront) renderer_.drawSprite(*it, view);

    if (levelNo_ == kTitleScreen) renderTitle();
    renderHud();                                                   // FUN_00442fa0
    if (mode_ == Mode::Paused) renderer_.drawText(assets_->font, 0x82, 0x60, "PAUSED", snap_.scrollX);
}

void Game::completeLevel() {
    // The original goes to the map screen (level 19) which picks the next
    // level; the map logic is not ported, so follow the tour order directly.
    static const int kTour[] = {kCeibaJungle, kXibalbaFalls, kTazamulMines, kLostCityOfCopan, kCopanTemple,
                                kLakamulRainForest, kYaxchilanLagoon, kBalankancheMine, kTikalRuins,
                                kTempleOfTikal, kPalenqueRuins, kTombOfPalenque, kJainaIslandFalls, kWarriorSpirit};
    playSound(0x2b);
    for (size_t i = 0; i + 1 < sizeof(kTour) / sizeof(kTour[0]); ++i)
        if (kTour[i] == levelNo_) { requestLevel(kTour[i + 1]); return; }
    requestLevel(kCredits);
}

uint32_t Game::random() {
    // FUN_0043387e.  s0 = DAT_00466772, s1 = DAT_00466776, s2 = DAT_0046677a (longs, little-endian).
    auto L = [&](int o) { uint32_t v; std::memcpy(&v, rng_ + o, 4); return v; };
    auto setL = [&](int o, uint32_t v) { std::memcpy(rng_ + o, &v, 4); };
    auto W = [&](int o) { uint16_t v; std::memcpy(&v, rng_ + o, 2); return v; };
    auto setW = [&](int o, uint16_t v) { std::memcpy(rng_ + o, &v, 2); };
    setL(8, L(8) + 1);
    uint32_t d0 = L(0) + L(4);
    setL(6, L(6) ^ d0);                 // overlaps the high word of s1 and the low word of s2
    d0 += L(8);
    setL(0, d0);
    // rcr / rcl by one with the carry cleared by the "add a0, 2" before each of them
    setW(0, uint16_t(W(0) >> 1));
    setW(2, uint16_t(W(2) << 1));
    setW(4, uint16_t(W(4) >> 1));
    setW(6, uint16_t(W(6) << 1));
    return demoPlaying() ? 1u : d0;
}

void Game::seedRandom() {
    // FUN_00434815: DAT_00466772 = 0xbbe307, DAT_00466776 = 0x89abcdef (DAT_0046677e = 0xbb40e64d
    // is outside the generator's state); DAT_0046677a keeps counting.
    const uint32_t s0 = 0xbbe307, s1 = 0x89abcdef;
    std::memcpy(rng_ + 0, &s0, 4);
    std::memcpy(rng_ + 4, &s1, 4);
}

void Game::setRandomSeed(uint32_t s0, uint32_t s1) {
    std::memcpy(rng_ + 0, &s0, 4);
    std::memcpy(rng_ + 4, &s1, 4);
}

void Game::loseLife() {
    // FUN_00430a69 death branch: lives-- ; at 0 a continue (if any) refills the
    // lives and clears the score and treasures, otherwise game over ("*1" -> title).
    if (--stats_.lives <= 0) {
        if (stats_.continues <= 0) {
            stats_ = PlayerStats{};
            mode_ = Mode::Title;
            requestLevel(kTitleScreen);
            return;
        }
        --stats_.continues;
        stats_.lives = stats_.difficulty ? 3 : 4;
        stats_.score = 0;
        stats_.treasures = 0;
    }
    requestLevel(levelNo_);                     // "*3": restart the level
}

void Game::drawTextWorld(int x, int y, const char* text) {
    renderer_.drawText(assets_->font, x, y, text, snap_.scrollX);
}

} // namespace game
