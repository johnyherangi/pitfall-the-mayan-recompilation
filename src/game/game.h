// Game driver: owns the loaded assets, the per-frame engine state and the
// level flow.  Mirrors the Windows engine half of PITFALL.EXE:
//
//   WinMain  FUN_0043c210   -> Game::Game / init
//   tick     FUN_00443370   -> Game::tick     (a logic step every 17 ms, timer driven)
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

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <deque>
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

enum class Mode { Title, Attract, Playing, LevelComplete, GameOver };

// Shared per-frame state (the globals the Genesis logic and the renderer exchange).
struct FrameState {
    int scrollX = 0, scrollY = 0;        // DAT_00450504 / DAT_00450508 (camera, world px)
    int maxScrollX = 0, maxScrollY = 0;  // DAT_00450fb8 / DAT_00450fbc
    int parX = 0, parY = 0;              // DAT_00450f94 / DAT_00450f98 (parallax camera)
    int bob = 0;                         // DAT_004517ec (vertical shake offset)
    int vaultCeiling = 0;                // [DAT_00450510] + 0x1f4 (big-endian word): the vaults' ceiling, their parallax layer's y
    std::vector<int8_t> lineScroll;      // DAT_004513e0 .. (per-line parallax offsets, level 0)
    // DAT_004513e0 .. on levels 1, 6, 18: the waterfalls' parallax, its first map column (the
    // pointer the original keeps there, as a tile column), then each 16-px column's height in it
    // (0 .. 511) or -1 (cleared), -1 ending them.
    std::vector<int32_t> waterColumns;
    int waveStart = 0;                   // DAT_004513d0 (DAT_004517f0): the first line's place in the water's wobble
    int stopWatch = 0;                   // DAT_004517e8 (DAT_0046652f as the logic's end left it): the water stands still
    bool rideLoop = false;               // level 7: the ride's loop runs (DAT_00465c56, not DAT_004661c1): no parallax
    std::vector<engine::SpriteInstance> displayList;   // 0x46aa94 (20-byte entries)
    uint32_t joypad = 0, joypadPrev = 0; // DAT_004517f8 / edge detection
    int atariScreen = 0;                 // DAT_00465ba8: the 2600 level's screen (FUN_00436fb4 reads it as it draws)
    int hudSlide = 0;                    // DAT_00451940: DAT_004667a8 as the logic left it (0x4364f9), the HUD's slide-in
    // The numbers the HUD shows as the logic's end left them (0x43641f; an exit skips it):
    // DAT_00451914 (the score, at most 999999), DAT_00451804 (the lives less one), DAT_00451a28
    // (the treasures), DAT_00451938 (the weapon), DAT_00451908 (its ammunition, at most 99).
    int hudScore = 0, hudLives = 0, hudTreasures = 0, hudWeapon = 0, hudAmmo = 0;
    int camX = 0, camY = 0;              // DAT_004658b4 / b6: the logic's camera (scrollY has the shake added)
    int frame = 0;                       // DAT_0045190c (ticks since level start)
    int globalFrame = 0;                 // DAT_0046fb74
    // The layers the level has (FUN_004460e0); the cutscenes' 0x40160e turns both off.
    bool mainLayer = true;               // DAT_00450fcc: FUN_00436fb4 draws the map, else clears the view
    bool parallaxLayer = true;           // DAT_00450514: FUN_00436534 draws the parallax map
    // The cutscenes (levels 21 .. 23, game/cutscene.cpp): their ten actors of 0x40 bytes at
    // 0x45623f, which FUN_00443c90 copies to 0x4564bf for the frame while DAT_0046fb90 is set.
    bool cutscene = false;               // DAT_0046fb90
    std::array<uint8_t, 0x280> cutActors{};
    bool reunion = false;                // DAT_00456049: level 22's second room (its map at DAT_004504f0)
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
    int damage = 0;       // DAT_00465b19 mirrored for the HUD (FUN_00435c4e)
    bool harryDead = false;   // DAT_00465b72 == 0xff: the HUD's crocodile closes on the hidden life icon
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

