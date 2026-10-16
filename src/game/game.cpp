#include "game/game.h"
#include "game/play_logic.h"
#include "game/objects.h"

#include "ph/manifests.h"
#include "ph/tables.h"

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

namespace {
// The CD's tracks ripped to files, mixed in with the samples (looping, as the disc's track is
// played again at its end); the mixer's music volume is the CD line's.
class RippedCd : public engine::CdPlayer {
public:
    RippedCd(const std::string& dataDir, engine::Mixer& mixer) : tracks_(dataDir), mixer_(mixer) {}
    bool available() const override { return tracks_.available(); }
    void play(int track) override {
        if (track == 0) { mixer_.stopMusic(); return; }
        if (engine::MusicPcm pcm = tracks_.load(track)) mixer_.playMusic(std::move(pcm));
        else mixer_.stopMusic();
    }
    bool playing() const override { return mixer_.musicPlaying(); }
    bool hasVolume() const override { return true; }
    void setVolume(int v) override { mixer_.setMusicVolume(v); }
private:
    engine::CdTracks tracks_;
    engine::Mixer& mixer_;
};
} // namespace

Game::Game(std::string dataDir, engine::Options& options)
    : dataDir_(std::move(dataDir)), options_(options) {
    palette_.setFreeRange(options_.staticColours, 256 - options_.staticColours);    // FUN_0043e1c0
    assets_ = std::make_unique<ph::AssetBank>(ph::loadInit(dataDir_, palette_, &arena_));   // FUN_004457d0
    fillSpriteSlots(false);
    logicMem_.slots = &spriteSlots_;
    mixer_.loadBank(*assets_);                                                      // FUN_0043ce10
    mixer_.setEnabled(options_.soundEnabled);
    cd_ = std::make_unique<RippedCd>(dataDir_, mixer_);                             // FUN_00441cf0
    setCdVolume(options_.cdVolume);
    cutData_ = cutsceneInitialData();
    // The logic's memory holds one table of the data section, the collision shapes' heights from
    // 0x4668c4 (which the probes read from ph::kCollisionHeights); the rest of it starts at zero.
    std::memcpy(logicMem_.at(0x4668c4), ph::kCollisionHeights, Harry::kBase + Harry::kSize - 0x4668c4);
    startLevel(kIntro);                                                             // WinMain: FUN_004460e0(0x15)
}

void Game::playSound(int id) {
    // FUN_0043cc10 on channel 0, at Option_WavVolume (0..100) * 0x7f / 100.
    mixer_.playChannel(0, id, options_.wavVolume * 0x7f / 100);
}

void Game::playChannel(int ch, int id) {
    mixer_.playChannel(ch, id, options_.wavVolume * 0x7f / 100);   // FUN_0043cc10(id, ch)
}

void Game::cdPlay(int track) {
    // FUN_00441b80: a track from its start (and again at its end), or the CD stopped (0); none
    // with Option_Slow.
    cd_->play(track != 0 && !options_.slow ? track : 0);
}

void Game::setCdPlayer(std::unique_ptr<engine::CdPlayer> player) {
    cd_->play(0);
    cd_ = std::move(player);
    setCdVolume(options_.cdVolume);
    if (!paused_) cdPlayTracks();
}

void Game::cdTrackEnded() {
    // 0x3b9 in the WndProc: at the disc's successful end of a track, the tracks again.
    if (options_.cdMusic || options_.cdAmbient) cdPlayTracks();
}

void Game::cdPlayTracks() {
    // The level's tracks by the options (FUN_004460e0's end, the end of a pause, MM_MCINOTIFY): the
    // ambient one after the music, so it is the one that plays when both are on.
    if (options_.cdMusic) cdPlay(musicTrack_);
    if (options_.cdAmbient) cdPlay(ambientTrack_);
}

void Game::setCdOptions(bool music, bool ambient) {
    // The options' apply (0x441071 ..): a track turned on starts, both off stop the CD.
    if (music && !options_.cdMusic) cdPlay(musicTrack_);
    if (ambient && !options_.cdAmbient) cdPlay(ambientTrack_);
    options_.cdAmbient = ambient;
    options_.cdMusic = music;
    if (!music && !ambient) cdPlay(0);
}

void Game::setSoundEnabled(bool on) {
    // 0x441094 ..: off closes the sound (FUN_0043d8e0), on opens it again (FUN_0043d5d0).
    options_.soundEnabled = on;
    mixer_.setEnabled(on);
}

void Game::setWavVolume(int pct) {
    // FUN_0043cd40: the option, and the two channels' samples that play take it at once.
    options_.wavVolume = pct;
    if (!options_.soundEnabled) return;
    for (int ch = 0; ch < engine::Mixer::kChannels; ++ch) mixer_.setChannelVolume(ch, pct * 0x7f / 100);
}

void Game::setCdVolume(int pct) {
    // FUN_00441c50: auxSetVolume with the option (0 .. 100) times 0x28f a channel.
    cd_->setVolume(pct * 0x28f);
}

void Game::setLogic(std::unique_ptr<LevelLogic> logic) { logic_ = std::move(logic); autoLogic_ = false; }

void Game::fillSpriteSlots(bool level) {
    // FUN_004453a0, once for each sprite FUN_004457d0 (INIT.PH) or FUN_00447190 (the level) loads:
    // the slot that call names takes the address the sprite was put at (a sprite the level loads
    // for two slots is two copies, one in each); the other slots keep what they held.
    const std::span<const ph::ArenaSprite> list = level ? ph::levelArenaSprites(levelNo_) : ph::initArenaSprites();
    const auto& sprites = level ? level_->sprites : assets_->sprites;
    for (size_t k = 0; k < list.size() && k < sprites.size(); ++k)
        if (sprites[k]->arenaAddr()) spriteSlots_.set(list[k].slot, sprites[k]->arenaAddr());
    spriteSlots_.index([&](auto add) {
        for (const auto& s : assets_->sprites) add(s.get());
        if (level_) for (const auto& s : level_->sprites) add(s.get());
    });
}

