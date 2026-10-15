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
    int vaultCeiling = 0;                // [DAT_00450510] + 0x1f4 (big-endian word): the vaults' ceiling, their parallax layer's y
    std::vector<int8_t> lineScroll;      // DAT_004513e0 .. (per-line parallax offsets)
    std::vector<engine::SpriteInstance> displayList;   // 0x46aa94 (20-byte entries)
    uint32_t joypad = 0, joypadPrev = 0; // DAT_004517f8 / edge detection
    int atariScreen = 0;                 // DAT_00465ba8: the 2600 level's screen (FUN_00436fb4 reads it as it draws)
    int frame = 0;                       // DAT_0045190c (ticks since level start)
    int globalFrame = 0;                 // DAT_0046fb74
};

struct PlayerStats {
    int score = 0;        // DAT_0046653b
    int lives = 4;        // DAT_00466527: 4 for a new game (FUN_004324c6), 4 / 3 (hard) after a continue; HUD "life x N-1"; mi_1up adds one
    int treasures = 0;    // DAT_00466526: 50 treasures = an extra continue (HUD "coin x NN")
    int continues = 3;    // DAT_00465ae3 (max 9): used up when the lives run out
    int weapon = 0;       // DAT_0046650b: 0 sling stones (pouch), 1 boomerang, 2 stone
    int ammoCount[3] = {0x14, 2, 1};   // DAT_00466528 stones, DAT_00466529 boomerangs, DAT_0046652a big stones (FUN_004324c6)
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
    // "*3" (FUN_004360e9): the level logic starts again without the level being loaded again.
    virtual void restart(Game& g) { enter(g); }
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
    void playChannel(int ch, int id) { mixer_.playChannel(ch, id, options_.wavVolume); }   // FUN_0043cc10(id, ch)
    bool channelBusy(int ch) const { return mixer_.channelBusy(ch); }                       // FUN_0043cbc0(ch)
    void requestLevel(int level) { pendingLevel_ = level; }           // "*" command strings
    void completeLevel();                                             // "*4": next level in the tour
    void loseLife();                                                  // FUN_00430eb6's tail: "*3", or a continue / game over
    void restartLevel() { pendingRestart_ = true; }                   // "*3" (FUN_004360e9): the logic starts again in place
    // The transitions of the vortex warps, "*50" .. "*53", "*6" and "*7" (the WinMain command
    // switch at 0x4445ef / 0x444661 / 0x4446bd).
    void command(const char* cmd);
    void newGame();                                                   // FUN_004324c6 (and the title's set-up)
    void drawTextWorld(int x, int y, const char* text);               // FUN_00442600
    // What a vortex warp carries from one level to another (FUN_004288ec / 0x4288a2), and the
    // engine's transition flags.  The game logic's own memory starts afresh with each level.
    struct Warp {
        std::vector<uint8_t> als;      // DAT_004601dd: the level's .als working copy (4 KB)
        uint8_t classes[256] = {};     // DAT_004611dd: its runtime trigger classes (DAT_004658c0)
        int level = 0;                 // DAT_00465af3: the level left
        int16_t x = 0, y = 0;          // DAT_00465af7 (Harry's x + 4) / DAT_00465af9
        uint8_t facing = 0;            // DAT_00465afd
        int16_t cpX = 0, cpY = 0, cpFacing = 0;   // DAT_0046a860 / 62 / 66: the checkpoint
        int16_t used[3] = {};          // DAT_0046a884 / 86 / 88: the x of the vault vortices taken (they stay shut)
        int returnLevel = 1;           // DAT_00450fc8: the level "*7" goes back to (1: none; 0xb after "*50")
        bool fromVault = false;        // DAT_004504cc: the next level start puts the saved level back
        bool fromAtari = false;        // DAT_004601c4: ... coming out of the 2600 level (Harry at 0x6e5, 0x587)
        bool cheat = false;            // DAT_0046d05c: "*50" leads to level 24 instead of the 2600 level
    };
    Warp& warp() { return warp_; }
    // Level-script flags DAT_00466510..25 (levers pulled, crates blown).  Index = address -
    // 0x466510.  A level start clears 0x00..0x13, a lost life 0x00..0x11 (FUN_00430eb6), a new
    // game all of them (0x14 / 0x15, the crates of level 2, last the whole game).
    uint8_t* levelFlags() { return levelFlags_; }
    // FUN_0043387e: the game logic's random generator (state DAT_00466772..7d, kept across
    // levels).  While a demo plays (DAT_00466500) it advances but always returns 1.
    uint32_t random();
    void seedRandom();                 // FUN_00434815 (new game): the fixed seeds
    void setRandomSeed(uint32_t s0, uint32_t s1);   // DAT_00466772 / DAT_00466776 (0x415c48: a boss dies)
    bool demoPlaying() const { return demoPos_ != 0x800; }   // DAT_00466500 (DAT_0046fb8c != 0x800)

private:
    void computeParallax();            // FUN_00443370 per-level switch
    void tickAttract();                // title / demo sequencing (FUN_00443c90 tail)
    void renderHud();                  // FUN_00442fa0
    void renderTitle();                // FUN_004426e0 title animation
    void renderVault();                // FUN_00443c90's layer order on the Loltun vaults
    void renderAtari2600();            // FUN_00436fb4 at 0x437078: the 2600 level's screen

    uint8_t levelFlags_[32] = {};
    uint8_t rng_[12] = {};             // DAT_00466772: three longs at +0 / +4 / +8 (+6 overlaps them)
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
    Warp warp_;
    Mode mode_ = Mode::Title;
    int levelNo_ = kTitleScreen;
    int pendingLevel_ = -1;
    bool pendingRestart_ = false;
    int demoIndex_ = -1;               // DAT_0046fbbc (-1 in the data section: the first demo is demo 0)
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
