#include "game/play_logic.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace game {

using namespace physics;

namespace {
// Animation scripts from the original data (FUN_004237a5 interpreter).
// Positive = frame index; the original also embeds sound/effect opcodes.
const int kRunScript[] = {8, 9, 0, 1, 2, 3, 4, 5, 6, 7};            // 0x46166b
// hyivjump frame by integer vy, index = vy + 12 (table at 0x461480..0x4614a4).
const uint8_t kJumpFrameByVy[] = {1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5,   // vy -12..-1
                                  6, 6, 7, 8, 9, 10, 10, 10, 10, 10, 10, 10, 10};   // vy 0..12
constexpr int kRunFrameTicks = 3;
}

ph::FloorHit PlayLogic::probeFeet(const ph::LevelData& lv, int x, int y) const {
    const ph::ColumnTable& t = (h_.usePlane2 && !lv.collision2.empty()) ? lv.collision2 : lv.collision;
    return ph::probeCell(t, x + kProbeDx, y + kProbeDy);
}

PlayLogic* g_debugLogic = nullptr;   // the most recent PlayLogic, for the debugging tools

void PlayLogic::enter(Game& g) {
    g_debugLogic = this;
    const ph::LevelData& lv = g.level();
    h_ = Harry{};                          // FUN_00430a69 resets the damage meter on restart
    collected_.clear();
    cleared_.clear();
    exiting_ = false;
    deathTimer_ = 0;
    camMinX_ = 0; camMaxX_ = -1; boss_ = nullptr; outroTicks_ = 0; shake_ = 0;   // 0x432a09 / 0x42adad: cleared at level start and on a death
    pistonPhase_ = 0; pistonTick_ = 0;                  // 0x4329a4
    // FUN_00432713 (0x432896): the trigger classes, with the doors of pulled levers opened again.
    std::memcpy(trgClass_, ph::kTriggerClass, sizeof ph::kTriggerClass);
    trgClass_[255] = 0;
    for (const LevelScriptEntry& e : kLevelScript)
        if (g.levelFlags()[e.flag]) trgClass_[e.door] = 0;
    if (g.levelFlags()[0x15]) trgClass_[0x5d] = 0;      // DAT_00466525 (a TNT crate)
    for (int id = 0x90; id < 0xb0; ++id)                 // one-way doors shut earlier (0x420860)
        if (g.levelFlags()[id - 0x90] == 3) trgClass_[id] = 3;
    levelW_ = lv.hasBg ? lv.bg.wTiles * 8 : 320;
    levelH_ = lv.hasBg ? lv.bg.hTiles * 8 : 224;

    // FUN_004331d8: the '^' trigger gives the start position.
    bool found = false;
    for (int c = 0; c < lv.triggers.columns() && !found; ++c) {
        for (const ph::ColumnEntry& e : lv.triggers.column(c)) {
            if (e.id == ph::kTrgStart) {
                h_.x = c * 16 - 8;
                h_.y = (e.row + 2) * 16;
                found = true;
                break;
            }
        }
    }
    if (!found) { h_.x = 160; h_.y = 160; }
    const int lvNo = g.levelNumber();
    h_.facingLeft = (lvNo >= 0 && lvNo < 19) ? ph::kStartFacing[lvNo] != 0 : false;
    if (g.checkpoint().valid) {               // FUN_00430a69 restart: DAT_0046679e.. after a death
        h_.x = g.checkpoint().x; h_.y = g.checkpoint().y; h_.facingLeft = g.checkpoint().facingLeft;
    }
    h_.onGround = true;
    // Snap to the floor if the start is a few pixels above it.
    ph::FloorHit f = ph::probeNear(lv.collision, h_.x + kProbeDx, h_.y + kProbeDy);
    if (f.hit) h_.y = f.surfaceY - kProbeDy;
    jumpWasDown_ = true;
    objects_.reset();
    rng_ = 0x2545f491u ^ uint32_t(lvNo * 0x9e3779b9u);
    updateCamera(g);
    g.state().scrollX = std::clamp(h_.x + kDisplayOffset - 160, 0, g.state().maxScrollX);
    g.state().scrollY = std::clamp(h_.y + kDisplayOffset - 128, 0, g.state().maxScrollY);
    emitSprites(g);
}

