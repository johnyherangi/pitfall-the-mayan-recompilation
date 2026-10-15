// Level 20: the Atari 2600 Pitfall! that The Mayan Adventure hides in the Lost City of Copan.
// It is a game of its own in the Genesis half of PITFALL.EXE (0x401e00 .. 0x403f92), which
// FUN_00435fc0 (0x401e00), FUN_004360e9 (0x401f3c, "*3") and FUN_0043628c (FUN_004263f3, then
// 0x4020d1) run in place of the usual level code; the renderer draws its screens from two strips
// of LEVEL20's map (FUN_00436fb4 at 0x437078, Game::render).
//
// Harry keeps his record (0x465b0f, the sprite als2600, the flags 0xe000) and his ground byte
// (DAT_00465b70, 0 while he jumps); the screen is the low word of his vy (DAT_00465ba8), the
// jump's step the low word of vx and its dx the high word (DAT_00465ba6).  The rest lives at
// 0x465e76 .. 0x46627c:
//   e82 / e83 / e84   the minutes, the seconds and the ticks of the second (20:00 to begin with)
//   e85               the lives left (2 to begin with)
//   e86               killed (the scorpion, a fire, a cobra)
//   e87               tripping over a log (a point lost each tick)
//   e88               waiting for the first input
//   e89 / e8a         the copyright line: its wait and its height (a word; the height its high byte)
//   e8e               sinking (a pit, a swamp, the pond)
//   e90               on a crocodile's head (the pond does not take him)
//   e95 / e96         on a ladder: the step timer
//   e97               falling into a hole
//   e9e               the screen's descriptor (0x4695a8 + screen * 4: trees, ground, object, vine)
//   ea2               dropping in after a death
//   ea4               hanging on the vine
//   ea5 / eac / eb0   off the vine in the air: the fall speed (16.16), the way it swung
//   ea8               the object that stands for Harry in the collision tests (its +0xe holds the
//                     box 0x4024dc, so FUN_00424af1 takes it as a box record)
//   46617c ..         a byte a screen: its treasure is taken
//   466172            the score (a long, 2000 to begin with)
// and [DAT_004504e4] + 0x12c .. 0x137 holds the vine's swing, which every vine follows.
#include "game/play_logic.h"

#include <cstring>

