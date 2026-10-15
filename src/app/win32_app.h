// The Win32 front end's state, shared by the window (main_win32.cpp) and the property sheet
// (win32_sheet.cpp).
#pragma once
#include "engine/options.h"
#include "game/game.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace app {

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;                 // DAT_0046d3cc
    HWND status = nullptr;               // DAT_00451a60: the status bar
    HWND loading = nullptr;              // DAT_0046d3d4: "Loading..."
    HMENU menu = nullptr;                // DAT_0046d3c8
    HMODULE exe = nullptr;               // PITFALL.EXE as a data file: its pictures (bitmaps, icons)
    HDC backDc = nullptr;
    HBITMAP backBmp = nullptr;
    uint32_t* backPixels = nullptr;      // 320x224 BGRX
    std::unique_ptr<game::Game> game;
    engine::Options options;
    std::string iniPath;
    std::string dataDir;
    bool fullscreen = false;             // DAT_0046e02c
    bool busy = false;                   // DAT_0046fbb0: a level loads or the options are open; the focus does not pause
    bool soundDevice = false;            // DAT_0046e100: a wave device opened
    bool joystickPresent = false;        // DAT_0046d410 (FUN_0043a0e0)
    int statusH = 0;                     // DAT_0046efd0: the status bar's height
    int checkedLevel = -1;               // DAT_0046d400: the level whose menu item is checked
    int statusSerial = -1;
    char statusText[64] = " ";           // the status bar's part 0 (FUN_00440bc0's text)
    int sheetPage = 0;                   // DAT_0046efe0: the property sheet's page last asked for
    RECT windowRect{};                   // the window before a full screen
    uint32_t paletteCache[256]{};

    // audio
    HWAVEOUT waveOut = nullptr;
    std::thread audioThread;
    std::atomic<bool> audioRun{false};
};
extern App g_app;

// FUN_0043a140 and the SetWindowPos after it: the window sized for a game area of w x h.
void sizeWindow(int w, int h);
void enterFullscreen();
void leaveFullscreen();                  // FUN_0043dd80
void applyPriority();                    // SetPriorityClass by Option_Priority
// The dialogs' WM_INITDIALOG (0x43aa10 ..): centred on the window's game area.
void centreOnGame(HWND dlg);
// FUN_00440c00: the property sheet at a page (0 System, 1 Sound, 2 Difficulty, 3 Joystick,
// 4 Keyboard).
void showPropertySheet(int page);
// FUN_00441cf0: the game's disc through MCI (null without it, or with Option_Slow's "no CD"),
// its device for MM_MCINOTIFY (win32_cd.cpp).
std::unique_ptr<engine::CdPlayer> openDisc(HWND notify);
unsigned discDevice();

} // namespace app