void PlayLogic::tick(Game& g) {
    FrameState& fs = g.state();
    uint32_t pad = fs.joypad;
    if (outroTicks_ > 0 && !(pad & engine::kJoyDebug)) {
        // FUN_0043426d: after a boss dies the controls are dropped for 0x50 ticks,
        // then "left" is held (DAT_00466767) until the exit trigger ends the level.
        if (outroTicks_ < 0x50) ++outroTicks_;
        pad = outroTicks_ >= 0x50 ? uint32_t(engine::kJoyLeft) : 0u;
    }
    if (pad & engine::kJoyDebug) {
        // Debug fly mode (FUN_00426810 head, Ctrl held with Debug_AllowCheat).
        if ((pad & engine::kJoyLeft) && h_.x > 7) h_.x -= 7;
        if ((pad & engine::kJoyUp) && h_.y > 7) h_.y -= 7;
        if ((pad & engine::kJoyRight) && h_.x < levelW_ - 7) h_.x += 7;
        if ((pad & engine::kJoyDown) && h_.y < levelH_ - 7) h_.y += 7;
        h_.onGround = false; h_.vy = 0;
        updateCamera(g); spawnObjects(g); runObjects(g); emitSprites(g);
        return;
    }
    // FUN_00426810 (0x4268b6): trigger id 3 is a wall for Harry only while he is on the ground,
    // id 2 never (the object pass sets it to 3 for each handler).
    trgClass_[3] = h_.onGround ? 1 : 0;
    trgClass_[2] = 0;
    if (h_.dead) {
        // Death animation plays out, then the level restarts (FUN_00430a69 "*3").
        if (std::strcmp(h_.anim, "hyisink") == 0) {
            if (const ph::Sprite* spr = g.findSprite(h_.anim))
                if (++h_.animTick % 6 == 0 && h_.frame + 1 < spr->frameCount()) ++h_.frame;
            h_.y += 1;
        } else {
            if ((h_.vy >> 16) < kTerminalVy) h_.vy += kGravity;
            h_.y += h_.vy >> 16;
        }
        runObjects(g);
        emitSprites(g);
        if (--deathTimer_ <= 0) g.loseLife();
        return;
    }
    if (h_.invulnerable > 0) --h_.invulnerable;
    if (h_.hurtTimer > 0) --h_.hurtTimer;
    if (h_.chiliTimer > 0) --h_.chiliTimer;
    if (h_.grabbedBy) {
        tickGrabbed(g, pad);
        updateCamera(g);
        spawnObjects(g);
        runObjects(g);
        g.stats().damage = h_.damage;
        emitSprites(g);
        jumpWasDown_ = (pad & engine::kJoyJump) != 0;
        return;
    }
    if (h_.bungee) {
        // State 0x429fe2: the vine handler moves and animates Harry and reads the jump button.
        spawnObjects(g);
        runObjects(g);
        updateCamera(g);
        g.stats().damage = h_.damage;
        g.stats().harryDead = h_.dead;
        emitSprites(g);
        jumpWasDown_ = (pad & engine::kJoyJump) != 0;
        return;
    }
    if (h_.whipTimer > 0) --h_.whipTimer;
    else if ((pad & engine::kJoyWhip) && !(fs.joypadPrev & engine::kJoyWhip) && h_.onGround) whip(g);
    stepHorizontal(g, pad);
    stepVertical(g, pad);
    animate(g);
    updateCamera(g);
    spawnObjects(g);
    runObjects(g);
    g.stats().damage = h_.damage;
    g.stats().harryDead = h_.dead;
    checkTriggers(g);
    emitSprites(g);
    jumpWasDown_ = (pad & engine::kJoyJump) != 0;
}

void PlayLogic::hurt(Game& g, int amount) {
    // FUN_00428233: ignored while invulnerable; damage accumulates up to 0xa0,
    // 0x96 (100 on hard) kills; 0x46 ticks of invulnerability follow.
    if (h_.invulnerable > 0 || h_.dead) return;
    h_.damage = std::min(h_.damage + amount, kDamageCap);
    h_.invulnerable = amount >= kInvulnTicks ? amount + 0x28 : kInvulnTicks;
    h_.hurtTimer = kHurtTicks;
    static const int kHurtSounds[4] = {0x17, 0x2f, 0x30, 0x31};
    g.playSound(kHurtSounds[g.state().globalFrame & 3]);
    if (!h_.onGround) h_.vx = h_.facingLeft ? 0x10000 : -0x10000;    // knock-back
    const int limit = g.stats().difficulty ? 100 : kDamageMax;
    if (h_.damage >= limit) die(g);
}

