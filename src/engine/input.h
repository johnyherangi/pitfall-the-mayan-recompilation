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

// Original default bindings (data at 0x46d08c..): Windows virtual-key codes.
struct KeyBindings {
    std::array<int, size_t(Action::Count)> vk{
        0x26, 0x28, 0x25, 0x27,     // VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT
        'W', 'E', 'Q', 0x20         // Key_Jump, Key_Shoot, Key_Whip, Key_Select (space)
    };
    // Joystick buttons for Jump / Shoot / Whip / Select (bit masks of JOYINFO.wButtons).
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
