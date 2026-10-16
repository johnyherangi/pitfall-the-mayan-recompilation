// Win32 / GDI front end: the original's window and what goes with it.
//
//   FUN_0043c210  WinMain: the class with menu 214, the window, its status bar, the
//                 "Loading..." dialog while INIT.PH and the intro load, the timer
//   FUN_004388c0  WndProc: the keys (F1 help, Esc the quit dialog, Pause, F10 / Alt+Y), the
//                 pause when the window loses the focus or a menu opens, the status bar's owner
//                 draw and its icons, the window dragged by its game area
//   FUN_0043b920  the menu's commands (FUN_0043a370: a level, after "give up the current game?")
//   FUN_0043a320 / FUN_0043a1c0  the menu's levels on by Option_Level, the current one checked
//   FUN_00443b90  present: the 320x224 view, stretched to the window's game area
//
// The logic ticks every 17 ms (the original's timeSetEvent(17, 17, FUN_004448c0)), paced here
// in the message loop; sound effects go through waveOut (the original used WAIL32 / Miles).
// The dialogs and the menu are the original's (pitfall.rc); their pictures come from
// PITFALL.EXE in the data directory when it is there.
//
// Command line:  pitfall.exe [data-dir] [-level N] [J]
// The data directory must contain INIT.PH and LEVEL00.PH .. LEVEL24.PH.
#include "app/resource.h"
#include "app/win32_app.h"
#include "engine/framebuffer.h"

#include <commctrl.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comctl32.lib")
#endif

