// pitstart.exe: the CD's launcher (PITSTART.EXE), rebuilt.
//
// Its dialogs, pictures and icon are the original's: they are read from PITSTART.EXE among the
// CD's files at run time (LoadLibraryEx with LOAD_LIBRARY_AS_DATAFILE), its sound is the CD's
// pitstart.wav and its intro the CD's intro.avi, played through MCIWnd as the original plays it (on
// a system that can still decode Indeo 3.2; elsewhere an intro.converted.avi beside it, the same
// film in a codec the system has, such as Cinepak, which Windows still ships:
//   ffmpeg -i INTRO.AVI -c:v cinepak -q:v 1 -c:a pcm_s16le intro.converted.avi
// and without that the intro's box stays black until a key, with MCIWnd's message, as the
// original's does).
// The CD's files are where pitfall.exe looks for its data (a folder given on the command line,
// pitstart.exe's own, or an "original" folder near it); like the original, which made "\assets"
// its current folder, it works in their ASSETS folder when there is one.
//
// What the port does differently: Play runs the port's pitfall.exe from pitstart.exe's folder,
// where the original copied PITFALL.EXE and WAIL32.DLL to the TEMP folder (0x401000) and ran them
// from there; the settings it checks and Uninstall removes are the port's pitfall.ini next to
// pitfall.exe, where the original's were HKLM\Software\Activision\Pitfall.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <vfw.h>

#include <string>

namespace {

HINSTANCE g_inst;              // DAT_00404104 in the original: its own module, where its resources were
HMODULE g_res;                 // PITSTART.EXE's resources
HWND g_hidden;                 // DAT_0040410c: the hidden "PitStart" window
HWND g_movie;                  // DAT_00404108: the MCIWnd of the intro
int g_more = 0;                // DAT_00406000: "More..." pressed (the logo goes, the other buttons show)
int g_picture = 0;             // DAT_00406004: 0 the title, 1 About Activision, 2 About Pitfall
int g_playing = 0;             // DAT_00406008: the intro is playing
int g_joystick = 0;            // DAT_0040600c: started by the joystick's button
int g_firstRun = 0;            // DAT_0040426c: no settings yet (the registration program runs first)
std::string g_game;            // pitfall.exe
std::string g_dataDir;         // the CD's files

constexpr char kTitle[] = "Pitfall";
constexpr UINT kStart = 0x6bc, kStartIntro = 0x720;

std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    const size_t slash = s.find_last_of("\\/");
    return slash == std::string::npos ? "." : s.substr(0, slash);
}

