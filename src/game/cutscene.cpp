// Levels 21 .. 23: the intro (DEEP IN THE MAYAN JUNGLE ..., Harry Jr., the 1-up and the shadow),
// the reunion with Harry Sr. after the Warrior Spirit, and the end's words and credits.
//
// The scenes are the PC build's own code (0x401000 .. 0x401dff), which FUN_00435fc0 /
// FUN_004360e9 (with the script at 0) and FUN_0043628c (after FUN_004263f3) run in place of the
// usual level code: FUN_00401071, FUN_00401176 and FUN_00401215, one a level.  Each starts its
// scene when DAT_00456000, the script's cursor, is 0, lets a key (FUN_00401000) jump to the
// script's last opcode, then runs one opcode (a pointer to its code, then its arguments; some go
// on into the next in the same tick, some run again next tick) and the routines of the ten
// actors at 0x45623f:
//   +0 x  +4 y (16.16; the texts' are whole)  +8 the sprite slot  +0xc the frame  +0x10 the flags
//   +0x14 the frame script's cursor  +0x18 its start  +0x1c the routine  +0x20 a value (the
//   hand's camera)  +0x24 the text  +0x28 the frame script's speed  +0x29 its count
// FUN_00443c90 copies them for the frame (0x4564bf) while DAT_0046fb90 is set and draws them in
// passes of their own (Game::renderActors, Game::renderScrollText).
//
// The data the code works on (0x456000 .. 0x4569df: the scripts, the texts, the frame scripts,
// the flags and the actors; 0x4699a4 .. 0x46a859: the credits, each line followed by its NUL and
// the delay before the next) is kept in the Game across levels, as the original's data section:
// a scene left during a wait leaves its count (DAT_00456004) to the next one.
#include "game/play_logic.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace game {

namespace {
constexpr uint32_t kCutBase = 0x456000, kCutSize = 0x9e0;
constexpr uint32_t kCreditsBase = 0x4699a4, kCreditsSize = 0xeb6;
constexpr uint32_t kActors = 0x45623f, kActorsEnd = 0x4564bf;
// Sprite slots.
constexpr uint32_t kHyirun = 0x47017c, kHyilooka = 0x470134, kHyiready = 0x470164, kHyihndad = 0x47011c,
    kHySrG = 0x470460, kMi1up = 0x470248, kMiPtfal = 0x470270, kFxSprk2 = 0x470080, kFxishado = 0x470004;
// The routines the code compares with.
constexpr uint32_t kRtText = 0x4014e4, kRtLine = 0x40157f, kRtExit = 0x4015f4;
}

std::vector<uint8_t> cutsceneInitialData() {
    std::vector<uint8_t> d(kCutSize + kCreditsSize);
    std::memcpy(d.data(), kData_cutscene, kCutSize);
    std::memcpy(d.data() + kCutSize, kData_credits, kCreditsSize);
    return d;
}

const char* cutsceneText(uint32_t va) {
    // The strings never change: the extracted data is read directly.
    if (va >= kCutBase && va < kCutBase + kCutSize) return reinterpret_cast<const char*>(kData_cutscene + (va - kCutBase));
    if (va >= kCreditsBase && va < kCreditsBase + kCreditsSize) return reinterpret_cast<const char*>(kData_credits + (va - kCreditsBase));
    return "";
}

uint8_t& PlayLogic::cb(uint32_t va) {
    std::vector<uint8_t>& d = game_->cutsceneData();
    if (va >= kCutBase && va < kCutBase + kCutSize) return d[va - kCutBase];
    if (va >= kCreditsBase && va < kCreditsBase + kCreditsSize) return d[kCutSize + (va - kCreditsBase)];
    static uint8_t junk;
    junk = 0;
    std::fprintf(stderr, "cutscene: access to %#x\n", va);
    return junk;
}

