#include "engine/input.h"

namespace engine {

const char* KeyBindings::registryName(Action a) {
    static const char* const names[] = {"Key_Up", "Key_Down", "Key_Left", "Key_Right",
                                        "Key_Jump", "Key_Shoot", "Key_Whip", "Key_Select"};
    return names[size_t(a)];
}

uint32_t Input::poll(const HostInput& host, bool useJoystick, bool allowCheat) const {
    uint32_t bits = 0;
    auto key = [&](Action a) { return host.keyDown[size_t(bindings.key[size_t(a)] & 0xff)]; };
    if (!useJoystick) {
        if (allowCheat && host.ctrlDown) bits |= kJoyDebug;
        if (key(Action::Up)) bits |= kJoyUp;
        if (key(Action::Down)) bits |= kJoyDown;
        if (key(Action::Left)) bits |= kJoyLeft;
        if (key(Action::Right)) bits |= kJoyRight;
        if (key(Action::Shoot)) bits |= kJoyShoot;
        if (key(Action::Jump)) bits |= kJoyJump;
        if (key(Action::Whip)) bits |= kJoyWhip;
        if (key(Action::Select)) bits |= kJoySelect;
    } else {
        // Joystick branch: a binding < 0x10 is a button mask, otherwise a key code.
        auto joyOrKey = [&](int code) {
            return code < 0x10 ? (host.joyButtons & uint32_t(code)) != 0 : host.keyDown[size_t(code & 0xff)];
        };
        if (joyOrKey(bindings.joyButton[3])) bits |= kJoySelect;
        if (joyOrKey(bindings.joyButton[2])) bits |= kJoyWhip;
        if (joyOrKey(bindings.joyButton[0])) bits |= kJoyJump;
        if (joyOrKey(bindings.joyButton[1])) bits |= kJoyShoot;
        if (host.joyY < 0x5000) bits |= kJoyUp;
        if (host.joyY > 0xb000) bits |= kJoyDown;
        if (host.joyX < 0x5000) bits |= kJoyLeft;
        if (host.joyX > 0xb000) bits |= kJoyRight;
    }
    return bits | (host.padBits & ~uint32_t(kJoyDebug));
}

uint32_t Input::pollVaultKeys(const HostInput& host) {
    uint32_t bits = 0;
    if (host.keyDown['1'] || host.keyDown[0x61]) bits |= kJoyShoot;   // VK_NUMPAD1
    if (host.keyDown['2'] || host.keyDown[0x62]) bits |= kJoyJump;
    if (host.keyDown['3'] || host.keyDown[0x63]) bits |= kJoyWhip;
    // a gamepad: its shoot, jump and whip buttons as the three levers (1, 2, 3)
    return bits | (host.padBits & (kJoyShoot | kJoyJump | kJoyWhip));
}

} // namespace engine
