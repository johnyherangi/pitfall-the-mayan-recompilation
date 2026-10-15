// Game driver: owns the loaded assets, the per-frame engine state and the
// level flow.  Mirrors the Windows engine half of PITFALL.EXE:
//
//   WinMain  FUN_0043c210   -> Game::Game / init
//   tick     FUN_00443370   -> Game::tick     (60 Hz logic step, timer driven)
//   render   FUN_00443c90   -> Game::render   (compose one frame)
//   level    FUN_004460e0   -> Game::startLevel
//
// The per-level gameplay (Harry's state machine, enemies "aliens", triggers)
// lives in the Genesis-derived half of the binary and is exposed here through
// the LevelLogic interface so it can be ported function by function.
#pragma once
#include "engine/audio.h"
#include "engine/framebuffer.h"
#include "engine/input.h"
#include "engine/options.h"
#include "engine/renderer.h"
#include "ph/level.h"
#include "ph/palette.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <vector>

namespace game {

// Level numbers as used by DAT_00451a2c and the LEVELxx.PH file names.
enum Level : int {
    kCeibaJungle = 0, kXibalbaFalls = 1, kTazamulMines = 2, kLostCityOfCopan = 3,
    kCopanTemple = 4, kLakamulRainForest = 5, kYaxchilanLagoon = 6, kBalankancheMine = 7,
    kTikalRuins = 8, kTempleOfTikal = 9, kWarriorSpirit = 10,
    kLoltunVault2 = 11, kLoltunVault3 = 12, kLoltunVault1 = 13,
    kContinueScreen = 14, kTitleScreen = 15,
    kPalenqueRuins = 16, kTombOfPalenque = 17, kJainaIslandFalls = 18,
    kMapScreen = 19, kAtari2600Level = 20, kIntro = 21, kSimonRoom = 22, kCredits = 23, kEnding = 24,
};

enum class Mode { Title, Attract, Playing, Paused, LevelComplete, GameOver };

// Shared per-frame state (the globals the Genesis logic and the renderer exchange).
struct FrameState {
    int scrollX = 0, scrollY = 0;        // DAT_00450504 / DAT_00450508 (camera, world px)
    int maxScrollX = 0, maxScrollY = 0;  // DAT_00450fb8 / DAT_00450fbc
    int parX = 0, parY = 0;              // DAT_00450f94 / DAT_00450f98 (parallax camera)
    int bob = 0;                         // DAT_004517ec (vertical shake offset)
    std::vector<int8_t> lineScroll;      // DAT_004513e0 .. (per-line parallax offsets)
    std::vector<engine::SpriteInstance> displayList;   // 0x46aa94 (20-byte entries)
    uint32_t joypad = 0, joypadPrev = 0; // DAT_004517f8 / edge detection
    int frame = 0;                       // DAT_0045190c (ticks since level start)
    int globalFrame = 0;                 // DAT_0046fb74
};

struct PlayerStats {
    int score = 0;        // DAT_0046653b
    int lives = 3;        // DAT_00466527 (3, 4 on the easy setting; HUD "life x N-1"; mi_1up adds one)
    int treasures = 0;    // DAT_00466526: 50 treasures = an extra continue (HUD "coin x NN")
    int continues = 3;    // DAT_00465ae3 (max 9): used up when the lives run out
    int weapon = 0;       // DAT_0046650b: 0 sling stones (pouch), 1 boomerang, 2 stone
    int ammoCount[3] = {0, 0, 0};  // DAT_00466528 stones, DAT_00466529 boomerang, DAT_0046652a stone
    int letters = 0;      // DAT_00465896: P-I-T-F-A-L-L letter bits (mi_ptfal frame)
    bool secret1up = false;   // DAT_00466504: this level's hidden 1-up has been revealed (cleared on a new game and at the level exit)
    int difficulty = 0;
    int damage = 0;       // DAT_00465b19 mirrored for the HUD (FUN_00435c4e)
    bool harryDead = false;
    int& ammo() { return ammoCount[weapon < 0 || weapon > 2 ? 0 : weapon]; }
    int ammo() const { return ammoCount[weapon < 0 || weapon > 2 ? 0 : weapon]; }
};

class Game;

// Hook for the per-level gameplay code.  The default implementation
// (ExplorerLogic, game.cpp) lets you scroll any level and animate Harry; the
// original behaviour is to be ported from the decompiled Genesis code
// (FUN_0043628c dispatcher and callees).
class LevelLogic {
public:
    virtual ~LevelLogic() = default;
    virtual void enter(Game& g) = 0;
    virtual void tick(Game& g) = 0;
};

class Game {
public:
    Game(std::string dataDir, engine::Options& options);

