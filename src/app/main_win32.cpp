// Win32 / GDI front end.
//
// Mirrors the original presentation path (FUN_0043c210 WinMain, FUN_004388c0
// WndProc, FUN_00443b90 present, FUN_0043e1c0 DIB setup):
//   * a 60 Hz logic tick driven by a multimedia timer (the original used
//     timeSetEvent(17 ms)); here the tick is paced in the message loop with
//     timeGetTime so no callback-thread locking is needed,
//   * the back buffer is copied to the window with BitBlt / StretchBlt,
//   * sound effects go through waveOut (the original used WAIL32 / Miles).
//
// Command line:  pitfall.exe [data-dir] [-level N]
// The data directory must contain INIT.PH and LEVEL00.PH .. LEVEL24.PH.
#include "engine/framebuffer.h"
#include "engine/options.h"
#include "game/game.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace {

constexpr wchar_t kClassName[] = L"Pitfall";                  // string resource #2
constexpr wchar_t kTitle[] = L"Pitfall: The Mayan Adventure"; // string resource #5

enum MenuIds : UINT {
    kMenuExit = 100, kMenuPause, kMenuWindow1x, kMenuWindow2x, kMenuFullscreen, kMenuSound,
    kMenuJoystick, kMenuFrameRate, kMenuAbout, kMenuLevelBase = 200,
};

struct App {
    HWND hwnd = nullptr;
    HMENU menu = nullptr;
    HDC backDc = nullptr;
    HBITMAP backBmp = nullptr;
    uint32_t* backPixels = nullptr;      // 320x224 BGRX
    std::unique_ptr<game::Game> game;
    engine::Options options;
    std::string iniPath;
    bool fullscreen = false;
    int scale = 1;
    uint32_t paletteCache[256]{};

    // audio
    HWAVEOUT waveOut = nullptr;
    std::thread audioThread;
    std::atomic<bool> audioRun{false};
};
App g_app;

std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    const size_t slash = s.find_last_of("\\/");
    return slash == std::string::npos ? "." : s.substr(0, slash);
}