namespace game {

namespace {
const ph::SpriteHitbox kHarryBox{"", -2, 9, -0x1b, 0x16};   // 0x4024dc
constexpr uint32_t kAls2600 = 0x47044c;                     // the sprite slot of als2600
int16_t le16(const uint8_t* p) { return int16_t(p[0] | p[1] << 8); }
}

const uint8_t* PlayLogic::a26Room() const {
    const uint32_t off = h_.ul(0x465e9e) - 0x4695a8;
    return kData_a26Rooms + (off < 0x400 ? off : 0);
}

void PlayLogic::a26ClearButtons() {
    // (and DAT_0046fb88, the engine's own start key, which the port does not keep)
    h_.b(0x466761) = 0; h_.b(0x466762) = 0; h_.b(0x466763) = 0; h_.b(0x466764) = 0;
}

void PlayLogic::a26Exit(const char* cmd) {
    // engineExit (0x4361d6): the display list is emptied and the stack unwound out of the game
    // logic; the engine then takes the command ("*3": FUN_004360e9, "*6": out of the level).
    game_->state().displayList.clear();
    a26Exited_ = true;
    if (cmd[1] == '3') game_->restartLevel();
    else game_->command(cmd);
}

void PlayLogic::a26Frame(Game& g, int entry) {
    // FUN_00435fc0 / FUN_004360e9 / FUN_0043628c on level 20: the display list starts empty, then
    // the level's start (0x401e00), a screen's start (0x401f3c) or a tick (the joypad, 0x4020d1);
    // the first two run on into a tick.  0x4363bd passes the camera on (engineExit skips it).
    game_ = &g;
    level_ = g.levelNumber();
    FrameState& fs = g.state();
    fs.displayList.clear();
    a26Exited_ = false;
    if (entry == 2) a26Start();
    else if (entry == 1) a26Screen();
    else { readPad(); a26Tick(); }
    fs.atariScreen = h_.uw(0x465ba8);                         // (FUN_00436fb4 reads it as it draws)
    if (a26Exited_) return;
    fs.scrollX = h_.w(0x4658b4);
    fs.scrollY = h_.w(0x4658b6);
    fs.bob = h_.w(0x465b02);
    ++tickCount_;
}

// ---------------------------------------------------------------- the start

void PlayLogic::a26Start() {
    // FUN_00401e00
    a26End_ = 0;
    setAlsLong(0x12c, 0);                                     // the vine's swing
    setAlsLong(0x136, 0);
    h_.b(0x465b72) = 0;
    levelVars();                                              // FUN_00433116
    h_.w(0x465b02) = 0x10;
    for (uint32_t a = 0x46617c; a <= 0x46627c; ++a) h_.b(a) = 0;   // no treasure taken
    h_.uw(0x465ae0) = 0x8b00;
    h_.b(0x465d48) = 0;
    resetObjects();                                           // FUN_00435274
    for (uint32_t a = 0x465e76; a < 0x46614a; ++a) h_.b(a) = 0;
    h_.w(0x465ba8) = 0;                                       // the first screen
    h_.vx() = 0;
    h_.l(0x466172) = 2000;
    h_.b(0x465e82) = 0x14;                                    // 20:00
    h_.b(0x465e83) = 0;
    h_.b(0x465e84) = 0x32;
    h_.b(0x465e85) = 2;
    h_.b(0x465e88) = 0xff;
    h_.w(0x465e8a) = 0x900;
    h_.onGround() = 0xff;
    h_.frame() = 0x14;
    h_.facing() = 0;
    h_.x() = 0x118;
    h_.y() = 0x18a;
    h_.b(0x465ea2) = 0;
    a26Screen();                                              // (on into 0x401f3c)
}

void PlayLogic::a26Screen() {
    // 0x401f3c: a screen starts (Harry crossed an edge, or "*3" after a death), then the tick
    // runs on (0x4020d1).
    resetObjects();                                           // FUN_00435274 (Harry's frame 0xff)
    h_.b(0x465e86) = 0;
    h_.b(0x465e8e) = 0;
    h_.b(0x465e90) = 0;
    h_.b(0x465ea4) = 0;
    h_.l(0x465e94) = 0;                                       // (e95 .. e97: off the ladder)
    h_.l(0x465e98) = 0;
    a26Deferred_ = 0;
    h_.w(0x465b02) = 0x10;                                    // (FUN_00434eef takes it off the y: the sprites sit at y)
    h_.setUl(0x465e9e, uint32_t(0x4695a8 + int16_t(h_.uw(0x465ba8) * 4)));
    h_.setSpriteSlot(kAls2600);
    h_.b(0x465b17) = 1;
    h_.uw(0x465b1b) = 0xe000;
    h_.b(0x465b7b) = 0;
    h_.w(0x4658b4) = 0xf8;
    h_.w(0x4658b6) = 0x100;
    Obj* box = objects_.allocTail();                          // FUN_00435780
    box->rawBox = &kHarryBox;
    box->frame = 0xff;
    h_.setUl(0x465ea8, addrOf(box));
    h_.w(0x4658b4) = 0x200;                                   // (the spawner's last columns and rows: it does not run here)
    h_.w(0x4658b6) = 0x20c;
    h_.uw(0x465ae0) = 0x8b00;
    h_.w(0x466168) = 0x200;
    h_.w(0x46616a) = 0x200;
    h_.w(0x465cf4) = 0x20c;
    h_.w(0x465cf6) = 0x20c;
    h_.w(0x466550) = 2;
    h_.w(0x4658b4) = 0x108;                                   // the camera for good
    h_.w(0x4658b6) = 0xf6;
    h_.frame() = 0x14;
    h_.uw(0x465b1b) = 0xe000;
    a26Populate();                                            // FUN_004031f7
    a26Objects();                                             // FUN_0040291c
    a26Show();
    h_.b(0x466554) = 0xff;
    a26Tick();                                                // (on into 0x4020d1)
}

void PlayLogic::a26Populate() {
    // FUN_004031f7: the screen's objects from its descriptor, each at the tail of the list.
    const uint8_t* r = a26Room();
    const ph::Sprite* spr = game_->findSprite("als2600");
    auto add = [&](uint8_t type, int16_t x, int16_t y) {      // FUN_00435780
        Obj* o = objects_.allocTail();
        o->type = type; o->x = x; o->y = y; o->setSprite(spr);
        return o;
    };
    if (r[1] == 0xa) {
        // the crocodile pond: the water and three crocodiles, their mouths closing in 0x28 ticks
        Obj* w = add(3, 0x196, 0x182);
        w->flags = 0x4000; w->frame = 0x1c;
        for (int16_t x : {int16_t(0x1ae), int16_t(0x18e), int16_t(0x16e)}) {
            Obj* c = add(7, x, 0x184);
            c->flags = 0xc000; c->frame = 3; c->b(0x32) = 0x28;
        }
    } else {
        // a pit: 0xc / 0xe come and go (the script at 0x403168 from hidden), 7 / 8 stay
        uint8_t d5 = 1;
        uint16_t flags = 0;
        if (r[1] == 0xc) flags = 0x2000;
        else if (r[1] == 0xe) flags = 0x4000;
        else { d5 = 0; if (r[1] == 7) flags = 0x2000; else if (r[1] == 8) flags = 0x4000; }
        if (flags) {
            Obj* p = objects_.allocTail();
            p->flags = flags;
            p->b(0x32) = d5;
            p->frame = d5 ? 0xff : 0x1c;
            p->type = 3; p->x = 0x196; p->y = 0x182; p->setSprite(spr);
        }
    }
    auto log = [&](int16_t x) {                               // a rolling log
        Obj* o = add(6, x, 0x189);
        o->behind = 0xff; o->flags = 0xc000; o->frame = 0x16;
    };
    switch (r[2]) {
    case 0x18: log(0x111); log(0x1f1); log(0x231); break;
    case 0x16: log(0x231); log(0x1f1); break;
    case 0x14: log(0x211); log(0x1f1); break;
    case 0x12: log(0x1f1); break;
    case 0x04:                                                // three logs lying still
        for (int16_t x : {int16_t(0x135), int16_t(0x235), int16_t(0x1f5)}) {
            Obj* o = add(2, x, 0x189);
            o->b(0x32) = 2; o->flags = 0x4000;
        }
        break;
    default:
        // one thing of the table at 0x403138: a log, a fire, a cobra, a treasure (unless taken)
        if (h_.b(0x46617c + h_.uw(0x465ba8)) == 0) {
            Obj* o = add(2, 0x1f9, 0x189);
            o->b(0x32) = r[2]; o->flags = 0x4000;
        }
        break;
    }
    if (r[3] == 0) {                                          // the vine
        Obj* o = add(4, 0x198, 0x135);
        o->frame = 0x1d; o->flags = 0x4000;
    }
    if (r[1] != 0 && r[1] != 2 && r[1] != 4 && r[1] != 6) {  // no ladder: the scorpion below
        Obj* o = add(5, 0x19e, 0x1b9);
        o->frame = 9; o->flags = 0xe000;
    }
}

// ---------------------------------------------------------------- the tick

void PlayLogic::a26Tick() {
    // 0x4020d1
    if (a26End_ != 0) {
        // 0x4020dd: the game is over; after DAT_00457410 ticks a button goes back ("*6")
        a26Show();
        a26Copyright();
        if (--a26End_ != 0) return;
        a26End_ = 1;
        if (h_.b(0x466761) == 0 && h_.b(0x466762) == 0 && h_.b(0x466763) == 0 && h_.b(0x466764) == 0) return;
        a26Exit("*6");
        return;
    }
    if (a26Deferred_ != 1) {
        if (a26Deferred_ != 0) {
            // 0x402168: a death's pause, then its code
            if (--a26Wait_ >= 0) { a26Show(); return; }
            a26Deferred(a26Deferred_);
            return;
        }
        // 0x4021d1
        if (h_.b(0x465e88) != 0) {
            a26Deferred_ = 1;
            h_.frame() = 0x14;
            h_.w(0x465e8a) = 0x900;
            h_.b(0x465e89) = 0x78;
        }
    }
    if (a26Deferred_ == 1) {
        // 0x4021fb: the screen stands still until a button or left / right
        a26Show();
        h_.b(0x466554) = 0xff;
        a26Copyright();
        if (h_.l(0x466761) == 0 && h_.w(0x466767) == 0) return;
        a26Deferred_ = 0;
        h_.b(0x465e88) = 0;
        h_.w(0x465e8a) = 0;
        h_.y() = 0x18a;
    }
    // 0x402257
    h_.b(0x465e87) = 0;
    a26Objects();                                             // FUN_0040291c
    a26Show();
    h_.b(0x466554) = 0xff;
    bool died = false;
    if (h_.b(0x465e8e) != 0) {
        // sinking: a frame a tick up to 0x3f
        h_.frame() += 1;
        if (h_.frame() == 0x40) { h_.frame() = 0x3f; h_.b(0x465e8e) = 0; died = true; }
    } else if (a26Harry()) {
        return;                                               // across an edge: the next screen has run a tick
    }
    if (died || h_.b(0x465e86) != 0) {
        // 0x4022b9 (after 0x31 calls of the stub FUN_004263a5): a life gone
        h_.b(0x465e85) -= 1;
        if (int8_t(h_.b(0x465e85)) < 0) {
            // 0x402359: the last one; 200 ticks, then the end (the Genesis music byte DAT_004627bd = 0x40 aside)
            a26Wait_ = 0xc8;
            a26Deferred_ = 0x402394;
        } else {
            a26Wait_ = 0x64;
            a26Deferred_ = 0x4022f6;
        }
        h_.b(0x465e85) += 1;                                  // (the count goes down when the pause is over)
        return;
    }
    // 0x4023d8: the clock
    h_.b(0x465e84) -= 1;
    if (h_.b(0x465e84) == 0) {
        h_.b(0x465e84) = 0x3c;
        h_.b(0x465e83) -= 1;
        if (int8_t(h_.b(0x465e83)) < 0) {
            if (h_.b(0x465e82) == 0) {
                // time is up
                h_.w(0x465e82) = 0;
                h_.b(0x465e8e) = 0;
                h_.b(0x465e85) = 0;
                sound1(0x20, 0x19);
                a26End_ = 0x1e;
                a26ClearButtons();
                return;
            }
            h_.b(0x465e83) = 0x3b;
            h_.b(0x465e82) -= 1;
        }
    }
    // 0x40246d: DAT_00465b72 (nothing in the 2600 code sets it) ends the game too
    if (h_.b(0x465b72) == 0) return;
    a26Wait_ = 0x12c;
    h_.b(0x465e85) += 1;
    a26Deferred_ = 0x402490;
    a26Deferred(0x402490);                                    // (on into it)
}

void PlayLogic::a26Deferred(uint32_t addr) {
    // `jmp [DAT_00457408]` when the pause runs out
    switch (addr) {
    case 0x4022f6:
        // a life lost: Harry drops in at the left of the screen, above ground or below, "*3"
        h_.onGround() = 0xff;
        h_.b(0x465e85) -= 1;
        h_.frame() = 0xd;
        h_.facing() = 0;
        h_.b(0x465ea2) = 0xff;
        h_.x() = 0x116;
        h_.y() = int16_t(h_.y() - 0x198) < 0 ? 0x168 : 0x1b1;
        a26Exit("*3");
        break;
    case 0x402394:                                            // the last life lost
        a26End_ = 0x1e;
        a26ClearButtons();
        break;
    case 0x402490:
        h_.b(0x465e85) -= 1;
        a26End_ = 0x1e;
        a26ClearButtons();
        break;
    default: break;
    }
}

// ---------------------------------------------------------------- the display

void PlayLogic::a26Show() {
    // FUN_0043524d (it clears words of the usual object pass, which nothing here reads), then
    // FUN_004024ee: the rolling logs (type 6) from the tail of the list, so that they are drawn
    // over everything; FUN_0040254e: the HUD; FUN_00435174: Harry, then every object from the
    // tail (the logs again among them).
    const std::vector<Obj*> objs = objects_.list();
    for (auto it = objs.rbegin(); it != objs.rend(); ++it)
        if ((*it)->type == 6) displayEntry(**it);
    a26Hud();
    h_.w(0x465886) = 0x7000;
    displayEntry(h_.x(), h_.y(), h_.frame(), h_.facing(), h_.b(0x465b38), h_.flags(), game_->findSprite(h_.sprite()));
    for (auto it = objs.rbegin(); it != objs.rend(); ++it) displayEntry(**it);
}

void PlayLogic::a26Glyph(uint32_t frame, int d6, int d7) {
    // FUN_00402526 -> FUN_00434e8a: a frame of chrs2600 at (d6 - 0x84, d7 - 0x84) on the screen;
    // the holes (0x13 / 0x14) go in front of Harry.
    FrameState& fs = game_->state();
    if (fs.displayList.size() >= 0x50) return;
    const ph::Sprite* spr = game_->findSprite("chrs2600");
    if (!spr) return;
    engine::SpriteInstance s;
    s.sprite = spr;
    s.frame = frame % uint32_t(std::max(1, spr->frameCount()));
    s.flags = (frame == 0x13 || frame == 0x14) ? 0x21u : 0x01u;
    s.x = uint16_t(d6 - 0x84 + h_.w(0x4658b4));
    s.y = uint16_t(d7 - 0x96 + h_.w(0x4658b6) + 0x12);
    fs.displayList.push_back(s);
}

void PlayLogic::a26Copyright() {
    // FUN_0040218b: the copyright line waits 0x78 ticks, rises 9 px in 1/8 px steps, waits again
    // and drops back.
    if (h_.b(0x465e89) != 0) {
        h_.b(0x465e89) -= 1;
        if (h_.b(0x465e89) == 0 && h_.b(0x465e8b) == 0) { h_.b(0x465e89) = 0x78; h_.w(0x465e8a) = 0x900; }
        return;
    }
    h_.w(0x465e8a) -= 0x20;
    if (h_.b(0x465e8b) == 0) h_.b(0x465e89) = 0x78;
}

void PlayLogic::a26Hud() {
    // FUN_0040254e.  First the vine's swing: every sixth tick the next {frame, facing} of 0x403198.
    uint8_t* als = alsMem_.data();
    als[0x12e] = uint8_t(als[0x12e] - 1);
    if (int8_t(als[0x12e]) < 0) {
        als[0x12e] = 5;
        uint16_t i = uint16_t((als[0x12c] | als[0x12d] << 8) + 2);
        if (i >= 0x5f || kData_a26Vine[i] == 0xff) i = 0;
        als[0x12c] = uint8_t(i); als[0x12d] = uint8_t(i >> 8);
        als[0x136] = kData_a26Vine[i];
        als[0x137] = kData_a26Vine[i + 1];
    }
    // the score: six digits from (0x9c, 0x90), the leading zeros left out (a negative score shows 0
    // and is 0 from then on)
    const int32_t score = h_.l(0x466172);
    if (score < 0) h_.l(0x466172) = 0;
    digits(score);
    uint8_t lead = 0xff;
    for (int i = 0, d6 = 0x9c; i < 6; ++i, d6 += 0xe) {
        const uint8_t d = h_.b(0x466556 + uint32_t(i));
        if (i != 5 && lead != 0) {
            if (d == 0) continue;
            lead = 0;
        }
        a26Glyph(d, d6, 0x90);
    }
    // the lives left: one or two strokes
    int d6 = 0x98;
    if (h_.b(0x465e85) == 1) a26Glyph(0xb, d6, 0x9e);
    else if (h_.b(0x465e85) != 0) a26Glyph(0xc, d6, 0x9e);
    d6 += 0x12;
    // the clock, mm:ss (a ten of minutes only when there is one)
    digits(h_.b(0x465e82));
    if (h_.b(0x46655a) != 0) a26Glyph(h_.b(0x46655a), d6, 0x9e);
    d6 += 0xe;
    a26Glyph(h_.b(0x46655b), d6, 0x9e);
    d6 += 0x11;
    a26Glyph(0xa, d6, 0x9e);
    d6 += 0xb;
    digits(h_.b(0x465e83));
    a26Glyph(h_.b(0x46655a), d6, 0x9e);
    d6 += 0xe;
    a26Glyph(h_.b(0x46655b), d6, 0x9e);
    // the Activision logo, and the copyright line at its height
    for (int i = 0; i < 3; ++i) a26Glyph(0x11, 0x98 + 0x20 * i, 0x148);
    for (int i = 0; i < 3; ++i) a26Glyph(0x12, 0x98 + 0x20 * i, 0x142);
    const int d7 = 0x14e + h_.b(0x465e8b);
    for (uint32_t g = 0xd; g <= 0x10; ++g) a26Glyph(g, 0xc3, d7);
    // the hole over the ladder, and the two beside it
    const uint8_t* r = a26Room();
    if (int8_t(r[1] - 7) >= 0) return;
    a26Glyph(0x13, 0x114, 0x112);
    if (int8_t(r[1] - 3) < 0) return;
    a26Glyph(0x14, 0xe0, 0x112);
    a26Glyph(0x14, 0x148, 0x112);
}

// ---------------------------------------------------------------- the objects

void PlayLogic::a26Trip() {
    // FUN_00403f68: every fourth tick of tripping makes the sound
    h_.b(0x465c6a) = uint8_t((h_.b(0x465c6a) + 1) & 3);
    if (h_.b(0x465c6a) == 0) sound1(0x1f, 3);
}

void PlayLogic::a26Objects() {
    // FUN_0040291c: its own object pass, from the head of the list (the next record taken before
    // each one runs, DAT_00465888).  The collision tests move the box object to Harry first.
    ObjCtx ctx{*game_, *this, game_->level(), h_};
    ctx.levelNo = level_;
    ctx.camX = h_.w(0x4658b4); ctx.camY = h_.w(0x4658b6);
    Obj* box = objAt(h_.ul(0x465ea8));
    auto touches = [&](Obj& o, int16_t x, int16_t y) {       // FUN_00424af1 with a5 = the box
        if (!box) return false;
        box->x = x; box->y = y;
        return ctx.boxesOverlap(*box, o);
    };
    for (Obj* o = objects_.first(); o; ) {
        Obj* next = objects_.nextOf(*o);
        switch (o->type) {
        case 7: {
            // a crocodile: the mouth opens and shuts every 0x79 ticks (frame 3 open, 4 shut); its
            // head holds Harry (0x11 px from x 3, the front 8 of them only while it is shut)
            o->b(0x32) -= 1;
            if (int8_t(o->b(0x32)) < 0) { o->b(0x32) = 0x78; o->frame = uint8_t(((o->frame - 1) ^ 1) + 1); }
            const uint16_t d = uint16_t(h_.x() - o->x);
            if (d < 0x12 && (d >= 0xf || (d >= 3 && (o->frame == 4 || d >= 0xb)))) h_.b(0x465e90) = 0xff;
            break;
        }
        case 6:
            // a rolling log: 1 px left a tick, round from x 0xf0 to 0x250; Harry trips over it
            // (unless on the vine), a point a tick
            if (h_.b(0x465ea4) == 0 && touches(*o, h_.x(), h_.y())) {
                if (h_.l(0x466172) != 0) h_.l(0x466172) -= 1;
                h_.b(0x465e87) = 0xff;
                a26Trip();
            }
            o->b(0x33) -= 1;
            if (int8_t(o->b(0x33)) < 0) { o->b(0x33) = 4; o->frame ^= 1; }
            o->x -= 1;
            if (o->x == 0xf0) o->x = 0x250;
            break;
        case 5: {
            // the scorpion: it stings (the box 7 px further on when Harry faces left, 9 up); every
            // seventh tick it steps 1.5 px towards him
            if (touches(*o, int16_t(h_.x() + (h_.facing() ? 7 : 0)), int16_t(h_.y() - 9))) {
                h_.b(0x465e86) = 0xff;
                sound1(0x20, 0x19);
            }
            const int16_t hx = h_.x();
            if (hx == o->x || int16_t(hx + 1) == o->x) break;
            o->b(0x33) -= 1;
            if (int8_t(o->b(0x33)) >= 0) break;
            o->b(0x33) = 6;
            o->frame = uint8_t(((o->frame + 1) ^ 1) - 1);
            if (int16_t(hx - o->x) >= 0) {
                o->facing = 0xff;
                const uint32_t f = uint32_t(o->xFrac) + 0x8000;
                o->xFrac = uint16_t(f);
                o->x = int16_t(o->x + 1 + int(f >> 16));
            } else {
                o->facing = 0;
                const bool borrow = o->xFrac < 0x8000;
                o->xFrac = uint16_t(o->xFrac - 0x8000);
                o->x = int16_t(o->x - 1 - (borrow ? 1 : 0));
            }
            break;
        }
        case 4: {
            // the vine: it swings with [als + 0x136]; its tip (0x40310c) catches Harry when it
            // passes within (-4 .. 9, -0x1b .. -0x12) of him; he hangs from it (frame 0x15) until
            // down lets go, off the way it swings (a quarter px a tick down to begin with)
            o->frame = alsMem_[0x136];
            o->facing = alsMem_[0x137];
            const int i = uint8_t(o->frame - 0x1d) * 4;
            int16_t d0 = 0, d1 = 0;
            if (i + 3 < 0x2c) { d0 = le16(kData_a26VineTips + i); d1 = le16(kData_a26VineTips + i + 2); }
            if (o->facing) d0 = int16_t(-d0);
            d0 = int16_t(d0 + o->x);
            d1 = int16_t(d1 + o->y);
            if (h_.b(0x465ea5) != 0) break;
            if (h_.b(0x465ea4) == 0) {
                int16_t d2 = int16_t(h_.x() - 4);
                if (int16_t(d0 - d2) < 0) break;
                d2 = int16_t(d2 + 0xe);
                if (int16_t(d0 - d2) >= 0) break;
                d2 = int16_t(h_.y() - 0x1b);
                if (int16_t(d1 - d2) < 0) break;
                d2 = int16_t(d2 + 0xa);
                if (int16_t(d1 - d2) >= 0) break;
                sound1(0x23, 9);
                h_.b(0x465ea4) = 0xff;
            }
            h_.x() = int16_t(d0 - 0xb + (h_.facing() ? 0x15 : 0));
            h_.y() = int16_t(d1 + 0x1b);
            h_.frame() = 0x15;
            if (h_.b(0x466766) != 0) {
                h_.b(0x465ea4) = 0;
                h_.b(0x465ea5) = 0xff;
                h_.l(0x465eac) = 0x4000;
                h_.b(0x465eb0) = o->facing;
                h_.facing() = uint8_t(o->facing ^ 1);
            }
            break;
        }
        case 3:
            // a pit, a swamp or the pond: Harry on the ground within its half width (by the
            // frame: 6, 0x1e, 0x2e, 0x36, 0x3e) sinks, unless a crocodile holds him
            if (h_.b(0x465e8e) == 0) {
                if (o->b(0x32) != 0) animStep(ctx, *o, o->anim(0x42), kData_a26Pit, 5);
                if (h_.b(0x465e90) == 0 && o->frame != 0xff && h_.y() == 0x18a) {
                    const int16_t d2 = o->frame == 0x19 ? 6 : o->frame == 0x1a ? 0x1e : o->frame == 0x28 ? 0x2e
                                     : o->frame == 0x1b ? 0x36 : 0x3e;
                    const int16_t d0 = int16_t(h_.x() - o->x);
                    if (int16_t(d2 - d0) >= 0 && int16_t(-d2 - d0) < 0) {
                        h_.y() -= 1;
                        h_.frame() = 0x2b;
                        h_.b(0x465e8e) = 0xff;
                        sound1(0x20, 0x19);
                    }
                }
            }
            h_.b(0x465e90) = 0;
            break;
        case 2: {
            // a thing on the ground (0x403138 by +0x32 / 2): its two frames take turns every 5
            // ticks; touching it trips (1), takes the treasure (2: value * 1000, the screen's
            // treasure gone for good) or kills (3)
            const uint8_t* e = kData_a26Items + (o->b(0x32) >> 1) * 4;
            o->b(0x33) -= 1;
            if (int8_t(o->b(0x33)) < 0) { o->b(0x33) = 4; o->w(0x34) ^= 1; }
            o->frame = e[uint16_t(o->w(0x34))];
            if (!touches(*o, h_.x(), h_.y())) break;
            if (e[2] == 1) {
                if (h_.l(0x466172) != 0) h_.l(0x466172) -= 1;
                h_.b(0x465e87) = 0xff;
                a26Trip();
            }
            if (e[2] == 3) {
                sound1(0x20, 0x19);
                h_.b(0x465e86) = 0xff;
            }
            if (e[2] == 2) {
                h_.l(0x466172) += int32_t(e[3]) * 1000;
                sound1(0x24, 5);
                h_.b(0x46617c + h_.uw(0x465ba8)) = 0xff;
                objects_.free(*o);                            // FUN_0043598f
            }
            break;
        }
        default: break;
        }
        o = next;
    }
}

// ---------------------------------------------------------------- Harry

bool PlayLogic::a26Harry() {
    // FUN_004036fc.  True when Harry crossed an edge of the screen: the code there pops its own
    // return address and jumps to 0x401f3c, which starts the next screen and runs a whole tick, and
    // whose `ret` then returns past its caller.
    if (h_.b(0x465ea4) != 0) return false;                    // on the vine (the vine moves him)
    if (a26Move()) {
        // a jump started: FUN_00403b0d jumped back to 0x4036fc instead of returning; that second
        // run returns to 0x40370e, unless it crossed an edge (it popped 0x40370e: what follows
        // then returns to the tick, past this run's own code)
        if (a26Harry()) return false;
    }
    // 0x40370e: the edges, x 0x220 and 0x10b: a screen on (three underground) and in at the
    // other side; screen 0xff is skipped
    if (h_.x() == 0x220 || h_.x() == 0x10b) {
        const bool right = h_.x() == 0x220;
        const int steps = int16_t(h_.y() - 0x190) < 0 ? 1 : 3;
        uint16_t s = h_.uw(0x465ba8);
        for (int k = 0; k < steps; ++k) {
            if (right) { s = uint16_t((s + 1) & 0xff); if (s == 0xff) s = 0; }
            else { s = uint16_t((s - 1) & 0xff); if (s == 0xff) s = 0xfe; }
        }
        h_.uw(0x465ba8) = s;
        h_.x() = int16_t(h_.x() - 0x190) < 0 ? 0x21e : 0x10c;
        a26Screen();
        return true;
    }
    const uint8_t* r = a26Room();
    if (int16_t(h_.y() - 0x198) >= 0) {
        // underground: the ladder up, and the brick walls
        if (int8_t(r[1] - 7) < 0 && h_.b(0x465e96) == 0 && h_.y() == 0x1c1 && h_.b(0x466765) != 0
            && int16_t(h_.x() - 0x18d) >= 0 && int16_t(h_.x() - 0x1a3) < 0) {
            h_.b(0x465e96) = 0xff;
            h_.x() = 0x198;
            h_.y() = 0x1bf;
            h_.facing() = 0;
            h_.frame() = 0x29;
            h_.b(0x465e95) = 5;
            return false;
        }
        // a wall turns him back (and round, in the air)
        auto bounce = [&](int16_t to) {
            h_.w(0x465ba6) = int16_t(-h_.w(0x465ba6));
            h_.x() = to;
            if (h_.y() != 0x1c1) h_.facing() ^= 1;
        };
        if (r[1] == 0 || r[1] == 4) {
            if (h_.x() == 0x12c) bounce(0x12d);
            else if (h_.x() == 0x113) bounce(0x112);
        } else if (r[1] == 2 || r[1] == 6) {
            if (h_.x() == 0x201) bounce(0x200);
            else if (h_.x() == 0x217) bounce(0x218);
        }
    }
    // 0x4039ce: on the ground over a ladder: down takes it, otherwise he falls into the hole
    // (100 points); so into the holes beside it
    if (int8_t(r[1] - 7) >= 0 || h_.y() != 0x18a || h_.b(0x465e97) != 0 || h_.b(0x465e96) != 0) return false;
    auto fall = [&]() {
        h_.l(0x466172) -= 100;
        sound1(0x21, 5);
        h_.b(0x465e97) = 0xff;
    };
    const int16_t x = h_.x();
    if (int16_t(x - 0x191) >= 0 && int16_t(x - 0x19f) < 0) {
        if (h_.b(0x466766) != 0) {
            h_.b(0x465e96) = 0xff;
            h_.x() = 0x198;
            h_.y() = 0x197;
            h_.facing() = 0;
            h_.frame() = 0x29;
            h_.b(0x465e95) = 5;
            return false;
        }
        fall();
    }
    if (int8_t(r[1] - 3) < 0) return false;
    if (int16_t(x - 0x159) >= 0 && int16_t(x - 0x16d) < 0) fall();
    if (int16_t(x - 0x1c1) >= 0 && int16_t(x - 0x1d5) < 0) fall();
    return false;
}

bool PlayLogic::a26Move() {
    // FUN_00403b0d: true when it ends in `jmp 0x4036fc` (a jump has just started).
    if (h_.b(0x465ea5) != 0) {
        // off the vine: falling faster (to 1 px a tick), 1.25 px a tick the way it swung
        h_.frame() = 0xd;
        if (h_.w(0x465eae) != 1) h_.l(0x465eac) += 0x1000;
        addY(h_.l(0x465eac));
        addX(-0x14000);
        if (h_.b(0x465eb0) != 0) addX(0x28000);
        if (int16_t(h_.y() - 0x18a) < 0) return false;
        h_.y() += 5;
        h_.b(0x465ea5) = 0;
        h_.onGround() = 0xff;
    }
    if (h_.b(0x465ea2) != 0) {
        // dropping in after a death, a px a tick to the ground or the tunnel's floor
        h_.y() += 1;
        if (h_.y() != 0x18a && h_.y() != 0x1c1) return false;
        h_.b(0x465ea2) = 0;
    }
    if (h_.b(0x465e96) != 0) {
        // on the ladder: a 4 px step every sixth tick; at the top left / right jumps off it
        h_.b(0x465e95) -= 1;
        if (int8_t(h_.b(0x465e95)) >= 0) return false;
        h_.b(0x465e95) = 5;
        auto step = [&]() { h_.frame() = uint8_t(((h_.frame() - 1) ^ 1) + 1); };
        bool off = false;
        if (h_.y() == 0x197) {
            if (h_.b(0x466766) == 0 && h_.w(0x466767) != 0) {
                sound1(0x22, 5);
                h_.y() = 0x18a;
                if (h_.b(0x466767) != 0) { h_.facing() = 1; h_.w(0x465ba6) = -1; }
                else { h_.w(0x465ba6) = 1; h_.facing() = 0; }
                h_.onGround() = 0;
                h_.w(0x465ba4) = 0;
                h_.b(0x465e96) = 0;
                off = true;
            }
        } else if (h_.b(0x466765) != 0) {
            h_.y() -= 4;
            step();
            return false;
        }
        if (!off) {
            // 0x403cc0
            if (h_.b(0x466766) == 0) return false;
            if (int16_t(h_.y() - 0x1bf) < 0) { h_.y() += 4; step(); return false; }
            h_.b(0x465e96) = 0;
            h_.onGround() = 0xff;
        }
    }
    if (h_.b(0x465e97) != 0) {
        // into a hole: a px a tick down to the tunnel
        h_.y() += 1;
        h_.frame() = 0xd;
        if (int16_t(h_.y() - 0x1aa) < 0) h_.frame() = 0x14;
        if (h_.y() != 0x1c1) return false;
        h_.b(0x465e97) = 0;
    }
    if (h_.onGround() == 0) {
        // in a jump: the dy words of 0x403f22 by vx (in twos), dx a tick
        h_.w(0x465ba4) += 2;
        const uint16_t k = uint16_t(h_.w(0x465ba4));
        const int16_t dy = k + 1 < 0x46 ? le16(kData_a26Jump + k) : int16_t(0x8000);
        if (dy != int16_t(0x8000)) {
            h_.y() += dy;
            h_.frame() = 0xd;
            h_.x() += h_.w(0x465ba6);
            return false;
        }
        h_.onGround() = 0xff;
    }
    h_.y() = int16_t(h_.y() - 0x1a0) >= 0 ? 0x1c1 : 0x18a;
    if (h_.b(0x46676d) == 1 || h_.b(0x46676e) == 1 || h_.b(0x46676c) == 1) {
        // a button pressed this tick: a jump, straight up or the way left / right is held
        h_.w(0x465ba6) = 0;
        if (h_.b(0x466767) != 0) h_.w(0x465ba6) = -1;
        if (h_.b(0x466768) != 0) h_.w(0x465ba6) = 1;
        h_.onGround() = 0;
        h_.w(0x465ba4) = 0;
        sound1(0x22, 6);
        return true;
    }
    if (h_.b(0x466768) != 0) { h_.x() += 1; h_.facing() = 0; }
    else if (h_.b(0x466767) != 0) { h_.x() -= 1; h_.facing() = 1; }
    else {
        if (h_.b(0x465e87) == 0) { h_.frame() = 0x14; h_.b(0x465b7b) = 0; }
        else h_.frame() = 0xe;
        return false;
    }
    // running: 1 px a tick, the frames of 0x403f0c (tripping: frame 0xe)
    if (h_.b(0x465e87) != 0) { h_.frame() = 0xe; return false; }
    h_.b(0x465b7b) += 1;
    const uint8_t k = h_.b(0x465b7b);
    h_.frame() = k < 0x15 ? kData_a26Run[k] : 0xff;
    if (h_.frame() == 0xff) { h_.frame() = 0xf; h_.b(0x465b7b) = 0; }
    return false;
}

} // namespace game