SpriteRef Game::slotSprite(uint32_t slot) const {
    SpriteRef r;
    r.addr = spriteSlots_.of(slot);
    r.p = spriteSlots_.at(r.addr);
    return r;
}

SpriteRef Game::slotSprite(const char* name) const {
    if (const uint32_t slot = spriteSlotOf(name)) return slotSprite(slot);
    SpriteRef r;
    r = findSprite(name);
    return r;
}

const ph::Sprite* Game::findSprite(const std::string& name) const {
    if (level_) if (const ph::Sprite* s = level_->sprite(name)) return s;
    return assets_->sprite(name);
}

void Game::startLevel(int level) {
    // FUN_004460e0
    ++levelLoads_;
    stages_.clear();
    exitCmd_.clear();                                    // DAT_0046b1a8 = 0
    titleSoundA_ = titleSoundB_ = false;                 // DAT_0046fa40 / 44 = 0
    // the menu's item for the level comes on, but in a demo (DAT_0046fb8c is 0 when one loads)
    const bool demoLoad = mode_ == Mode::Attract && level != kTitleScreen && level != kIntro && level != kCredits;
    if (!demoLoad && level >= 0 && level < 32 && !(options_.levelBits & (1u << level))) { options_.levelBits |= 1u << level; menuChanged_ = true; }
    levelNo_ = level;
    if (level == kTitleScreen) newGame();                // FUN_004324c6
    // FUN_0043a1c0 (the menu's check mark, the front end's) and FUN_00440bc0: the level's name in
    // the status bar (the string table's 0x75 + level, " " for the levels without one).
    setStatusText(level < kIntro && level != kContinueScreen && level != kTitleScreen && level != kMapScreen
                      ? std::string("\t") + ph::levelName(level) : std::string(" "));
    mixer_.stopAll();                                    // FUN_0043d910: the two channels end, and the CD
    cdPlay(0);
    brightness_ = 0;                                     // DAT_0046b1b0, DAT_00452770
    shownBrightness_ = 0;
    appliedBrightness_ = 0;                              // FUN_004451a0 forgets the effect (without its colours)
    effectSaved_ = false;
    // But for the map screen the view is cleared and shown, and in a window (but on the title)
    // "Loading..." stays up while the files load.
    if (level != kMapScreen) {
        fb_.clear();
        if (loadingHook) loadingHook(level == kTitleScreen ? 2 : 1);
    }
    level_ = std::make_unique<ph::LevelData>(ph::loadLevel(level, dataDir_, palette_, &arena_));
    fillSpriteSlots(true);                               // (FUN_00447190's slots)
    // (DAT_0046fb74, the engine's tick count, runs on from the boot: only the frame rate's text
    // starts it again, FUN_00443c90; the clouds drift on its fourth ticks)
    const int globalFrame = fs_.globalFrame;
    fs_ = FrameState{};
    fs_.globalFrame = globalFrame;
    fs_.mainLayer = level_->hasBg;                       // DAT_00450fcc
    fs_.parallaxLayer = level_->hasParallax;             // DAT_00450514
    cloudDrift_ = 0;                                     // FUN_00442410: DAT_00450f94 = 0
    deferred_.clear();
    if (level_->hasBg) {
        // DAT_00450fb8 = (w/2 - 20) * 16, DAT_00450fbc = (h/2 - 14) * 16
        fs_.maxScrollX = std::max(0, (level_->bg.wTiles / 2 - 0x14) * 16);
        fs_.maxScrollY = std::max(0, (level_->bg.hTiles / 2 - 0xe) * 16);
        // FUN_00427e30: the game logic clamps its own camera a little tighter on two levels.
        if (level == 0) fs_.maxScrollY = 0x32e & 0xfff0;
        if (level == 6) fs_.maxScrollX = 0x13c3;
    } else {
        fs_.maxScrollX = 0x280;                          // 0x44654c: a level without a background
        fs_.maxScrollY = 0xe0;
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
    logicMem_.b(0x466500) = demoPos_ < kDemoNotActive ? 1 : 0;   // 0x4461f3: DAT_00466500, a demo plays (the logic reads it)
    if (autoLogic_) {
        // The game logic (FUN_00435fc0) runs every level but the title: the levels with collision
        // data, and the map, the 2600 level and the cutscenes with their own code.  (The explorer,
        // a free camera, is left for a level without either; none of the shipped ones.)
        // (The last level's logic goes first: it gives back the object records' places.)
        logic_.reset();
        if (level == kTitleScreen) {}                       // (FUN_00442410 starts no logic on the title)
        else if (!level_->collision.empty() || level == kAtari2600Level || level == kMapScreen || (level >= kIntro && level <= kCredits))
            logic_ = std::make_unique<PlayLogic>(logicMem_, objRecords_);
        else logic_ = std::make_unique<ExplorerLogic>();
    }
    // The level's CD tracks (0x470470 / 0x4704d8; the intro after the demos plays track 5).
    musicTrack_ = engine::levelMusicTrack(level);
    ambientTrack_ = engine::levelAmbientTrack(level);
    if (introAfterDemos_ && level == kIntro) musicTrack_ = ambientTrack_ = 5;
    cdPlayTracks();
    cdVolumeCmd_ = 0;
    startKey_ = 0;                                       // FUN_00442410: DAT_0046fb88 = 0
    if (level == kTitleScreen) titleSetup();
    if (loadingHook) loadingHook(0);                     // (the dialog goes before FUN_00442410)
    if (logic_) logic_->enter(*this);
    preTicks(1 + preTickCount(level));                   // FUN_004460e0's end (on the title: no logic, the counters and the pan)
    // Then the first frame (FUN_00443c90) and, but on levels 21 .. 23, the fade in (FUN_0043e810).
    drawPending_ = true;
    if (level < kIntro || level > kCredits) enterStage(Stage{Stage::FadeIn, kFadeInMs});
}

void Game::togglePause() { setPaused(!paused_); }

void Game::setStatusText(const std::string& text) {
    statusText_ = text;
    ++statusSerial_;
}

bool Game::menuNeedsConfirm() const {
    // FUN_0043a370: DAT_00451914 (the score the logic's end published), no demo, and not on the
    // title, the intro, the reunion or the end.
    return fs_.hudScore != 0 && !demoPlaying() && levelNo_ != kTitleScreen && levelNo_ != kIntro &&
           levelNo_ != kSimonRoom && levelNo_ != kCredits;
}

void Game::menuStart(int level) {
    // FUN_0043a370 once the question is answered: the demo off (DAT_0046fb8c = 0x800), a new game
    // (FUN_004324c6), the pause ended, the CD stopped, and the level loaded (FUN_004460e0).
    demoPos_ = kDemoNotActive;
    mode_ = Mode::Playing;
    newGame();
    setPaused(false);
    if (options_.cdMusic || options_.cdAmbient) cdPlay(0);
    startLevel(level);
}

void Game::specialKey() {
    // 0x439c6c: F10 (WM_SYSKEYDOWN / UP) or Alt+Y (WM_SYSCHAR 'y').
    if (levelNo_ == kWarriorSpirit) { if (!channelBusy(0)) playChannel(0, 0x52); }
    else if (levelNo_ == kTitleScreen) demoIndex_ = 2;    // DAT_0046fbbc
    else specialIdle_ = true;                            // DAT_0046d04c
}

void Game::setPaused(bool on) {
    // FUN_00444ee0: not while a level loads (nor in the switch's waits), nor into a pause in the
    // vaults.  The pause stops the two channels and the CD (FUN_0043d910) and shows "PAUSED" at
    // once (DAT_00450fa8 = 0x20); its end plays the level's tracks from their start.
    if (!stages_.empty() || on == paused_) return;
    if (on && levelNo_ >= kLoltunVault2 && levelNo_ <= kLoltunVault1) return;
    if (on) {
        paused_ = true;
        mixer_.stopAll();
        cdPlay(0);
        timerTicks_ = 0x20;
        return;
    }
    cdPlayTracks();
    paused_ = false;
}

void Game::pollJoypad(const engine::HostInput& host) {
    // FUN_00444940: the joypad from the keyboard / joystick, or from the demo script.  During a
    // demo any key makes it 0xff, and so does the script's end (0xff, 0xff): FUN_0043628c turns
    // 0xff into "*2", which goes back to the title.
    fs_.joypadPrev = fs_.joypad;
    uint32_t pad;
    if (levelNo_ >= kLoltunVault2 && levelNo_ <= kLoltunVault1) pad = engine::Input::pollVaultKeys(host);
    else pad = input_.poll(host, options_.joystick, options_.allowCheat);
    input_.bindings = options_.keys;
    if (joypadHook) {
        const int p = joypadHook(fs_.globalFrame + 1);           // (the original polls after the count's step)
        if (p >= 0) pad = uint32_t(p);
    }

    if (demoPlaying() && engineOn_) {                    // (DAT_0046fbb4: not during a load's engine ticks)
        if (pad != 0) { fs_.joypad = 0xff; return; }
        if (--demoCountdown_ == 0) {                     // DAT_0046fb98, a byte
            const DemoScript& d = demoScript(demoIndex_);
            demoCountdown_ = d.pairs[demoPos_];
            demoButtons_ = d.pairs[demoPos_ + 1];
            demoPos_ += 2;
        }
        pad = demoButtons_;
    }
    fs_.joypad = pad;
}

void Game::engineTick() {
    // FUN_00443370 after its joypad: the frame counters, the logic (FUN_0043628c) and the
    // per-level parallax camera.  (The original polls the joypad between the counters and the
    // logic; nothing in between reads it.)
    ++fs_.globalFrame;
    ++fs_.frame;
    if (levelNo_ != kTitleScreen && logic_) logic_->tick(*this);
    if (exitPending()) fs_.displayList.clear();          // engineExit empties the list (0x43650d)
    computeParallax();
    titleTick();
}

void Game::preTicks(int n) {
    // Engine ticks before the first frame: FUN_004460e0 runs one after the level's logic start
    // and thirteen more on levels 0, 2, 5, 8 and 16 (the table at 0x46ffa0), the "*3" path the
    // thirteen alone.  FUN_00443370 does nothing once a command is set.  DAT_00451940 is put
    // back after them, so the HUD slides in from where the logic's start left it.  The engine is
    // off meanwhile (DAT_0046fbb4 = 0 from the load's start to the end of its fade in): the demo
    // script waits (FUN_00444940), the joypad is the keys as they are.
    const int slide = fs_.hudSlide;
    engineOn_ = false;
    for (int i = 0; i < n && !exitPending(); ++i) {
        pollJoypad(lastHost_);
        engineTick();
    }
    engineOn_ = true;
    fs_.hudSlide = slide;
}

int Game::preTickCount(int level) {
    static const uint8_t kTable[0x20] = {1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1};   // 0x46ffa0
    return level >= 0 && level < 0x20 && kTable[level] ? 13 : 0;
}

void Game::tick(const engine::HostInput& host) {
    // One callback of the 17 ms timer (FUN_004448c0), and what the main loop does with the frame
    // it makes ready (FUN_00443c90's tail: the command switch, the title and the attract).
    ++timerTicks_;                                                   // DAT_00450fa8
    if (!mixer_.deviceClock()) {                                     // (no audio device: the samples play on game time)
        audioRem_ += engine::Mixer::kOutputRate * kTickMs;
        mixer_.advance(audioRem_ / 1000);
        audioRem_ %= 1000;
    }
    // WM_KEYDOWN (FUN_004388c0) for any key but F1, Esc and Pause: a key pressed ends a pause;
    // otherwise it is the engine's start key (DAT_0046fb88).  The messages come from the keyboard's
    // presses and, for a key held, its repeats after the keyboard's delay (half a second), so a key
    // down from before the window had the focus (the one that closed the warning, say) sends none
    // until then; the host's key states give the presses by their edges, and a key down at the
    // first tick is taken as held from before.  (The states include the mouse buttons, VK_LBUTTON
    // .. VK_XBUTTON2, which send no WM_KEYDOWN: a click on the window is not a key.)
    if (firstTick_) {
        lastHost_ = host; firstTick_ = false;
        for (int k = 0; k < 256; ++k) keyHeld_[size_t(k)] = host.keyDown[size_t(k)] ? 1 : 0;
    }
    bool pressed = false, held = false;
    for (int k = 7; k < 256; ++k) {
        if (k == 0x70 || k == 0x1b || k == 0x13 || !host.keyDown[size_t(k)]) { keyHeld_[size_t(k)] = 0; continue; }
        const bool edge = !lastHost_.keyDown[size_t(k)];
        if (edge) { pressed = true; keyHeld_[size_t(k)] = 0; }
        if (keyHeld_[size_t(k)] < 0xffff) ++keyHeld_[size_t(k)];
        if (edge || keyHeld_[size_t(k)] > kKeyRepeatTicks) held = true;
    }
    if (host.padBits & ~uint32_t(engine::kJoyDebug)) held = true;   // (the port's gamepad: its buttons as keys)
    if (host.padPressed) pressed = true;
    if (paused_) { if (pressed) setPaused(false); }
    else if (held) startKey_ = 1;
    lastHost_ = host;
    if (!stages_.empty()) { runStages(kTickMs); return; }           // the main thread is busy: no ticks, no frames

    if (paused_) return;
    if (exitPending()) { beginCommand(); return; }                   // (a command set outside a tick)
    pollJoypad(host);
    engineTick();                                                    // FUN_00443370
    if (exitPending()) beginCommand();                               // the switch on DAT_0046b1a8
    else tickAttract();
}

void Game::loadLevel(int level) {
    Stage st{Stage::Load};
    st.level = level;
    enterStage(st);
}

void Game::enterStage(const Stage& st) {
    // A stage of the command switch begins.
    stages_.push_back(st);
    if (stages_.size() == 1) { stageMs_ = 0; beginStage(); }
}

void Game::beginStage() {
    Stage& st = stages_.front();
    switch (st.kind) {
    case Stage::FadeOut:
        // FUN_0043e470: the brightness effect gives its colours back first; when it has the screen
        // black already (-0x100) there is nothing to fade.
        if (shownBrightness_ == -0x100 && effectSaved_) st.ms = 0;
        else if (shownBrightness_ != 0 && effectSaved_) endEffect();
        break;
    case Stage::GameOver:
        // 0x44426b: the status bar's text " ", the sound, "[GAME OVER]" on a cleared screen, every
        // colour set again (FUN_0043ed00).
        setStatusText(" ");
        playChannel(0, 0x20);
        mode_ = Mode::GameOver;
        drawPending_ = true;
        palette_.showAll();
        break;
    case Stage::FadeIn:
        // FUN_0043e810: the free colours black, the frame shown.
        for (int i = palette_.firstFree(); i < palette_.endFree(); ++i) palette_.show(i, ph::Rgb{});
        break;
    default:
        break;
    }
}

void Game::runStages(int ms) {
    // The blocking parts of the switch, run against the timer's clock.
    stageMs_ += ms;
    while (!stages_.empty()) {
        const Stage st = stages_.front();
        switch (st.kind) {
        case Stage::WaitSounds:
            // FUN_0043cb50: the logic's two channels play out (when the sound is on).
            if (options_.soundEnabled && (mixer_.channelBusy(0) || mixer_.channelBusy(1))) { stageMs_ = 0; return; }
            stageMs_ = 0;
            break;
        case Stage::FadeOut:
        case Stage::FadeIn:
            if (stageMs_ < st.ms) { showFade(); return; }
            stageMs_ -= st.ms;
            if (st.kind == Stage::FadeOut) { if (shownBrightness_ != 0) endEffect(); }
            else palette_.showAll();                     // FUN_0043ebc0 for every entry
            break;
        case Stage::Wait:
        case Stage::GameOver:
            if (stageMs_ < st.ms) return;
            stageMs_ -= st.ms;
            break;
        case Stage::NewGame:
            newGame();                                   // FUN_004324c6
            break;
        case Stage::CdFade: {
            // FUN_00442190: the CD's volume a twentieth down every 100 ms, then the CD stopped and
            // its volume set back.
            const int v0 = options_.cdVolume * 0x28f;
            if (stageMs_ < st.ms) { cd_->setVolume(v0 - std::min(stageMs_ / 100 + 1, 20) * (v0 / 20)); return; }
            stageMs_ -= st.ms;
            cdPlay(0);
            setCdVolume(options_.cdVolume);
            break;
        }
        case Stage::Load:
            // FUN_004460e0 starts with the CD's fade (FUN_00442190: only while it plays, with a
            // volume to turn down)
            if (!st.cdFaded && cd_->playing() && cd_->hasVolume() && !options_.slow) {
                stages_.front().cdFaded = true;
                stages_.push_front(Stage{Stage::CdFade, kCdFadeMs});
                stageMs_ = 0;
                continue;
            }
            stages_.pop_front();
            startLevel(st.level);                        // (its own fade in follows)
            return;
        case Stage::Restart:
            // "*3": DAT_0046b1a8 = 0, FUN_004360e9, the thirteen engine ticks, the frame, the fade in.
            stages_.pop_front();
            exitCmd_.clear();
            if (logic_) logic_->restart(*this);
            preTicks(preTickCount(levelNo_));
            drawPending_ = true;
            if (levelNo_ < kIntro || levelNo_ > kCredits) enterStage(Stage{Stage::FadeIn, kFadeInMs});
            return;
        }
        stages_.pop_front();
        if (!stages_.empty()) beginStage();
    }
}

void Game::showFade() {
    // The colours the screen shows during a fade (AnimatePalette on the free range).
    const Stage& st = stages_.front();
    const int a = palette_.firstFree(), b = palette_.endFree();
    if (st.kind == Stage::FadeOut) {
        // FUN_0043e470: 13 darker every 30 ms, 19 times (the brightest colours keep 8 at the end).
        const int d = 13 * std::min(stageMs_ / kFadeStepMs + 1, 19);
        auto f = [d](uint8_t c) { return uint8_t(c >= d ? c - d : 0); };
        for (int i = a; i < b; ++i) { const ph::Rgb c = palette_.colour(i); palette_.show(i, ph::Rgb{f(c.r), f(c.g), f(c.b)}); }
    } else if (stageMs_ >= kFadeStepMs) {
        // FUN_0043e810: after 30 ms of black, half the colour plus 4, 8 .. 60, every 30 ms.
        const int add = 4 * std::min((stageMs_ - kFadeStepMs) / kFadeStepMs + 1, 15);
        auto f = [add](uint8_t c) { return uint8_t(std::min<int>(c, (c >> 1) + add)); };
        for (int i = a; i < b; ++i) { const ph::Rgb c = palette_.colour(i); palette_.show(i, ph::Rgb{f(c.r), f(c.g), f(c.b)}); }
    }
}

void Game::endEffect() {
    // The brightness effect's colours back (DAT_0046dc2c -> DAT_0046d824) and the effect forgotten.
    if (effectSaved_)
        for (int i = palette_.firstFree(); i < palette_.endFree(); ++i) palette_.setColour(i, effectBackup_[size_t(i)]);
    effectSaved_ = false;
    shownBrightness_ = 0;
    appliedBrightness_ = 0;
    brightness_ = 0;
}

void Game::applyBrightness() {
    // FUN_0043de70, at every present: DAT_00452770 (the logic's DAT_0046b1b0 as the frame took
    // it) added to the free colours, clamped, the colours kept aside while it lasts.
    const int v = shownBrightness_;
    if (appliedBrightness_ == v) return;
    const int a = palette_.firstFree(), b = palette_.endFree();
    if (v != 0 && appliedBrightness_ == 0 && !effectSaved_ && a != b) {
        effectSaved_ = true;
        for (int i = a; i < b; ++i) effectBackup_[size_t(i)] = palette_.colour(i);
    }
    if (v == 0 && appliedBrightness_ != 0 && effectSaved_) {
        if (a != b) {
            effectSaved_ = false;
            for (int i = a; i < b; ++i) palette_.setColour(i, effectBackup_[size_t(i)]);
        }
        appliedBrightness_ = 0;
        palette_.showAll();
        return;
    }
    auto f = [v](uint8_t c) {
        const int x = int(c) + v;
        return uint8_t(v < 0 ? (x < 1 ? 0 : x) : (x >= 0x100 ? 0xff : x));
    };
    for (int i = a; i < b; ++i) {
        const ph::Rgb c = effectBackup_[size_t(i)];
        palette_.setColour(i, ph::Rgb{f(c.r), f(c.g), f(c.b)});
    }
    appliedBrightness_ = v;
    palette_.showAll();
}

void Game::command(const char* cmd) {
    // The logic's engine exit: the command string (DAT_0046b1a8) for the switch after the tick,
    // then `jmp [DAT_0046b1a4]` out of the logic's call (bridge).
    if (exitCmd_.empty()) exitCmd_ = cmd;
    if (bridges_ > 0) throw EngineExit{};
}

void Game::beginCommand() {
    // FUN_00443c90's tail: the switch on DAT_0046b1a8[1].  The fades (FUN_0043e470 out,
    // FUN_0043e810 in at the end of the level load) and the waits block the engine meanwhile.
    const std::string cmd = exitCmd_;
    drawPending_ = true;                                 // the exit tick's frame is still drawn (without sprites)
    auto fadeOut = [&] { enterStage(Stage{Stage::FadeOut, kFadeOutMs}); };
    auto waitSounds = [&] { enterStage(Stage{Stage::WaitSounds}); };
    auto wait = [&](int ms) { enterStage(Stage{Stage::Wait, ms}); };
    auto load = [&](int level) { loadLevel(level); };
    switch (cmd.size() > 1 ? cmd[1] : 0) {
    case '0':                                            // 0x444200: the continue screen; the level is kept for "*1"
        fadeOut();
        nextLevel_ = levelNo_;
        load(kContinueScreen);
        break;
    case '1':                                            // 0x444216: the level kept (after the map screen, a continue)
        fadeOut();
        if (levelNo_ != kMapScreen) wait(750);
        load(nextLevel_);
        break;
    case '2':                                            // 0x44423d: game over (3000 ms), or a demo's end
        fadeOut();
        if (!demoPlaying()) enterStage(Stage{Stage::GameOver, 3000});
        demoPos_ = kDemoNotActive;
        load(kTitleScreen);
        break;
    case '3':                                            // 0x4442fe: the logic again at the checkpoint
        waitSounds();
        fadeOut();
        wait(750);
        enterStage(Stage{Stage::Restart});
        break;
    case '4':                                            // 0x4445b9: the map screen, or the reunion after the Warrior Spirit
        fadeOut();
        wait(750);
        load(levelNo_ == kWarriorSpirit ? kSimonRoom : kMapScreen);
        break;
    case '5':                                            // 0x4445ef: into a vault, or the 2600 level
        waitSounds();
        fadeOut();
        if (cmd.size() > 2 && cmd[2] == '0') {
            warp_.returnLevel = kLoltunVault2;           // (0xb marks "back from the 2600 level" for "*6")
            load(warp_.cheat ? kEnding : kAtari2600Level);
        } else {
            warp_.returnLevel = levelNo_;
            load(cmd[2] - 0x26);                         // "*51" .. "*53": levels 11 .. 13
        }
        break;
    case '6':                                            // 0x444661: out of the 2600 level
        waitSounds();
        fadeOut();
        if (warp_.returnLevel == kLoltunVault2) { warp_.fromAtari = true; load(kLostCityOfCopan); }
        else load(warp_.cheat ? kEnding : kAtari2600Level);
        break;
    case '7':                                            // 0x4446bd: out of a vault
        waitSounds();
        fadeOut();
        if (warp_.returnLevel != 1) { warp_.fromVault = true; load(warp_.returnLevel); }
        else load(levelNo_);
        break;
    case '8':
        load(kTitleScreen);
        break;
    case '9':
        load(kCredits);
        break;
    default:
        std::fprintf(stderr, "game: unhandled command %s\n", cmd.c_str());   // (a MessageBox in the original)
        exitCmd_.clear();
        break;
    }
}

void Game::newGame() {
    // FUN_004324c6 with the title's set-up: lives, ammo, score and the cross-level state; in the
    // logic's memory DAT_00465b39 and Harry's damage, and the cutscenes' script cursor.
    stats_ = PlayerStats{};
    logicMem_.b(0x465b39) = 0;
    logicMem_.b(0x465b19) = 0;
    if (cutData_.size() >= 4) std::memset(cutData_.data(), 0, 4);   // DAT_00456000
    hudAnim_.oneUp = false;                              // DAT_0046fa38
    hudAnim_.prevMeter = 0;                              // DAT_0046b178
    std::memset(levelFlags_, 0, sizeof levelFlags_);
    const bool cheat = warp_.cheat;
    warp_ = Warp{};                                      // DAT_0046a884 / 86 / 88 = 0, DAT_00450fc8 = 1
    warp_.cheat = cheat;
}

void Game::tickAttract() {
    // FUN_00443c90's tail after the switch: the title's start key and the attract sequence.
    if (levelNo_ == kTitleScreen) {
        titleFrame();                                    // (what the frame's composition does first)
        if (fs_.frame < kTitleAcceptInputAfter) {
            fs_.joypad = 0;
            startKey_ = 0;
        } else {
            if (options_.joystick) { pollJoypad(lastHost_); startKey_ = fs_.joypad != 0; }
            if (startKey_) {
                // 0x444797: fade out, 500 ms, a new game (FUN_004324c6), level 0
                enterStage(Stage{Stage::FadeOut, kFadeOutMs});
                enterStage(Stage{Stage::Wait, 500});
                enterStage(Stage{Stage::NewGame});
                loadLevel(kCeibaJungle);
                return;
            }
        }
        if (fs_.frame > kTitleAttractFrames) {
            // 0x4447f1: the next demo (0: level 0, 1: level 1, 2: the 2600 level), each a new
            // game (FUN_004324c6); after the third the intro again.  No fade out.
            ++demoIndex_;
            if (demoIndex_ == 3) { demoIndex_ = -1; introAfterDemos_ = true; loadLevel(kIntro); return; }   // DAT_0046fb84
            mode_ = Mode::Attract;
            newGame();
            loadLevel(demoScript(demoIndex_).level);
        }
    }
}

uint8_t Game::bigGlyph(uint8_t ch, int& width) {
    // FUN_00431b73: the frame of ltr_lg for a character and its width (the frame's own); a
    // character it does not know is one of the two spaces, by turns (DAT_0046a878).
    uint8_t g;
    if (ch >= 'A' && ch <= 'Z') g = uint8_t(ch - 'A');
    else switch (ch) {
        case '2': g = 0x1a; break;
        case '3': g = 0x1b; break;
        case '5': g = 0x1c; break;
        case '6': g = 0x1d; break;
        case '9': g = 0x1e; break;
        case '\\': g = 0x24; break;
        case '.': g = 0x25; break;
        case ',': g = 0x26; break;
        case ':': g = 0x27; break;
        case '?': g = 0x28; break;
        case '!': g = 0x29; break;
        case '-': g = 0x2a; break;
        case '[': g = 0x2b; break;
        case ']': g = 0x2e; break;
        default:
            bigSpace_ = uint8_t((bigSpace_ + 1) & 1);
            g = uint8_t(0x2c + bigSpace_);
            break;
    }
    const ph::Sprite* spr = assets_->sprite("ltr_lg");
    width = spr && g < spr->frameCount() ? spr->frame(g).w : 0;
    return g;
}

void Game::renderGameOver() {
    // 0x44426b: the whole buffer cleared and "[GAME OVER]" drawn by FUN_00431bfb: ltr_lg letters
    // centred on x 0xa0, at y 0x5f of the screen.
    fb_.clear();
    hostTexts_.clear();
    const char* text = "[GAME OVER]";
    int total = 0;
    for (const char* c = text; *c; ++c) { int w; bigGlyph(uint8_t(*c), w); total += w; }
    int x = 0xa0 - (total >> 1);
    const engine::View view{0, 0};
    for (const char* c = text; *c; ++c) {
        int w;
        engine::SpriteInstance s;
        s.frame = bigGlyph(uint8_t(*c), w);
        s.sprite = assets_->sprite("ltr_lg");
        s.x = x; s.y = 0x5f; s.flags = engine::kSprVisible;
        if (s.sprite) renderer_.drawSprite(s, view);
        x += w;
    }
}

void Game::render() {
    // While the command switch blocks, the screen keeps its last frame (the fades change only the
    // colours), but for the frame a level load or "*3" composes and the game-over screen.
    if (!stages_.empty() && !drawPending_) return;
    drawPending_ = false;
    if (mode_ == Mode::GameOver) { renderGameOver(); return; }
    renderer_.setHatman(hatman_, assets_->sprite("hatman"));       // DAT_0046d058
    composeFrame();
    applyBrightness();                                             // FUN_00443b90 -> FUN_0043de70
}

void Game::composeFrame() {
    // ---- FUN_00443c90: snapshot the logic state --------------------------
    snap_ = fs_;
    if (snap_.cutscene) {
        // DAT_0046fb90: the cutscenes' actors copied for the frame (0x45623f -> 0x4564bf, 0x280
        // bytes), which their drawing reads
        std::memcpy(cutData_.data() + (0x4564bf - 0x456000), cutData_.data() + (0x45623f - 0x456000), 0x280);
        std::memcpy(snap_.cutActors.data(), cutData_.data() + (0x4564bf - 0x456000), 0x280);
    }
    hostTexts_.clear();
    shownBrightness_ = int32_t(brightness_);                       // DAT_00452770 = DAT_0046b1b0
    if (cdVolumeCmd_ == 1) setCdVolume(options_.cdVolume >> 2);     // DAT_00450500: the stop-watch
    if (cdVolumeCmd_ == 2) setCdVolume(options_.cdVolume);
    cdVolumeCmd_ = 0;
    engine::View view{snap_.scrollX, snap_.scrollY};
    deferred_.clear();

    if (levelNo_ >= kLoltunVault2 && levelNo_ <= kLoltunVault1) { renderVault(); return; }
    const bool cut = snap_.cutscene;                               // DAT_0046fb90: the cutscenes' actors
    const ph::Background* deferredMap = nullptr;                   // the map of the high-priority tiles kept (DAT_0046b1f8)
    // FUN_00436534: the parallax map (DAT_00450514).
    if (snap_.parallaxLayer && level_->hasParallax) {
        if (levelNo_ == kCeibaJungle || levelNo_ == kLakamulRainForest) {
            renderer_.drawParallaxLinear(level_->parallaxLinear, snap_.parX, snap_.parY, snap_.scrollX,
                                         snap_.lineScroll.empty() ? nullptr : snap_.lineScroll.data(),
                                         int(snap_.lineScroll.size()));
        } else if (levelNo_ == kXibalbaFalls || levelNo_ == kYaxchilanLagoon || levelNo_ == kJainaIslandFalls) {
            // 0x4366ca: the waterfalls, a column at a time (camera.cpp)
            renderer_.drawWaterfall(level_->parallax, snap_.waterColumns.data(), snap_.waterColumns.size(), snap_.parX, snap_.scrollX);
        } else if (levelNo_ == kBalankancheMine && snap_.rideLoop) {
            // 0x4366a8: none while the ride's loop runs (the map covers the whole view there)
        } else if (levelNo_ == kIntro) {
            // 0x4365e2: the intro's scene is its parallax map, drawn at the main camera (whole rows)
            // with its high-priority tiles kept for after the actors (FUN_0043731c)
            const int fine = snap_.scrollX & 7;
            renderer_.drawWrappedLayer(level_->parallax, snap_.scrollX, snap_.scrollY & ~7, fine, fine, &deferred_);
            deferredMap = &level_->parallax;
        } else {
            // 0x436589: at the parallax camera, wrapping both ways, in whole rows of 8 px but on
            // level 9 (0x4371a5)
            renderer_.drawWrappedLayer(level_->parallax, snap_.parX, levelNo_ == kTempleOfTikal ? snap_.parY : snap_.parY & ~7,
                                       snap_.parX & 7, snap_.scrollX & 7, nullptr);
        }
    } else {
        renderer_.clearView(snap_.scrollX);
    }
    // FUN_004012d8: the scrolling lines of level 23 (level 21 draws its own later).
    if (cut && levelNo_ != kIntro) renderScrollText();
    // FUN_00436fb4: the main map (level 21 draws it last); without one (DAT_00450fcc) the view is cleared.
    if (levelNo_ != kIntro) {
        // level 22's second room is the map at DAT_004504f0 (room2_extra)
        const ph::Background& bg = levelNo_ == kSimonRoom && snap_.reunion && level_->hasExtra ? level_->extra : level_->bg;
        if (!snap_.mainLayer || !level_->hasBg) renderer_.clearView(snap_.scrollX);
        else if (levelNo_ == kAtari2600Level) renderAtari2600();
        else if (levelNo_ == kMapScreen)                           // 0x43718c: the map wraps both ways, no priority list
            renderer_.drawWrappedLayer(bg, snap_.scrollX, snap_.scrollY, snap_.scrollX & 7, snap_.scrollX & 7, nullptr);
        else { renderer_.drawTileLayer(bg, view, deferred_); deferredMap = &bg; }
    }
    // FUN_00437ba0: on levels 6 and 18, but while the stop-watch runs, the lines below the water
    // (world y 0x1a0 / 0x3a0) wobble: each turned round by the next long of the table at 0x455090.
    if ((levelNo_ == kYaxchilanLagoon || levelNo_ == kJainaIslandFalls) && snap_.stopWatch == 0) {
        const int n = snap_.scrollY - (levelNo_ == kYaxchilanLagoon ? 0xc0 : 0x2c0);
        auto wave = [](int i) { int32_t w; std::memcpy(&w, kData_waterWave + size_t(i) * 4, 4); return w; };
        int idx = snap_.waveStart;
        for (int r = 0xe0 - n; n > 0 && r < 0xe0; ++r) {
            renderer_.rotateRow(r, wave(idx), snap_.scrollX);
            if (wave(++idx) == 0x8000) idx = 0;
        }
    }
    renderer_.setWaterClip(levelNo_ == kJainaIslandFalls || levelNo_ == kYaxchilanLagoon ? levelNo_ : 0);
    // FUN_004426e0: the title's logo, or the sprites behind, back to front (from the list's end).
    if (levelNo_ == kTitleScreen) renderTitle();
    else
        for (auto it = snap_.displayList.rbegin(); it != snap_.displayList.rend(); ++it)
            if (!(it->flags & engine::kSprFront)) renderer_.drawSprite(*it, view);
    if (levelNo_ == kTitleScreen) renderTitleRunner();            // FUN_00442bf0
    if (cut) renderActors(false);                                  // FUN_00401346
    if (deferredMap) renderer_.drawDeferredTiles(*deferredMap, deferred_);   // FUN_00436b00
    if (cut && levelNo_ == kIntro) renderScrollText();             // FUN_004012d8 on level 21
    for (auto it = snap_.displayList.rbegin(); it != snap_.displayList.rend(); ++it)   // FUN_00442dc0: the front sprites
        if (it->flags & engine::kSprFront) renderer_.drawSprite(*it, view);
    if (cut) renderActors(true);                                   // FUN_004013f8
    // FUN_00436fb4 last on level 21 (0x43702c): its vines in front of everything, at the parallax
    // camera, wrapping (0x4371a5).
    if (levelNo_ == kIntro && level_->hasBg)
        renderer_.drawWrappedLayer(level_->bg, snap_.parX, snap_.parY, snap_.parX & 7, snap_.scrollX & 7, nullptr);
    renderer_.setWaterClip(0);                                     // DAT_0046d040 = 1 for the HUD
    renderHud();                                                   // FUN_00442fa0
    renderPaused();
    renderFrameRate();
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
    renderPaused();
    renderFrameRate();
}

void Game::renderPaused() {
    // FUN_00443c90: "PAUSED" with TextOutA at (0x82, 0x60) of the buffer, blinking on the timer's
    // count (here in the 8x8 font, at the view's place: the fine scroll left, 9 lines up).
    if (!paused_ || (timerTicks_ & 0x60) == 0) return;
    if (hostText_) hostTexts_.push_back(HostText{0x82 - (snap_.scrollX & 7), 0x60 - 9, "PAUSED"});
    else renderer_.drawText(assets_->font, 0x82 - (snap_.scrollX & 7), 0x60 - 9, "PAUSED", snap_.scrollX);
}

void Game::completeLevel() {
    // 0x430912: the level is kept for the map screen (DAT_0046b1ac), then "*4"
    tourLevel_ = levelNo_;
    command("*4");
}

uint32_t Game::random() {
    // FUN_0043387e.  s0 = DAT_00466772, s1 = DAT_00466776, s2 = DAT_0046677a (longs, little-endian),
    // in the logic's memory.
    uint8_t* const rng = logicMem_.at(0x466772);
    auto L = [&](int o) { uint32_t v; std::memcpy(&v, rng + o, 4); return v; };
    auto setL = [&](int o, uint32_t v) { std::memcpy(rng + o, &v, 4); };
    auto W = [&](int o) { uint16_t v; std::memcpy(&v, rng + o, 2); return v; };
    auto setW = [&](int o, uint16_t v) { std::memcpy(rng + o, &v, 2); };
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

void Game::setRandomSeed(uint32_t s0, uint32_t s1) {
    logicMem_.l(0x466772) = int32_t(s0);
    logicMem_.l(0x466776) = int32_t(s1);
}

void Game::loseLife() {
    // FUN_00430eb6's tail: a life gone (a byte), "*3" starts the level again at the checkpoint;
    // with none left "*0" goes to the continue screen while there are continues, else "*2" ends
    // the game.
    stats_.lives = (stats_.lives - 1) & 0xff;
    if (stats_.lives != 0) { command("*3"); return; }
    command(stats_.continues != 0 ? "*0" : "*2");
}

void Game::drawTextWorld(int x, int y, const char* text) {
    renderer_.drawText(assets_->font, x, y, text, snap_.scrollX);
}

} // namespace game