void PlayLogic::whip(Game& g) {
    // FUN_0042babc: the whip swing (hyiwhip, 9 frames); enemies in reach are hit.
    h_.whipTimer = kWhipTicks;
    h_.vx = 0;
    h_.anim = "hyiwhip"; h_.frame = 0; h_.animTick = 0;
    g.playSound(0x1c);
}

void PlayLogic::die(Game& g) {
    if (h_.dead) return;
    h_.dead = true;
    h_.anim = "hyidie"; h_.frame = 0;
    h_.vx = 0; h_.vy = -0x30000;
    deathTimer_ = 90;
    g.playSound(0x8c);
}

void PlayLogic::sink(int y) {
    // FUN_0042a325: Harry sinks into a tar pit (hyisink, script 0x461875) and is lost.
    if (h_.dead) return;
    h_.dead = true;
    h_.y = y; h_.x = h_.x;
    h_.anim = "hyisink"; h_.frame = 0;
    h_.vx = 0; h_.vy = 0x4000;              // slow sink instead of the death leap
    deathTimer_ = 120;
}

void PlayLogic::checkTriggers(Game& g) {
    // FUN_00430229 probes (x - 0x10, y - 0x10) through FUN_004251f9, which adds 0x20 to x and
    // clamps y to 4: the cell is ((x + 0x10) >> 4, (y - 0x10) >> 4).
    const ph::LevelData& lv = g.level();
    const int tx = h_.x - 0x10 + kTriggerDx, ty = std::max(4, h_.y - 0x10);
    if (tx < 0) return;
    const ph::ColumnEntry* e = lv.triggers.find(tx >> 4, ty >> 4);
    if (!e) return;
    switch (e->id) {
    case ph::kTrgExit:
        if (!exiting_) { exiting_ = true; g.completeLevel(); }
        break;
    case ph::kTrgBossExit:
        if (!exiting_) { exiting_ = true; g.requestLevel(kEnding); }
        break;
    case ph::kTrgVault1: case ph::kTrgVault2: case ph::kTrgVault3:
        // Bonus vaults need the Loltun (Simon) mini-game logic: not ported, ignore.
        break;
    case ph::kTrgPlane1: h_.usePlane2 = false; break;
    case ph::kTrgPlane2: h_.usePlane2 = true; break;
    case ph::kTrgWater: die(g); h_.drowning = true; break;     // (state 0x427528)
    default: break;
    }
}