namespace app {

App g_app;

namespace {

constexpr wchar_t kClassName[] = L"Pitfall";                  // string resource #2
constexpr wchar_t kTitle[] = L"Pitfall: The Mayan Adventure"; // string resource #5
constexpr char kCaption[] = "Pitfall";
constexpr char kGiveUp[] = "Are you sure you want to\ngive up the current game?";   // string 0xe8
constexpr int kStatusId = 0x12d;
constexpr UINT kHideCursor = 0x658;                           // the original's private message

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
    g_app.soundDevice = true;
    g_app.audioRun = true;
    g_app.audioThread = std::thread(audioLoop);
    g_app.game->mixer().setDeviceClock(true);
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

int staticColours() {
    // FUN_0043e1c0's look at the display: a palette device (RC_PALETTE, or 256 colours) keeps its
    // static colours, NUMRESERVED / 2 at each end of the palette (DAT_00453184); any other display
    // none, and the game says so (string 0x11) and goes on with all 256 for its own.
    HDC dc = GetDC(g_app.hwnd);
    int n = 0;
    if ((GetDeviceCaps(dc, RASTERCAPS) & RC_PALETTE) || GetDeviceCaps(dc, SIZEPALETTE) == 0x100) {
        SetSystemPaletteUse(dc, SYSPAL_NOSTATIC);
        SetSystemPaletteUse(dc, SYSPAL_STATIC);
        if (GetSystemPaletteUse(dc) != SYSPAL_ERROR) {
            const int reserved = GetDeviceCaps(dc, NUMRESERVED);   // DAT_0045305c
            if (reserved != 0 && reserved != -1) n = reserved >> 1;
        }
    }
    ReleaseDC(g_app.hwnd, dc);
    if (n == 0)
        MessageBoxW(nullptr, L"Pitfall must be played in 256 color mode for optimum performance.", L"Pitfall", MB_OK | MB_ICONHAND);
    return n;
}

void present() {
    // FUN_00443b90: the 320x224 view out of the 448x256 back buffer, BitBlt at its own size or
    // StretchBlt to the game area (Window_SizeX x Window_SizeY); full screen keeps the view's
    // shape with black bars (the original switched the display mode).
    const game::Game& g = *g_app.game;
    const ph::Palette& pal = g.palette();
    for (int i = 0; i < 256; ++i) {
        const ph::Rgb c = pal.shown(i);                  // the LOGPALETTE / DIB colour table
        g_app.paletteCache[i] = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
    }
    const uint8_t* src = g.framebuffer().viewBase(g.viewScrollX());
    for (int y = 0; y < engine::kViewH; ++y) {
        uint32_t* d = g_app.backPixels + size_t(y) * engine::kViewW;
        const uint8_t* s = src + size_t(y) * engine::kBufW;
        for (int x = 0; x < engine::kViewW; ++x) d[x] = g_app.paletteCache[s[x]];
    }
    // FUN_00443c90's TextOutA ("PAUSED", the frame rate): the font WinMain made (20 px high, the
    // Roman family), red as the DIB's colours have it nearest, see-through.
    if (!g.hostTexts().empty()) {
        static HFONT font = CreateFontW(0x14, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                        CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FF_ROMAN, nullptr);
        int best = 0, bestD = 1 << 30;
        for (int i = 0; i < 256; ++i) {
            const ph::Rgb c = pal.shown(i);
            const int d = (c.r - 255) * (c.r - 255) + c.g * c.g + c.b * c.b;
            if (d < bestD) { bestD = d; best = i; }
        }
        const ph::Rgb red = pal.shown(best);
        HGDIOBJ old = SelectObject(g_app.backDc, font);
        SetTextColor(g_app.backDc, RGB(red.r, red.g, red.b));
        SetBkMode(g_app.backDc, TRANSPARENT);
        for (const auto& t : g.hostTexts()) TextOutA(g_app.backDc, t.x, t.y, t.text.c_str(), int(t.text.size()));
        SelectObject(g_app.backDc, old);
        GdiFlush();
    }
    HDC dc = GetDC(g_app.hwnd);
    if (!g_app.fullscreen) {
        const int w = g_app.options.windowSizeX, h = g_app.options.windowSizeY;
        if (w == engine::kViewW && h == engine::kViewH) {
            BitBlt(dc, 0, 0, engine::kViewW, engine::kViewH, g_app.backDc, 0, 0, SRCCOPY);
        } else {
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchBlt(dc, 0, 0, w, h, g_app.backDc, 0, 0, engine::kViewW, engine::kViewH, SRCCOPY);
        }
    } else {
        RECT cr; GetClientRect(g_app.hwnd, &cr);
        const int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
        int dw = cw, dh = cw * engine::kViewH / engine::kViewW;
        if (dh > ch) { dh = ch; dw = ch * engine::kViewW / engine::kViewH; }
        const int ox = (cw - dw) / 2, oy = (ch - dh) / 2;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, ox, oy, dw, dh, g_app.backDc, 0, 0, engine::kViewW, engine::kViewH, SRCCOPY);
        HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
        if (ox) { RECT l{0, 0, ox, ch}, r{ox + dw, 0, cw, ch}; FillRect(dc, &l, black); FillRect(dc, &r, black); }
        if (oy) { RECT t{0, 0, cw, oy}, b{0, oy + dh, cw, ch}; FillRect(dc, &t, black); FillRect(dc, &b, black); }
    }
    ReleaseDC(g_app.hwnd, dc);
}

// ------------------------------------------------------------- status bar --
void setStatusParts() {
    // FUN_00440b50: two parts, the text and the icons (the last 0x79 px).
    if (!g_app.status) return;
    RECT c; GetClientRect(g_app.hwnd, &c);
    int parts[2] = {c.right - 0x79, c.right};
    SendMessageW(g_app.status, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
}

void createStatusBar() {
    // FUN_00440830: CreateStatusWindowA(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, "", 0x12d), at
    // least 20 px high, both parts drawn by the window (the level's name, the bitmap 0x7e).
    g_app.status = CreateStatusWindowA(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, "", g_app.hwnd, kStatusId);
    if (!g_app.status) return;
    SendMessageW(g_app.status, SB_SETMINHEIGHT, 0x14, 0x14);
    SendMessageW(g_app.status, WM_SIZE, 0, 0);
    setStatusParts();
    SendMessageA(g_app.status, SB_SETTEXTA, 0 | SBT_OWNERDRAW, reinterpret_cast<LPARAM>(g_app.statusText));
    SendMessageA(g_app.status, SB_SETTEXTA, 1 | SBT_OWNERDRAW, 0x7e);
    RECT r; GetWindowRect(g_app.status, &r);
    g_app.statusH = r.bottom - r.top;                  // DAT_0046efd0
}

void drawStatusPart(const DRAWITEMSTRUCT& di) {
    // FUN_004409e0 (WM_DRAWITEM): part 0 DrawStatusTextA of the text ("\t" centres it), part 1
    // the icons (bitmap 0x7e) 5 px in, centred on the height.
    if (di.itemID == 0) {
        RECT r = di.rcItem;
        DrawStatusTextA(di.hDC, &r, reinterpret_cast<const char*>(di.itemData), 0);
        return;
    }
    if (di.itemID != 1 || !g_app.exe) return;
    HBITMAP bmp = static_cast<HBITMAP>(LoadImageW(g_app.exe, MAKEINTRESOURCEW(0x7e), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (!bmp) return;
    BITMAP bm; GetObjectW(bmp, sizeof bm, &bm);
    const int y = std::max(0, int((di.rcItem.bottom - di.rcItem.top - 0x10) / 2));
    HDC mem = CreateCompatibleDC(di.hDC);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(di.hDC, di.rcItem.left + 5, di.rcItem.top + y, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
}

void statusIconClick(POINT pt) {
    // FUN_004406e0 (WM_PARENTNOTIFY of a click): the icons open the property sheet at their page.
    if (!g_app.status) return;
    RECT sb; GetWindowRect(g_app.status, &sb);
    POINT tl{sb.left, sb.top}, br{sb.right, sb.bottom};
    ScreenToClient(g_app.hwnd, &tl); ScreenToClient(g_app.hwnd, &br);
    RECT c; GetClientRect(g_app.hwnd, &c);
    const RECT r{c.right - 0x79, tl.y, br.x, br.y};
    if (!PtInRect(&r, pt)) return;
    const int x = pt.x - r.left;
    int page = -1;
    if (x <= 0x15) page = 3;                           // the joystick
    else if (x <= 0x2c) page = 4;                      // the keyboard
    else if (x <= 0x41) page = 0;                      // the system
    else if (x <= 0x57) page = 1;                      // the sound
    else if (x <= 0x73) page = 2;                      // the difficulty
    if (page >= 0) PostMessageW(g_app.hwnd, WM_USER, WPARAM(page), 0);
}

void updateStatusText() {
    if (g_app.game->statusSerial() == g_app.statusSerial) return;
    g_app.statusSerial = g_app.game->statusSerial();
    std::snprintf(g_app.statusText, sizeof g_app.statusText, "%s", g_app.game->statusText().c_str());
    if (g_app.status) {
        SendMessageA(g_app.status, SB_SETTEXTA, 0 | SBT_OWNERDRAW, reinterpret_cast<LPARAM>(g_app.statusText));
        ShowWindow(g_app.status, g_app.fullscreen ? SW_HIDE : SW_SHOWNOACTIVATE);
    }
}

// ------------------------------------------------------------------- menu --
// FUN_0043a320's table at 0x46d338: the item of each level bit (0: none).
constexpr UINT kLevelItems[21] = {
    IDM_CEIBA, IDM_XIBALBA, IDM_TAZAMUL, IDM_LOSTCITY, IDM_COPANTEMPLE, IDM_LAKAMUL, IDM_YAXCHILAN,
    IDM_BALANKANCHE, IDM_TIKALRUINS, IDM_TIKALTEMPLE, IDM_WARRIOR, IDM_VAULT2, IDM_VAULT3, IDM_VAULT1,
    0, 0, IDM_PALENQUE, IDM_TOMB, IDM_JAINA, 0, IDM_ATARI,
};

void enableMenuLevels() {
    // FUN_0043a320
    uint32_t bits = g_app.game->menuUnlocks();
    for (UINT id : kLevelItems) {
        if (id) EnableMenuItem(g_app.menu, id, MF_BYCOMMAND | ((bits & 1) ? MF_ENABLED : (MF_GRAYED | MF_DISABLED)));
        bits >>= 1;
    }
}

void checkMenuLevel() {
    // FUN_0043a1c0: the last level's item unchecked, the new one's checked.
    const int last = g_app.checkedLevel, now = g_app.game->levelNumber();
    if (last >= 0 && last < 21 && kLevelItems[last]) CheckMenuItem(g_app.menu, kLevelItems[last], MF_BYCOMMAND | MF_UNCHECKED);
    if (now >= 0 && now < 21 && kLevelItems[now]) CheckMenuItem(g_app.menu, kLevelItems[now], MF_BYCOMMAND | MF_CHECKED);
    g_app.checkedLevel = now;
}

void updateMenu() {
    if (g_app.game->menuChanged()) enableMenuLevels();
    if (g_app.game->levelNumber() != g_app.checkedLevel) checkMenuLevel();
}

void startFromMenu(int level) {
    // FUN_0043a370 (and the items' own copies of it): a game with points is given up only on OK.
    game::Game& g = *g_app.game;
    if (g.menuNeedsConfirm() && MessageBoxA(g_app.hwnd, kGiveUp, kCaption, MB_OKCANCEL | MB_ICONHAND) != IDOK) {
        g.setPaused(false);
        return;
    }
    g.menuStart(level);
    updateMenu();
}

void winHelp(UINT cmd, ULONG_PTR data) {
    // F1 and the Help items: WinHelp on PITFALL0.HLP (the original's, in its directory).
    g_app.game->setPaused(true);
    const std::string file = g_app.dataDir + "\\PITFALL0.HLP";
    WinHelpA(g_app.hwnd, cmd == HELP_HELPONHELP ? nullptr : file.c_str(), cmd, data);
}

// ---------------------------------------------------------------- dialogs --
HBITMAP exeBitmap(int id) {
    return g_app.exe ? static_cast<HBITMAP>(LoadImageW(g_app.exe, MAKEINTRESOURCEW(id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION))
                     : nullptr;
}

// A picture of PITFALL.EXE at a dialog's control (the cheats' and the About's).
void paintPicture(HWND dlg, HDC dc, int bitmap, bool stretch) {
    HBITMAP bmp = exeBitmap(bitmap);
    if (!bmp) return;
    RECT r; GetWindowRect(GetDlgItem(dlg, IDC_PICTURE), &r);
    POINT tl{r.left, r.top}, br{r.right, r.bottom};
    ScreenToClient(dlg, &tl); ScreenToClient(dlg, &br);
    BITMAP bm; GetObjectW(bmp, sizeof bm, &bm);
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bmp);
    if (stretch) {
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, tl.x, tl.y, br.x - tl.x, br.y - tl.y, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    } else {
        BitBlt(dc, tl.x, tl.y, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
}

INT_PTR CALLBACK loadingProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    // 0x43ab40
    if (msg == WM_INITDIALOG) { centreOnGame(dlg); return TRUE; }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK) { EndDialog(dlg, 1); return TRUE; }
    return FALSE;
}

INT_PTR CALLBACK quitProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    // 0x43aa10: OK closes the window.
    if (msg == WM_INITDIALOG) { centreOnGame(dlg); return TRUE; }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDOK) PostMessageW(g_app.hwnd, WM_CLOSE, 0, 0);
        EndDialog(dlg, 1);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK aboutProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    // 0x43ad10: the picture (bitmap 0x7a) stretched into the frame.
    switch (msg) {
    case WM_INITDIALOG: centreOnGame(dlg); return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(dlg, &ps);
        paintPicture(dlg, dc, 0x7a, true);
        EndPaint(dlg, &ps);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) EndDialog(dlg, 1);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK cheatProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    // The cheats' dialogs (0x43a430 / 0x43a720): centred, the photograph (bitmap 0x80 / 0x81) at
    // the frame, OK ends them.
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        centreOnGame(dlg);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(dlg, &ps);
        paintPicture(dlg, dc, int(GetWindowLongPtrW(dlg, DWLP_USER)), false);
        EndPaint(dlg, &ps);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) EndDialog(dlg, 1);
        return TRUE;
    }
    return FALSE;
}

void showCheatDialog(int id) {
    // FUN_00444ee0(1), the dialog, FUN_00444ee0(0)
    g_app.game->setPaused(true);
    DialogBoxParamW(g_app.inst, MAKEINTRESOURCEW(id == 0x7f ? IDD_DOOM : IDD_TESTERS), g_app.hwnd, cheatProc,
                    id == 0x7f ? 0x80 : 0x81);
    g_app.game->setPaused(false);
}

void showModal(int id, DLGPROC proc) {
    g_app.game->setPaused(true);
    DialogBoxParamW(g_app.inst, MAKEINTRESOURCEW(id), g_app.hwnd, proc, 0);
    g_app.game->setPaused(false);
}

void loadingDialog(int phase) {
    // FUN_004460e0: the cleared view shown, then in a window (not in full screen) "Loading..." while
    // the level's files load, taken down when they are in.
    if (phase != 0) {
        g_app.busy = true;
        updateMenu();                                      // FUN_0043a1c0 and FUN_00440bc0 come first
        updateStatusText();
        present();
        if (phase != 1 || g_app.loading || g_app.fullscreen) return;
        g_app.loading = CreateDialogParamW(g_app.inst, MAKEINTRESOURCEW(IDD_LOADING), g_app.hwnd, loadingProc, 0);
        if (g_app.loading) RedrawWindow(g_app.loading, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    } else {
        if (g_app.loading) DestroyWindow(g_app.loading);
        g_app.loading = nullptr;
        g_app.busy = false;
    }
}

// ----------------------------------------------------------- the commands --
void command(UINT id) {
    // FUN_0043b920
    game::Game& g = *g_app.game;
    switch (id) {
    case IDM_NEWGAME:                                  // the intro; its credits at the normal speed
        g.setIntroAfterDemos(false);                   // (DAT_0046fb84 = 0 before the question)
        startFromMenu(game::kIntro);
        return;
    case IDM_PROPERTIES:
        g.setPaused(true);
        showPropertySheet(g_app.sheetPage);
        g.setPaused(false);
        return;
    case IDM_EXIT: PostMessageW(g_app.hwnd, WM_CLOSE, 0, 0); return;
    case IDM_HELP_CONTENTS: winHelp(HELP_CONTENTS, 0); return;
    case IDM_HELP_SEARCH: winHelp(HELP_PARTIALKEY, reinterpret_cast<ULONG_PTR>("")); return;
    case IDM_HELP_HOWTO: winHelp(HELP_HELPONHELP, 0); return;
    case IDM_ABOUT: showModal(IDD_ABOUT, aboutProc); return;
    case IDM_VAULT1: case IDM_VAULT2: case IDM_VAULT3: case IDM_ATARI:
        // DAT_00450fc8 = 1: no level to go back to; "mijmrowhtrae" makes the 2600 item level 24
        g.warp().returnLevel = 1;
        startFromMenu(id == IDM_VAULT1 ? game::kLoltunVault1 : id == IDM_VAULT2 ? game::kLoltunVault2 :
                      id == IDM_VAULT3 ? game::kLoltunVault3 : g.warp().cheat ? game::kEnding : game::kAtari2600Level);
        return;
    default:
        for (int level = 0; level < 21; ++level)
            if (kLevelItems[level] == id) { startFromMenu(level); return; }
    }
}

// ----------------------------------------------------------- the window --
void hostKeys(engine::HostInput& host) {
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
    host.padBits = GetForegroundWindow() == g_app.hwnd ? readGamepads(g_app.options.joystick, host.padPressed) : 0;
    if (!host.padBits) host.padPressed = false;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    game::Game* g = g_app.game.get();
    switch (msg) {
    case WM_SIZE:
        // FUN_00440b50 and the game area (DAT_00453068 / DAT_0045306c)
        if (!g_app.fullscreen && hwnd == g_app.hwnd && wp != SIZE_MINIMIZED) {
            if (g_app.status) { SendMessageW(g_app.status, WM_SIZE, 0, 0); setStatusParts(); }
            g_app.options.windowSizeX = LOWORD(lp);
            g_app.options.windowSizeY = std::max(0, int(HIWORD(lp)) - g_app.statusH);
        }
        break;
    case WM_WINDOWPOSCHANGING: {
        // the window's game area on a multiple of 8 px across (the original's ((x + 4) & ~7) - the
        // border, which a 4-px border makes this; with wider borders it would creep)
        auto* wpos = reinterpret_cast<WINDOWPOS*>(lp);
        if (!g_app.fullscreen && !(wpos->flags & SWP_NOMOVE) && !IsZoomed(hwnd) && !IsIconic(hwnd)) {
            RECT r{0, 0, 0, 0};
            AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
            wpos->x = ((wpos->x - r.left) & ~7) + r.left;
            return 0;
        }
        break;
    }
    case WM_NCHITTEST:
        // the game area drags the window as its caption does
        if (!g_app.fullscreen) {
            const LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
            return hit == HTCLIENT ? HTCAPTION : hit;
        }
        break;
    case WM_ACTIVATE:
        // losing the activation pauses (but while a level loads)
        if (LOWORD(wp) == WA_INACTIVE && reinterpret_cast<HWND>(lp) != hwnd && g && !g_app.busy) g->setPaused(true);
        break;
    case WM_KILLFOCUS:
        // the focus elsewhere: the window again (from the full screen), paused
        if (reinterpret_cast<HWND>(wp) != hwnd && g && !g_app.busy) {
            if (g_app.fullscreen) leaveFullscreen();
            g->setPaused(true);
        }
        break;
    case WM_ENTERMENULOOP:
        if (g) g->setPaused(true);
        return 0;
    case WM_SYSCOMMAND:
        // no screen saver nor monitor power-down while the game runs
        if ((wp & 0xfff0) == SC_SCREENSAVE || (wp & 0xfff0) == SC_MONITORPOWER) return 0;
        break;
    case WM_COMMAND:
        if (g && HIWORD(wp) == 0) { command(LOWORD(wp)); return 0; }
        break;
    case WM_USER:
        // the status bar's icons: the property sheet at their page
        if (!g_app.fullscreen && g) showPropertySheet(int(wp));
        return 0;
    case WM_PARENTNOTIFY:
        if (LOWORD(wp) == WM_LBUTTONDOWN && !g_app.fullscreen) statusIconClick(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;
    case WM_DRAWITEM:
        if (wp == kStatusId) { drawStatusPart(*reinterpret_cast<const DRAWITEMSTRUCT*>(lp)); return TRUE; }
        break;
    case WM_CHAR:
        // 0x439868 ..: the typed cheat codes
        if (g) g->typeChar(uint8_t(wp));
        return 0;
    case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSCHAR:
        // 0x439c6c: F10 (or Alt+Y, WM_SYSCHAR 'y') for Harry's special idle and the like; any
        // other in full screen goes back to the window.  (Alt+Enter: the port's full screen.)
        if (wp == 0x79) { if (g) g->specialKey(); return 0; }
        if (msg == WM_SYSKEYDOWN && wp == VK_RETURN) {
            if (g_app.fullscreen) leaveFullscreen(); else enterFullscreen();
            return 0;
        }
        if (g_app.fullscreen && msg == WM_SYSKEYDOWN) leaveFullscreen();
        break;
    case WM_KEYDOWN:
        // F1 help, Esc the quit dialog (full screen: the window), Pause (not its repeats); any
        // other key goes to the game (its start key, the end of a pause) and hides the cursor.
        if (!g) break;
        if (wp == VK_F1) { if (g_app.fullscreen) leaveFullscreen(); winHelp(HELP_CONTENTS, 0); return 0; }
        if (wp == VK_ESCAPE) {
            if (g_app.fullscreen) { leaveFullscreen(); return 0; }
            showModal(IDD_QUIT, quitProc);
            return 0;
        }
        if (wp == VK_PAUSE) { if (!(lp & 0x40000000)) g->togglePause(); return 0; }
        PostMessageW(hwnd, kHideCursor, 0, 0);
        return 0;
    case kHideCursor:
        SetCursor(nullptr);
        return 0;
    case MM_MCINOTIFY:
        // the disc's track at its end: the tracks again
        if (wp == MCI_NOTIFY_SUCCESSFUL && UINT(lp) == discDevice() && g) g->cdTrackEnded();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
        if (g) present();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        // FUN_004404a0 saves the options (the window's place among them)
        if (!g_app.fullscreen) {
            RECT r; GetWindowRect(hwnd, &r);
            g_app.options.windowTopX = r.left; g_app.options.windowTopY = r.top;
        }
        g_app.options.save(g_app.iniPath);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool joystickThere() {
    // FUN_0043a0e0
    if (joyGetNumDevs() == 0) return false;
    JOYCAPSW caps{};
    if (joyGetDevCapsW(JOYSTICKID1, &caps, sizeof caps) != JOYERR_NOERROR) return false;
    JOYINFO ji{};
    return joyGetPos(JOYSTICKID1, &ji) == JOYERR_NOERROR;
}

} // namespace

void centreOnGame(HWND dlg) {
    RECT d, c;
    GetWindowRect(dlg, &d);
    GetClientRect(g_app.hwnd, &c);
    POINT pt{(c.right - c.left) / 2, ((c.bottom - g_app.statusH) - c.top) / 2};
    ClientToScreen(g_app.hwnd, &pt);
    const int w = d.right - d.left, h = d.bottom - d.top;
    MoveWindow(dlg, pt.x - w / 2, pt.y - h / 2, w, h, TRUE);
}

void sizeWindow(int w, int h) {
    // FUN_0043a140: the window around a game area of w x h and the status bar; then the place
    // with the game area on a multiple of 8 px.
    RECT r{0, 0, w, h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
    RECT now; GetWindowRect(g_app.hwnd, &now);
    g_app.options.windowSizeX = w;
    g_app.options.windowSizeY = h;
    SetWindowPos(g_app.hwnd, nullptr, now.left, now.top, r.right - r.left, r.bottom - r.top + g_app.statusH,
                 SWP_NOZORDER | SWP_NOCOPYBITS);
}

void enterFullscreen() {
    // FUN_0043db40 (DISPDIB): here a borderless window over the whole screen, the view kept in
    // shape; the menu and the status bar go.
    if (g_app.fullscreen) return;
    GetWindowRect(g_app.hwnd, &g_app.windowRect);
    g_app.fullscreen = true;
    if (g_app.status) ShowWindow(g_app.status, SW_HIDE);
    SetMenu(g_app.hwnd, nullptr);
    SetWindowLongPtrW(g_app.hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(g_app.hwnd, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_FRAMECHANGED);
}

void leaveFullscreen() {
    // FUN_0043dd80
    if (!g_app.fullscreen) return;
    g_app.fullscreen = false;
    SetWindowLongPtrW(g_app.hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetMenu(g_app.hwnd, g_app.menu);
    const RECT& r = g_app.windowRect;
    SetWindowPos(g_app.hwnd, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED);
    if (g_app.status) { ShowWindow(g_app.status, SW_SHOWNOACTIVATE); SendMessageW(g_app.status, WM_SIZE, 0, 0); setStatusParts(); }
    InvalidateRect(g_app.hwnd, nullptr, FALSE);
}

void applyPriority() {
    // Option_Priority: normal, high, real-time
    static const DWORD kClass[3] = {NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS, REALTIME_PRIORITY_CLASS};
    SetPriorityClass(GetCurrentProcess(), kClass[std::clamp(g_app.options.priority, 0, 2)]);
}

} // namespace app

using namespace app;

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    g_app.inst = hInst;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string hint; int startLevel = -1; bool joyArg = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"-level" && i + 1 < argc) startLevel = _wtoi(argv[++i]);
        else if (a == L"J") joyArg = true;                       // the original's "J": the joystick on
        else { char buf[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, argv[i], -1, buf, MAX_PATH, nullptr, nullptr); hint = buf; }
    }
    LocalFree(argv);

    // FUN_004400f0: the options; the single instance.
    g_app.iniPath = exeDir() + "\\pitfall.ini";
    g_app.options.load(g_app.iniPath);
    if (joyArg) g_app.options.joystick = true;
    if (HWND other = FindWindowW(kClassName, kTitle)) { SetForegroundWindow(other); return 0; }

    const std::string dataDir = findDataDir(hint);
    g_app.dataDir = dataDir;
    if (dataDir.empty()) {
        MessageBoxW(nullptr, L"Could not find file INIT.PH.\nCopy the original game's *.PH files next to pitfall.exe "
                             L"or pass the data directory on the command line.", kTitle, MB_OK | MB_ICONSTOP);
        return 1;
    }
    g_app.exe = LoadLibraryExA((dataDir + "\\PITFALL.EXE").c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);

    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc{};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;                // 0x23 like the original
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, L"PITFALL_ICON");
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU);
    wc.lpszClassName = kClassName;
    if (!RegisterClassW(&wc)) return 0;

    const int sizeX = std::max(g_app.options.windowSizeX, 1), sizeY = std::max(g_app.options.windowSizeY, 1);
    g_app.hwnd = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPEDWINDOW, g_app.options.windowTopX, g_app.options.windowTopY,
                                 engine::kViewW, engine::kViewH, nullptr, nullptr, hInst, nullptr);
    g_app.menu = GetMenu(g_app.hwnd);
    createStatusBar();
    sizeWindow(sizeX, sizeY);
    ShowWindow(g_app.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_app.hwnd);
    createBackBuffer();
    g_app.options.staticColours = staticColours();                  // FUN_0043e1c0 (before INIT.PH's colours)
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);   // (the original turned the screen saver off)

    // "Loading..." while INIT.PH and the intro load (WinMain to FUN_004460e0's end).
    HWND boot = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_LOADING), nullptr, loadingProc, 0);
    if (boot) RedrawWindow(boot, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    g_app.busy = true;
    try {
        g_app.game = std::make_unique<game::Game>(dataDir, g_app.options);
    } catch (const std::exception& e) {
        if (boot) DestroyWindow(boot);
        MessageBoxA(nullptr, e.what(), "Pitfall", MB_OK | MB_ICONSTOP);
        return 1;
    }
    if (boot) DestroyWindow(boot);
    g_app.busy = false;
    g_app.game->loadingHook = loadingDialog;
    g_app.game->setHostText(true);
    if (startLevel >= 0) { g_app.game->setMode(game::Mode::Playing); g_app.game->startLevel(startLevel); }
    if (!g_app.options.slow)
        if (auto disc = openDisc(g_app.hwnd)) g_app.game->setCdPlayer(std::move(disc));   // the disc, when it is in
    g_app.joystickPresent = joystickThere();
    if (!g_app.joystickPresent) g_app.options.joystick = false;
    enableMenuLevels();
    updateMenu();
    updateStatusText();
    startAudio();
    applyPriority();
    PostMessageW(g_app.hwnd, kHideCursor, 0, 0);
    SetForegroundWindow(g_app.hwnd);

    // FUN_00439fd0's loop, with a logic tick every 17 ms (58.8 Hz).
    timeBeginPeriod(1);
    DWORD last = timeGetTime();
    double acc = 0;
    engine::HostInput host;
    MSG msg;
    bool running = true, quitting = false;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        const DWORD now = timeGetTime();
        acc += double(now - last) / 17.0;
        last = now;
        if (acc > 6) acc = 6;                                      // don't spiral after a stall
        bool ticked = false;
        while (acc >= 1.0) { hostKeys(host); g_app.game->tick(host); acc -= 1.0; ticked = true; }
        if (ticked) {
            g_app.game->render();
            present();
            updateMenu();
            updateStatusText();
        } else {
            Sleep(1);
        }
        if (const int d = g_app.game->takeCheatDialog()) showCheatDialog(d);
        if (g_app.game->quitRequested() && !quitting) { quitting = true; PostMessageW(g_app.hwnd, WM_CLOSE, 0, 0); }
    }
    timeEndPeriod(1);
    stopAudio();
    g_app.game.reset();                                            // (FUN_004420e0: the disc closed)
    SetThreadExecutionState(ES_CONTINUOUS);
    if (g_app.exe) FreeLibrary(g_app.exe);
    return 0;
}
