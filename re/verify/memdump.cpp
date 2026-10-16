// memdump <ticks>: the port's run as cmpmem.py reads it, a line per engine tick (the driver's loop
// step): the step, the engine's tick count, the logic's, the level, the random generator, the logic's
// memory 0x465800 .. 0x4668ff (with the player's numbers the port keeps apart put at their
// addresses), the 80 object records, every byte as the original lays them out (recordByte:
// the sprite's address in the load block, the list's links and the .als entry's address), and the
// cutscenes' data 0x456000 .. 0x4569df (their scripts' cursor and counts, the texts, the actors)
// and the sound channels' state 0x4627be .. c2.
//
//   MENU=<level>@<step>  the level picked from the menu before that step (the original's start less 2,
//                        less the load's 13 ticks on levels 0, 2, 5, 8 and 16: the port's ticks are the original's)
//   PAD=<file>           mkpad.py's joypad, replayed tick by tick from the level's start (Game::joypadHook),
//                        with the engine's start key as each tick began where it has it (a third number),
//                        and its "type <tick> <text>" lines: the cheats typed after that tick of the original
//   KEYS=vk@from-to,...  or keys held (Windows key codes, from one step to before another, -1: on)
//   HARD=1               the hard difficulty (Option_Difficulty 1, as the original's registry had it)
//   FRAMES=<file> FRAMEPRED=<file> FRAMEOUT=<file> [FRAMEPPM=<prefix>]
//                        with PAD, the original's pictures (capmem.sh's FRAMES) against the port's:
//                        framepred.py's lines give each the original's engine ticks while it was on
//                        the screen, and it is matched with the port's pictures of those ticks and
//                        the 4 before (what the screen shows trails the tick); "picture first-tick
//                        last-tick latest-tick tick differing-pixels level load" lines (the original's
//                        ticks; -1: before the level, which has no match; load 1: the original showed
//                        a single colour or a load's dark red and black, a load's screen, which the
//                        port does not take the time to show) and the first picture of
//                        each run of inexact ones as <prefix>_<picture>.ppm (the original, the port,
//                        the differing pixels); NOOUT=1 leaves the memory out, FRAMEWIN=<before>,<after>
//                        sets the ticks around (4,0)
//
// Build it as re/drivers' and run it from the repository root (it loads original/).
#include "game/game.h"
#include "game/play_logic.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace game { extern PlayLogic* g_debugLogic; extern uint32_t g_debugCartD5; }
int main(int argc, char** argv) {
    const int t1 = std::atoi(argv[1]);
    engine::Options opts; opts.soundEnabled = false; opts.slow = true;
    if (const char* h = std::getenv("HARD")) opts.difficulty = std::atoi(h);
    game::Game g("original", opts);
    int menuLevel = -1, menuTick = -1;
    if (const char* m = std::getenv("MENU")) std::sscanf(m, "%d@%d", &menuLevel, &menuTick);
    std::vector<std::array<int, 3>> keys;
    if (const char* ks = std::getenv("KEYS")) {
        std::string s(ks); size_t p = 0;
        while (p < s.size()) {
            size_t e = s.find(',', p); if (e == std::string::npos) e = s.size();
            std::array<int, 3> k{}; std::sscanf(s.substr(p, e - p).c_str(), "%i@%d-%d", &k[0], &k[1], &k[2]);
            keys.push_back(k); p = e + 1;
        }
    }
    // PAD=<file from mkpad.py>: the original's joypad replayed by engine tick from the level's start
    // (the port's first tick on the menu's level against the original's), through the default keys
    std::vector<int> pad, padKey; int oStart = -1, pStart = -1;
    std::vector<std::pair<int, std::string>> typed;        // (the original's tick, the text typed after it)
    if (const char* pf = std::getenv("PAD")) {
        FILE* f = std::fopen(pf, "r");
        char line[128];
        while (f && std::fgets(line, sizeof line, f)) {
            int a = 0, b = 0;
            char text[64];
            if (std::sscanf(line, "start %d", &a) == 1) { oStart = a; continue; }
            if (std::sscanf(line, "type %d %63s", &a, text) == 2) { typed.emplace_back(a, text); continue; }
            int c = -1;
            if (std::sscanf(line, "%d %d %d", &a, &b, &c) >= 2 && a >= 0) {
                if (size_t(a) >= pad.size()) { pad.resize(size_t(a) + 1, 0); padKey.resize(size_t(a) + 1, -1); }
                pad[size_t(a)] = b;
                padKey[size_t(a)] = c;
            }
        }
        if (f) std::fclose(f);
    }
    if (!pad.empty())
        g.joypadHook = [&](uint32_t gf) -> int {          // each engine tick's, the load's ticks too
            if (pStart < 0) return -1;
            if (g.demoPlaying()) return -1;                // (a demo plays its own joypad, which the original's shows)
            const long og = long(gf) - pStart + oStart;
            if (og >= 0 && size_t(og) < padKey.size() && padKey[size_t(og)] >= 0) g.startKey() = uint8_t(padKey[size_t(og)]);
            return og >= 0 && size_t(og) < pad.size() ? pad[size_t(og)] : 0;
        };
    // FRAMES: each of the original's pictures against the port's of the ticks it was on the screen
    constexpr int FW = 320, FH = 224, FN = FW * FH;
    struct Pred { int j, g0, g1, gl; };
    std::vector<Pred> preds;
    FILE* frames = nullptr; FILE* fout = nullptr;
    bool framesPiped = false;
    const char* ppm = std::getenv("FRAMEPPM");
    int winBefore = 4, winAfter = 0;
    if (const char* fw = std::getenv("FRAMEWIN")) std::sscanf(fw, "%d,%d", &winBefore, &winAfter);
    if (const char* fr = std::getenv("FRAMES")) {
        const std::string name(fr);
        if (name.size() > 3 && name.compare(name.size() - 3, 3, ".gz") == 0) {
            frames = popen(("gzip -dc '" + name + "'").c_str(), "r");
            framesPiped = true;
        } else frames = std::fopen(fr, "rb");
        if (FILE* pf = std::fopen(std::getenv("FRAMEPRED") ? std::getenv("FRAMEPRED") : "", "r")) {
            Pred p{};
            char line[128];
            while (std::fgets(line, sizeof line, pf))
                if (std::sscanf(line, "%d %d %d %d", &p.j, &p.g0, &p.g1, &p.gl) >= 3) preds.push_back(p);
            std::fclose(pf);
        }
        fout = std::fopen(std::getenv("FRAMEOUT") ? std::getenv("FRAMEOUT") : "frames_cmp.txt", "w");
    }
    struct PortFrame { int gf; int level; std::vector<uint32_t> rgb; };
    std::deque<PortFrame> ring;
    size_t nextPred = 0;
    int curJ = -1, lastDiff = 0, saved = 0;
    std::vector<uint8_t> raw(size_t(FN) * 3);
    std::vector<uint32_t> orig(static_cast<size_t>(FN));
    auto picture = [&](int j) {                          // (read forward: the predictions come in order)
        while (curJ < j) {
            if (std::fread(raw.data(), 1, raw.size(), frames) != raw.size()) return false;
            ++curJ;
        }
        for (int i = 0; i < FN; ++i)
            orig[size_t(i)] = uint32_t(raw[size_t(i) * 3]) << 16 | uint32_t(raw[size_t(i) * 3 + 1]) << 8 | raw[size_t(i) * 3 + 2];
        return curJ == j;
    };
    auto resolve = [&](bool all) {
        if (!frames) return;
        const int gfNow = int(g.state().globalFrame);
        while (nextPred < preds.size()) {
            const Pred p = preds[nextPred];
            const bool aligned = pStart >= 0 && oStart >= 0;
            const int pg0 = aligned ? p.g0 - oStart + pStart : -1, pg1 = aligned ? p.g1 - oStart + pStart : -1;
            if (!all && (!aligned || pg1 + winAfter >= gfNow)) break;
            ++nextPred;
            int best = -1, bd = FN + 1, lv = -1;
            const PortFrame* bf = nullptr;
            bool flat = false;                           // (the original's picture a load's screen)
            if (aligned && p.g0 >= oStart && picture(p.j)) {
                // one colour, or the load's dark red (palette entry 0, the font's) being cleared to black
                flat = std::all_of(orig.begin(), orig.end(), [&](uint32_t c) { return c == orig[0]; }) ||
                       std::all_of(orig.begin(), orig.end(), [&](uint32_t c) { return c == 0x430000u || c == 0; });
                const int pgl = p.gl - oStart + pStart;
                for (const PortFrame& f : ring) {
                    if (f.gf < pg0 - winBefore || f.gf > pg1 + winAfter) continue;
                    int d = 0;
                    for (int i = 0; i < FN && d <= bd; ++i) d += f.rgb[size_t(i)] != orig[size_t(i)];
                    // a tie (a still scene) goes to the tick nearest the one the count was last seen on
                    if (d < bd || (d == bd && std::abs(f.gf - pgl) <= std::abs(best - pgl))) { bd = d; best = f.gf; lv = f.level; bf = &f; }
                }
            }
            const int og = best < 0 ? -1 : best - pStart + oStart;
            std::fprintf(fout, "%d %d %d %d %d %d %d %d\n", p.j, p.g0, p.g1, p.gl, og, best < 0 ? -1 : bd, lv, int(flat));
            if (bf && bd > 0 && (lastDiff == 0 || std::getenv("FRAMEPPMALL")) && ppm && saved < 60) {
                char nm[512];
                std::snprintf(nm, sizeof nm, std::getenv("FRAMEPPMALL") ? "%s_%05d_%d.ppm" : "%s_%05d.ppm", ppm, p.j, og);
                if (FILE* o = std::fopen(nm, "wb")) {
                    std::fprintf(o, "P6 %d %d 255\n", FW * 3, FH);
                    auto put = [&](uint32_t c) { std::fputc(int(c >> 16), o); std::fputc(int(c >> 8 & 0xff), o); std::fputc(int(c & 0xff), o); };
                    for (int y = 0; y < FH; ++y) {
                        for (int x = 0; x < FW; ++x) put(orig[size_t(y * FW + x)]);
                        for (int x = 0; x < FW; ++x) put(bf->rgb[size_t(y * FW + x)]);
                        for (int x = 0; x < FW; ++x) put(orig[size_t(y * FW + x)] != bf->rgb[size_t(y * FW + x)] ? 0xff0000u : 0u);
                    }
                    std::fclose(o);
                    ++saved;
                }
            }
            if (best >= 0) lastDiff = bd;
        }
    };
    const bool noOut = std::getenv("NOOUT") != nullptr;
    for (int t = 0; t <= t1; ++t) {
        if (t == menuTick) g.menuStart(menuLevel);
        engine::HostInput hi;
        for (const auto& k : keys) if (t >= k[1] && (k[2] < 0 || t < k[2])) hi.keyDown[size_t(k[0])] = true;
        if (!pad.empty() && pStart < 0 && t > menuTick && menuTick >= 0 && g.levelNumber() == menuLevel) pStart = int(g.state().globalFrame);
        g.tick(hi);
        g.render();
        // a cheat typed between two of the original's ticks (its WM_CHAR, 0x439868) comes in after the
        // earlier one, which the sampler may well have seen after it (the lives' 99)
        for (auto& [at, text] : typed)
            if (pStart >= 0 && at >= 0 && long(g.state().globalFrame) - pStart + oStart >= at) {
                for (char c : text) g.typeChar(uint8_t(c));
                at = -1;
            }
        if (frames) {
            PortFrame f{int(g.state().globalFrame), g.levelNumber(), std::vector<uint32_t>(size_t(FN))};
            const uint8_t* src = g.framebuffer().viewBase(g.viewScrollX());
            uint32_t pal[256];
            for (int i = 0; i < 256; ++i) { const ph::Rgb c = g.palette().shown(i); pal[i] = uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | c.b; }
            for (int y = 0; y < FH; ++y) for (int x = 0; x < FW; ++x) f.rgb[size_t(y * FW + x)] = pal[src[size_t(y) * engine::kBufW + size_t(x)]];
            // every picture of the tick (a fade or a wait draws more than one with the engine standing
            // still): the original's screen may have held any of them
            if (ring.empty() || ring.back().gf != f.gf || ring.back().rgb != f.rgb) ring.push_back(std::move(f));
            while (!ring.empty() && ring.front().gf < int(g.state().globalFrame) - 24) ring.pop_front();
            resolve(false);
        }
        if (noOut) continue;
        std::printf("%d %d %u %d ", t, g.state().globalFrame, g.logicTicks(), g.levelNumber());
        for (int i = 0; i < 12; ++i) std::printf("%02x", *g.logicMemory().at(0x466772 + uint32_t(i)));
        std::printf(" ");
        uint8_t mem[game::Harry::kSize];
        std::memcpy(mem, g.logicMemory().mem, sizeof mem);
        {
            // the player's numbers at their addresses
            auto put = [&](uint32_t a, const void* v, size_t n) { std::memcpy(mem + (a - game::Harry::kBase), v, n); };
            const auto& st = g.stats();
            const int32_t score = st.score; put(0x46653b, &score, 4);
            const uint8_t b[] = {uint8_t(st.treasures), uint8_t(st.lives), uint8_t(st.ammoCount[0]), uint8_t(st.ammoCount[1]), uint8_t(st.ammoCount[2])};
            put(0x466526, b, 5);
            const uint8_t c = uint8_t(st.continues), w = uint8_t(st.weapon), l = uint8_t(st.letters);
            put(0x465ae3, &c, 1); put(0x46650b, &w, 1); put(0x465896, &l, 1);
            put(0x466510, g.levelFlags(), 0x16);              // the level-script flags (Game::levelFlags_)
        }
        for (uint32_t i = 0; i < game::Harry::kSize; ++i) std::printf("%02x", mem[i]);
        std::printf(" ");
        if (game::g_debugLogic)
            for (uint32_t a = game::kObjPoolAddr; a < game::kObjPoolAddr + 0x50 * game::kObjSize; ++a)
                std::printf("%02x", game::g_debugLogic->memByte(a));
        else                                                  // (no logic, as on the title: the records as the last left them)
            for (const auto& rec : g.objectRecords())
                for (uint8_t b : rec) std::printf("%02x", b);
        std::printf(" ");
        for (size_t i = 0; i < 0x9e0; ++i) std::printf("%02x", g.cutsceneData()[i]);
        std::printf(" ");
        for (uint8_t b : g.logicMemory().sound) std::printf("%02x", b);   // DAT_004627be .. c2
        if (std::getenv("CARTD5"))                    // d5 at the carts' weapon tests (an instrumented original's 0x4627b0)
            for (int i = 0; i < 4; ++i) std::printf(i ? "%02x" : " %02x", (game::g_debugCartD5 >> (8 * i)) & 0xff);
        std::printf("\n");
    }
    if (frames) {
        resolve(true);
        if (framesPiped) pclose(frames); else std::fclose(frames);
        std::fclose(fout);
    }
}