uint32_t PlayLogic::cl(uint32_t va) { return uint32_t(cb(va)) | uint32_t(cb(va + 1)) << 8 | uint32_t(cb(va + 2)) << 16 | uint32_t(cb(va + 3)) << 24; }
void PlayLogic::setCl(uint32_t va, uint32_t v) { for (int i = 0; i < 4; ++i) cb(va + uint32_t(i)) = uint8_t(v >> (8 * i)); }
uint16_t PlayLogic::cw(uint32_t va) { return uint16_t(cb(va) | cb(va + 1) << 8); }
void PlayLogic::setCw(uint32_t va, uint16_t v) { cb(va) = uint8_t(v); cb(va + 1) = uint8_t(v >> 8); }

uint32_t PlayLogic::cutClear(uint32_t i) {
    // 0x4015b6: the actor cleared (its sixteen longs), its address in edi
    const uint32_t a = kActors + (i << 6);
    for (uint32_t k = 0; k < 0x40; ++k) cb(a + k) = 0;
    return a;
}

void PlayLogic::cutClearAll() {
    for (uint32_t a = kActors; a < kActorsEnd; ++a) cb(a) = 0;    // rep stos, 0xa0 longs
}

uint32_t PlayLogic::cutLen(uint32_t va) {
    uint32_t n = 0;
    while (n < 0x100 && cb(va + n) != 0) ++n;
    return n;
}

// ---------------------------------------------------------------- the entry points

void PlayLogic::cutFrame(Game& g, int entry) {
    // FUN_0043628c (entry 0): FUN_004263f3, then the level's routine; FUN_004360e9 (1) and
    // FUN_00435fc0 (2, which reads the joypad first on levels 22 and 23) start the scene again
    // (DAT_00456000 = 0).  0x4363bd ends them (engineExit skips it).
    game_ = &g;
    level_ = g.levelNumber();
    cutExited_ = false;
    if (entry == 0 || (entry == 2 && level_ != kIntro)) readPad();
    if (entry != 0) setCl(0x456000, 0);
    cutTick();
    FrameState& fs = g.state();
    for (size_t i = 0; i < fs.cutActors.size(); ++i) fs.cutActors[i] = cb(kActors + uint32_t(i));
    fs.reunion = cb(0x456049) != 0;
    if (!cutExited_) epilogue(g);
}

bool PlayLogic::cutSkip() {
    // FUN_00401000 (ZF clear: skip): for the first 30 ticks of a scene (and after its key, 0x401759)
    // no key counts, and those held then are forgotten; nor while the brightness is off 0 (a
    // fade).  Then a button or any key (DAT_0046fb88) skips.
    if (mapKeysOff_ != 0) {
        if (--mapKeysOff_ == 0) {
            h_.b(0x466761) = 0; h_.b(0x466762) = 0; h_.b(0x466763) = 0; h_.b(0x466764) = 0;
            game_->startKey() = 0;
        }
        return false;
    }
    if (game_->brightness() != 0) return false;
    return h_.b(0x466761) || h_.b(0x466762) || h_.b(0x466763) || h_.b(0x466764) || game_->startKey() != 0;
}

void PlayLogic::cutStart(uint32_t script) {
    // What the three set-ups share: the keys' wait, DAT_0046fb90 (the actors are drawn), no actor,
    // the camera at 0 and no shake.
    mapKeysOff_ = 0x1e;                                       // DAT_0046a88a
    game_->state().cutscene = true;                           // DAT_0046fb90
    cutClearAll();
    h_.w(0x4658b4) = 0;
    h_.w(0x4658b6) = 0;
    h_.w(0x465b02) = 0;
    setCl(0x456000, script);
}