    // FUN_004460e0 + FUN_00447190: load a level and reset per-level state.
    void startLevel(int level);
    void setLogic(std::unique_ptr<LevelLogic> logic);

    // FUN_00443370: one logic step at 60 Hz.  `host` is the raw host input.
    void tick(const engine::HostInput& host);
    // FUN_00443c90: compose a frame into the framebuffer.
    void render();

    // Presentation.
    const engine::Framebuffer& framebuffer() const { return fb_; }
    const ph::Palette& palette() const { return palette_; }
    ph::Palette& palette() { return palette_; }
    int viewScrollX() const { return snap_.scrollX; }   // for the (scrollX & 7) blit offset
    bool paused() const { return mode_ == Mode::Paused; }
    void togglePause();
    engine::Mixer& mixer() { return mixer_; }
    bool quitRequested() const { return quit_; }

    // Accessors for level logic.
    FrameState& state() { return fs_; }
    PlayerStats& stats() { return stats_; }
    const ph::LevelData& level() const { return *level_; }
    const ph::AssetBank& assets() const { return *assets_; }
    int levelNumber() const { return levelNo_; }
    Mode mode() const { return mode_; }
    void setMode(Mode m) { mode_ = m; }
    engine::Options& options() { return options_; }
    const ph::Sprite* findSprite(const std::string& name) const;   // level first, then INIT.PH
    void playSound(int id) { mixer_.play(id, options_.wavVolume); }   // FUN_0043cc10
    bool soundPlaying(int id) const { return mixer_.playing(id); }
    void stopSound(int id) { mixer_.stop(id); }
    void requestLevel(int level) { pendingLevel_ = level; }           // "*" command strings
    void completeLevel();                                             // "*4": next level in the tour
    void loseLife();                                                  // death: restart or game over
    void drawTextWorld(int x, int y, const char* text);               // FUN_00442600
    // .als entries that stay gone after a death (FUN_00430a69 keeps the 0x465a40 list
    // of permanently killed enemies); cleared when another level starts.
    std::vector<std::pair<int, int>>& permanentlyGone() { return gone_; }
    // Checkpoint set by the fx_mls markers (DAT_0046679e..a4): where Harry respawns after a death.
    struct Checkpoint { bool valid = false; int x = 0, y = 0; bool facingLeft = false; int markerX = 0; };
    Checkpoint& checkpoint() { return checkpoint_; }
    // Level-script flags DAT_00466510..2f (levers pulled, crates blown): kept across deaths,
    // cleared with the level (FUN_00430eb6).  Index = address - 0x466510.
    uint8_t* levelFlags() { return levelFlags_; }

private:
    void computeParallax();            // FUN_00443370 per-level switch
    void tickAttract();                // title / demo sequencing (FUN_00443c90 tail)
    void renderHud();                  // FUN_00442fa0
    void renderTitle();                // FUN_004426e0 title animation

    uint8_t levelFlags_[32] = {};
    std::string dataDir_;
    engine::Options& options_;
    ph::Palette palette_;
    std::unique_ptr<ph::AssetBank> assets_;
    std::unique_ptr<ph::LevelData> level_;
    std::unique_ptr<LevelLogic> logic_;
    bool autoLogic_ = true;            // pick PlayLogic / ExplorerLogic per level
    engine::Framebuffer fb_;
    engine::Renderer renderer_{fb_};
    engine::Input input_;
    engine::Mixer mixer_;
    FrameState fs_;
    FrameState snap_;                  // copy taken under the critical section in FUN_00443c90
    PlayerStats stats_;
    std::vector<std::pair<int, int>> gone_;
    Checkpoint checkpoint_;
    int goneLevel_ = -1;
    Mode mode_ = Mode::Title;
    int levelNo_ = kTitleScreen;
    int pendingLevel_ = -1;
    int demoIndex_ = 0;                // DAT_0046fbbc
    int demoPos_ = 0;                  // DAT_0046fb8c (0x800 == not in demo)
    int demoCountdown_ = 1;            // DAT_0046fb98
    uint32_t demoButtons_ = 0;         // DAT_0046fb94
    int cloudDrift_ = 0;               // DAT_00450f94 accumulator for drifting layers
    int ruinsCounter_ = 0;             // DAT_004517e0
    int titlePan_ = 0;                 // DAT_00451a10
    std::vector<engine::DeferredTile> deferred_;
    bool quit_ = false;
};

// Attract-mode demo scripts recorded in the original (data at 0x46fbc0..).
struct DemoScript {
    const uint8_t* pairs;   // (frames, joypad) pairs, terminated by 0xff,0xff
    int level;              // level the demo plays on
};
const DemoScript& demoScript(int index);   // 0..2

} // namespace game