void PlayLogic::stepHorizontal(Game& g, uint32_t pad) {
    const ph::LevelData& lv = g.level();
    const bool left = (pad & engine::kJoyLeft) != 0, right = (pad & engine::kJoyRight) != 0;
    const int32_t maxV = h_.powerJump() ? kRunMaxPower : kRunMax;
    const int32_t accel = h_.onGround ? kRunAccel : kAirAccel;
    if (h_.whipTimer > 0 && h_.onGround) { h_.vx = 0; return; }
    if (h_.leverTicks > 0) { h_.vx = 0; return; }
    if (h_.knockDecel) {
        // 0x4272c1: a knock-back ignores the pad and slides to a stop (DAT_00465b59 per tick).
        if (h_.vx < 0) { h_.vx += h_.knockDecel; if (h_.vx > 0) h_.vx = 0; }
        else if (h_.vx > 0) { h_.vx -= h_.knockDecel; if (h_.vx < 0) h_.vx = 0; }
        if (h_.vx == 0 || h_.hurtTimer == 0) h_.knockDecel = 0;
    } else if (left && !right) {
        h_.vx -= accel;
        if (h_.vx < -maxV) h_.vx = -maxV;
        h_.facingLeft = true;
    } else if (right && !left) {
        h_.vx += accel;
        if (h_.vx > maxV) h_.vx = maxV;
        h_.facingLeft = false;
    } else if (h_.onGround) {
        // FUN_004300e0(0x4000): decelerate towards zero without overshooting.
        if (h_.vx < 0) { h_.vx += kRunDecel; if (h_.vx > 0) h_.vx = 0; }
        else if (h_.vx > 0) { h_.vx -= kRunDecel; if (h_.vx < 0) h_.vx = 0; }
    }
    // 32-bit fixed-point position update (obj+2 is the fractional word).
    const int32_t sum = int32_t(h_.xFrac) + (h_.vx & 0xffff);
    h_.xFrac = uint16_t(sum & 0xffff);
    int32_t nx = h_.x + (h_.vx >> 16) + (sum >> 16);
    nx = std::clamp(nx, 8, levelW_ - 8);
    if (nx != h_.x) {
        // Wall test: a surface more than 10 px above the feet in the new column blocks.
        if (h_.onGround) {
            ph::FloorHit ahead = ph::probeNear(lv.collision, nx + kProbeDx, h_.y + kProbeDy);
            if (ahead.hit && ahead.surfaceY < h_.y + kProbeDy - 10) { h_.vx = 0; nx = h_.x; }
        } else {
            ph::FloorHit ahead = ph::probeCell(lv.collision, nx + kProbeDx, h_.y + kProbeDy - 8);
            if (ahead.hit && ahead.surfaceY < h_.y + kProbeDy - 8) { h_.vx = 0; nx = h_.x; }
        }
        // FUN_0042e25e / FUN_0042ea52: invisible walls are .trg cells whose class has bit 0,
        // probed 8 and 24 px ahead at y - 4 and 24 px ahead at y - 0x1c (through FUN_004251f9,
        // which adds 0x20 to x); Harry snaps to the cell edge (+7 going left, +9 going right).
        auto wall = [&](int px, int py) {
            const int tx = px + 0x20, ty = std::max(4, py);
            if (tx < 0) return false;
            const ph::ColumnEntry* e = lv.triggers.find(tx >> 4, ty >> 4);
            if (!e) return false;
            const uint8_t cls = trgClass_[e->id];
            return cls < 0xf0 && (cls & 1) != 0;
        };
        if (nx > h_.x) {
            if (wall(nx + 8, h_.y - 4) || wall(nx + 0x18, h_.y - 4) || wall(nx + 0x18, h_.y - 0x1c)) { nx = (nx & ~0xf) + 9; h_.vx = 0; }
        } else {
            if (wall(nx - 0x28, h_.y - 4) || wall(nx - 0x38, h_.y - 4) || wall(nx - 0x38, h_.y - 0x1c)) { nx = (nx & ~0xf) + 7; h_.vx = 0; }
        }
        h_.x = nx;
    }
}

void PlayLogic::stepVertical(Game& g, uint32_t pad) {
    const ph::LevelData& lv = g.level();
    const bool jumpDown = (pad & engine::kJoyJump) != 0;
    if (h_.onGround) {
        if (jumpDown && !jumpWasDown_) {
            h_.vy = h_.powerJump() ? kJumpVyPower : kJumpVy;
            h_.jumpHold = kJumpHoldTicks;
            h_.onGround = false;
            h_.anim = "hyivjump"; h_.animTick = 0;
            g.playSound(kSndJump);
            return;
        }
        if (h_.platformTicks > 0) { --h_.platformTicks; return; }   // carried by an object
        // Follow the floor (FUN_00425455 ground branch: same row, one below, two above).
        ph::FloorHit f = probeFeet(lv, h_.x, h_.y);
        if (!f.hit) f = probeFeet(lv, h_.x, h_.y + 16);
        if (!f.hit) f = probeFeet(lv, h_.x, h_.y - 16);
        if (f.hit && f.surfaceY - (h_.y + kProbeDy) <= 16 && (h_.y + kProbeDy) - f.surfaceY <= 16) {
            h_.y = f.surfaceY - kProbeDy;
        } else {
            h_.onGround = false;            // walked off an edge
            h_.vy = 0;
        }
        return;
    }
    // Airborne (FUN_00428dfa jump/fall loop).
    if ((h_.vy >> 16) < 6) {
        bool skipGravity = false;
        if (h_.jumpHold > 0) {
            --h_.jumpHold;
            if (jumpDown) skipGravity = true; else h_.jumpHold = 0;
        }
        if (!skipGravity && (h_.vy >> 16) < kTerminalVy) h_.vy += kGravity;
    } else if ((h_.vy >> 16) < kTerminalVy) {
        h_.vy += kGravity;
    }
    const int oldFeet = h_.y + kProbeDy;
    const int32_t sum = int32_t(h_.yFrac) + (h_.vy & 0xffff);
    h_.yFrac = uint16_t(sum & 0xffff);
    h_.y += (h_.vy >> 16) + (sum >> 16);
    if (h_.vy > 0) {
        // Landing: look for a surface between the old and the new feet position.
        const int newFeet = h_.y + kProbeDy;
        for (int y = oldFeet & ~15; y <= newFeet; y += 16) {
            ph::FloorHit f = ph::probeCell(h_.usePlane2 && !lv.collision2.empty() ? lv.collision2 : lv.collision,
                                           h_.x + kProbeDx, y);
            if (f.hit && f.surfaceY >= oldFeet - 1 && f.surfaceY <= newFeet) {
                h_.y = f.surfaceY - kProbeDy;
                h_.vy = 0; h_.yFrac = 0;
                h_.onGround = true;
                h_.anim = (h_.vx != 0) ? "hyirun" : "hyiready";
                h_.animTick = 0; h_.scriptPos = 0;
                g.playSound(kSndLand);
                break;
            }
        }
    }
    if (h_.y > levelH_ + 32) die(g);           // fell out of the level
}