void PlayLogic::cutTick() {
    // FUN_00401071 / FUN_00401176 / FUN_00401215
    const bool afterDemos = game_->introAfterDemos();         // DAT_0046fb84
    uint32_t skipTo = 0;
    bool canSkip = true;
    if (level_ == kIntro) {
        if (cl(0x456000) == 0) {
            cb(0x456048) = 0;                                 // no jungle sounds yet
            cb(0x46a844) = 0xff;                              // the credits after the demos end before "[TO BE CONTINUED]"
            setCl(0x45604a, 0x4699b2);                        // from "[PITFALL]"
            setCl(0x45604e, 0);
            cutStart(afterDemos ? 0x45688b : 0x456753);
        }
        skipTo = afterDemos ? 0x4568b7 : 0x456883;            // the exit opcode
    } else if (level_ == kSimonRoom) {
        if (cl(0x456000) == 0) {
            cb(0x456049) = 0;                                 // the first room
            cutStart(0x4568bf);
        }
        skipTo = 0x456977;
    } else {
        if (cl(0x456000) == 0) {
            cb(0x456052) = 0;
            cb(0x46a844) = 0;                                 // the whole list
            setCl(0x45604a, 0x4699a4);                        // from "[THE END]"
            setCl(0x45604e, 0);
            cutStart(0x45697f);
            h_.w(0x4658b6) = 0x20;
        }
        skipTo = 0x4569d3;
        canSkip = cb(0x456052) == 0;                          // not while 0x4016fd waits for its key
    }
    if (cl(0x456000) < skipTo && canSkip && cutSkip()) setCl(0x456000, skipTo);
    cutScript();
    if (cutExited_) return;
    for (uint32_t a = kActors; a != kActorsEnd; a += 0x40) {
        if (cl(a + 0x1c) != 0) cutRoutine(a);
        if (cutExited_) return;
    }
    if (level_ == kIntro && cb(0x456048) == 1) mapAmbient();  // FUN_00431a67
}

// ---------------------------------------------------------------- the script

