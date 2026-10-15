#include "engine/options.h"

#include <cstdio>
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
} // namespace

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
    level        = geti(kv, "Option_Level", level);
    displayMode  = geti(kv, "Option_DisplayMode", displayMode);
    for (int i = 0; i < int(Action::Count); ++i)
        keys.vk[size_t(i)] = geti(kv, KeyBindings::registryName(Action(i)), keys.vk[size_t(i)]);
    static const char* const joyNames[] = {"Joy_Jump", "Joy_Shoot", "Joy_Whip", "Joy_Select"};
    for (int i = 0; i < 4; ++i) keys.joyButton[size_t(i)] = geti(kv, joyNames[i], keys.joyButton[size_t(i)]);
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
    f << "Option_Level=" << level << "\nOption_DisplayMode=" << displayMode << "\n";
    for (int i = 0; i < int(Action::Count); ++i)
        f << KeyBindings::registryName(Action(i)) << "=" << keys.vk[size_t(i)] << "\n";
    static const char* const joyNames[] = {"Joy_Jump", "Joy_Shoot", "Joy_Whip", "Joy_Select"};
    for (int i = 0; i < 4; ++i) f << joyNames[i] << "=" << keys.joyButton[size_t(i)] << "\n";
    return true;
}

} // namespace engine