bool fileExists(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string findDataDir(const std::string& hint) {
    const std::string candidates[] = {hint, exeDir(), exeDir() + "\\original", exeDir() + "\\ASSETS",
                                      exeDir() + "\\..\\original", exeDir() + "\\..\\..\\original", "."};
    for (const auto& c : candidates)
        if (!c.empty() && (fileExists(c + "\\INIT.PH") || fileExists(c + "\\init.ph"))) return c;
    return "";
}

// ------------------------------------------------------------------ audio --
void audioLoop() {
    constexpr int kFrames = 1024, kBuffers = 4;
    std::vector<int16_t> data(size_t(kFrames) * 2 * kBuffers);
    WAVEHDR hdr[kBuffers]{};
    for (int i = 0; i < kBuffers; ++i) {
        hdr[i].lpData = reinterpret_cast<LPSTR>(&data[size_t(i) * kFrames * 2]);
        hdr[i].dwBufferLength = kFrames * 4;
        waveOutPrepareHeader(g_app.waveOut, &hdr[i], sizeof(WAVEHDR));
        hdr[i].dwFlags |= WHDR_DONE;
    }
    int next = 0;
    while (g_app.audioRun) {
        WAVEHDR& h = hdr[next];
        if (!(h.dwFlags & WHDR_DONE)) { Sleep(2); continue; }
        g_app.game->mixer().render(reinterpret_cast<int16_t*>(h.lpData), kFrames);
        h.dwFlags &= ~WHDR_DONE;
        waveOutWrite(g_app.waveOut, &h, sizeof(WAVEHDR));
        next = (next + 1) % kBuffers;
    }
    waveOutReset(g_app.waveOut);
    for (auto& h : hdr) waveOutUnprepareHeader(g_app.waveOut, &h, sizeof(WAVEHDR));
}

void startAudio() {
    if (waveOutGetNumDevs() == 0) return;
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 2; fmt.nSamplesPerSec = engine::Mixer::kOutputRate;
    fmt.wBitsPerSample = 16; fmt.nBlockAlign = 4; fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * 4;
    if (waveOutOpen(&g_app.waveOut, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_app.waveOut = nullptr; return;
    }
    g_app.audioRun = true;
    g_app.audioThread = std::thread(audioLoop);
}

void stopAudio() {
    if (!g_app.waveOut) return;
    g_app.audioRun = false;
    if (g_app.audioThread.joinable()) g_app.audioThread.join();
    waveOutClose(g_app.waveOut);
    g_app.waveOut = nullptr;
}

// ------------------------------------------------------------------ video --
void createBackBuffer() {
    HDC dc = GetDC(g_app.hwnd);
    g_app.backDc = CreateCompatibleDC(dc);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = engine::kViewW;
    bmi.bmiHeader.biHeight = -engine::kViewH;      // top-down like the original DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    g_app.backBmp = CreateDIBSection(g_app.backDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    g_app.backPixels = static_cast<uint32_t*>(bits);
    SelectObject(g_app.backDc, g_app.backBmp);
    ReleaseDC(g_app.hwnd, dc);
}

void applyWindowSize() {
    if (g_app.fullscreen) {
        SetWindowLongPtr(g_app.hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_app.hwnd, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_FRAMECHANGED);
        SetMenu(g_app.hwnd, nullptr);
    } else {
        SetWindowLongPtr(g_app.hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetMenu(g_app.hwnd, g_app.menu);
        RECT r{0, 0, engine::kViewW * g_app.scale, engine::kViewH * g_app.scale};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
        SetWindowPos(g_app.hwnd, HWND_NOTOPMOST, g_app.options.windowTopX, g_app.options.windowTopY,
                     r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED);
    }
}

void present() {
    // FUN_00443b90: copy the 320x224 view out of the 448x256 back buffer.
    const game::Game& g = *g_app.game;
    const ph::Palette& pal = g.palette();
    for (int i = 0; i < 256; ++i) {
        const ph::Rgb c = pal.colour(i);
        g_app.paletteCache[i] = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
    }
    const uint8_t* src = g.framebuffer().viewBase(g.viewScrollX());
    for (int y = 0; y < engine::kViewH; ++y) {
        uint32_t* d = g_app.backPixels + size_t(y) * engine::kViewW;
        const uint8_t* s = src + size_t(y) * engine::kBufW;
        for (int x = 0; x < engine::kViewW; ++x) d[x] = g_app.paletteCache[s[x]];
    }
    HDC dc = GetDC(g_app.hwnd);
    RECT cr; GetClientRect(g_app.hwnd, &cr);
    const int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
    if (cw == engine::kViewW && ch == engine::kViewH) {
        BitBlt(dc, 0, 0, engine::kViewW, engine::kViewH, g_app.backDc, 0, 0, SRCCOPY);
    } else {
        // Keep the 320x224 aspect; letterbox with black.
        int dw = cw, dh = cw * engine::kViewH / engine::kViewW;
        if (dh > ch) { dh = ch; dw = ch * engine::kViewW / engine::kViewH; }
        const int ox = (cw - dw) / 2, oy = (ch - dh) / 2;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, ox, oy, dw, dh, g_app.backDc, 0, 0, engine::kViewW, engine::kViewH, SRCCOPY);
        if (ox) { RECT l{0, 0, ox, ch}, r{ox + dw, 0, cw, ch}; FillRect(dc, &l, (HBRUSH)GetStockObject(BLACK_BRUSH)); FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH)); }
        if (oy) { RECT t{0, 0, cw, oy}, b{0, oy + dh, cw, ch}; FillRect(dc, &t, (HBRUSH)GetStockObject(BLACK_BRUSH)); FillRect(dc, &b, (HBRUSH)GetStockObject(BLACK_BRUSH)); }
    }
    ReleaseDC(g_app.hwnd, dc);
}