void PlayLogic::animate(Game& g) {
    (void)g;
    h_.runTicks = (h_.onGround && h_.vx != 0) ? h_.runTicks + 1 : 0;    // DAT_00465b9a
    if (h_.whipTimer > 0) {
        h_.anim = "hyiwhip";
        h_.frame = std::min(8, (kWhipTicks - h_.whipTimer) / 2);
        return;
    }
    if (!h_.onGround) {
        if (h_.somersault > 0) {                  // state 0x42cdb6: tumbling until he lands
            h_.anim = "hyisomr2";
            h_.frame = (h_.somersault++ / 3) % 8;
            return;
        }
        const int vyInt = std::clamp(int(h_.vy >> 16), -12, 12);
        h_.frame = kJumpFrameByVy[vyInt + 12];
        h_.anim = (h_.vx != 0) ? "hyihjump" : "hyivjump";
        return;
    }
    h_.somersault = 0;
    if (h_.leverTicks > 0) {                // state 0x42a6e7: hyipull over script 0x427e1b, the lever follows 0x427e26
        static const uint8_t kPullHarry[11] = {0, 1, 1, 2, 3, 4, 5, 5, 6, 7, 7};
        static const uint8_t kPullLever[11] = {0, 0, 0, 1, 2, 3, 4, 4, 4, 4, 4};
        const int step = std::min(10, (44 - h_.leverTicks) / 4);
        h_.anim = "hyipull"; h_.frame = kPullHarry[step];
        if (h_.lever && !h_.lever->freed) h_.lever->frame = kPullLever[step];
        if (--h_.leverTicks == 0) { if (h_.lever && !h_.lever->freed) h_.lever->frame = 4; h_.lever = nullptr; }
        return;
    }
    if (h_.tripTicks > 0) {                 // FUN_0042e9b2 with the 0x46168e script: 8 frames, 5 ticks each
        --h_.tripTicks;
        h_.anim = "hyitrip1";
        h_.frame = std::min(7, (40 - h_.tripTicks) / 5);
        return;
    }
    if (h_.vx != 0) {
        if (std::strcmp(h_.anim, "hyirun") != 0) { h_.anim = "hyirun"; h_.scriptPos = 0; h_.animTick = 0; }
        if (++h_.animTick >= kRunFrameTicks) {
            h_.animTick = 0;
            h_.scriptPos = (h_.scriptPos + 1) % int(sizeof(kRunScript) / sizeof(kRunScript[0]));
        }
        h_.frame = kRunScript[h_.scriptPos];
    } else {
        h_.anim = "hyiready";
        h_.frame = 0;
    }
}

void PlayLogic::updateCamera(Game& g) {
    FrameState& fs = g.state();
    fs.bob = shake_;                        // DAT_004517ec = DAT_00465b02
    // Keep Harry inside a window; the original camera (DAT_004658b4/b6) has
    // per-level scripting that is not ported yet, so this is a plain follow.
    const int targetX = h_.x + kDisplayOffset - 160;
    int sx = fs.scrollX;
    if (targetX > sx + 24) sx = targetX - 24;
    if (targetX < sx - 24) sx = targetX + 24;
    const int targetY = h_.y + kDisplayOffset - 128;
    int sy = fs.scrollY;
    if (targetY > sy + 32) sy = targetY - 32;
    if (targetY < sy - 32) sy = targetY + 32;
    const int maxX = camMaxX_ >= 0 ? std::min(camMaxX_, fs.maxScrollX) : fs.maxScrollX;   // DAT_00465b0b
    fs.scrollX = std::clamp(sx, std::min(camMinX_, maxX), maxX);   // 0x404e17: DAT_00465b09 lock
    fs.scrollY = std::clamp(sy, 0, fs.maxScrollY);
}


