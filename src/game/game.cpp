#include "game/game.h"
#include "game/play_logic.h"
#include "game/objects.h"

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
    pendingRestart_ = false;
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
        // Levels with collision data get the playable logic, the rest the explorer; the 2600
        // level has its own code in the game logic (FUN_00435fc0's 0x401e00).
        if (!level_->collision.empty() || level == kAtari2600Level || level == kMapScreen) logic_ = std::make_unique<PlayLogic>();
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
    else if (pendingRestart_) {
        // "*3": FUN_0043e470 stops the sounds, then FUN_004360e9 starts the level logic again.
        pendingRestart_ = false;
        mixer_.stopAll();
        if (logic_) logic_->restart(*this);
    }
}

void Game::command(const char* cmd) {
    // The WinMain switch on DAT_0046b1a8[1], the cases of the vortex warps.
    switch (cmd[1]) {
    case '1':                                            // 0x444216: the level kept (after the map screen, a continue)
        mixer_.stopAll();                                // FUN_0043e470 (then 750 ms unless on the map)
        requestLevel(nextLevel_);
        break;
    case '4':                                            // 0x4445b9: the map screen, or the reunion after the Warrior Spirit
        mixer_.stopAll();
        requestLevel(levelNo_ == kWarriorSpirit ? kSimonRoom : kMapScreen);
        break;
    case '5':                                            // 0x4445ef: into a vault, or the 2600 level
        if (cmd[2] == '0') {
            warp_.returnLevel = kLoltunVault2;           // (0xb marks "back from the 2600 level" for "*6")
            requestLevel(warp_.cheat ? kEnding : kAtari2600Level);
        } else {
            warp_.returnLevel = levelNo_;
            requestLevel(cmd[2] - 0x26);                 // "*51" .. "*53": levels 11 .. 13
        }
        break;
    case '6':                                            // 0x444661: out of the 2600 level
        if (warp_.returnLevel == kLoltunVault2) { warp_.fromAtari = true; requestLevel(kLostCityOfCopan); }
        else requestLevel(warp_.cheat ? kEnding : kAtari2600Level);
        break;
    case '7':                                            // 0x4446bd: out of a vault
        if (warp_.returnLevel != 1) { warp_.fromVault = true; requestLevel(warp_.returnLevel); }
        else requestLevel(levelNo_);
        break;
    default:
        std::fprintf(stderr, "game: unhandled command %s\n", cmd);
        break;
    }
}

void Game::newGame() {
    // FUN_004324c6 with the title's set-up: lives, ammo, score and the cross-level state.
    stats_ = PlayerStats{};
    stats_.difficulty = options_.difficulty;
    std::memset(levelFlags_, 0, sizeof levelFlags_);
    const bool cheat = warp_.cheat;
    warp_ = Warp{};                                      // DAT_0046a884 / 86 / 88 = 0, DAT_00450fc8 = 1
    warp_.cheat = cheat;
    seedRandom();                                        // FUN_00434815
}

void Game::tickAttract() {
    // Title screen sequencing from the tail of FUN_00443c90.
    if (levelNo_ == kTitleScreen) {
        const bool start = fs_.frame >= kTitleAcceptInputAfter && (fs_.joypad & ~fs_.joypadPrev) != 0;
        if (start) {
            mode_ = Mode::Playing;
            newGame();
            playSound(0x25);
            requestLevel(kCeibaJungle);                      // FUN_004324c6 + FUN_004460e0(0)
            return;
        }
        if (fs_.frame > kTitleAttractFrames) {
            // 0x4447f1: the next demo (0: level 0, 1: level 1, 2: the 2600 level), each a new
            // game (FUN_004324c6); after the third the intro again
            ++demoIndex_;
            if (demoIndex_ == 3) { demoIndex_ = -1; requestLevel(kIntro); return; }
            mode_ = Mode::Attract;
            newGame();
            requestLevel(demoScript(demoIndex_).level);
        }
    } else if (mode_ == Mode::Attract && fs_.frame > kTitleAttractFrames) {
        requestLevel(kTitleScreen);
    } else if (levelNo_ == kIntro && fs_.frame > kTitleAttractFrames) {
        requestLevel(kTitleScreen);
    }
}