void PlayLogic::cutScript() {
    // The main loop's `mov esi, [0x456000]; lods eax; call eax`, then the cursor kept: an opcode
    // reads its arguments with lods; one that ends with `lods eax; jmp eax` goes on into the next
    // in the same tick; one that backs esi up runs again next tick.
    FrameState& fs = game_->state();
    uint32_t esi = cl(0x456000);
    uint32_t saved = 0;                                       // 0x401776 pushes the cursor, 0x4017c0 pops it
    auto lods = [&] { const uint32_t v = cl(esi); esi += 4; return v; };
    auto actor = [](uint32_t i) { return kActors + (i << 6); };
    // The animated actors' set-ups: the sprite, the first frame, the flags, the frame script, the
    // routine and a speed of 6.
    auto animated = [&](uint32_t a, uint32_t slot, uint32_t frameAt, uint32_t flags, uint32_t script, uint32_t routine) {
        setCl(a + 8, slot);
        setCl(a + 0xc, cb(frameAt));
        setCl(a + 0x10, flags);
        setCl(a + 0x14, script);
        setCl(a + 0x18, script);
        setCl(a + 0x1c, routine);
        cb(a + 0x28) = 6;
    };
    uint32_t& bright = game_->brightness();                   // DAT_0046b1b0
    uint32_t op = lods();
    for (int guard = 0; guard < 0x40; ++guard) {
        bool chain = false;
        switch (op) {
        case 0x4014aa: {                                      // a text: actor, string, y (0x4d: 32 px right)
            const uint32_t a = cutClear(lods());
            const uint32_t s = lods();
            setCl(a + 0x24, s);
            uint32_t x = 0xa0 - (cutLen(s) * 8 >> 1);
            const uint32_t y = lods();
            if (y == 0x4d) x += 0x20;
            setCl(a, x);
            setCl(a + 4, y);
            setCl(a + 0x1c, kRtText);
            chain = true;
            break;
        }
        case 0x4014e5: {                                      // the scrolling lines from DAT_0045604a
            uint32_t t = cl(0x45604e);
            if (afterDemos()) t -= 0x8000;                    // (twice as fast after an attract)
            t -= 0x8000;
            setCl(0x45604e, t);
            if (int32_t(t) >= 0) { esi -= 4; break; }
            uint32_t a = kActors;
            while (a != kActorsEnd && cl(a + 0x1c) != 0) a += 0x40;
            if (a == kActorsEnd) { setCl(0x45604e, cl(0x45604e) + 1); esi -= 4; break; }   // no actor free
            for (uint32_t k = 0; k < 0x40; ++k) cb(a + k) = 0;
            const uint32_t line = cl(0x45604a);
            setCl(a + 0x24, line);
            const uint32_t n = cutLen(line);
            setCl(a, 0xa0 - (n * 8 >> 1));
            setCw(a + 4, 0);
            setCw(a + 6, 0xf0);                               // at y 240, 16.16
            setCl(a + 0x1c, kRtLine);
            const uint8_t delay = cb(line + n + 1);           // after the NUL: the half-pixels to the next line
            setCw(0x456050, delay);
            setCl(0x45604a, line + n + 2);
            if (delay != 0xff) esi -= 4;                      // 0xff: the last line
            break;
        }
        case 0x4015a5: cb(0x456048) = 1; break;              // the jungle's sounds (FUN_00431a67) from now on
        case 0x4015ad: cutClear(lods()); chain = true; break; // an actor off
        case 0x4015cc: {                                      // the exit: actor 9 leaves with "*d" when it runs
            const uint32_t a = cutClear(9);
            const uint32_t d = lods();
            cb(0x456008) = '*';
            cb(0x456009) = uint8_t(d + 0x30);
            cb(0x45600a) = 0;
            setCl(a + 0x1c, kRtExit);
            break;
        }
        case 0x40160e: fs.mainLayer = false; fs.parallaxLayer = false; break;   // DAT_00450fcc / DAT_00450514
        case 0x401623: bright = lods(); break;               // DAT_0046b1b0
        case 0x40162a:                                        // fade out: 8 down a tick, below -0x100
            esi -= 4;
            bright -= 8;
            if (bright < 0xffffff00u) esi += 4;
            break;
        case 0x401644:                                        // fade in: 8 up a tick, to 0
            if (bright == 0) break;
            esi -= 4;
            if (bright + 8u < bright) { esi += 4; bright = 0; }   // the carry
            else bright += 8;
            break;
        case 0x401667: {                                      // wait n + 1 ticks (DAT_00456004)
            const uint32_t n = lods();
            if (cl(0x456004) != 0) {
                setCl(0x456004, cl(0x456004) - 1);
                if (cl(0x456004) == 0) break;
                esi -= 8;
                break;
            }
            setCl(0x456004, n);
            esi -= 8;
            break;
        }
        case 0x401686: {                                      // wait for flag i (0x45622f + i)
            const uint32_t i = lods();
            if (cb(0x45622f + i) == 0) { esi -= 8; break; }
            chain = true;
            break;
        }
        case 0x401693:                                        // the second room of level 22, the camera at 0
            cb(0x456049) = 1;
            h_.w(0x4658b4) = 0;
            h_.w(0x4658b6) = 0;
            h_.w(0x465b02) = 0;
            break;
        case 0x4016b6: {                                      // Harry Sr.
            const uint32_t a = cutClear(lods());
            setCl(a, 0xa00000);
            setCl(a + 4, 0x8c0000);
            setCl(a + 8, kHySrG);
            setCl(a + 0xc, 0);
            setCl(a + 0x10, 1);
            setCl(a + 0x1c, 0x401af9);
            chain = true;
            break;
        }
        case 0x4016e9: setCl(actor(lods()) + 0xc, 1); chain = true; break;   // frame 1
        case 0x4016fd: {                                      // wait for a key (any key, or the joystick when it is on)
            cb(0x456052) = 1;
            const bool key = (game_->options().joystick && fs.joypad != 0) || game_->startKey() != 0;
            if (key) {
                h_.b(0x466761) = 0; h_.b(0x466762) = 0; h_.b(0x466763) = 0; h_.b(0x466764) = 0;
                game_->startKey() = 0;
                fs.joypad = 0;
                cb(0x456052) = 0;
                mapKeysOff_ = 0x1e;
            } else {
                esi -= 4;
            }
            break;
        }
        case 0x401767: cutClearAll(); break;
        case 0x401776:                                        // the end's words by the difficulty (DAT_0046fb78) ...
            saved = esi;
            esi = game_->options().difficulty == 1 ? 0x456163 : 0x456153;
            op = 0x4014aa;
            continue;
        case 0x40178f:                                        // ... and by the letters: all seven, or not
            esi = game_->stats().letters == 0x7f ? 0x456193 : 0x456173;
            op = 0x4014aa;
            continue;
        case 0x4017a2: esi = 0x456183; op = 0x4014aa; continue;
        case 0x4017b6: esi = 0x4561a3; op = 0x4014aa; continue;
        case 0x4017c0: {                                      // the letters found, P-I-T-F-A-L-L in actors 3 .. 9
            uint32_t bit = 1, x = 0x53;
            for (uint32_t i = 3; i != 0xa; ++i, x += 0x1a, bit += bit) {
                const uint32_t a = cutClear(i);
                if (!(uint32_t(game_->stats().letters) & bit & 0xff)) continue;
                setCw(a + 2, uint16_t(x));
                setCw(a + 6, 0x52);
                setCl(a + 8, kMiPtfal);
                setCl(a + 0xc, cb(0x4561b3 + i));
                setCl(a + 0x10, 1);
                setCl(a + 0x1c, 0x40181a);
            }
            esi = saved;
            break;
        }
        case 0x40181b: {                                      // Harry Jr. runs in from the left
            const uint32_t a = cutClear(lods());
            setCl(a, 0xffa60000);
            setCl(a + 4, 0xbe0000);
            animated(a, kHyirun, 0x4561bd, 1, 0x4561bd, 0x401a9e);
            cb(0x45622f) = 0;
            chain = true;
            break;
        }
        case 0x40186a:                                        // ... looks up
            animated(actor(lods()), kHyilooka, 0x4561e4, 1, 0x4561e4, 0x401b00);
            cb(0x45622f) = 0;
            chain = true;
            break;
        case 0x4018b0:                                        // ... stands (its first frame from the look's script)
            animated(actor(lods()), kHyiready, 0x4561e4, 1, 0x4561db, 0x401afa);
            chain = true;
            break;
        case 0x4018ef:                                        // ... runs off to the left
            animated(actor(lods()), kHyirun, 0x4561bd, 0x41, 0x4561bd, 0x401b19);
            cb(0x45622f) = 0;
            chain = true;
            break;
        case 0x401935:                                        // ... looks up again
            animated(actor(lods()), kHyilooka, 0x4561e4, 1, 0x4561e4, 0x401b00);
            chain = true;
            break;
        case 0x401974: {                                      // Harry Jr. runs into the room
            const uint32_t a = cutClear(lods());
            setCl(a, 0xffc40000);
            setCl(a + 4, 0xdc0000);
            animated(a, kHyirun, 0x4561bd, 1, 0x4561bd, 0x401b33);
            cb(0x45622f) = 0;
            chain = true;
            break;
        }
        case 0x4019c3:                                        // ... looks up, and keeps looking
            animated(actor(lods()), kHyilooka, 0x4561f8, 1, 0x4561f8, 0x401b00);
            cb(0x45622f) = 0;
            chain = true;
            break;
        case 0x401a09:                                        // ... stands, facing left (no script uses it)
            animated(actor(lods()), kHyiready, 0x4561db, 0x41, 0x4561db, 0x401afa);
            chain = true;
            break;
        case 0x401a48: {                                      // the hand with the photograph rises
            const uint32_t a = cutClear(lods());
            setCl(a, 0xa00000);
            setCl(a + 4, 0x11d0000);
            animated(a, kHyihndad, 0x45622a, 1, 0x45622a, 0x401bc2);
            setCl(a + 0x20, 0x200000);
            cb(a + 0x28) = 0xc;
            cb(0x45622f) = 0;
            chain = true;
            break;
        }
        case 0x401c06: {                                      // the 1-up, with its sound
            const uint32_t a = cutClear(lods());
            setCl(a, 0x1660000);
            setCl(a + 4, 0xa00000);
            setCl(a + 8, kMi1up);
            setCl(a + 0xc, 0);
            setCl(a + 0x10, 1);
            setCl(a + 0x1c, 0x401c4a);
            sound1Now(0x16);                                  // FUN_004261b0
            chain = true;
            break;
        }
        case 0x401ced: {                                      // the shadow comes
            const uint32_t a = cutClear(lods());
            setCl(a, 0xff7e0000);
            setCl(a + 4, 0xbc0000);
            setCl(a + 8, kFxishado);
            setCl(a + 0xc, 0);
            setCl(a + 0x10, 1);
            setCl(a + 0x14, 0x4561cc);
            setCl(a + 0x18, 0x4561cc);
            setCl(a + 0x1c, 0x401d39);
            cb(a + 0x28) = 6;
            cb(0x456230) = 0;
            chain = true;
            break;
        }
        default:
            std::fprintf(stderr, "cutscene: opcode %#x at %#x\n", op, esi - 4);
            esi -= 4;                                         // (stays put)
            break;
        }
        if (!chain) break;
        op = lods();
    }
    setCl(0x456000, esi);
}

