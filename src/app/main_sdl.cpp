// SDL2 front end (portable).  Build with -DPITFALL_USE_SDL=ON.
//
// Same game loop as the Win32 front end: a logic tick every 17 ms, the 320x224 view
// converted through the palette into an RGB texture, sound mixed in the SDL
// audio callback.  Keyboard input is translated to the Windows virtual-key
// codes the engine's key bindings use.
#include "engine/framebuffer.h"
#include "engine/options.h"
#include "game/game.h"

#include <SDL.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

int vkFromScancode(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP: return 0x26; case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_LEFT: return 0x25; case SDL_SCANCODE_RIGHT: return 0x27;
    case SDL_SCANCODE_SPACE: return 0x20; case SDL_SCANCODE_RETURN: return 0x0d;
    case SDL_SCANCODE_ESCAPE: return 0x1b; case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x11;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_KP_1: return 0x61; case SDL_SCANCODE_KP_2: return 0x62; case SDL_SCANCODE_KP_3: return 0x63;
    default: break;
    }
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return '0';
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return 0x70 + (sc - SDL_SCANCODE_F1);
    return -1;
}

void audioCallback(void* userdata, Uint8* stream, int len) {
    auto* g = static_cast<game::Game*>(userdata);
    g->mixer().render(reinterpret_cast<int16_t*>(stream), len / 4);
}

} // namespace

int main(int argc, char** argv) {
    std::string dataDir = argc > 1 ? argv[1] : "original";
    int startLevel = -1;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "-level" && i + 1 < argc) startLevel = std::atoi(argv[++i]);

    engine::Options options;
    options.load("pitfall.ini");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    std::unique_ptr<game::Game> g;
    try { g = std::make_unique<game::Game>(dataDir, options); }
    catch (const std::exception& e) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pitfall", e.what(), nullptr); return 1; }
    if (startLevel >= 0) { g->setMode(game::Mode::Playing); g->startLevel(startLevel); }

    const int scale = options.displayMode == 1 ? 2 : 1;
    SDL_Window* win = SDL_CreateWindow("Pitfall: The Mayan Adventure", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       engine::kViewW * scale, engine::kViewH * scale,
                                       SDL_WINDOW_RESIZABLE | (options.displayMode >= 2 ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, engine::kViewW, engine::kViewH);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, engine::kViewW, engine::kViewH);

    SDL_AudioSpec want{}, have{};
    want.freq = engine::Mixer::kOutputRate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    want.callback = audioCallback; want.userdata = g.get();
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);
    SDL_Joystick* joy = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : nullptr;

    engine::HostInput host;
    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    double acc = 0;
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.scancode == SDL_SCANCODE_P || ev.key.keysym.scancode == SDL_SCANCODE_PAUSE) g->togglePause();
                if (ev.key.keysym.scancode == SDL_SCANCODE_RETURN && (ev.key.keysym.mod & KMOD_ALT)) {
                    const bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                if (ev.key.keysym.scancode >= SDL_SCANCODE_F1 && ev.key.keysym.scancode <= SDL_SCANCODE_F12 && (ev.key.keysym.mod & KMOD_SHIFT)) {
                    const int lv = ev.key.keysym.scancode - SDL_SCANCODE_F1;   // Shift+F1..F12: explorer level select
                    g->setMode(game::Mode::Playing); g->startLevel(lv);
                }
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        acc += double(now - last) / freq * (1000.0 / 17.0);    // the original's 17 ms timer
        last = now;
        if (acc > 6) acc = 6;
        bool ticked = false;
        while (acc >= 1.0) {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            host.keyDown.fill(false);
            for (int sc = 0; sc < SDL_NUM_SCANCODES; ++sc) {
                if (!keys[sc]) continue;
                const int vk = vkFromScancode(SDL_Scancode(sc));
                if (vk >= 0) host.keyDown[size_t(vk)] = true;
            }
            host.ctrlDown = host.keyDown[0x11];
            host.joystickPresent = joy != nullptr;
            if (joy) {
                host.joyButtons = 0;
                for (int b = 0; b < 4; ++b) if (SDL_JoystickGetButton(joy, b)) host.joyButtons |= 1u << b;
                host.joyX = int(SDL_JoystickGetAxis(joy, 0)) + 0x8000;
                host.joyY = int(SDL_JoystickGetAxis(joy, 1)) + 0x8000;
            }
            g->tick(host);
            acc -= 1.0; ticked = true;
        }
        if (ticked) {
            g->render();
            void* pixels; int pitch;
            SDL_LockTexture(tex, nullptr, &pixels, &pitch);
            const ph::Palette& pal = g->palette();
            uint32_t lut[256];
            for (int i = 0; i < 256; ++i) { const ph::Rgb c = pal.colour(i); lut[i] = 0xff000000u | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b; }
            const uint8_t* src = g->framebuffer().viewBase(g->viewScrollX());
            for (int y = 0; y < engine::kViewH; ++y) {
                auto* d = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + size_t(y) * pitch);
                for (int x = 0; x < engine::kViewW; ++x) d[x] = lut[src[size_t(y) * engine::kBufW + x]];
            }
            SDL_UnlockTexture(tex);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
        } else {
            SDL_Delay(1);
        }
        if (g->quitRequested()) running = false;
    }
    if (dev) SDL_CloseAudioDevice(dev);
    if (joy) SDL_JoystickClose(joy);
    options.save("pitfall.ini");
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