void PlayLogic::grabHarry(Obj& bat) {
    // FUN_004285d7: dazed in the bat's grip; 12 input changes break free.
    h_.grabbedBy = &bat;
    h_.anim = "hyidazed"; h_.frame = 0;
    h_.struggleLeft = 0xc;
    h_.lastDirs = 0; h_.lastButtons = 0;
    h_.nipTimer = int(random() & 7);
    h_.dazedAnim = AnimCursor{};
    h_.vx = 0; h_.vy = 0;
    h_.onGround = false;
}

void PlayLogic::standOn(int y) {
    // 0x415230: the platform sets Harry's height and puts him on the ground.
    h_.y = y; h_.yFrac = 0; h_.vy = 0;
    h_.platformTicks = 2; h_.platformY = y;
    if (!h_.onGround) {
        h_.onGround = true;
        h_.anim = (h_.vx != 0) ? "hyirun" : "hyiready";
        h_.animTick = 0; h_.scriptPos = 0;
    }
}

void PlayLogic::bounce(int y, int32_t vx, int32_t vy, bool faceRight) {
    // 0x406d17: a web throws Harry back up (states 0x42c518 / 0x42cdb6).
    h_.y = y; h_.yFrac = 0;
    h_.onGround = false; h_.platformTicks = 0;
    h_.vx = vx; h_.vy = vy; h_.jumpHold = 0;
    h_.anim = vx ? "hyihjump" : "hyivjump"; h_.frame = 0; h_.animTick = 0;
    if (vx) h_.facingLeft = !faceRight;
}

void PlayLogic::launch(int32_t vx, int32_t vy) {
    // 0x415a41: state 0x42c518 with the given velocity.
    h_.onGround = false; h_.platformTicks = 0;
    h_.vx = vx; h_.vy = vy; h_.jumpHold = 0;
    h_.anim = "hyihjump"; h_.frame = 0; h_.animTick = 0;
}

void PlayLogic::knockBack(bool toLeft, bool strong) {
    // 0x427148 / 0x427207: pushed away at 7 (2) px/tick, falling with gravity.
    h_.vx = strong ? 0x70000 : 0x20000;
    if (toLeft) h_.vx = -h_.vx;
    h_.knockDecel = strong ? 0x3e00 : 0xc00;
    h_.onGround = false; h_.platformTicks = 0;
    if (h_.vy > 0) h_.vy = 0;
    h_.anim = "hyihjump"; h_.frame = 0; h_.animTick = 0;
    h_.hurtTimer = kHurtTicks;
}

void PlayLogic::pullLever(Obj& lever) {
    h_.lever = &lever;
    h_.leverTicks = 44;
    h_.vx = 0;
}

void PlayLogic::grabBungee(Obj& vine) {
    // 0x40c784: harryCont = 0x429fe2; the vine owns Harry's position and sprite from here.
    h_.bungee = &vine;
    h_.vx = 0; h_.vy = 0; h_.jumpHold = 0;
    h_.onGround = false; h_.platformTicks = 0;
    h_.whipTimer = 0; h_.knockDecel = 0;
}

void PlayLogic::releaseHarry() {
    if (!h_.grabbedBy) return;
    h_.grabbedBy = nullptr;
    h_.anim = "hyivjump"; h_.frame = 0;
    h_.vy = 0;
}