bool PlayLogic::afterDemos() const { return game_->introAfterDemos(); }

// ---------------------------------------------------------------- the actors

void PlayLogic::cutAnim(uint32_t a) {
    // 0x401dbc: every `speed` ticks the next byte of the frame script: a frame, 0xf0 back to its
    // start, 0xfe and a sound (FUN_004261b0).
    cb(a + 0x29) += 1;
    if (cb(a + 0x29) < cb(a + 0x28)) return;
    cb(a + 0x29) = 0;
    setCl(a + 0x14, cl(a + 0x14) + 1);
    for (int guard = 0; guard < 0x40; ++guard) {
        const uint8_t b = cb(cl(a + 0x14));
        if (b == 0xf0) { setCl(a + 0x14, cl(a + 0x18)); continue; }
        if (b == 0xfe) {
            setCl(a + 0x14, cl(a + 0x14) + 1);
            sound1Now(cb(cl(a + 0x14)));
            setCl(a + 0x14, cl(a + 0x14) + 1);
            continue;
        }
        setCl(a + 0xc, b);
        return;
    }
}

void PlayLogic::cutReady(uint32_t a) {
    // An actor done running stands (hyiready) and flag 0 is up.
    cb(0x45622f) = 1;
    setCl(a + 8, kHyiready);
    setCl(a + 0xc, cb(0x4561db));
    setCl(a + 0x14, 0x4561db);
    setCl(a + 0x18, 0x4561db);
    setCl(a + 0x1c, 0x401afa);
    cb(a + 0x28) = 6;
}

