// The options' property sheet (FUN_00440c00) and its five pages, as the original's:
//
//   page 0  System      0x441800    the screen size, "Sync Full Screen to Monitor", the priority
//   page 1  Sound       0x4411e0    sound effects, CD music / ambient sounds, the two volumes
//   page 2  Difficulty  0x441710    normal / hard
//   page 3  JoyStick    FUN_0043b4f0  the buttons (a button pressed, or a key, in an action's box)
//   page 4  Keyboard    FUN_0043b0f0  the keys (a key pressed in an action's box)
//
// The sheet works on copies of the options (DAT_00451a30 .. DAT_00451a58): a page writes them as
// it is left (PSN_KILLACTIVE) and takes them back on Cancel (PSN_RESET); the keys and buttons are
// changed as they are pressed and put back on Cancel.  When the sheet closes the copies are
// applied, a change of difficulty with a game going on only after "give up the current game?".
#include "app/resource.h"
#include "app/win32_app.h"

#include <commctrl.h>
#include <mmsystem.h>
#include <prsht.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace app {
namespace {

constexpr char kCaption[] = "Pitfall";
constexpr char kGiveUp[] = "Are you sure you want to\ngive up the current game?";     // string 0xe8
constexpr char kDuplicate[] = "Cannot map the same key to more than one action!";      // string 0xde
// The screen sizes (0x46efe8): "Current Settings" leaves the window as it is.
const char* const kScreens[5] = {"Current Settings", "Standard Window (320x224)", "Double Window (640x448)",
                                 "Full Screen (Fast - 320x200)", "Full Screen (Smooth - 320x224)"};

// DAT_00451a30 .. DAT_00451a58
struct Copies {
    bool cdMusic = false, cdAmbient = false, sound = false;
    int cdVolume = 0, wavVolume = 0;
    bool joystick = false, smooth = false;
    int priority = 0, difficulty = 0, screen = 0;
} c;

// The tables at 0x46d058 (the keys) and 0x46d238 (the buttons): an action's name as shown, its
// value, and both as they were when the page opened.
struct Entry {
    std::string name, savedName;
    int value = 0, savedValue = 0;
    int control = 0;
};
Entry keys[8];          // Up, Down, Left, Right, Jump, Shoot, Whip, Select
Entry buttons[4];       // Jump, Shoot, Whip, Select
HWND keyPage = nullptr, joyPage = nullptr;   // DAT_004531d0
WNDPROC editProc = nullptr;                  // DAT_004531d4

constexpr int kKeyControls[8] = {IDC_KEY_UP, IDC_KEY_DOWN, IDC_KEY_LEFT, IDC_KEY_RIGHT,
                                 IDC_KEY_JUMP, IDC_KEY_SHOOT, IDC_KEY_WHIP, IDC_KEY_SELECT};
constexpr int kJoyControls[4] = {IDC_JOY_JUMP, IDC_JOY_SHOOT, IDC_JOY_WHIP, IDC_JOY_SELECT};

std::string keyName(int value) {
    // GetKeyNameTextA of the value's scan code (an older value of only the key code: its scan code).
    LONG lp = LONG(uint32_t(value) & 0xffff0000u);
    if (lp == 0 && (value & 0xff)) lp = LONG(MapVirtualKeyW(UINT(value & 0xff), MAPVK_VK_TO_VSC) << 16);
    char buf[16] = "";
    GetKeyNameTextA(lp, buf, 15);
    return buf;
}

std::string buttonName(int value) {
    switch (value) {
    case 1: return "Button 1";
    case 2: return "Button 2";
    case 4: return "Button 3";
    case 8: return "Button 4";
    default: return value >= 0x10 ? keyName(value) : std::string();
    }
}

bool duplicate(const Entry* e, int n) {
    // the same key twice (its scan code, extended bit and key code)
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (((uint32_t(e[i].value) ^ uint32_t(e[j].value)) & 0x1ff00ffu) == 0) return true;
    return false;
}

void setResult(HWND page, LONG_PTR r) { SetWindowLongPtrW(page, DWLP_MSGRESULT, r); }

void setPageIcon(HWND page, int control, int icon) {
    // the page's picture (icons 207 .. 210 of PITFALL.EXE)
    if (!g_app.exe) return;
    if (HICON h = static_cast<HICON>(LoadImageW(g_app.exe, MAKEINTRESOURCEW(icon), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)))
        SendDlgItemMessageW(page, control, STM_SETICON, reinterpret_cast<WPARAM>(h), 0);
}

void paintRamp(HWND page, HDC dc, int control) {
    // the slope beside a slider (bitmap 0xd4 of PITFALL.EXE) stretched over its hidden frame
    if (!g_app.exe) return;
    HBITMAP bmp = static_cast<HBITMAP>(LoadImageW(g_app.exe, MAKEINTRESOURCEW(0xd4), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (!bmp) return;
    RECT pr, r;
    GetWindowRect(page, &pr);
    GetWindowRect(GetDlgItem(page, control), &r);
    HDC mem = CreateCompatibleDC(dc);
    BITMAP bm; GetObjectW(bmp, sizeof bm, &bm);
    HGDIOBJ old = SelectObject(mem, bmp);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchBlt(dc, r.left - pr.left, r.top - pr.top, r.right - r.left, r.bottom - r.top, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
}

// ------------------------------------------------------------------ System --
INT_PTR CALLBACK systemProc(HWND page, UINT msg, WPARAM, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        setPageIcon(page, IDC_ICON_SYSTEM, 207);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(page, &ps);
        paintRamp(page, dc, IDC_RAMP_PRIORITY);
        EndPaint(page, &ps);
        return FALSE;
    }
    case WM_NOTIFY: {
        const NMHDR* n = reinterpret_cast<const NMHDR*>(lp);
        if (n->code == UINT(PSN_RESET)) {
            c.smooth = g_app.options.smooth;
            c.screen = 0;                                // DAT_0046efdc, never set
            c.priority = g_app.options.priority;
            setResult(page, TRUE);
            return FALSE;
        }
        if (n->code == UINT(PSN_KILLACTIVE)) {
            // (Windows 95's path: the slider's top is real-time)
            c.priority = 2 - int(SendDlgItemMessageW(page, IDC_PRIORITY, TBM_GETPOS, 0, 0));
            c.smooth = IsDlgButtonChecked(page, IDC_SYNC) != 0;
            c.screen = int(SendDlgItemMessageW(page, IDC_SCREENSIZE, CB_GETCURSEL, 0, 0));
            setResult(page, FALSE);
            return TRUE;
        }
        if (n->code == UINT(PSN_SETACTIVE)) {
            SendDlgItemMessageW(page, IDC_PRIORITY, TBM_SETRANGE, TRUE, MAKELONG(0, 2));
            SendDlgItemMessageW(page, IDC_PRIORITY, TBM_SETPOS, TRUE, 2 - c.priority);
            CheckDlgButton(page, IDC_SYNC, c.smooth ? BST_CHECKED : BST_UNCHECKED);
            SendDlgItemMessageW(page, IDC_SCREENSIZE, CB_RESETCONTENT, 0, 0);
            for (const char* s : kScreens) SendDlgItemMessageA(page, IDC_SCREENSIZE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            SendDlgItemMessageW(page, IDC_SCREENSIZE, CB_SETCURSEL, WPARAM(c.screen), 0);
            setResult(page, 0);
            return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

// ------------------------------------------------------------------- Sound --
INT_PTR CALLBACK soundProc(HWND page, UINT msg, WPARAM wp, LPARAM lp) {
    game::Game& g = *g_app.game;
    switch (msg) {
    case WM_INITDIALOG:
        setPageIcon(page, IDC_ICON_SOUND, 208);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(page, &ps);
        paintRamp(page, dc, IDC_RAMP_WAV);
        paintRamp(page, dc, IDC_RAMP_CD);
        EndPaint(page, &ps);
        return FALSE;
    }
    case WM_COMMAND:
        // the CD's music and its ambient sounds go one at a time
        if (LOWORD(wp) == IDC_CDMUSIC) { CheckDlgButton(page, IDC_CDAMBIENT, BST_UNCHECKED); c.cdAmbient = false; }
        if (LOWORD(wp) == IDC_CDAMBIENT) { CheckDlgButton(page, IDC_CDMUSIC, BST_UNCHECKED); c.cdMusic = false; }
        return FALSE;
    case WM_VSCROLL: {
        // a slider dragged: the volume at once (the samples' with sound 0x19 to hear it)
        const int code = LOWORD(wp);
        if (code != TB_THUMBPOSITION && code != TB_THUMBTRACK) return FALSE;
        const int pos = short(HIWORD(wp));
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
        if (id == IDC_WAVVOLUME) { c.wavVolume = 100 - pos; g.setWavVolume(c.wavVolume); g.playChannel(0, 0x19); }
        if (id == IDC_CDVOLUME) { c.cdVolume = 100 - pos; g.setCdVolume(c.cdVolume); }
        return FALSE;
    }
    case WM_NOTIFY: {
        const NMHDR* n = reinterpret_cast<const NMHDR*>(lp);
        if (n->code == UINT(PSN_RESET)) {
            c.cdMusic = g_app.options.cdMusic; c.cdAmbient = g_app.options.cdAmbient;
            c.sound = g_app.options.soundEnabled;
            c.cdVolume = g_app.options.cdVolume; c.wavVolume = g_app.options.wavVolume;
            setResult(page, TRUE);
            return FALSE;
        }
        if (n->code == UINT(PSN_KILLACTIVE)) {
            c.cdMusic = IsDlgButtonChecked(page, IDC_CDMUSIC) != 0;
            c.cdAmbient = IsDlgButtonChecked(page, IDC_CDAMBIENT) != 0;
            c.sound = IsDlgButtonChecked(page, IDC_SOUNDFX) != 0;
            c.wavVolume = 100 - int(SendDlgItemMessageW(page, IDC_WAVVOLUME, TBM_GETPOS, 0, 0));
            g.setWavVolume(c.wavVolume);
            c.cdVolume = 100 - int(SendDlgItemMessageW(page, IDC_CDVOLUME, TBM_GETPOS, 0, 0));
            g.setCdVolume(c.cdVolume);
            setResult(page, FALSE);
            return TRUE;
        }
        if (n->code == UINT(PSN_SETACTIVE)) {
            SendDlgItemMessageW(page, IDC_WAVVOLUME, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
            SendDlgItemMessageW(page, IDC_WAVVOLUME, TBM_SETPOS, TRUE, 100 - c.wavVolume);
            SendDlgItemMessageW(page, IDC_CDVOLUME, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
            SendDlgItemMessageW(page, IDC_CDVOLUME, TBM_SETPOS, TRUE, 100 - c.cdVolume);
            if (!g.cdAvailable()) {                       // no disc (here: none of its tracks ripped)
                EnableWindow(GetDlgItem(page, IDC_CDVOLUME), FALSE);
                EnableWindow(GetDlgItem(page, IDC_CDMUSIC), FALSE);
                EnableWindow(GetDlgItem(page, IDC_CDAMBIENT), FALSE);
            } else {
                CheckDlgButton(page, IDC_CDMUSIC, c.cdMusic ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(page, IDC_CDAMBIENT, c.cdAmbient ? BST_CHECKED : BST_UNCHECKED);
            }
            if (!g_app.soundDevice) {                     // no sound card
                EnableWindow(GetDlgItem(page, IDC_WAVVOLUME), FALSE);
                EnableWindow(GetDlgItem(page, IDC_SOUNDFX), FALSE);
            } else {
                CheckDlgButton(page, IDC_SOUNDFX, c.sound ? BST_CHECKED : BST_UNCHECKED);
            }
            setResult(page, 0);
            return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

// -------------------------------------------------------------- Difficulty --
INT_PTR CALLBACK difficultyProc(HWND page, UINT msg, WPARAM, LPARAM lp) {
    if (msg != WM_NOTIFY) return msg == WM_INITDIALOG;
    const NMHDR* n = reinterpret_cast<const NMHDR*>(lp);
    if (n->code == UINT(PSN_RESET)) { c.difficulty = g_app.options.difficulty; setResult(page, TRUE); return FALSE; }
    if (n->code == UINT(PSN_KILLACTIVE)) { c.difficulty = IsDlgButtonChecked(page, IDC_DIFF_HARD) != 0; setResult(page, FALSE); return TRUE; }
    if (n->code == UINT(PSN_SETACTIVE)) {
        CheckRadioButton(page, IDC_DIFF_NORMAL, IDC_DIFF_HARD, c.difficulty ? IDC_DIFF_HARD : IDC_DIFF_NORMAL);
        setResult(page, 0);
        return TRUE;
    }
    return FALSE;
}

// ------------------------------------------------- Keyboard and JoyStick --
// 0x43b000 / 0x43b400: an action's box takes the key pressed in it (but Tab, Alt, Esc, Shift,
// Ctrl, Pause, F1, F10 and Insert, which do what they do), and swallows the characters.
LRESULT CALLBACK actionEditProc(HWND edit, UINT msg, WPARAM wp, LPARAM lp) {
    const int idx = int(GetWindowLongPtrW(edit, GWLP_USERDATA));
    const bool joy = GetParent(edit) == joyPage;
    if (msg == WM_CHAR) return 0;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        switch (wp) {
        case VK_TAB: case VK_MENU: case VK_ESCAPE: case VK_SHIFT: case VK_CONTROL: case VK_PAUSE:
        case VK_F1: case VK_F10: case VK_INSERT:
            break;
        default: {
            const int value = int((uint32_t(lp) & 0xffffff00u) + uint32_t(wp));
            Entry& e = joy ? buttons[idx] : keys[idx];
            e.name = keyName(value);
            e.value = value;
            if (joy) g_app.options.keys.joyButton[size_t(idx)] = value;
            else g_app.options.keys.key[size_t(idx)] = value;
            SendDlgItemMessageA(GetParent(edit), e.control, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(e.name.c_str()));
            return 0;
        }
        }
    }
    return CallWindowProcW(editProc, edit, msg, wp, lp);
}

void initEntries(HWND page, Entry* e, int n, const int* controls, bool joy) {
    // WM_INITDIALOG: each action's name and value kept as they are, its box shows the name and
    // takes the keys (GWL_USERDATA its index).
    for (int i = 0; i < n; ++i) {
        const int value = joy ? g_app.options.keys.joyButton[size_t(i)] : g_app.options.keys.key[size_t(i)];
        e[i].value = e[i].savedValue = value;
        e[i].name = e[i].savedName = joy ? buttonName(value) : keyName(value);
        e[i].control = controls[i];
        HWND edit = GetDlgItem(page, controls[i]);
        SendMessageA(edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(e[i].name.c_str()));
        editProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(actionEditProc)));
        SetWindowLongPtrW(edit, GWLP_USERDATA, i);
    }
}

void resetEntries(Entry* e, int n, bool joy) {
    for (int i = 0; i < n; ++i) {
        e[i].name = e[i].savedName;
        e[i].value = e[i].savedValue;
        if (joy) g_app.options.keys.joyButton[size_t(i)] = e[i].value;
        else g_app.options.keys.key[size_t(i)] = e[i].value;
    }
}

INT_PTR CALLBACK keyboardProc(HWND page, UINT msg, WPARAM, LPARAM lp) {
    keyPage = page;
    if (msg == WM_INITDIALOG) {
        initEntries(page, keys, 8, kKeyControls, false);
        setPageIcon(page, IDC_ICON_KEYBOARD, 210);
        return TRUE;
    }
    if (msg != WM_NOTIFY) return FALSE;
    const NMHDR* n = reinterpret_cast<const NMHDR*>(lp);
    if (n->code == UINT(PSN_RESET)) {
        resetEntries(keys, 8, false);
        if (g_app.joystickPresent) c.joystick = g_app.options.joystick;
        setResult(page, TRUE);
        return FALSE;
    }
    if (n->code == UINT(PSN_KILLACTIVE)) {
        // "Enable Keyboard" is the joystick off; the same key twice may not leave the page
        if (g_app.joystickPresent) c.joystick = IsDlgButtonChecked(page, IDC_ENABLE_KEYBOARD) == 0;
        if (duplicate(keys, 8)) {
            MessageBoxA(page, kDuplicate, kCaption, MB_ICONEXCLAMATION);
            setResult(page, TRUE);
        } else {
            setResult(page, FALSE);
        }
        return TRUE;
    }
    if (n->code == UINT(PSN_SETACTIVE)) {
        if (!g_app.joystickPresent) EnableWindow(GetDlgItem(page, IDC_ENABLE_KEYBOARD), FALSE);
        else CheckDlgButton(page, IDC_ENABLE_KEYBOARD, c.joystick ? BST_UNCHECKED : BST_CHECKED);
        setResult(page, 0);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK joystickProc(HWND page, UINT msg, WPARAM wp, LPARAM lp) {
    joyPage = page;
    if (msg == WM_INITDIALOG) {
        initEntries(page, buttons, 4, kJoyControls, true);
        setPageIcon(page, IDC_ICON_JOYSTICK, 209);
        return TRUE;
    }
    if (msg == MM_JOY1BUTTONDOWN) {
        // a button pressed with an action's box focused: that action's button
        HWND focus = GetFocus();
        if (!focus || GetParent(focus) != page ||
            reinterpret_cast<WNDPROC>(GetWindowLongPtrW(focus, GWLP_WNDPROC)) != actionEditProc) return FALSE;
        const int idx = int(GetWindowLongPtrW(focus, GWLP_USERDATA));
        if (idx < 0 || idx >= 4) return FALSE;
        const int b = int((wp >> 8) & 0xff);
        Entry& e = buttons[idx];
        if (b == 1 || b == 2 || b == 4 || b == 8) e.name = buttonName(b);
        e.value = b;
        g_app.options.keys.joyButton[size_t(idx)] = b;
        SendDlgItemMessageA(page, e.control, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(e.name.c_str()));
        return FALSE;
    }
    if (msg != WM_NOTIFY) return FALSE;
    const NMHDR* n = reinterpret_cast<const NMHDR*>(lp);
    if (n->code == UINT(PSN_RESET)) {
        resetEntries(buttons, 4, true);
        if (g_app.joystickPresent) { joyReleaseCapture(JOYSTICKID1); c.joystick = g_app.options.joystick; }
        setResult(page, TRUE);
        return FALSE;
    }
    if (n->code == UINT(PSN_KILLACTIVE)) {
        if (duplicate(buttons, 4)) {
            MessageBoxA(page, kDuplicate, kCaption, MB_ICONEXCLAMATION);
            setResult(page, TRUE);
            return TRUE;
        }
        setResult(page, FALSE);
        if (g_app.joystickPresent) { joyReleaseCapture(JOYSTICKID1); c.joystick = IsDlgButtonChecked(page, IDC_ENABLE_JOYSTICK) != 0; }
        return TRUE;
    }
    if (n->code == UINT(PSN_SETACTIVE)) {
        if (g_app.joystickPresent) {
            CheckDlgButton(page, IDC_ENABLE_JOYSTICK, c.joystick ? BST_CHECKED : BST_UNCHECKED);
            joySetCapture(page, JOYSTICKID1, 0x11, FALSE);
        } else {
            EnableWindow(GetDlgItem(page, IDC_ENABLE_JOYSTICK), FALSE);
        }
        setResult(page, 0);
        return TRUE;
    }
    return FALSE;
}

PROPSHEETPAGEW makePage(int id, const wchar_t* title, DLGPROC proc) {
    PROPSHEETPAGEW p{};
    p.dwSize = sizeof p;
    p.dwFlags = PSP_USETITLE;
    p.hInstance = g_app.inst;
    p.pszTemplate = MAKEINTRESOURCEW(id);
    p.pfnDlgProc = proc;
    p.pszTitle = title;
    return p;
}

} // namespace

void showPropertySheet(int page) {
    game::Game& g = *g_app.game;
    engine::Options& o = g_app.options;
    g_app.sheetPage = page;                                // DAT_0046efe0
    g_app.busy = true;                                     // DAT_0046fbb0: the engine stands still
    c.joystick = o.joystick; c.smooth = o.smooth; c.priority = o.priority; c.difficulty = o.difficulty;
    c.cdMusic = o.cdMusic; c.cdAmbient = o.cdAmbient; c.sound = o.soundEnabled;
    c.cdVolume = o.cdVolume; c.wavVolume = o.wavVolume;
    c.screen = 0;
    if (!g_app.fullscreen && o.windowSizeX == 0x140 && o.windowSizeY == 0xe0) c.screen = 1;
    if (!g_app.fullscreen && o.windowSizeX == 0x280 && o.windowSizeY == 0x1c0) c.screen = 2;

    PROPSHEETPAGEW pages[5] = {
        makePage(IDD_SYSTEM, L"System", systemProc),
        makePage(IDD_SOUND, L"Sound", soundProc),
        makePage(IDD_DIFFICULTY, L"Difficulty", difficultyProc),
        makePage(IDD_JOYSTICK, L"Joystick", joystickProc),
        makePage(IDD_KEYBOARD, L"Keyboard", keyboardProc),
    };
    PROPSHEETHEADERW h{};
    h.dwSize = sizeof h;
    h.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
    h.hwndParent = g_app.hwnd;
    h.hInstance = g_app.inst;
    h.pszCaption = L"Pitfall Properties";
    h.nPages = 5;
    h.nStartPage = UINT(std::clamp(page, 0, 4));
    h.ppsp = pages;
    PropertySheetW(&h);
    keyPage = joyPage = nullptr;

    // What the pages left (Cancel took them back to the options): the options again.
    o.smooth = c.smooth;
    o.priority = c.priority;
    o.joystick = c.joystick;
    g.setCdOptions(c.cdMusic, c.cdAmbient);                // a track turned on starts, both off stop the CD
    if (c.sound != o.soundEnabled) g.setSoundEnabled(c.sound);
    o.cdVolume = c.cdVolume;
    g.setCdVolume(o.cdVolume);
    g.setWavVolume(c.wavVolume);
    applyPriority();
    // The difficulty: with a game going on (points, not on the title or the intro) only after the
    // question, which a new game follows and the level again (but the reunion and the end keep
    // theirs, and a demo changes it without asking).
    const int level = g.levelNumber();
    if (g.score() == 0 || o.difficulty == c.difficulty || level == game::kTitleScreen || level == game::kIntro) {
        if (level != game::kSimonRoom && level != game::kCredits) o.difficulty = c.difficulty;
    } else if (level != game::kSimonRoom) {
        if (level != game::kCredits && !g.demoPlaying()) {
            if (MessageBoxA(g_app.hwnd, kGiveUp, "Difficulty Change", MB_OKCANCEL | MB_ICONHAND) == IDOK) {
                g.newGame();
                if (level != game::kCredits && level != game::kMapScreen && level != game::kContinueScreen &&
                    level != game::kTitleScreen && level != game::kIntro)
                    g.startLevel(level);
            } else {
                c.difficulty = o.difficulty;
            }
        }
        if (level != game::kCredits) o.difficulty = c.difficulty;
    }                                                      // (the reunion keeps its difficulty)
    // The screen size: a window of 320x224 or 640x448, or the full screen.
    if (c.screen == 1 || c.screen == 2) { sizeWindow(c.screen == 1 ? 0x140 : 0x280, c.screen == 1 ? 0xe0 : 0x1c0); ShowWindow(g_app.hwnd, SW_SHOW); }
    if (c.screen == 3 || c.screen == 4) enterFullscreen();
    g_app.busy = false;
}

} // namespace app
