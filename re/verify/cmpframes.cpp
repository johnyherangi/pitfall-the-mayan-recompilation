// cmpframes <capture.raw> <out.txt> <anchor frame> [ticks per frame] [ticks] [static colours]:
// the original's frames (capframes.sh: 320x224 RGB24, one after the other) against the port's.
// The port runs from the boot with a frame rendered after every tick; MENU=<level>@<step> picks a
// level from the menu before that step and KEYS=vk@from-to,... holds keys (Windows key codes, from
// one step to before another, -1: to the end).  The anchor frame is matched against every tick
// (ANCHOR_T=<tick> narrows it to 400 ticks around one); from there the frames forward and backward
// are matched near the tick their time predicts (ticks per frame: 58.82 / the capture's rate), the
// prediction following each good match, and a frame lost (a load: wall time without ticks) is
// searched for on the ticks ahead.  Writes "frame tick differing-pixels level" lines; the first
// frame of each run of inexact ones is saved as cmp_<frame>.ppm (the original, the port, the
// differing pixels).  In a demo the blinking "DEMONSTRATION" (the timer's count, wall time) is left
// out.  Build it as re/drivers' and run it from the repository root (it loads original/).
#include "game/game.h"
#include "game/play_logic.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

constexpr int W = 320, H = 224, N = W * H;

struct PortFrame { int level; bool demo; std::vector<uint8_t> idx; uint32_t pal[256]; };