// ------------------------------------------------------------------- menu --
HMENU buildMenu() {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, kMenuPause, L"&Pause\tP");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, kMenuExit, L"E&xit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&Game");
    HMENU opts = CreatePopupMenu();
    AppendMenuW(opts, MF_STRING, kMenuWindow1x, L"Standard Window (320x224)");
    AppendMenuW(opts, MF_STRING, kMenuWindow2x, L"Double Window (640x448)");
    AppendMenuW(opts, MF_STRING, kMenuFullscreen, L"Full Screen\tAlt+Enter");
    AppendMenuW(opts, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(opts, MF_STRING, kMenuSound, L"Sound Effects");
    AppendMenuW(opts, MF_STRING, kMenuJoystick, L"Joystick");
    AppendMenuW(opts, MF_STRING, kMenuFrameRate, L"Show Frame Counter");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)opts, L"&Options");
    HMENU lv = CreatePopupMenu();
    for (int i = 0; i < ph::kLevelCount; ++i) {
        wchar_t name[64];
        swprintf_s(name, 64, L"%d  %hs", i, ph::levelName(i));
        AppendMenuW(lv, MF_STRING, kMenuLevelBase + UINT(i), name);
    }
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)lv, L"&Level");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, kMenuAbout, L"&About Pitfall...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

void refreshMenuChecks() {
    HMENU m = GetMenu(g_app.hwnd);
    if (!m) return;
    CheckMenuItem(m, kMenuSound, g_app.options.soundEnabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, kMenuJoystick, g_app.options.joystick ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, kMenuFrameRate, g_app.options.frameRate ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, kMenuWindow1x, (!g_app.fullscreen && g_app.scale == 1) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, kMenuWindow2x, (!g_app.fullscreen && g_app.scale == 2) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, kMenuFullscreen, g_app.fullscreen ? MF_CHECKED : MF_UNCHECKED);
}

