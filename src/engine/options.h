// Player options.
//
// The original stored these under HKCU\Software\Activision\Pitfall
// (FUN_004400f0 / FUN_004404a0).  We keep the exact value names so the
// settings are recognisable, but persist them in a plain pitfall.ini next
// to the executable instead of the registry.
#pragma once
#include "engine/input.h"

#include <string>

namespace engine {

struct Options {
    // value name            original global   default
    int  windowSizeX  = 320;   // Window_SizeX    DAT_00453068  0x140
    int  windowSizeY  = 224;   // Window_SizeY    DAT_0045306c  0xe0
    int  windowTopX   = 0x90;  // Window_TopX     DAT_00453060
    int  windowTopY   = 0x6e;  // Window_TopY     DAT_00453064
    bool joystick     = false; // Option_JoyStick DAT_0046e060
    int  difficulty   = 0;     // Option_Difficulty DAT_0046fb78 (0 normal, 1 hard)
    int  priority     = 0;     // Option_Priority DAT_0046efd8 (0 normal,1 high,2 realtime)
    int  cdVolume     = 0x50;  // Option_CDVolume DAT_0046f098 (0..100: auxSetVolume's 0x28f a step)
    int  wavVolume    = 0x50;  // Option_WavVolume DAT_0046e104 (0..100: the samples at * 0x7f / 100)
    bool cdMusic      = true;  // Option_CDMusic  DAT_0046e11c
    bool cdAmbient    = false; // Option_CDAmbient DAT_0046e118
    bool soundEnabled = true;  // Option_SoundEnabled DAT_0046d060
    bool slow         = false; // Option_Slow     DAT_0046ed5c (allow slow machines)
    bool frameRate    = false; // Option_FrameRate DAT_0046e054 (show fps counter)
    bool smooth       = false; // Option_Smooth1  DAT_0046e048 (full screen smoothing)
    bool allowCheat   = false; // Debug_AllowCheat DAT_0046e064
    int  level        = 0;     // Option_Level    DAT_00453190 (debug start level)
    int  displayMode  = 0;     // 0 window 320x224, 1 double 640x448, 2 full 320x200, 3 full 320x224
    KeyBindings keys;

    bool load(const std::string& iniPath);
    bool save(const std::string& iniPath) const;
};

// Menu strings from the original resources (for the display-mode menu).
inline const char* displayModeName(int m) {
    switch (m) {
        case 0: return "Standard Window (320x224)";
        case 1: return "Double Window (640x448)";
        case 2: return "Full Screen (Fast - 320x200)";
        default: return "Full Screen (Smooth - 320x224)";
    }
}

} // namespace engine