int main(int argc, char** argv) {
    FILE* in = std::fopen(argv[1], "rb");
    if (!in) return 1;
    const int anchorJ = std::atoi(argv[3]);
    const double tpf = argc > 4 ? std::atof(argv[4]) : 58.82 / 30.0;
    const int maxTicks = argc > 5 ? std::atoi(argv[5]) : 4800;
    std::vector<std::vector<uint32_t>> orig;
    {
        std::vector<uint8_t> raw(size_t(N) * 3);
        while (std::fread(raw.data(), 1, raw.size(), in) == raw.size()) {
            std::vector<uint32_t> f(N);
            for (int i = 0; i < N; ++i) f[size_t(i)] = (uint32_t(raw[size_t(i) * 3]) << 16) | (uint32_t(raw[size_t(i) * 3 + 1]) << 8) | raw[size_t(i) * 3 + 2];
            orig.push_back(std::move(f));
        }
    }
    engine::Options opts;
    opts.soundEnabled = false;
    opts.slow = true;
    if (argc > 6) opts.staticColours = std::atoi(argv[6]);
    game::Game g("original", opts);
    engine::HostInput idle;
    std::vector<PortFrame> port(static_cast<size_t>(maxTicks));
    int menuLevel = -1, menuTick = -1;
    if (const char* m = std::getenv("MENU")) std::sscanf(m, "%d@%d", &menuLevel, &menuTick);
    // KEYS=vk@from-to,... (Windows virtual-key codes held from tick `from` to before `to`, -1: to the end)
    std::vector<std::array<int, 3>> keys;
    if (const char* ks = std::getenv("KEYS")) {
        std::string s(ks);
        size_t p = 0;
        while (p < s.size()) {
            size_t e = s.find(',', p); if (e == std::string::npos) e = s.size();
            std::array<int, 3> k{}; std::sscanf(s.substr(p, e - p).c_str(), "%i@%d-%d", &k[0], &k[1], &k[2]);
            keys.push_back(k); p = e + 1;
        }
    }
    for (int t = 0; t < maxTicks; ++t) {
        if (t == menuTick) g.menuStart(menuLevel);
        engine::HostInput in;
        for (const auto& k : keys) if (t >= k[1] && (k[2] < 0 || t < k[2])) in.keyDown[size_t(k[0])] = true;
        g.tick(in);
        g.render();
        PortFrame& f = port[size_t(t)];
        f.level = g.levelNumber();
        f.demo = g.demoPlaying();
        f.idx.resize(N);
        const uint8_t* src = g.framebuffer().viewBase(g.viewScrollX());
        for (int y = 0; y < H; ++y) std::memcpy(f.idx.data() + size_t(y) * W, src + size_t(y) * engine::kBufW, W);
        for (int i = 0; i < 256; ++i) {
            const ph::Rgb c = g.palette().shown(i);
            f.pal[i] = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
        }
    }
    // In a demo "DEMONSTRATION" blinks by the timer's count (DAT_00450fa8), wall time: left out.
    std::vector<uint8_t> demoMask(N, 0);
    for (int y = 14; y < 22; ++y) for (int x = 112; x < 216; ++x) demoMask[size_t(y * W + x)] = 1;
    auto diff = [&](int j, int t, int cap) {
        const PortFrame& f = port[size_t(t)];
        const std::vector<uint32_t>& o = orig[size_t(j)];
        int d = 0;
        for (int i = 0; i < N && d < cap; ++i) d += f.pal[f.idx[size_t(i)]] != o[size_t(i)] && !(f.demo && demoMask[size_t(i)]);
        return d;
    };
    const int nj = int(orig.size());
    std::vector<int> tick(size_t(nj), -1), dif(size_t(nj), N);
    // the anchor against every tick
    {
        int best = -1, bd = N + 1;
        int lo = 0, hi = maxTicks;
        if (const char* at = std::getenv("ANCHOR_T")) { const int c = std::atoi(at); lo = std::max(0, c - 400); hi = std::min(maxTicks, c + 400); }
        for (int t = lo; t < hi; ++t) { const int d = diff(anchorJ, t, bd); if (d < bd) { bd = d; best = t; } }
        tick[size_t(anchorJ)] = best; dif[size_t(anchorJ)] = bd;
        std::fprintf(stderr, "anchor %d: tick %d, %d pixels differ\n", anchorJ, best, bd);
    }
    auto track = [&](int from, int to, int step) {
        int lastJ = from, lastT = tick[size_t(from)];
        for (int j = from + step; j != to; j += step) {
            const int p = int(std::lround(lastT + (j - lastJ) * tpf));
            const int a = std::max(0, p - 40), b = std::min(maxTicks - 1, p + 40);
            int best = -1, bd = N + 1;
            // nearest to the prediction first, so a tie keeps the nearer tick
            for (int k = 0; k <= 40; ++k) {
                for (int s : {p + k, p - k}) {
                    if (s < a || s > b || (k == 0 && s != p)) continue;
                    if (step > 0 ? s < lastT : s > lastT) continue;
                    const int d = diff(j, s, bd);
                    if (d < bd) { bd = d; best = s; }
                }
                if (bd == 0) break;
            }
            if (bd >= N / 50 && step > 0) {
                // lost (a level load takes the original wall time without ticks): every tick ahead
                for (int s = lastT; s < std::min(maxTicks, lastT + 900); ++s) {
                    const int d = diff(j, s, bd);
                    if (d < bd) { bd = d; best = s; }
                }
            }
            tick[size_t(j)] = best; dif[size_t(j)] = bd;
            if (best >= 0 && bd < N / 50) { lastJ = j; lastT = best; }
        }
    };
    track(anchorJ, nj, 1);
    track(anchorJ, -1, -1);
    FILE* out = std::fopen(argv[2], "w");
    int exact = 0, saved = 0;
    for (int j = 0; j < nj; ++j) {
        const int t = tick[size_t(j)];
        std::fprintf(out, "%d %d %d %d\n", j, t, dif[size_t(j)], t >= 0 ? port[size_t(t)].level : -1);
        if (dif[size_t(j)] == 0) { ++exact; continue; }
        if (t < 0 || saved >= 60 || (j > 0 && dif[size_t(j - 1)] != 0)) continue;
        char name[64];
        std::snprintf(name, sizeof name, "cmp_%05d.ppm", j);
        FILE* p = std::fopen(name, "wb");
        std::fprintf(p, "P6 %d %d 255\n", W * 3, H);
        const PortFrame& f = port[size_t(t)];
        const std::vector<uint32_t>& o = orig[size_t(j)];
        auto put = [&](uint32_t c) { std::fputc(int(c >> 16), p); std::fputc(int(c >> 8 & 0xff), p); std::fputc(int(c & 0xff), p); };
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) put(o[size_t(y * W + x)]);
            for (int x = 0; x < W; ++x) put(f.pal[f.idx[size_t(y * W + x)]]);
            for (int x = 0; x < W; ++x) put(o[size_t(y * W + x)] != f.pal[f.idx[size_t(y * W + x)]] ? 0xff0000u : 0u);
        }
        std::fclose(p);
        ++saved;
    }
    std::fclose(out);
    std::fprintf(stderr, "%d frames, %d exact\n", nj, exact);
}