void setDisplayMode(int mode) {
    g_app.options.displayMode = mode;
    g_app.fullscreen = mode >= 2;
    g_app.scale = mode == 1 ? 2 : 1;
    applyWindowSize();
    refreshMenuChecks();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        const UINT id = LOWORD(wp);
        if (id == kMenuExit) { DestroyWindow(hwnd); return 0; }
        if (id == kMenuPause) { g_app.game->togglePause(); return 0; }
        if (id == kMenuWindow1x) { setDisplayMode(0); return 0; }
        if (id == kMenuWindow2x) { setDisplayMode(1); return 0; }
        if (id == kMenuFullscreen) { setDisplayMode(g_app.fullscreen ? 0 : 3); return 0; }
        if (id == kMenuSound) { g_app.options.soundEnabled = !g_app.options.soundEnabled; g_app.game->mixer().setEnabled(g_app.options.soundEnabled); refreshMenuChecks(); return 0; }
        if (id == kMenuJoystick) { g_app.options.joystick = !g_app.options.joystick; refreshMenuChecks(); return 0; }
        if (id == kMenuFrameRate) { g_app.options.frameRate = !g_app.options.frameRate; refreshMenuChecks(); return 0; }
        if (id == kMenuAbout) {
            MessageBoxW(hwnd, L"Pitfall: The Mayan Adventure\nReconstruction from PITFALL.EXE (Kinesoft / Activision, 1995).\n\n"
                              L"Arrows: move   W: jump   Q: whip   E: shoot   Space: select   P: pause\n"
                              L"Level menu: load any level in explorer mode.", kTitle, MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (id >= kMenuLevelBase && id < kMenuLevelBase + UINT(ph::kLevelCount)) {
            g_app.game->setMode(game::Mode::Playing);
            g_app.game->startLevel(int(id - kMenuLevelBase));
            return 0;
        }
        break;
    }
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN && (lp & (1 << 29))) { setDisplayMode(g_app.fullscreen ? 0 : 3); return 0; }
        break;
    case WM_KEYDOWN:
        if (wp == 'P' || wp == VK_PAUSE) { g_app.game->togglePause(); return 0; }
        if (wp == VK_ESCAPE && g_app.fullscreen) { setDisplayMode(0); return 0; }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
        if (g_app.game) present();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOVE:
        if (!g_app.fullscreen) { RECT r; GetWindowRect(hwnd, &r); g_app.options.windowTopX = r.left; g_app.options.windowTopY = r.top; }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void pollHost(engine::HostInput& host) {
    for (int vk = 0; vk < 256; ++vk) host.keyDown[size_t(vk)] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    host.ctrlDown = host.keyDown[VK_CONTROL];
    host.joystickPresent = false;
    if (g_app.options.joystick) {
        JOYINFO ji{};
        if (joyGetPos(JOYSTICKID1, &ji) == JOYERR_NOERROR) {      // FUN_00444940
            host.joystickPresent = true;
            host.joyButtons = ji.wButtons; host.joyX = int(ji.wXpos); host.joyY = int(ji.wYpos);
        }
    }
    if (GetForegroundWindow() != g_app.hwnd) host.keyDown.fill(false);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // FUN_0043c210: single instance.
    if (HWND other = FindWindowW(kClassName, kTitle)) { SetForegroundWindow(other); return 0; }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string hint; int startLevel = -1;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"-level" && i + 1 < argc) startLevel = _wtoi(argv[++i]);
        else { char buf[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, argv[i], -1, buf, MAX_PATH, nullptr, nullptr); hint = buf; }
    }
    LocalFree(argv);

    g_app.iniPath = exeDir() + "\\pitfall.ini";
    g_app.options.load(g_app.iniPath);
    if (startLevel >= 0) g_app.options.level = startLevel;

    const std::string dataDir = findDataDir(hint);
    if (dataDir.empty()) {
        MessageBoxW(nullptr, L"Could not find file INIT.PH.\nCopy the original game's *.PH files next to pitfall.exe "
                             L"or pass the data directory on the command line.", kTitle, MB_OK | MB_ICONSTOP);
        return 1;
    }

    WNDCLASSW wc{};
    wc.style = CS_BYTEALIGNCLIENT | CS_HREDRAW | CS_VREDRAW;     // 0x23 like the original
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, L"PITFALL_ICON");
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    if (!RegisterClassW(&wc)) return 0;

    g_app.hwnd = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPEDWINDOW, g_app.options.windowTopX, g_app.options.windowTopY,
                                 engine::kViewW, engine::kViewH, nullptr, (g_app.menu = buildMenu()), hInst, nullptr);
    createBackBuffer();

    try {
        g_app.game = std::make_unique<game::Game>(dataDir, g_app.options);
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Pitfall", MB_OK | MB_ICONSTOP);
        return 1;
    }
    if (startLevel >= 0) { g_app.game->setMode(game::Mode::Playing); g_app.game->startLevel(startLevel); }

    setDisplayMode(g_app.options.displayMode);
    ShowWindow(g_app.hwnd, SW_SHOW);
    UpdateWindow(g_app.hwnd);
    startAudio();

    // Main loop (FUN_00439fd0) with a 60 Hz logic tick.
    timeBeginPeriod(1);
    DWORD last = timeGetTime();
    double acc = 0;
    engine::HostInput host;
    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        const DWORD now = timeGetTime();
        acc += double(now - last) * 60.0 / 1000.0;
        last = now;
        if (acc > 6) acc = 6;                                      // don't spiral after a stall
        bool ticked = false;
        while (acc >= 1.0) { pollHost(host); g_app.game->tick(host); acc -= 1.0; ticked = true; }
        if (ticked) { g_app.game->render(); present(); }
        else Sleep(1);
        if (g_app.game->quitRequested()) running = false;
    }
    timeEndPeriod(1);
    stopAudio();
    g_app.options.save(g_app.iniPath);
    return 0;
}