void PlayLogic::tickGrabbed(Game& g, uint32_t pad) {
    // 0x428625
    Obj* bat = h_.grabbedBy;
    if (!bat || bat->freed || bat->type != 0x12 || bat->w(0x4a) == 0) { releaseHarry(); return; }
    h_.x = bat->x; h_.y = bat->y + 0x30;
    if (--h_.nipTimer < 0) {
        h_.nipTimer = int(random() & 0xf) + 10;
        g.playSound(2);
        h_.damage = std::min(h_.damage + 1, kDamageCap);
        const int limit = g.stats().difficulty ? 100 : kDamageMax;
        if (h_.damage >= limit) { releaseHarry(); die(g); return; }
    }
    // hyidazed frames 0..4 at 6 ticks (script 0x4619c1).
    if (++h_.dazedAnim.tick > 6) { h_.dazedAnim.tick = 0; h_.dazedAnim.pos = uint8_t((h_.dazedAnim.pos + 1) % 5); }
    h_.frame = h_.dazedAnim.pos;
    const uint8_t buttons = uint8_t(((pad & engine::kJoyJump) ? 1 : 0) | ((pad & engine::kJoyWhip) ? 2 : 0) | ((pad & engine::kJoyShoot) ? 4 : 0)) ? 1 : 0;
    if (buttons != h_.lastButtons) {
        h_.lastButtons = buttons;
        if (--h_.struggleLeft == 0) releaseHarry();
        return;
    }
    const uint16_t dirs = uint16_t(pad & (engine::kJoyLeft | engine::kJoyRight | engine::kJoyUp | engine::kJoyDown));
    if (dirs && dirs != h_.lastDirs) {
        h_.lastDirs = dirs;
        if (--h_.struggleLeft == 0) releaseHarry();
    }
}

// ---------------------------------------------------------------- objects

uint32_t PlayLogic::random() {
    // FUN_0043387e is a 32-bit shift-and-xor generator over DAT_00466772..; any
    // generator with the same use (low bits masked by the callers) behaves alike.
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return rng_;
}

int PlayLogic::physics_kChiliTicks() { return kChiliTicks; }

void PlayLogic::markCollected(const Obj& o) { if (o.col >= 0) collected_.push_back({o.col, o.row}); }

void PlayLogic::restoreEntry(const Obj& o) {
    // The handler exits write the saved id back into the working .als copy
    // (obj+0x1c / obj+0x20); an object released any other way stays cleared
    // until the next death restores the backup copy.
    for (size_t i = 0; i < cleared_.size(); ++i)
        if (cleared_[i].first == o.col && cleared_[i].second == o.row) { cleared_.erase(cleared_.begin() + long(i)); return; }
}

void PlayLogic::markKilled(const Obj& o) {
    // The 0x465a40 list: the entry stays cleared across deaths until the level changes.
    if (o.col < 0) return;
    collected_.push_back({o.col, o.row});
}

bool PlayLogic::isGone(int col, int row, const Game& g) const {
    for (const auto& cr : cleared_) if (cr.first == col && cr.second == row) return true;
    for (const auto& cr : collected_) if (cr.first == col && cr.second == row) return true;
    for (const auto& cr : const_cast<Game&>(g).permanentlyGone()) if (cr.first == col && cr.second == row) return true;
    return false;
}

void PlayLogic::spawnObjects(Game& g) {
    // FUN_00403fa0 / FUN_00404016 / FUN_00404269 / FUN_004042fe: objects whose
    // .als cell enters a margin around the camera are instantiated from their
    // template (FUN_0040454e); the entry's id is cleared while the object lives
    // and restored by the handler's despawn exit.
    const ph::LevelData& lv = g.level();
    const FrameState& fs = g.state();
    const int camX = fs.scrollX, camY = fs.scrollY;
    const int c0 = std::max(0, (camX - 0x40) >> 4), c1 = std::min(lv.objects.columns() - 1, (camX + 320 + 0x40) >> 4);
    const int r0 = std::max(0, (camY >> 4) - 3), r1 = (camY >> 4) + 16;
    for (int c = c0; c <= c1; ++c) {
        for (const ph::ColumnEntry& e : lv.objects.column(c)) {
            if (e.row < r0 || e.row > r1 || e.id == 0) continue;
            if (isGone(c, e.row, g)) continue;
            const ph::ObjectTemplate& t = ph::kObjectTemplates[e.id];
            if (!t.sprite[0] && t.type == 0) continue;
            Obj* o = objects_.alloc();
            if (!o) return;
            cleared_.push_back({c, e.row});
            o->col = c; o->row = e.row; o->id = e.id;
            o->x = int16_t(c * 16 + 0x18 + t.xOff);                 // FUN_0040454e
            o->y = int16_t(e.row * 16 - 0x10 + t.yOff);
            if (t.snap) {
                ph::FloorHit f = ph::probeNear(lv.collision, o->x, o->y + t.snap);
                if (f.hit) o->y = int16_t(f.surfaceY - t.snap);
            }
            o->sprite = t.sprite[0] ? g.findSprite(t.sprite) : nullptr;
            o->frame = t.sub;
            o->type = t.type;
            o->flags = t.flags;
            if (t.soundSlot) o->w(0x26) = 0x6663;                   // FUN_004263a6: the Genesis sound-channel request, a stub here
        }
    }
}

