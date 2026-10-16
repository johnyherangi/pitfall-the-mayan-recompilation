// The port's gamepads (the original knew only the joystick API's one stick and four buttons, behind
// its JoyStick option): an Xbox pad through XInput (loaded at run time, so the game runs without
// it) and any other, a PlayStation pad say, through the joystick API with its d-pad (the POV hat),
// read every frame with the keyboard, whatever the JoyStick option says.
//
//   d-pad, left stick      up / down / left / right
//   A  / cross             jump
//   B  / circle            shoot
//   X  / square            whip
//   Y  / triangle, Start / Options, Back / Create    select
//
// A PlayStation pad is known by Sony's vendor id in the joystick's capabilities; any other pad on
// the joystick API takes the original's order (buttons 1 .. 4: jump, shoot, whip, select, and 7 / 8
// select too).  The joystick API is left alone while the JoyStick option reads it, so a button
// means one thing.
#include "win32_app.h"
#include "engine/input.h"
#include <windows.h>
#include <mmsystem.h>

namespace app {
namespace {

// XInput, loaded when there is one (1.4 on Windows 8 and later, 9.1.0 on Vista and 7).
struct XInputState { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XInputState*);
XInputGetStateFn xinputGetState = nullptr;
bool xinputTried = false;
DWORD xinputRetry[4] = {};                 // a pad not there is asked again after a while (the call is slow)

void loadXInput() {
    xinputTried = true;
    for (const wchar_t* name : {L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll"}) {
        if (HMODULE m = LoadLibraryW(name)) {
            xinputGetState = reinterpret_cast<XInputGetStateFn>(reinterpret_cast<void*>(GetProcAddress(m, "XInputGetState")));
            if (xinputGetState) return;
            FreeLibrary(m);
        }
    }
}

uint32_t stickBits(int x, int y, int centre, int dead) {
    uint32_t b = 0;
    if (x < centre - dead) b |= engine::kJoyLeft;
    if (x > centre + dead) b |= engine::kJoyRight;
    if (y < centre - dead) b |= engine::kJoyUp;
    if (y > centre + dead) b |= engine::kJoyDown;
    return b;
}

uint32_t povBits(DWORD pov) {
    if (pov == 0xffff || pov == 0xffffffff) return 0;          // centred
    uint32_t b = 0;
    if (pov > 31500 || pov < 4500) b |= engine::kJoyUp;
    if (pov > 4500 && pov < 13500) b |= engine::kJoyRight;
    if (pov > 13500 && pov < 22500) b |= engine::kJoyDown;
    if (pov > 22500 && pov < 31500) b |= engine::kJoyLeft;
    return b;
}

uint32_t readXInput() {
    if (!xinputTried) loadXInput();
    if (!xinputGetState) return 0;
    uint32_t bits = 0;
    const DWORD now = GetTickCount();
    for (DWORD i = 0; i < 4; ++i) {
        if (xinputRetry[i] && now - xinputRetry[i] < 2000) continue;
        XInputState st{};
        if (xinputGetState(i, &st) != ERROR_SUCCESS) { xinputRetry[i] = now ? now : 1; continue; }
        xinputRetry[i] = 0;
        const WORD b = st.buttons;
        if (b & 0x0001) bits |= engine::kJoyUp;                  // DPAD_UP
        if (b & 0x0002) bits |= engine::kJoyDown;
        if (b & 0x0004) bits |= engine::kJoyLeft;
        if (b & 0x0008) bits |= engine::kJoyRight;
        if (b & 0x1000) bits |= engine::kJoyJump;                // A
        if (b & 0x2000) bits |= engine::kJoyShoot;               // B
        if (b & 0x4000) bits |= engine::kJoyWhip;                // X
        if (b & (0x8000 | 0x0010 | 0x0020)) bits |= engine::kJoySelect;   // Y, Start, Back
        bits |= stickBits(st.lx, -st.ly, 0, 12000);              // (XInput's y goes up)
    }
    return bits;
}

uint32_t readJoystickApi() {
    uint32_t bits = 0;
    const UINT n = joyGetNumDevs();
    for (UINT i = 0; i < n && i < 4; ++i) {
        JOYINFOEX ji{};
        ji.dwSize = sizeof ji;
        ji.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNPOV | JOY_RETURNBUTTONS;
        if (joyGetPosEx(i, &ji) != JOYERR_NOERROR) continue;
        JOYCAPSW caps{};
        const bool sony = joyGetDevCapsW(i, &caps, sizeof caps) == JOYERR_NOERROR && caps.wMid == 0x054c;
        const DWORD b = ji.dwButtons;
        if (sony) {
            if (b & 0x0002) bits |= engine::kJoyJump;            // cross
            if (b & 0x0004) bits |= engine::kJoyShoot;           // circle
            if (b & 0x0001) bits |= engine::kJoyWhip;            // square
            if (b & (0x0008 | 0x0100 | 0x0200)) bits |= engine::kJoySelect;   // triangle, Create, Options
        } else {
            if (b & 0x0001) bits |= engine::kJoyJump;
            if (b & 0x0002) bits |= engine::kJoyShoot;
            if (b & 0x0004) bits |= engine::kJoyWhip;
            if (b & (0x0008 | 0x0040 | 0x0080)) bits |= engine::kJoySelect;
        }
        bits |= povBits(ji.dwPOV);
        bits |= stickBits(int(ji.dwXpos), int(ji.dwYpos), 0x8000, 0x3000);
    }
    return bits;
}

uint32_t lastBits = 0;

} // namespace

// The pads' joypad bits this frame, and whether a button went down since the last frame.
uint32_t readGamepads(bool joystickOptionOn, bool& pressed) {
    uint32_t bits = readXInput();
    if (!joystickOptionOn) bits |= readJoystickApi();
    const uint32_t buttons = engine::kJoyJump | engine::kJoyShoot | engine::kJoyWhip | engine::kJoySelect;
    pressed = (bits & buttons & ~lastBits) != 0;
    lastBits = bits;
    return bits;
}

} // namespace app
