// Joypad state and key bindings.
//
// The game logic (ported Genesis code) reads a Genesis-style button byte
// (DAT_004517f8).  The Windows engine fills it from the keyboard or joystick
// each frame in FUN_00444940, or from a recorded demo script in attract mode.
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace engine {

enum JoyBits : uint32_t {
    kJoyUp     = 0x01,   // FUN_00442390
    kJoyDown   = 0x02,   // FUN_004423a0
    kJoyLeft   = 0x04,   // FUN_004423b0
    kJoyRight  = 0x08,   // FUN_004423c0
    kJoyJump   = 0x10,   // FUN_004423e0  (Key_Jump,  default W,     joystick button 1)
    kJoyWhip   = 0x20,   // FUN_004423f0  (Key_Whip,  default Q,     joystick button 3)
    kJoyShoot  = 0x40,   // FUN_004423d0  (Key_Shoot, default E,     joystick button 2)
    kJoySelect = 0x80,   // FUN_00442400  (Key_Select,default Space, joystick button 4)
    kJoyDebug  = 0x100,  // FUN_00442380  (Ctrl, only when Debug_AllowCheat is set)
};

enum class Action { Up, Down, Left, Right, Jump, Shoot, Whip, Select, Count };

// Original default bindings (the table at 0x46d058 .., Key_Up .. Key_Select): each key as the
// options page took it (0x43b000), the WM_KEYDOWN's lParam but its low byte, which is the
// virtual-key code: the scan code in bits 16 .. 23 (GetKeyNameTextA names it), the code in 0 .. 7.
struct KeyBindings {
    std::array<int, size_t(Action::Count)> key{
        0x09480026, 0x09500028, 0x094b0025, 0x094d0027,   // Up, Down, Left, Right (the arrows)
        0x08110057, 0x08120045, 0x08100051, 0x08390020    // Jump W, Shoot E, Whip Q, Select Space
    };
    // Joystick buttons for Jump / Shoot / Whip / Select (the table at 0x46d238 .., Joy_Jump ..):
    // bit masks of JOYINFO.wButtons, or a key as above (the page takes either).
    std::array<int, 4> joyButton{1, 2, 4, 8};
    static const char* registryName(Action a);   // "Key_Up", "Key_Jump", ...
};

// Snapshot of the host's raw input for one frame, filled by the platform layer.
struct HostInput {
    std::array<bool, 256> keyDown{};       // indexed by Windows virtual-key code
    bool joystickPresent = false;
    uint32_t joyButtons = 0;               // JOYINFO.wButtons
    int joyX = 0x8000, joyY = 0x8000;      // 0..0xffff, centre 0x8000
    bool ctrlDown = false;
};

class Input {
public:
    // FUN_00444940 keyboard / joystick branch.
    uint32_t poll(const HostInput& host, bool useJoystick, bool allowCheat) const;
    KeyBindings bindings;
    // Loltun vault (Simon) levels 11..13 use keys 1/2/3 (and numpad) instead.
    static uint32_t pollVaultKeys(const HostInput& host);
};

} // namespace engine