    // FUN_004448c0 / FUN_00443370: one callback of the 17 ms timer.  `host` is the raw host input.
    void tick(const engine::HostInput& host);
    // FUN_00443c90: compose a frame into the framebuffer.
    void render();

    // Presentation.
    const engine::Framebuffer& framebuffer() const { return fb_; }
    const ph::Palette& palette() const { return palette_; }
    ph::Palette& palette() { return palette_; }
    int viewScrollX() const { return snap_.scrollX; }   // for the (scrollX & 7) blit offset
    bool paused() const { return paused_; }               // DAT_0046d050
    void togglePause();                                   // the Pause key (FUN_00444ee0)
    void setPaused(bool on);
    void setCdOptions(bool music, bool ambient);          // the options dialog's CD settings (0x4410a9)
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
    void playSound(int id);                                           // FUN_0043cc10(id, 0)
    bool soundPlaying(int id) const { return mixer_.playing(id); }
    void stopSound(int id) { mixer_.stop(id); }
    void playChannel(int ch, int id);                                 // FUN_0043cc10(id, ch)
    // DAT_00450500: the stop-watch turns the CD down to a quarter (1) and back (2) at the next frame.
    void setCdVolumeCmd(int c) { cdVolumeCmd_ = c; }
    bool channelBusy(int ch) const { return mixer_.channelBusy(ch); }                       // FUN_0043cbc0(ch)
    void completeLevel();                                             // 0x430912: the level is kept (DAT_0046b1ac), "*4"
    int tourLevel() const { return tourLevel_; }                      // DAT_0046b1ac: the level last completed
    void setNextLevel(int l) { nextLevel_ = l; }                      // DAT_0045050c: the level "*1" loads
    uint8_t bigGlyph(uint8_t ch, int& width);                         // FUN_00431b73: ltr_lg's frame for a character
    void loseLife();                                                  // FUN_00430eb6's tail: "*3", or a continue / game over
    bool exitPending() const { return !exitCmd_.empty(); }           // a command is set (DAT_0046b1a8)
    // The logic's engine exit with a command ("*0" .. "*9"); FUN_00443c90's switch takes it
    // after the tick, with its fades and waits (transitionActive meanwhile).
    void command(const char* cmd);
    bool transitionActive() const { return !stages_.empty(); }
    uint32_t& brightness() { return brightness_; }                   // DAT_0046b1b0: the logic's palette brightness (FUN_0043de70)
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
    // (FUN_00434815, the Genesis seeding, is unreachable on the PC: the state starts at zero.)
    void setRandomSeed(uint32_t s0, uint32_t s1);   // DAT_00466772 / DAT_00466776 (0x415c48: a boss dies)
    bool demoPlaying() const { return demoPos_ != 0x800; }   // DAT_00466500 (DAT_0046fb8c != 0x800)
    // DAT_00462694 as FUN_0043628c sets it every tick: the difficulty option (DAT_0046fb78),
    // normal while a demo plays.
    bool hardMode() const { return options_.difficulty != 0 && !demoPlaying(); }
    // DAT_004601d9 / DAT_004601d5: the logic's tick count (0x4363fd, twice at a level start) and
    // the tick of Harry's last landing sound; the engine never resets them.
    uint32_t& logicTicks() { return logicTicks_; }
    uint32_t& lastLandTick() { return lastLandTick_; }
    // 0x41ff20: the collected 1-up reaches the lives (type 0xaa): mi_life's 1-up animation.
    void lifeIconOneUp() { hudAnim_.oneUp = true; hudAnim_.oneUpStart = logicTicks_; }
    bool invincible() const { return invincible_; }   // DAT_0046d054: the typed cheat (0x439868) clears the damage each tick
    uint8_t& startKey() { return startKey_; }         // DAT_0046fb88: a key went down (WM_KEYDOWN, a held key repeats)
    // WM_CHAR (FUN_004388c0, 0x439868 ..): a typed character goes into a ring of 128
    // (DAT_004533a0 / DAT_00453384); when the last ones spell one of the codes at 0x4614c2 ..
    // (kept 0x2d higher, compared ignoring case) its cheat acts, with sound 0x24 (game/cheats.cpp).
    void typeChar(uint8_t c);
    // A typed code that shows one of the original's dialogs, paused: 0x7f ("iddqd", bitmap 0x80)
    // or 0x100 ("paidtoplaygames", bitmap 0x81); 0 when none is due.  The front end shows it.
    int takeCheatDialog() { const int d = cheatDialog_; cheatDialog_ = 0; return d; }
    // DAT_0046d38c: the menu's level items that are on (FUN_0043a320), a bit a level reached
    // outside the demos (FUN_004460e0) or a cheat; menuChanged() says the menu needs them again.
    uint32_t menuUnlocks() const { return menuUnlocks_; }
    void setMenuUnlocks(uint32_t m) { menuUnlocks_ = m; }
    bool menuChanged() { const bool c = menuChanged_; menuChanged_ = false; return c; }
    uint8_t hatman() const { return hatman_; }        // DAT_0046d058: the "hatman" cheat (0 .. 2)
    bool showPosition() const { return showPosition_; }   // DAT_0046e058: "beammeup", the camera's position
    // DAT_0046fb84: the attract loaded the intro (track 5, the credits after it); only the menu's
    // new game (0x9c64) clears it, so the end's credits scroll twice as fast after an attract.
    bool introAfterDemos() const { return introAfterDemos_; }
    // 0x456000 .. 0x4569df and 0x4699a4 .. 0x46a859: the cutscenes' scripts, texts and state
    // (game/cutscene.cpp), kept across levels as the original's data section.
    std::vector<uint8_t>& cutsceneData() { return cutData_; }

private:
    void computeParallax();            // FUN_00443370 per-level switch
    void tickAttract();                // title / demo sequencing (FUN_00443c90 tail)
    void composeFrame();               // FUN_00443c90's drawing
    void renderHud();                  // FUN_00442fa0
    void hudMeter(engine::SpriteInstance& croc, engine::SpriteInstance& life, int top);   // FUN_00435d45
    void titleFrame();                 // FUN_004426e0's title branch and FUN_00442bf0 before they draw
    void renderTitle();                // FUN_004426e0's title branch: the logo, its dust and stones
    void renderTitleRunner();          // FUN_00442bf0: Harry runs in, later the jaguar runs out
    void titleSetup();                 // FUN_00442410 on the title
    void titleTick();                  // FUN_00443370's tail: the runner's frames and the camera's pan
    void titleAmbient();               // FUN_00431a67 as the title calls it (the logic does not run there)
    void renderActors(bool front);     // FUN_00401346 (the actors behind) / FUN_004013f8 (texts, actors in front)
    void renderScrollText();           // FUN_004012d8: the scrolling lines (levels 21 and 23)
    void drawBigText(int y, const char* s);   // FUN_00431bfb: ltr_lg letters centred on x 0xa0, y of the screen
    void renderFrameRate();            // FUN_00443c90's frame rate / position text (DAT_0046e054 / DAT_0046e058)
    void renderPaused();               // FUN_00443c90's "PAUSED"
    void renderVault();                // FUN_00443c90's layer order on the Loltun vaults
    void renderAtari2600();            // FUN_00436fb4 at 0x437078: the 2600 level's screen
    void renderGameOver();             // "*2": 0x44426b
    void pollJoypad(const engine::HostInput& host);   // FUN_00444940
    void engineTick();                 // FUN_00443370 after the joypad
    void preTicks(int n);              // the engine ticks a level load / "*3" runs before the first frame
    static int preTickCount(int level);   // 0x46ffa0: 13 on levels 0, 2, 5, 8, 16

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
    int demoIndex_ = -1;               // DAT_0046fbbc (-1 in the data section: the first demo is demo 0)
    engine::HostInput lastHost_;       // the host input of the last tick (the pre-ticks poll it again)
    uint8_t bigSpace_ = 0;             // DAT_0046a878: which of ltr_lg's two spaces is next
    int tourLevel_ = 0;                // DAT_0046b1ac: the level last completed (the map screen's path)
    int nextLevel_ = 0;                // DAT_0045050c: the level "*1" loads (the map's next one, the one a continue goes back to)
    int demoPos_ = 0;                  // DAT_0046fb8c (0x800 == not in demo)
    uint8_t demoCountdown_ = 1;        // DAT_0046fb98
    uint32_t demoButtons_ = 0;         // DAT_0046fb94
    int cloudDrift_ = 0;               // DAT_00450f94 accumulator for drifting layers
    int ruinsCounter_ = 0;             // DAT_004517e0
    // The engine's water (FUN_00443370; zero in the BSS, never reset): the forest's shimmer
    // (DAT_0046fb48 / 4c), the six falling phases of the waterfalls (DAT_00450014 / 18 / 1c / 20 /
    // 24, DAT_004500e8), their wobble's place and countdown (DAT_004517f0, DAT_00450010).
    int leafCount_ = 0, leafPhase_ = 0;
    int32_t water_[6] = {};
    int waveIndex_ = 0, waterCount_ = 0;
    // The title (FUN_00442410 / FUN_00443370 / FUN_004426e0 / FUN_00442bf0): records of
    // {x, y, sprite, frame, flags} at 0x451810 (the logo), 0x451830 / 0x451850 (its dust),
    // 0x451870 .. 0x4518d0 (the four stones under it) and 0x451a10 (the runner, later the jaguar).
    struct TitleSprite { int32_t x = 0, y = 0; uint32_t slot = 0; uint32_t frame = 0; uint8_t flags = 0; };
    TitleSprite titleLogo_[7];
    TitleSprite titleRunner_;
    int titleScript_ = 0;              // DAT_00451984: the runner's frame script (0x46fa50), a dword index
    int32_t titleFrame_ = 0;           // DAT_0046fa80: the runner's frame, every 5 ticks
    int32_t titleFrameShown_ = 0;      // DAT_0046fa84: ... as the frame took it
    uint32_t titleSlot_ = 0;           // DAT_00451800: the runner's sprite (the script's -2 changes it)
    uint32_t titleSlotShown_ = 0;      // DAT_00451934
    int titleCount_ = 0;               // DAT_0046fa7c: ticks since the last frame (counted on every level)
    bool titleShine_ = false;          // DAT_0046fa3c: the stones' last frame is up
    bool titleSoundA_ = false;         // DAT_0046fa40: the logo's sound 0x25 (later the jaguar's 0x10) played
    bool titleSoundB_ = false;         // DAT_0046fa44: ... 0x29 (later 0x2a)
    int16_t titleAmbientWait_ = 0;     // DAT_00465b04, which FUN_00431a67 counts down
    uint32_t logicTicks_ = 0;          // DAT_004601d9
    uint32_t lastLandTick_ = 0;        // DAT_004601d5
    uint32_t timerTicks_ = 0;          // DAT_00450fa8: the 17 ms timer's callbacks (FUN_004448c0), paused or not
    bool invincible_ = false;          // DAT_0046d054 (the cheat codes are not ported yet)
    // FUN_00435d45's own state (engine globals; DAT_0046b178 and DAT_0046fa38 cleared by a new game).
    struct HudAnim {
        uint32_t prevMeter = 0;        // DAT_0046b178: the meter as last drawn
        bool healing = false;          // DAT_0046b180: the meter went down, mi_life's frames 0 .. 10
        uint32_t healStart = 0;        // DAT_0046b17c
        bool oneUp = false;            // DAT_0046fa38: a 1-up arrived, frames 0x15 / 0x16 ...
        bool oneUpGlow = false;        // DAT_0046b184: ... then 0xc .. 0x14
        uint32_t oneUpStart = 0;       // DAT_00451a24
    } hudAnim_;
    // The command switch's blocking parts, run a timer callback (17 ms) at a time while the engine
    // stands still: waiting for the sounds, the fade out (FUN_0043e470), the waits, the game-over
    // screen, the level load or "*3", and the load's fade in (FUN_0043e810).
    static constexpr int kTickMs = 17;           // timeSetEvent(17, ...)
    static constexpr int kFadeStepMs = 30;       // the fades' busy waits
    static constexpr int kFadeOutMs = 19 * 30;
    static constexpr int kFadeInMs = 30 + 15 * 30;
    static constexpr int kCdFadeMs = 20 * 100;   // FUN_00442190(2.0, 20): the CD's volume down in 20 steps
    struct Stage {
        enum Kind { WaitSounds, FadeOut, Wait, GameOver, NewGame, CdFade, Load, Restart, FadeIn } kind;
        int ms = 0;                    // the stage's length (fades, waits)
        int level = 0;                 // Load
        bool cdFaded = false;          // Load: FUN_00442190 has run
    };
    void beginCommand();               // the switch on DAT_0046b1a8[1]
    void loadLevel(int level);         // FUN_004460e0 as the switch calls it (with the CD's fade)
    void enterStage(const Stage& st);
    void beginStage();
    void runStages(int ms);
    void showFade();                   // the shown colours of the fade in progress
    void endEffect();                  // the brightness effect's colours back
    void applyBrightness();            // FUN_0043de70
    std::deque<Stage> stages_;
    int stageMs_ = 0;                  // the time spent in stages_.front()
    std::string exitCmd_;              // DAT_0046b1a8
    bool drawPending_ = false;         // a frame to compose while the stages run (the load's first)
    uint8_t startKey_ = 0;             // DAT_0046fb88: a key went down (WM_KEYDOWN)
    int audioRem_ = 0;                 // (the game-time audio clock's remainder)
    bool paused_ = false;              // DAT_0046d050
    // CD audio (engine/music.h): the level's music and ambient tracks (DAT_0046e110 / DAT_0046e114),
    // the intro the attract loads playing track 5 (DAT_0046fb84), the stop-watch's volume command.
    engine::CdTracks cd_;
    int musicTrack_ = 0, ambientTrack_ = 0;
    bool introAfterDemos_ = false;
    int cdVolumeCmd_ = 0;
    void cdPlay(int track);            // FUN_00441b80
    void cdPlayTracks();
    void setCdVolume(int pct);         // FUN_00441c50
    // FUN_0043de70's effect: the logic's brightness (DAT_0046b1b0), as the last frame took it
    // (DAT_00452770), as last applied (DAT_00452768), the colours kept meanwhile (DAT_0046dc2c,
    // DAT_0045276c).
    uint32_t brightness_ = 0;
    int32_t shownBrightness_ = 0;
    int32_t appliedBrightness_ = 0;
    bool effectSaved_ = false;
    std::array<ph::Rgb, 256> effectBackup_{};
    std::vector<engine::DeferredTile> deferred_;
    std::vector<uint8_t> cutData_;
    // The cheats (game/cheats.cpp).
    uint8_t typed_[0x80] = {};         // DAT_004533a0: the last characters typed
    int typedPos_ = 0;                 // DAT_00453384
    int cheatDialog_ = 0;
    uint32_t menuUnlocks_ = 1;         // DAT_0046d38c (1 when the registry has nothing valid)
    bool menuChanged_ = true;
    uint8_t hatman_ = 0;               // DAT_0046d058
    bool showPosition_ = false;        // DAT_0046e058
    // The frame rate text: frames composed (DAT_00453390), the time of its last update
    // (DAT_00453388, ms) and the text (DAT_004500f0).
    uint32_t fpsFrames_ = 0;
    uint64_t fpsTime_ = 0;
    std::string fpsText_;
    bool quit_ = false;
};

std::vector<uint8_t> cutsceneInitialData();   // game/cutscene.cpp: the data section's bytes
const char* cutsceneText(uint32_t va);        // a cutscene's string by its address ("" elsewhere)

// Attract-mode demo scripts recorded in the original (data at 0x46fbc0..).
struct DemoScript {
    const uint8_t* pairs;   // (frames, joypad) pairs, terminated by 0xff,0xff
    int level;              // level the demo plays on
};
const DemoScript& demoScript(int index);   // 0..2

} // namespace game