void PlayLogic::runObjects(Game& g) {
    const FrameState& fs = g.state();
    // 0x40d9bd (called every tick from the main loop): the piston cycle steps every 5 ticks, 0x50 steps.
    if (--pistonTick_ < 0) { pistonTick_ = 4; if (++pistonPhase_ == 0x50) pistonPhase_ = 0; }
    h_.platformTicks = 0;      // 0x4051c4: DAT_00465b73 is cleared before every object pass; a platform re-sets it
    trgClass_[3] = 0;          // 0x4051ab: trigger id 3 (a wall for Harry on the ground) never stops objects
    ObjCtx ctx{g, *this, g.level(), h_};
    ctx.levelNo = g.levelNumber();
    ctx.camX = fs.scrollX; ctx.camY = fs.scrollY;
    ctx.pad = fs.joypad;
    ctx.padPressed = fs.joypad & ~fs.joypadPrev;
    ctx.hard = g.stats().difficulty != 0;
    ctx.boss = boss_ != nullptr;
    ctx.pistonPhase = pistonPhase_; ctx.pistonTick = pistonTick_;
    ctx.freeze = false;
    ctx.hstate = h_.dead ? (h_.drowning ? HarryState::Drowning : HarryState::Dead)
               : h_.grabbedBy ? HarryState::Grabbed
               : h_.bungee ? HarryState::OnBungee
               : (h_.hurtTimer > 0 && !h_.onGround) ? HarryState::Hurt
               : !h_.onGround ? HarryState::Jumping : HarryState::Normal;
    ctx.harryUntouchable = h_.dead || exiting_;
    ctx.harrySprite = h_.anim;
    // Whip box (FUN_0042b2bc, table at 0x4612e0): hyiwhip frames 2 and 3 reach out.
    if (h_.whipTimer > 0 && std::strcmp(h_.anim, "hyiwhip") == 0 && (h_.frame == 2 || h_.frame == 3)) {
        const int x0 = h_.frame == 2 ? 6 : 7, x1 = h_.frame == 2 ? 76 : 45;
        const int y0 = h_.frame == 2 ? -61 : -39, y1 = h_.frame == 2 ? -31 : -22;
        if (h_.facingLeft) { ctx.whipX0 = h_.x - x1; ctx.whipX1 = h_.x - x0; }
        else { ctx.whipX0 = h_.x + x0; ctx.whipX1 = h_.x + x1; }
        ctx.whipY0 = h_.y + y0; ctx.whipY1 = h_.y + y1;
    }
    objects_.tick(ctx);
}

void PlayLogic::emitSprites(Game& g) {
    FrameState& fs = g.state();
    fs.displayList.clear();
    // FUN_00434eef / 0x434fd2: objects flagged "behind" first, then Harry, then the rest.
    auto emit = [&](const Obj& o) {
        if (o.freed || !o.sprite || o.frame == 0xff) return;
        engine::SpriteInstance s;
        s.x = o.x + kDisplayOffset; s.y = o.y + kDisplayOffset - fs.bob; s.sprite = o.sprite;
        s.frame = uint32_t(o.frame % std::max(1, o.sprite->frameCount()));
        s.flags = engine::kSprVisible | (o.facing ? engine::kSprHFlip : 0u) | (o.vflip ? engine::kSprVFlip : 0u)
                | ((o.flags & 0x8000) ? engine::kSprFront : 0u);
        fs.displayList.push_back(s);
    };
    for (const auto& p : objects_.objects()) if (p->behind) emit(*p);
    if (!(h_.invulnerable > 0 && !h_.dead && (fs.globalFrame & 2))) {   // hit flicker
        if (const ph::Sprite* spr = g.findSprite(h_.anim)) {
            engine::SpriteInstance s;
            s.x = h_.x + kDisplayOffset; s.y = h_.y + kDisplayOffset - fs.bob; s.sprite = spr;
            s.frame = uint32_t(std::min(h_.frame, spr->frameCount() - 1));
            s.flags = engine::kSprVisible | (h_.facingLeft ? engine::kSprHFlip : 0u);
            fs.displayList.push_back(s);
        }
    }
    for (const auto& p : objects_.objects()) if (!p->behind) emit(*p);
}

} // namespace game
