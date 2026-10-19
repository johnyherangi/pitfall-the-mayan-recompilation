#include "engine/options.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace engine {

namespace {
using KV = std::map<std::string, std::string>;

KV readIni(const std::string& path) {
    KV kv;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == ';' || line[0] == '[') continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

int geti(const KV& kv, const char* k, int def) {
    auto it = kv.find(k);
    return it == kv.end() ? def : std::atoi(it->second.c_str());
}

// The Microsoft C library's rand() (FUN_00449853 / FUN_00449860).
struct CrtRand {
    uint32_t seed;
    int next() { seed = seed * 0x343fd + 0x269ec3; return int((seed >> 16) & 0x7fff); }
};
} // namespace

std::array<uint8_t, 64> levelBitsEncode(uint32_t bits) {
    // FUN_0043a2b0
    std::array<uint8_t, 64> b{};
    CrtRand r{bits};
    for (auto& c : b) c = uint8_t(r.next());
    int32_t d = int32_t(bits);
    for (int i = 0; i < 64; i += 2, d >>= 1) b[size_t(i)] = uint8_t((b[size_t(i)] & ~0x20) | ((d & 1) << 5));
    uint8_t sum = 0;
    for (int i = 0; i < 63; ++i) sum = uint8_t(sum + b[size_t(i)]);
    b[63] = sum;
    return b;
}

uint32_t levelBitsDecode(const std::array<uint8_t, 64>& b) {
    // FUN_0043a230: the bits back from the even bytes, then the random bytes and the sum checked.
    uint32_t bits = 0;
    for (int i = 62; i >= 0; i -= 2) bits = bits * 2 + ((b[size_t(i)] & 0x20) >> 5);
    CrtRand r{bits};
    uint32_t out = bits;
    for (int i = 0; i < 63; ++i) if (((r.next() ^ b[size_t(i)]) & 0xdf) != 0) out = 1;
    uint8_t sum = 0;
    for (int i = 0; i < 63; ++i) sum = uint8_t(sum + b[size_t(i)]);
    return b[63] == sum ? out : 1;
}

bool Options::load(const std::string& iniPath) {
    const KV kv = readIni(iniPath);
    if (kv.empty()) return false;
    windowSizeX  = geti(kv, "Window_SizeX", windowSizeX);
    windowSizeY  = geti(kv, "Window_SizeY", windowSizeY);
    windowTopX   = geti(kv, "Window_TopX", windowTopX);
    windowTopY   = geti(kv, "Window_TopY", windowTopY);
    joystick     = geti(kv, "Option_JoyStick", joystick) != 0;
    difficulty   = geti(kv, "Option_Difficulty", difficulty);
    priority     = geti(kv, "Option_Priority", priority);
    cdVolume     = geti(kv, "Option_CDVolume", cdVolume);
    wavVolume    = geti(kv, "Option_WavVolume", wavVolume);
    cdMusic      = geti(kv, "Option_CDMusic", cdMusic) != 0;
    cdAmbient    = geti(kv, "Option_CDAmbient", cdAmbient) != 0;
    soundEnabled = geti(kv, "Option_SoundEnabled", soundEnabled) != 0;
    slow         = geti(kv, "Option_Slow", slow) != 0;
    frameRate    = geti(kv, "Option_FrameRate", frameRate) != 0;
    smooth       = geti(kv, "Option_Smooth1", smooth) != 0;
    allowCheat   = geti(kv, "Debug_AllowCheat", allowCheat) != 0;
    {                                                   // Option_Level: the 64 bytes in hex
        auto it = kv.find("Option_Level");
        levelBits = 1;
        if (it != kv.end() && it->second.size() >= 128) {
            std::array<uint8_t, 64> b{};
            for (size_t i = 0; i < 64; ++i) b[i] = uint8_t(std::strtoul(it->second.substr(i * 2, 2).c_str(), nullptr, 16));
            levelBits = levelBitsDecode(b);
        }
    }
    for (int i = 0; i < int(Action::Count); ++i)
        keys.key[size_t(i)] = geti(kv, KeyBindings::registryName(Action(i)), keys.key[size_t(i)]);
    static const char* const joyNames[] = {"Joy_Jump", "Joy_Shoot", "Joy_Whip", "Joy_Select"};
    for (int i = 0; i < 4; ++i) keys.joyButton[size_t(i)] = geti(kv, joyNames[i], keys.joyButton[size_t(i)]);
    scale        = geti(kv, "Shell_Scale", scale);
    fullscreen   = geti(kv, "Shell_Fullscreen", fullscreen) != 0;
    warning256   = geti(kv, "Shell_Warning256", warning256) != 0;
    return true;
}

bool Options::save(const std::string& iniPath) const {
    std::ofstream f(iniPath);
    if (!f) return false;
    f << "[Software\\Activision\\Pitfall]\n";
    f << "Window_SizeX=" << windowSizeX << "\nWindow_SizeY=" << windowSizeY << "\n";
    f << "Window_TopX=" << windowTopX << "\nWindow_TopY=" << windowTopY << "\n";
    f << "Option_JoyStick=" << joystick << "\nOption_Difficulty=" << difficulty << "\n";
    f << "Option_Priority=" << priority << "\nOption_CDVolume=" << cdVolume << "\n";
    f << "Option_WavVolume=" << wavVolume << "\nOption_CDMusic=" << cdMusic << "\n";
    f << "Option_CDAmbient=" << cdAmbient << "\nOption_SoundEnabled=" << soundEnabled << "\n";
    f << "Option_Slow=" << slow << "\nOption_FrameRate=" << frameRate << "\n";
    f << "Option_Smooth1=" << smooth << "\nDebug_AllowCheat=" << allowCheat << "\n";
    f << "Option_Level=";
    for (uint8_t c : levelBitsEncode(levelBits)) { char h[3]; std::snprintf(h, sizeof h, "%02x", c); f << h; }
    f << "\n";
    for (int i = 0; i < int(Action::Count); ++i)
        f << KeyBindings::registryName(Action(i)) << "=" << keys.key[size_t(i)] << "\n";
    static const char* const joyNames[] = {"Joy_Jump", "Joy_Shoot", "Joy_Whip", "Joy_Select"};
    for (int i = 0; i < 4; ++i) f << joyNames[i] << "=" << keys.joyButton[size_t(i)] << "\n";
    f << "Shell_Scale=" << scale << "\nShell_Fullscreen=" << fullscreen << "\nShell_Warning256=" << warning256 << "\n";
    return true;
}

} // namespace engine