bool fileExists(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string settingsFile() { return exeDir() + "\\pitfall.ini"; }

// 0x401000, Play's preparation: the original copied PITFALL.EXE and WAIL32.DLL to the TEMP folder
// (or the system folder when TEMP was not there) and checked the copy; 0 when it can be run.
int prepareGame() {
    if (fileExists(g_game)) return 0;
    MessageBoxA(nullptr, "Cannot find PITFALL.EXE.", kTitle, MB_ICONEXCLAMATION);
    return 1;
}

// 0x4011f6: Uninstall.  The original deleted HKLM\Software\Activision\Pitfall (and
// Software\Activision when that was left empty), its copies in TEMP and WinHelp's files for
// PITFALL0.HLP; the port's settings are pitfall.ini.
void uninstall() {
    DeleteFileA(settingsFile().c_str());
    char win[MAX_PATH];
    GetWindowsDirectoryA(win, MAX_PATH);
    for (const char* f : {"\\Help\\pitfall0.gid", "\\Help\\pitfall0.ann", "\\Recent\\pitfall0.gid", "\\Recent\\pitfall0.hlp"}) {
        const std::string p = std::string(win) + f;
        SetFileAttributesA(p.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileA(p.c_str());
    }
    for (const char* f : {"pitfall0.gid", "pitfall0.ann"}) DeleteFileA(f);   // (WinHelp's index beside the file now)
    MessageBoxA(nullptr, "All Pitfall files and settings have been deleted from your system", kTitle, MB_OK);
}

bool run(const char* cmd, bool wait) {
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::string line = cmd;
    if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    if (wait) WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// 0x40115a: a palette of the DIB's colours (biClrUsed of them, else 1 << biBitCount; none for 24 bits).
HPALETTE dibPalette(const BITMAPINFOHEADER* bi) {
    WORD n = WORD(bi->biClrUsed);
    if (n == 0 && bi->biBitCount != 24) n = WORD(1u << bi->biBitCount);
    if (n == 0) return HPALETTE(GetStockObject(DEFAULT_PALETTE));
    LOGPALETTE* lp = static_cast<LOGPALETTE*>(LocalAlloc(LMEM_FIXED, sizeof(LOGPALETTE) + n * sizeof(PALETTEENTRY)));
    if (!lp) return HPALETTE(GetStockObject(DEFAULT_PALETTE));
    lp->palVersion = 0x300;
    lp->palNumEntries = n;
    const RGBQUAD* c = reinterpret_cast<const RGBQUAD*>(reinterpret_cast<const BYTE*>(bi) + bi->biSize);
    for (WORD i = 0; i < n; ++i) lp->palPalEntry[i] = PALETTEENTRY{c[i].rgbRed, c[i].rgbGreen, c[i].rgbBlue, 0};
    HPALETTE pal = CreatePalette(lp);
    LocalFree(lp);
    return pal;
}

// WM_PAINT of dialog 101 (0x401494): the picture (bitmap 102, 123 or 113, with its own palette)
// stretched over control 103, and until "More..." the logo (bitmap 109) over control 110 (placed
// from the window's corner, the frame's width off the control, as the original places it).
void paintMain(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT r;
    GetWindowRect(GetDlgItem(hwnd, 103), &r);
    POINT a{r.left, r.top}, b{r.right, r.bottom};
    ScreenToClient(hwnd, &a);
    ScreenToClient(hwnd, &b);
    static const WORD kPictures[3] = {102, 123, 113};
    HRSRC rs = FindResourceA(g_res, MAKEINTRESOURCEA(kPictures[g_picture % 3]), MAKEINTRESOURCEA(2));
    const BITMAPINFOHEADER* bi = rs ? static_cast<const BITMAPINFOHEADER*>(LockResource(LoadResource(g_res, rs))) : nullptr;
    if (bi) {
        // (the bits follow 1 << biBitCount colours: the original does not look at biClrUsed here)
        const DWORD colours = bi->biBitCount == 24 ? 0 : (1u << bi->biBitCount);
        const BYTE* bits = reinterpret_cast<const BYTE*>(bi) + bi->biSize + colours * sizeof(RGBQUAD);
        HPALETTE pal = dibPalette(bi);
        HPALETTE oldPal = SelectPalette(hdc, pal, FALSE);
        RealizePalette(hdc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bm = CreateCompatibleBitmap(hdc, bi->biWidth, bi->biHeight);
        SetDIBits(hdc, bm, 0, UINT(bi->biHeight), bits, reinterpret_cast<const BITMAPINFO*>(bi), DIB_RGB_COLORS);
        BITMAP info;
        GetObjectA(bm, sizeof info, &info);
        HGDIOBJ old = SelectObject(mem, bm);
        StretchBlt(hdc, a.x, a.y, b.x - a.x, b.y - a.y, mem, 0, 0, info.bmWidth, info.bmHeight, SRCCOPY);
        SelectPalette(hdc, oldPal, TRUE);
        SelectObject(mem, old);
        DeleteObject(pal);
        DeleteObject(bm);
        DeleteDC(mem);
    }
    if (!g_more) {
        RECT w, c;
        GetWindowRect(hwnd, &w);
        GetWindowRect(GetDlgItem(hwnd, 110), &c);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bm = LoadBitmapA(g_res, MAKEINTRESOURCEA(109));
        BITMAP info{};
        GetObjectA(bm, sizeof info, &info);
        HGDIOBJ old = SelectObject(mem, bm);
        StretchBlt(hdc, c.left - w.left, c.top - w.top, c.right - c.left, c.bottom - c.top, mem, 0, 0, info.bmWidth,
                   info.bmHeight, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bm);
        DeleteDC(mem);
    }
    EndPaint(hwnd, &ps);
}

void centre(HWND hwnd) {
    RECT w, d;
    GetWindowRect(hwnd, &w);
    GetClientRect(GetDesktopWindow(), &d);
    const int x = (d.right - d.left) / 2 - (w.right - w.left) / 2, y = (d.bottom - d.top) / 2 - (w.bottom - w.top) / 2;
    MoveWindow(hwnd, x, y, w.right - w.left, w.bottom - w.top, TRUE);
}

// 0x401420: dialog 101, the launcher's window.
INT_PTR CALLBACK mainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM) {
    auto show = [&](int id, int cmd) { ShowWindow(GetDlgItem(hwnd, id), cmd); };
    auto redraw = [&] { InvalidateRect(hwnd, nullptr, TRUE); UpdateWindow(hwnd); };
    switch (msg) {
    case WM_DESTROY:
        joyReleaseCapture(JOYSTICKID1);
        return TRUE;
    case WM_PAINT:
        paintMain(hwnd);
        return TRUE;
    case WM_CHAR:
        if (wp == VK_ESCAPE) EndDialog(hwnd, 0);
        return TRUE;
    case WM_INITDIALOG:
        g_firstRun = fileExists(settingsFile()) ? 0 : 1;
        if (g_firstRun) EnableWindow(GetDlgItem(hwnd, 1003), FALSE);   // nothing to uninstall
        joySetCapture(hwnd, JOYSTICKID1, 20, FALSE);
        centre(hwnd);
        UpdateWindow(hwnd);
        SetWindowTextA(hwnd, kTitle);
        sndPlaySoundA("pitstart.wav", SND_ASYNC);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case 1000:                                                     // More...
            g_more = 1;
            show(1000, SW_HIDE); show(1007, SW_HIDE);
            redraw();
            show(110, SW_HIDE);
            for (int id : {1002, 1001, 1009, 1004, 1003, 1008}) show(id, SW_SHOW);
            return TRUE;
        case 1001: {                                                   // Technical Help
            WIN32_FIND_DATAA fd;
            HANDLE f = FindFirstFileA("TECH.WRI", &fd);
            if (f == INVALID_HANDLE_VALUE) { MessageBoxA(nullptr, "Cannot find TECH.WRI file.", kTitle, MB_ICONEXCLAMATION); return TRUE; }
            FindClose(f);
            if (!run("write tech.wri", false)) MessageBoxA(nullptr, "Cannot find WRITE.EXE program.", kTitle, MB_ICONEXCLAMATION);
            return TRUE;
        }
        case 1002:                                                     // Online Registration
            g_firstRun = 0;
            if (!run("REGISTER.EXE", false)) MessageBoxA(nullptr, "Cannot find REGISTER.EXE program.", kTitle, MB_ICONEXCLAMATION);
            return TRUE;
        case 1003:                                                     // Uninstall Pitfall
            uninstall();
            EnableWindow(GetDlgItem(hwnd, 1003), FALSE);
            EndDialog(hwnd, 0);
            return TRUE;
        case 1004: g_picture = 1; redraw(); return TRUE;              // About Activision
        case 1009: g_picture = 2; redraw(); return TRUE;              // About Pitfall
        case 1006: EndDialog(hwnd, prepareGame() == 0 ? 1 : 0); return TRUE;   // Play
        case 1007: case 1008: EndDialog(hwnd, 0); return TRUE;         // Exit
        default: return FALSE;
        }
    case WM_QUERYNEWPALETTE:
        redraw();
        return TRUE;
    case WM_PALETTECHANGED:
        if (HWND(wp) != hwnd) redraw();
        return TRUE;
    case MM_JOY1BUTTONDOWN:
        g_joystick = 1;
        EndDialog(hwnd, prepareGame() == 0 ? 2 : 0);
        return TRUE;
    default:
        return FALSE;
    }
}

// 0x401c08: an MCIWnd child of `parent` playing nothing yet, `file` opened in it (0 when it cannot be).
HWND createMovie(HWND parent, int x, int y, int w, int h, const char* file, bool quiet = false) {
    // (0x4000110b: no MCIWNDF_NOERRORDLG, so a file it cannot play brings up MCIWnd's message)
    HWND mci = MCIWndCreateA(parent, g_inst, WS_CHILD | MCIWNDF_NOTIFYERROR | MCIWNDF_NOTIFYMODE | MCIWNDF_NOMENU |
                                                 MCIWNDF_NOPLAYBAR | MCIWNDF_NOAUTOSIZEWINDOW | (quiet ? MCIWNDF_NOERRORDLG : 0), nullptr);
    MoveWindow(mci, x, y, w, h, FALSE);
    if (SendMessageA(mci, MCIWNDM_OPENA, 0, LPARAM(file)) != 0) { DestroyWindow(mci); return nullptr; }
    RECT r;
    GetClientRect(parent, &r);
    SendMessageA(mci, MCIWNDM_PUT_DEST, 0, LPARAM(&r));
    SendMessageA(mci, MCIWNDM_REALIZE, TRUE, 0);
    ShowWindow(mci, SW_SHOW);
    return mci;
}

// 0x401c91: play (the dialog told when it ends) or stop the movie; pitstart.wav stops with the play.
void playMovie(HWND notify, HWND mci, bool play) {
    const MCIDEVICEID dev = MCIDEVICEID(LOWORD(SendMessageA(mci, MCIWNDM_GETDEVICEID, 0, 0)));
    MCI_PLAY_PARMS p{};
    if (play) {
        p.dwCallback = DWORD_PTR(notify);
        sndPlaySoundA(nullptr, SND_NOWAIT);
        mciSendCommandA(dev, MCI_PLAY, MCI_NOTIFY, DWORD_PTR(&p));
    } else {
        mciSendCommandA(dev, MCI_STOP, 0, DWORD_PTR(&p));
    }
}

// 0x401d15: dialog 114, the intro: intro.avi in control 124 until it ends, a key, a click of the
// hidden OK button or the joystick's button.
INT_PTR CALLBACK introProc(HWND hwnd, UINT msg, WPARAM, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        joySetCapture(hwnd, JOYSTICKID1, 20, FALSE);
        centre(hwnd);
        UpdateWindow(hwnd);
        SetWindowTextA(hwnd, kTitle);
        PostMessageA(hwnd, kStartIntro, 0, 0);
        return TRUE;
    case kStartIntro: {
        HWND box = GetDlgItem(hwnd, 124);
        RECT r;
        GetClientRect(box, &r);
        // The port: the CD's film first, without MCIWnd's message; where its Indeo 3.2 cannot be
        // decoded, a converted copy beside it; without that, the original's own try and message.
        g_movie = createMovie(box, r.left, r.top, r.right, r.bottom, "intro.avi", true);
        if (!g_movie && fileExists("intro.converted.avi")) g_movie = createMovie(box, r.left, r.top, r.right, r.bottom, "intro.converted.avi", true);
        if (!g_movie) g_movie = createMovie(box, r.left, r.top, r.right, r.bottom, "intro.avi");
        playMovie(hwnd, g_movie, true);                   // (with no movie it still stops pitstart.wav, as the original)
        g_playing = 1;
        return TRUE;
    }
    case MM_JOY1BUTTONDOWN:
        g_joystick = 1;
        [[fallthrough]];
    case WM_DESTROY: case WM_COMMAND: case MM_MCINOTIFY:
        if (g_playing) {
            playMovie(hwnd, g_movie, false);
            if (g_movie) SendMessageA(g_movie, WM_CLOSE, 0, 0);
        }
        joyReleaseCapture(JOYSTICKID1);
        EndDialog(hwnd, 1);
        return TRUE;
    default:
        return FALSE;
    }
}

// 0x401ed6: the hidden window.  The launcher's dialog, then on Play the intro (not when the
// joystick's button played), the registration program when there are no settings yet (not with
// the joystick either), and the game (" J" when the joystick started it).
LRESULT CALLBACK hiddenProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
    } else if (msg == kStart) {
        // (the CD's own PITSTART.EXE: dialog 101; the European pressing's launcher, in four
        // languages behind a chooser, has the English one as 127)
        const int mainDlg = FindResourceA(g_res, MAKEINTRESOURCEA(101), MAKEINTRESOURCEA(5)) ? 101 : 127;
        const INT_PTR r = DialogBoxParamA(g_res, MAKEINTRESOURCEA(mainDlg), hwnd, mainProc, 0);
        if (r == 1) DialogBoxParamA(g_res, MAKEINTRESOURCEA(114), hwnd, introProc, 0);
        if (r != 0) {
            if (g_firstRun && !g_joystick) run("REGISTER.EXE", true);
            std::string cmd = "\"" + g_game + "\" \"" + g_dataDir + "\"";
            if (g_joystick) cmd += " J";
            run(cmd.c_str(), false);
        }
        DestroyWindow(g_hidden);
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace

// 0x402052
int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    g_inst = inst;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string hint;
    bool uninstallArg = false;
    for (int i = 1; i < argc; ++i) {
        char buf[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, argv[i], -1, buf, MAX_PATH, nullptr, nullptr);
        if ((buf[0] == 'U' || buf[0] == 'u') && buf[1] == 0) uninstallArg = true;   // AUTORUN.INF's "pitstart.exe U"
        else hint = buf;
    }
    LocalFree(argv);
    const std::string candidates[] = {hint, exeDir(), exeDir() + "\\original", exeDir() + "\\..\\original",
                                      exeDir() + "\\..\\..\\original", "."};
    for (const auto& c : candidates)
        if (!c.empty() && fileExists(c + "\\PITSTART.EXE")) { g_dataDir = c; break; }
    g_game = exeDir() + "\\pitfall.exe";
    // (the original: SetCurrentDirectory("\\assets"), the CD's folder of the game's files)
    if (!g_dataDir.empty()) SetCurrentDirectoryA(fileExists(g_dataDir + "\\ASSETS") ? (g_dataDir + "\\ASSETS").c_str() : g_dataDir.c_str());
    if (uninstallArg) { uninstall(); return 0; }
    if (HWND w = FindWindowA("Pitfall", "Pitfall: The Mayan Adventure")) { SetForegroundWindow(w); return 0; }
    if (HWND w = FindWindowA("PitStart", kTitle)) { SetForegroundWindow(w); return 0; }
    if (!g_dataDir.empty()) g_res = LoadLibraryExA((g_dataDir + "\\PITSTART.EXE").c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!g_res) {
        MessageBoxA(nullptr, "Could not find PITSTART.EXE.\nCopy the original CD's files next to pitstart.exe "
                             "or pass their folder on the command line.", kTitle, MB_OK | MB_ICONSTOP);
        return 1;
    }
    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = hiddenProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconA(g_res, MAKEINTRESOURCEA(105));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = "PitStart";
    RegisterClassA(&wc);
    g_hidden = CreateWindowExA(0, "PitStart", kTitle, WS_OVERLAPPEDWINDOW, 10, 10, 50, 50, nullptr, nullptr, inst, nullptr);
    PostMessageA(g_hidden, kStart, 0, 0);
    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