void Game::render() {
    // ---- FUN_00443c90: snapshot the logic state --------------------------
    snap_ = fs_;
    engine::View view{snap_.scrollX, snap_.scrollY};
    deferred_.clear();

    if (levelNo_ >= kLoltunVault2 && levelNo_ <= kLoltunVault1) { renderVault(); return; }
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
    if (levelNo_ == kAtari2600Level && level_->hasBg) renderAtari2600();
    else if (levelNo_ == kMapScreen && level_->hasBg)                // 0x43718c: the map wraps both ways, no priority list
        renderer_.drawWrappedLayer(level_->bg, snap_.scrollX, snap_.scrollY, snap_.scrollX & 7, snap_.scrollX & 7, nullptr);
    else if (level_->hasBg) renderer_.drawTileLayer(level_->bg, view, deferred_);
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

void Game::renderAtari2600() {
    // FUN_00436fb4 (0x437078) on level 20: the screen is two strips of the map, 40 tiles wide,
    // at the columns the screen's descriptor (0x4695a8 + screen * 4) picks through the engine's
    // tables at 0x46b1fc / 0x46b20c (dwords, indexed by the byte * 2): 17 rows of jungle from row 1
    // by its first byte, at buffer row 5, and 10 rows of ground from row 0x2e by its second (7:
    // column 0x50), at buffer row 0x8d; the 12 lines below are cleared.  No high-priority list.
    const uint8_t* r = kData_a26Rooms + (snap_.atariScreen & 0xff) * 4;
    auto column = [&](int off) { uint32_t v; std::memcpy(&v, kData_a26Columns + off, 4); return int(v); };
    renderer_.drawTileStrip(level_->bg, column(r[0] * 2), 1, 0x28, 0x11, 5);
    renderer_.drawTileStrip(level_->bg, r[1] == 7 ? 0x50 : column(0x10 + r[1] * 2), 0x2e, 0x28, 0xa, 0x8d);
    renderer_.clearRows(0xdd, 0xc);
}

void Game::renderVault() {
    // FUN_00443c90 on the Loltun vaults: the room is the parallax layer, drawn at the main scroll
    // (rounded to 8 px) with its high-priority tiles kept for after the sprites (FUN_0043731c);
    // the main layer is the ceiling, drawn over everything at the parallax scroll (0x4371a5,
    // FUN_00436fb4 after FUN_00442dc0).
    engine::View view{snap_.scrollX, snap_.scrollY};
    const int fine = snap_.scrollX & 7;
    renderer_.drawWrappedLayer(level_->parallax, snap_.scrollX, snap_.scrollY & ~7, snap_.parX & 7, fine, &deferred_);
    for (auto it = snap_.displayList.rbegin(); it != snap_.displayList.rend(); ++it)
        if (!(it->flags & engine::kSprFront)) renderer_.drawSprite(*it, view);
    renderer_.drawDeferredTiles(level_->parallax, deferred_);
    for (auto it = snap_.displayList.rbegin(); it != snap_.displayList.rend(); ++it)
        if (it->flags & engine::kSprFront) renderer_.drawSprite(*it, view);
    renderer_.drawWrappedLayer(level_->bg, snap_.parX, snap_.parY, fine, fine, nullptr);
    renderHud();
    if (mode_ == Mode::Paused) renderer_.drawText(assets_->font, 0x82, 0x60, "PAUSED", snap_.scrollX);
}

void Game::completeLevel() {
    // 0x430912: the level is kept for the map screen (DAT_0046b1ac), then "*4"
    tourLevel_ = levelNo_;
    command("*4");
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
    // FUN_00430eb6's tail: a life gone, "*3" starts the level again at the checkpoint.  With
    // none left, "*0" goes to the continue screen (level 14, not ported: the continue is taken
    // at once, as its 0x430d09 does, and "*1" loads the level again from its start), or with
    // no continues either "*2" ends the game.
    if (--stats_.lives > 0) { restartLevel(); return; }
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
    requestLevel(levelNo_);
}

void Game::drawTextWorld(int x, int y, const char* text) {
    renderer_.drawText(assets_->font, x, y, text, snap_.scrollX);
}

} // namespace game