void PlayLogic::cutRoutine(uint32_t a) {
    // `call [esi + 0x1c]` for each actor (0x401151)
    switch (cl(a + 0x1c)) {
    case kRtText: case 0x401af9: case 0x40181a:               // ret
        return;
    case kRtLine: {                                           // a scrolling line goes up half a pixel a tick (a pixel after an attract)
        uint32_t y = cl(a + 4);
        if (afterDemos()) y -= 0x8000;
        y -= 0x8000;
        setCl(a + 4, y);
        if (cw(a + 6) == 0xffec) setCl(a + 0x1c, 0);          // at -20 it is free again
        return;
    }
    case kRtExit: {                                           // DAT_0046fb90 = 0, engineExit with "*d"
        game_->state().cutscene = false;
        std::string cmd;
        for (uint32_t va = 0x456008; cb(va) != 0 && cmd.size() < 4; ++va) cmd += char(cb(va));
        game_->state().displayList.clear();
        cutExited_ = true;
        game_->command(cmd.c_str());                          // (unwinds out of the tick)
        return;
    }
    case 0x401a9e: {                                          // running in: 3 px a tick, the camera after him from x 0x50
        cutAnim(a);
        setCl(a, cl(a) + 0x30000);
        if (cb(a + 3) & 0x80) return;
        const uint16_t x = cw(a + 2);
        if (x < 0x50) return;
        if (x < 0x10e) { h_.w(0x4658b4) += 1; return; }
        cutReady(a);
        return;
    }
    case 0x401afa:                                            // standing
        cutAnim(a);
        return;
    case 0x401b00: {                                          // looking: the last frame held, flag 0 up
        cutAnim(a);
        const uint32_t c = cl(a + 0x14);
        if (cb(c + 1) != 0xf0) return;
        setCl(a + 0x14, c - 1);
        cb(0x45622f) = 1;
        return;
    }
    case 0x401b19:                                            // running off to the left: flag 0 up at the edge
        cutAnim(a);
        setCl(a, cl(a) - 0x30000);
        if (cw(a + 2) >= 0x1e) return;
        cb(0x45622f) = 1;
        return;
    case 0x401b33: {                                          // running into the room, up and down its steps
        cutAnim(a);
        setCl(a, cl(a) + 0x30000);
        if (cb(a + 3) & 0x80) return;
        const uint16_t x = cw(a + 2);
        if (x < 0x50) setCw(a + 6, uint16_t(cw(a + 6) - 1));
        if (x >= 0x1ae && x < 0x1cc) setCw(a + 6, uint16_t(cw(a + 6) + 1));
        if (x >= 0x26c && x < 0x28a) setCw(a + 6, uint16_t(cw(a + 6) - 1));
        if (x < 0x294) h_.w(0x4658b4) += 2;
        if (x < 0x2a8) return;
        cutReady(a);
        return;
    }
    case 0x401bc2: {                                          // the hand rises (and the camera with it), stops on its first frame
        if (cw(a + 6) < 0xd2 && cl(a + 0xc) == 0) return;
        cutAnim(a);
        const uint32_t v = cl(a + 0x20);
        setCl(a + 0x20, v >= 0x2000 ? v - 0x2000 : 0);
        h_.w(0x4658b6) = int16_t(cw(a + 0x22));
        setCl(a + 4, cl(a + 4) - 0x6000);
        if (cw(a + 6) == 0xd2) cb(0x45622f) = 1;
        return;
    }
    case 0x401c4a: {                                          // the 1-up rises to y 0x7e, then sparkles now and then
        cb(a + 0x28) = uint8_t((cb(a + 0x28) + 1) & 1);
        if (cb(a + 0x28) != 0) return;
        if (cw(a + 6) != 0x7e) { setCw(a + 6, uint16_t(cw(a + 6) - 1)); return; }
        if (cl(kActors + 3 * 0x40 + 0x10) != 0) return;     // (DAT_0045630f: actor 3's flags) a sparkle is up
        if ((random() & 7) != 0) return;
        const uint32_t s = cutClear(3);
        setCl(s, 0x1640000);
        setCl(s + 4, 0x760000);
        const uint32_t r = random() & 0xff;
        setCl(s + 4, cl(s + 4) - ((r & 0xf) << 16));
        setCl(s, cl(s) - ((((r >> 4) & 0xf) - 4) << 16));
        setCl(s + 8, kFxSprk2);
        setCl(s + 0xc, 0);
        setCl(s + 0x10, 1);
        setCl(s + 0x1c, 0x401cd0);
        return;
    }
    case 0x401cd0:                                            // the sparkle: a frame every 4 ticks, gone after 4
        cb(a + 0x28) = uint8_t((cb(a + 0x28) + 1) & 3);
        if (cb(a + 0x28) != 0) return;
        setCl(a + 0xc, cl(a + 0xc) + 1);
        if (cl(a + 0xc) == 4) cutClear(3);
        return;
    case 0x401d39:                                            // the shadow's steps, off the screen, then it comes in
        setCw(a + 2, uint16_t(cw(a + 2) + 1));
        if (cw(a + 2) == 0xffce) {
            setCl(a, 0x7b0000);
            setCl(a + 0x14, 0x4561fe);
            setCl(a + 0x18, 0x4561fe);
            setCl(a + 0x1c, 0x401d6a);
            cb(a + 0x28) = 8;
            return;
        }
        cutAnim(a);
        return;
    case 0x401d6a: {                                          // the shadow stamps: each sound shakes the screen 6 or 7 px up
        const uint32_t c = cl(a + 0x14);
        uint8_t al = cb(c + 1);
        h_.w(0x465b02) = 0;
        if (al == 0x0a) cb(0x456230) = 1;
        if (al == 0xfe && cb(c + 2) != 0x17) {
            uint32_t d;
            do { d = random() & 7; } while (d < 6);
            h_.w(0x465b02) = int16_t(-int32_t(d));
            al = uint8_t(-int32_t(d));
        }
        if (al == 0xf0) { for (uint32_t k = 0; k < 0x40; ++k) cb(a + k) = 0; return; }
        cutAnim(a);
        return;
    }
    default:
        std::fprintf(stderr, "cutscene: routine %#x\n", cl(a + 0x1c));
        setCl(a + 0x1c, 0);
        return;
    }
}

} // namespace game
