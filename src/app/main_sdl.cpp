// The modern shell: the SDL2 front end (-DPITFALL_SHELL=modern), for Windows, macOS and Linux.
//
// The same game as the classic shell's, run the same way (a logic tick every 17 ms, the 320x224
// view through the palette into a texture, the sound mixed in the audio callback, the keys as the
// Windows virtual-key codes the engine's bindings use), in a window of today: the picture scaled
// by whole numbers (crisp), full screen on F11 or Alt+Enter, gamepads through SDL's mapping, and
// in place of the original's menu bar and property sheet an in-game menu on Esc (or a pad's
// Start) drawn in the game's own 8x8 font over the paused picture: resume, new game, the levels
// the original's File menu offered (as they are unlocked), the options and quit.  The settings
// are the same pitfall.ini (in the user's configuration folder on macOS and Linux).
#include "engine/framebuffer.h"
#include "engine/options.h"
#include "game/game.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// ------------------------------------------------------------------ keys --
int vkFromScancode(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP: return 0x26; case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_LEFT: return 0x25; case SDL_SCANCODE_RIGHT: return 0x27;
    case SDL_SCANCODE_SPACE: return 0x20; case SDL_SCANCODE_RETURN: return 0x0d;
    case SDL_SCANCODE_ESCAPE: return 0x1b; case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x11;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x12;
    case SDL_SCANCODE_TAB: return 0x09; case SDL_SCANCODE_BACKSPACE: return 0x08;
    case SDL_SCANCODE_INSERT: return 0x2d; case SDL_SCANCODE_DELETE: return 0x2e;
    case SDL_SCANCODE_HOME: return 0x24; case SDL_SCANCODE_END: return 0x23;
    case SDL_SCANCODE_PAGEUP: return 0x21; case SDL_SCANCODE_PAGEDOWN: return 0x22;
    case SDL_SCANCODE_KP_ENTER: return 0x0d; case SDL_SCANCODE_KP_0: return 0x60;
    case SDL_SCANCODE_KP_PLUS: return 0x6b; case SDL_SCANCODE_KP_MINUS: return 0x6d;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x6a; case SDL_SCANCODE_KP_DIVIDE: return 0x6f;
    case SDL_SCANCODE_KP_PERIOD: return 0x6e;
    case SDL_SCANCODE_SEMICOLON: return 0xba; case SDL_SCANCODE_EQUALS: return 0xbb;
    case SDL_SCANCODE_COMMA: return 0xbc; case SDL_SCANCODE_MINUS: return 0xbd;
    case SDL_SCANCODE_PERIOD: return 0xbe; case SDL_SCANCODE_SLASH: return 0xbf;
    case SDL_SCANCODE_GRAVE: return 0xc0; case SDL_SCANCODE_LEFTBRACKET: return 0xdb;
    case SDL_SCANCODE_BACKSLASH: return 0xdc; case SDL_SCANCODE_RIGHTBRACKET: return 0xdd;
    case SDL_SCANCODE_APOSTROPHE: return 0xde;
    default: break;
    }
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return '0';
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return 0x61 + (sc - SDL_SCANCODE_KP_1);
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return 0x70 + (sc - SDL_SCANCODE_F1);
    return -1;
}

// The name of a bound key (a Windows virtual-key code: the classic shell's bindings, read here too)
std::string keyName(int code) {
    const int vk = code & 0xff;
    for (int sc = 0; sc < SDL_NUM_SCANCODES; ++sc)
        if (vkFromScancode(SDL_Scancode(sc)) == vk) {
            std::string n = SDL_GetScancodeName(SDL_Scancode(sc));
            if (n.rfind("Left ", 0) == 0) n = n.substr(5);         // ("Left Ctrl": either one)
            return n.empty() ? "KEY " + std::to_string(vk) : n;
        }
    return "KEY " + std::to_string(vk);
}

void audioCallback(void* userdata, Uint8* stream, int len) {
    auto* g = static_cast<game::Game*>(userdata);
    g->mixer().render(reinterpret_cast<int16_t*>(stream), len / 4);
}

// ----------------------------------------------------------------- files --
// The settings: next to the executable where that can be written (Windows: as the classic shell),
// else the user's configuration folder (SDL_GetPrefPath: ~/Library/Application Support/... on
// macOS, ~/.local/share/... on Linux).
std::string settingsPath() {
    std::string base;
    if (char* b = SDL_GetBasePath()) { base = b; SDL_free(b); }
#ifdef _WIN32
    if (!base.empty()) return base + "pitfall.ini";
#else
    if (!base.empty() && fs::exists(base + "pitfall.ini")) return base + "pitfall.ini";
#endif
    if (char* p = SDL_GetPrefPath("Activision", "Pitfall")) { const std::string s = p; SDL_free(p); return s + "pitfall.ini"; }
    return "pitfall.ini";
}

// The game's files: the folder named on the command line, or "original" / "ASSETS" by the
// executable (inside a macOS bundle: Resources, or next to the .app), or by the settings.
std::string findDataDir(const std::string& hint) {
    std::vector<std::string> c;
    if (!hint.empty()) c.push_back(hint);
    std::string base;
    if (char* b = SDL_GetBasePath()) { base = b; SDL_free(b); }
    for (const char* sub : {"original", "ASSETS", "", "../original", "../../original", "../../../original", "../../../../original"})
        if (!base.empty()) c.push_back(base + sub);
    const std::string pref = settingsPath();
    c.push_back(fs::path(pref).parent_path().string() + "/original");
    c.emplace_back("original");
    c.emplace_back(".");
    for (const auto& d : c)
        if (!d.empty() && (fs::exists(d + "/INIT.PH") || fs::exists(d + "/init.ph") ||
                           fs::exists(d + "/ASSETS/INIT.PH") || fs::exists(d + "/ASSETS/init.ph"))) return d;   // (the CD's layout)
    return "";
}

// ------------------------------------------------------------------ menu --
// Drawn into the game's 8-bit view in the game's font (the load screens' look: the font's dark red
// behind white letters, grey for what is not picked, a cursor before the pick).
struct Shell;
struct MenuItem {
    std::string label;
    std::string value;                                   // (what Left / Right change, "" for none)
    bool enabled = true;
    int id = 0;
};
enum class Page { None, Main, Levels, Options, Keys, Confirm, Capture };

// FUN_0043a320's order of the original's File menu, with each item's level
struct LevelItem { const char* name; int level; };
constexpr LevelItem kLevelItems[] = {
    {"CEIBA JUNGLE", 0}, {"XIBALBA FALLS", 1}, {"TAZAMUL MINES", 2}, {"LOST CITY OF COPAN", 3},
    {"COPAN TEMPLE", 4}, {"LAKAMUL RAIN FOREST", 5}, {"YAXCHILAN LAGOON", 6}, {"PALENQUE RUINS", 16},
    {"TOMB OF PALENQUE", 17}, {"BALANKANCHE MINE", 7}, {"JAINA ISLAND FALLS", 18}, {"TIKAL RUINS", 8},
    {"TEMPLE OF TIKAL", 9}, {"WARRIOR SPIRIT", 10}, {"LOLTUN VAULT 1", 13}, {"LOLTUN VAULT 2", 11},
    {"LOLTUN VAULT 3", 12}, {"2600 LEVEL", 20},
};
constexpr const char* kActionNames[] = {"UP", "DOWN", "LEFT", "RIGHT", "JUMP", "SHOOT", "WHIP", "SELECT"};

struct Shell {
    game::Game& g;
    engine::Options& options;
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    Page page = Page::None, pageAfterConfirm = Page::None;
    int cursor = 0, cursorMain = 0;
    std::vector<MenuItem> items;
    std::string title, confirmText;
    int confirmAction = 0, confirmLevel = -1, captureAction = -1;
    bool quit = false;
    // the font's colours, found among the palette's entries the font's glyphs use
    uint8_t cWhite = 0, cGrey = 0, cDark = 0, cDim = 0, cBlack = 0;

    Shell(game::Game& game, engine::Options& o) : g(game), options(o) {}

    bool open() const { return page != Page::None; }

    void findColours() {
        const ph::RawImage& font = g.assets().font;
        int lum[256]; bool used[256] = {};
        for (uint8_t v : font.pixels()) used[v] = true;
        for (int i = 0; i < 256; ++i) { const ph::Rgb c = g.palette().shown(i); lum[i] = c.r * 3 + c.g * 6 + c.b; }
        cWhite = cGrey = cDark = cDim = 0;
        int best = -1, second = -1, darkest = 1 << 20, dim = -1;
        for (int i = 1; i < 256; ++i) {
            if (!used[i] || lum[i] == 0) continue;
            if (lum[i] > best) { second = best; cGrey = cWhite; best = lum[i]; cWhite = uint8_t(i); }
            else if (lum[i] > second) { second = lum[i]; cGrey = uint8_t(i); }
            if (lum[i] < darkest) { darkest = lum[i]; cDark = uint8_t(i); }
        }
        for (int i = 1; i < 256; ++i)
            if (used[i] && lum[i] > darkest && lum[i] < second && lum[i] > dim) { dim = lum[i]; cDim = uint8_t(i); }
        cBlack = 0;
        for (int i = 0; i < 256; ++i) if (lum[i] == 0) { cBlack = uint8_t(i); break; }
        // the panel: the load screens' dark red (the font's first colour, palette entry 0 on a
        // true-colour display) where a level's palette still has it, else the nearest dark colour
        int bestD = 1 << 30;
        for (int i = 0; i < 256; ++i) {
            const ph::Rgb c = g.palette().shown(i);
            const int d = (c.r - 67) * (c.r - 67) + c.g * c.g * 2 + c.b * c.b * 2;
            if (lum[i] > 0 && lum[i] < 400 && d < bestD) { bestD = d; cDark = uint8_t(i); }
        }
        if (bestD > 40 * 40) cDark = cBlack;
    }

    uint8_t* at(int x, int y) { return g.framebuffer().viewBase(g.viewScrollX()) + size_t(y) * engine::kBufW + x; }
    void fill(int x, int y, int w, int h, uint8_t c) {
        x = std::max(x, 0); y = std::max(y, 0); w = std::min(w, engine::kViewW - x); h = std::min(h, engine::kViewH - y);
        for (int yy = 0; yy < h; ++yy) std::memset(at(x, y + yy), c, size_t(w));
    }
    void frame(int x, int y, int w, int h, uint8_t c) {
        fill(x, y, w, 1, c); fill(x, y + h - 1, w, 1, c); fill(x, y, 1, h, c); fill(x + w - 1, y, 1, h, c);
    }
    // a glyph of the font with its white turned to `white` (its shades kept)
    void glyph(int cell, int x, int y, uint8_t white) {
        const ph::RawImage& font = g.assets().font;
        if (cell < 0 || cell >= font.cellCount()) return;
        const uint8_t* src = font.cell(cell);
        for (int yy = 0; yy < 8; ++yy)
            for (int xx = 0; xx < 8; ++xx) {
                const uint8_t v = src[yy * 8 + xx];
                if (!v || x + xx < 0 || x + xx >= engine::kViewW || y + yy < 0 || y + yy >= engine::kViewH) continue;
                *at(x + xx, y + yy) = v == cWhite ? white : v;
            }
    }
    void text(int x, int y, const std::string& s, uint8_t white) {
        for (char ch : s) {
            const int c = std::toupper(static_cast<unsigned char>(ch));
            int cell = -1;
            if (c >= 'A' && c <= 'Z') cell = c - 'A';
            else if (c >= '0' && c <= '9') cell = c - 0x16;
            else if (c == ',') cell = 0x24; else if (c == '!') cell = 0x25; else if (c == '.') cell = 0x26;
            else if (c == '-') fill(x + 1, y + 3, 6, 2, white);
            else if (c == ':') { fill(x + 3, y + 1, 2, 2, white); fill(x + 3, y + 5, 2, 2, white); }
            else if (c == '/') { for (int i = 0; i < 7; ++i) fill(x + 6 - i, y + i, 1, 1, white); }
            else if (c == '?') { fill(x + 1, y, 5, 1, white); fill(x + 5, y + 1, 2, 2, white); fill(x + 3, y + 3, 2, 2, white); fill(x + 3, y + 6, 2, 2, white); }
            else if (c == '\'') fill(x + 3, y, 2, 3, white);
            if (cell >= 0) glyph(cell, x, y, white);
            x += 8;
        }
    }
    void cursorMark(int x, int y, uint8_t c) {              // a small arrow head
        for (int i = 0; i < 4; ++i) fill(x + i, y + i, 1, 7 - 2 * i, c);
    }

    // ---- pages ----
    void show(Page p) {
        page = p; items.clear(); cursor = 0;
        switch (p) {
        case Page::Main:
            title = "PITFALL";
            items.push_back({"RESUME", "", true, 1});
            items.push_back({"NEW GAME", "", true, 2});
            items.push_back({"LEVELS", "", true, 3});
            items.push_back({"OPTIONS", "", true, 4});
            items.push_back({"QUIT", "", true, 5});
            cursor = cursorMain;
            break;
        case Page::Levels: {
            title = "LEVELS";
            const uint32_t bits = g.menuUnlocks();
            for (const LevelItem& l : kLevelItems) items.push_back({l.name, "", (bits >> l.level & 1) != 0, 100 + l.level});
            for (size_t i = 0; i < items.size(); ++i) if (items[i].id == 100 + g.levelNumber()) cursor = int(i);
            break;
        }
        case Page::Options:
            title = "OPTIONS";
            items.push_back({"SOUND EFFECTS", "", true, 10});
            items.push_back({"SOUND VOLUME", "", true, 11});
            items.push_back({"MUSIC", "", true, 12});
            items.push_back({"AMBIENT SOUNDS", "", true, 13});
            items.push_back({"CD VOLUME", "", true, 14});
            items.push_back({"DIFFICULTY", "", true, 15});
            items.push_back({"WINDOW SCALE", "", true, 16});
            items.push_back({"FULL SCREEN", "", true, 17});
            items.push_back({"KEYS", "", true, 18});
            items.push_back({"BACK", "", true, 19});
            break;
        case Page::Keys:
            title = "KEYS";
            for (int i = 0; i < 8; ++i) items.push_back({kActionNames[i], "", true, 20 + i});
            items.push_back({"DEFAULTS", "", true, 28});
            items.push_back({"BACK", "", true, 29});
            break;
        case Page::Confirm:
            items.push_back({"YES", "", true, 1});
            items.push_back({"NO", "", true, 0});
            cursor = 1;
            break;
        default: break;
        }
        refreshValues();
    }
    void refreshValues() {
        auto onOff = [](bool b) { return std::string(b ? "ON" : "OFF"); };
        for (MenuItem& it : items) switch (it.id) {
            case 10: it.value = onOff(options.soundEnabled); break;
            case 11: it.value = std::to_string(options.wavVolume); break;
            case 12: it.value = onOff(options.cdMusic); break;
            case 13: it.value = onOff(options.cdAmbient); break;
            case 14: it.value = std::to_string(options.cdVolume); break;
            case 15: it.value = options.difficulty ? "HARD" : "NORMAL"; break;
            case 16: it.value = options.scale ? std::to_string(options.scale) + "X" : "FIT"; break;
            case 17: it.value = onOff(options.fullscreen); break;
            default:
                if (it.id >= 20 && it.id < 28) it.value = keyName(options.keys.key[size_t(it.id - 20)]);
                break;
        }
    }

    std::vector<uint8_t> under;                          // the game's frame as the menu opened

    void openMenu() {
        if (open()) return;
        findColours();
        g.setPaused(true);                               // (the original's pause: the channels and the CD stop)
        under.assign(g.framebuffer().px.begin(), g.framebuffer().px.end());
        show(Page::Main);
    }
    void closeMenu() {
        page = Page::None;
        g.setPaused(false);
    }

    void confirm(const std::string& what, int action, Page back) {
        confirmText = what; confirmAction = action; pageAfterConfirm = back;
        show(Page::Confirm);
    }

    void startLevel(int level) {
        if (g.menuNeedsConfirm()) { confirmLevel = level; confirm("GIVE UP THE CURRENT GAME?", 2, Page::Levels); return; }
        g.menuStart(level);
        page = Page::None;
    }

    void applyWindow();

    // Up / Down, Left / Right, accept, back
    void move(int d) {
        if (items.empty()) return;
        for (int n = 0; n < int(items.size()); ++n) {
            cursor = (cursor + d + int(items.size())) % int(items.size());
            if (items[size_t(cursor)].enabled) break;
        }
        if (page == Page::Main) cursorMain = cursor;
    }
    void change(int d) {
        if (items.empty()) return;
        const int id = items[size_t(cursor)].id;
        switch (id) {
        case 10: options.soundEnabled = !options.soundEnabled; g.setSoundEnabled(options.soundEnabled); break;
        case 11: options.wavVolume = std::clamp(options.wavVolume + 10 * d, 0, 100); g.setWavVolume(options.wavVolume); break;
        case 12: options.cdMusic = !options.cdMusic; if (options.cdMusic) options.cdAmbient = false; g.setCdOptions(options.cdMusic, options.cdAmbient); break;
        case 13: options.cdAmbient = !options.cdAmbient; if (options.cdAmbient) options.cdMusic = false; g.setCdOptions(options.cdMusic, options.cdAmbient); break;
        case 14: options.cdVolume = std::clamp(options.cdVolume + 10 * d, 0, 100); g.setCdVolume(options.cdVolume); break;
        case 15: options.difficulty = options.difficulty ? 0 : 1; break;
        case 16: options.scale = (options.scale + d + 5) % 5; applyWindow(); break;
        case 17: options.fullscreen = !options.fullscreen; applyWindow(); break;
        default: break;
        }
        refreshValues();
    }
    void accept() {
        if (items.empty()) return;
        const MenuItem& it = items[size_t(cursor)];
        if (!it.enabled) return;
        switch (it.id) {
        case 1: closeMenu(); break;
        case 2:
            if (g.menuNeedsConfirm()) { confirmLevel = game::kIntro; confirm("GIVE UP THE CURRENT GAME?", 2, Page::Main); }
            else { g.setIntroAfterDemos(false); g.menuStart(game::kIntro); page = Page::None; }
            break;
        case 3: show(Page::Levels); break;
        case 4: show(Page::Options); break;
        case 5: confirm("QUIT THIS ADVENTURE?", 1, Page::Main); break;
        case 18: show(Page::Keys); break;
        case 19: show(Page::Main); break;
        case 28: options.keys = engine::KeyBindings{}; refreshValues(); break;
        case 29: show(Page::Options); break;
        default:
            if (it.id >= 100) startLevel(it.id - 100);
            else if (it.id >= 20 && it.id < 28) { captureAction = it.id - 20; page = Page::Capture; }
            else if (it.id >= 10 && it.id <= 17) change(1);
            break;
        }
    }
    void confirmed(bool yes) {
        const int action = confirmAction;
        show(pageAfterConfirm);
        if (!yes) return;
        if (action == 1) quit = true;
        else if (action == 2) { g.setIntroAfterDemos(false); g.menuStart(confirmLevel); page = Page::None; }
    }
    void back() {
        switch (page) {
        case Page::Main: closeMenu(); break;
        case Page::Levels: case Page::Options: show(Page::Main); break;
        case Page::Keys: show(Page::Options); break;
        case Page::Confirm: confirmed(false); break;
        case Page::Capture: page = Page::Keys; break;
        default: break;
        }
    }
    void captured(SDL_Scancode sc) {
        const int vk = vkFromScancode(sc);
        if (vk > 0 && vk != 0x1b && captureAction >= 0) options.keys.key[size_t(captureAction)] = vk;
        show(Page::Keys);
        cursor = captureAction >= 0 ? captureAction : 0;
    }

    // ---- drawing, after the game's frame ----
    void draw() {
        if (!open()) return;
        if (under.size() == g.framebuffer().px.size()) std::copy(under.begin(), under.end(), g.framebuffer().px.begin());
        findColours();
        const int lineH = items.size() > 12 ? 10 : 12;
        if (page == Page::Confirm || page == Page::Capture) {
            const std::string msg = page == Page::Capture ? "PRESS A KEY FOR " + std::string(kActionNames[captureAction]) : confirmText;
            const int w = std::max(int(msg.size()) * 8 + 24, 120), h = page == Page::Capture ? 36 : 36 + lineH * 2;
            const int x = (engine::kViewW - w) / 2, y = (engine::kViewH - h) / 2;
            fill(x, y, w, h, cDark); frame(x, y, w, h, cDim); frame(x + 2, y + 2, w - 4, h - 4, cGrey);
            text(x + (w - int(msg.size()) * 8) / 2, y + 12, msg, cWhite);
            if (page == Page::Confirm)
                for (size_t i = 0; i < items.size(); ++i) {
                    const int ix = x + w / 2 - 12, iy = y + 12 + lineH + lineH * int(i);
                    text(ix, iy, items[i].label, int(i) == cursor ? cWhite : cGrey);
                    if (int(i) == cursor) cursorMark(ix - 10, iy, cWhite);
                }
            return;
        }
        int labelW = 0, valueW = 0;
        for (const MenuItem& it : items) { labelW = std::max(labelW, int(it.label.size())); valueW = std::max(valueW, int(it.value.size())); }
        const int cols = labelW + (valueW ? valueW + 2 : 0);
        const int w = std::clamp(cols * 8 + 40, 136, engine::kViewW - 16);
        const int h = 24 + lineH * int(items.size()) + 12;
        const int x = (engine::kViewW - w) / 2, y = std::max(4, (engine::kViewH - h) / 2);
        fill(x, y, w, h, cDark); frame(x, y, w, h, cDim); frame(x + 2, y + 2, w - 4, h - 4, cGrey);
        text(x + (w - int(title.size()) * 8) / 2, y + 8, title, cWhite);
        fill(x + 8, y + 19, w - 16, 1, cDim);
        for (size_t i = 0; i < items.size(); ++i) {
            const MenuItem& it = items[i];
            const int iy = y + 24 + lineH * int(i), ix = x + 20;
            const uint8_t c = !it.enabled ? cDim : int(i) == cursor ? cWhite : cGrey;
            text(ix, iy, it.label, c);
            if (!it.value.empty()) text(x + w - 12 - int(it.value.size()) * 8, iy, it.value, c);
            if (int(i) == cursor) cursorMark(ix - 12, iy, cWhite);
        }
    }

    // keys and pad presses while the menu is open
    void key(SDL_Scancode sc, bool alt) {
        if (page == Page::Capture) { captured(sc); return; }
        switch (sc) {
        case SDL_SCANCODE_UP: move(-1); break;
        case SDL_SCANCODE_DOWN: move(1); break;
        case SDL_SCANCODE_LEFT: if (page == Page::Confirm) move(-1); else change(-1); break;
        case SDL_SCANCODE_RIGHT: if (page == Page::Confirm) move(1); else change(1); break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
            if (alt && sc == SDL_SCANCODE_RETURN) break;
            if (page == Page::Confirm) confirmed(items[size_t(cursor)].id == 1); else accept();
            break;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: back(); break;
        default: break;
        }
    }
    void pad(uint32_t pressedBits) {
        if (page == Page::Capture) return;
        if (pressedBits & engine::kJoyUp) move(-1);
        if (pressedBits & engine::kJoyDown) move(1);
        if (pressedBits & engine::kJoyLeft) { if (page == Page::Confirm) move(-1); else change(-1); }
        if (pressedBits & engine::kJoyRight) { if (page == Page::Confirm) move(1); else change(1); }
        if (pressedBits & engine::kJoyJump) { if (page == Page::Confirm) confirmed(items[size_t(cursor)].id == 1); else accept(); }
        if (pressedBits & engine::kJoyShoot) back();
    }
};

void Shell::applyWindow() {
    SDL_SetWindowFullscreen(win, options.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!options.fullscreen && options.scale > 0) {
        SDL_SetWindowSize(win, engine::kViewW * options.scale, engine::kViewH * options.scale);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_RenderSetIntegerScale(ren, options.scale > 0 || options.fullscreen ? SDL_TRUE : SDL_FALSE);
}

} // namespace

int main(int argc, char** argv) {
    std::string hint;
    int startLevel = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-level" && i + 1 < argc) startLevel = std::atoi(argv[++i]);
        else if (a[0] != '-' && hint.empty()) hint = a;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const std::string ini = settingsPath();
    engine::Options options;
    options.load(ini);
    const std::string dataDir = findDataDir(hint);
    if (dataDir.empty()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pitfall",
                                 "Could not find the game's files (INIT.PH).\nPut the original game's *.PH files in a folder named "
                                 "\"original\" next to the program, or give the folder on the command line.", nullptr);
        return 1;
    }
    std::unique_ptr<game::Game> g;
    try { g = std::make_unique<game::Game>(dataDir, options); }
    catch (const std::exception& e) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pitfall", e.what(), nullptr); return 1; }
    if (startLevel >= 0) { g->setMode(game::Mode::Playing); g->startLevel(startLevel); }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");    // (whole pixels)
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    const int scale = std::clamp(options.scale, 0, 4);
    options.scale = scale;
    const int winW = scale ? engine::kViewW * scale : std::max(options.windowSizeX, engine::kViewW);
    const int winH = scale ? engine::kViewH * scale : std::max(options.windowSizeY, engine::kViewH);
    SDL_Window* win = SDL_CreateWindow("Pitfall: The Mayan Adventure", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (options.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, engine::kViewW, engine::kViewH);
    SDL_RenderSetIntegerScale(ren, scale > 0 || options.fullscreen ? SDL_TRUE : SDL_FALSE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, engine::kViewW, engine::kViewH);

    SDL_AudioSpec want{}, have{};
    want.freq = engine::Mixer::kOutputRate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    want.callback = audioCallback; want.userdata = g.get();
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev) { SDL_PauseAudioDevice(dev, 0); g->mixer().setDeviceClock(true); }
    SDL_Joystick* joy = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : nullptr;   // (the original's JoyStick option)
    // the port's gamepads (SDL's game controller mapping: the d-pad and the left stick, A / cross
    // jump, B / circle shoot, X / square whip, Y / triangle, Start and Back select)
    std::vector<SDL_GameController*> pads;
    auto openPads = [&]() {
        for (SDL_GameController* c : pads) SDL_GameControllerClose(c);
        pads.clear();
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_IsGameController(i)) if (SDL_GameController* c = SDL_GameControllerOpen(i)) pads.push_back(c);
    };
    openPads();
    uint32_t lastPad = 0;

    Shell shell(*g, options);
    shell.win = win; shell.ren = ren;

    engine::HostInput host;
    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    double acc = 0;
    bool running = true, redraw = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) openPads();
            if (ev.type == SDL_TEXTINPUT && !shell.open())            // WM_CHAR: the typed cheat codes
                for (const char* p = ev.text.text; *p; ++p) g->typeChar(uint8_t(*p));
            if (ev.type == SDL_KEYDOWN) {
                const SDL_Scancode sc = ev.key.keysym.scancode;
                const bool alt = (ev.key.keysym.mod & KMOD_ALT) != 0;
                if ((sc == SDL_SCANCODE_RETURN && alt) || sc == SDL_SCANCODE_F11) {
                    if (!ev.key.repeat) { options.fullscreen = !options.fullscreen; shell.applyWindow(); }
                    continue;
                }
                if (shell.open()) { if (!ev.key.repeat || sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN) shell.key(sc, alt); redraw = true; continue; }
                if (sc == SDL_SCANCODE_ESCAPE && !ev.key.repeat) { shell.openMenu(); redraw = true; continue; }
                if (sc == SDL_SCANCODE_PAUSE && !ev.key.repeat) g->togglePause();   // (any other key ends a pause)
                // F10 or Alt+Y (WM_SYSKEYDOWN / WM_SYSCHAR, 0x439c6c): Harry's special idle, and the like
                if (sc == SDL_SCANCODE_F10 || (sc == SDL_SCANCODE_Y && alt)) g->specialKey();
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        acc += double(now - last) / freq * (1000.0 / 17.0);    // the original's 17 ms timer
        last = now;
        if (acc > 6) acc = 6;
        bool ticked = false;
        while (acc >= 1.0) {
            acc -= 1.0;
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            host.keyDown.fill(false);
            if (!shell.open())
                for (int sc = 0; sc < SDL_NUM_SCANCODES; ++sc) {
                    if (!keys[sc]) continue;
                    const int vk = vkFromScancode(SDL_Scancode(sc));
                    if (vk >= 0) host.keyDown[size_t(vk)] = true;
                }
            host.ctrlDown = host.keyDown[0x11];
            host.joystickPresent = joy != nullptr;
            if (joy) {
                host.joyButtons = 0;
                for (int b = 0; b < 4; ++b) if (SDL_JoystickGetButton(joy, b)) host.joyButtons |= 1u << b;
                host.joyX = int(SDL_JoystickGetAxis(joy, 0)) + 0x8000;
                host.joyY = int(SDL_JoystickGetAxis(joy, 1)) + 0x8000;
            }
            uint32_t padBits = 0;
            for (SDL_GameController* c : pads) {
                auto on = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b) != 0; };
                if (on(SDL_CONTROLLER_BUTTON_DPAD_UP)) padBits |= engine::kJoyUp;
                if (on(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) padBits |= engine::kJoyDown;
                if (on(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) padBits |= engine::kJoyLeft;
                if (on(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) padBits |= engine::kJoyRight;
                if (on(SDL_CONTROLLER_BUTTON_A)) padBits |= engine::kJoyJump;
                if (on(SDL_CONTROLLER_BUTTON_B)) padBits |= engine::kJoyShoot;
                if (on(SDL_CONTROLLER_BUTTON_X)) padBits |= engine::kJoyWhip;
                if (on(SDL_CONTROLLER_BUTTON_Y) || on(SDL_CONTROLLER_BUTTON_BACK)) padBits |= engine::kJoySelect;
                if (on(SDL_CONTROLLER_BUTTON_START)) padBits |= engine::kJoySelect | engine::kJoyDebug;   // (Start: the menu too)
                const int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
                if (lx < -12000) padBits |= engine::kJoyLeft;
                if (lx > 12000) padBits |= engine::kJoyRight;
                if (ly < -12000) padBits |= engine::kJoyUp;
                if (ly > 12000) padBits |= engine::kJoyDown;
            }
            const uint32_t pressedBits = padBits & ~lastPad;
            lastPad = padBits;
            if (shell.open()) {
                if (pressedBits) { shell.pad(pressedBits); redraw = true; }
                continue;                                  // (the game stands still under the menu)
            }
            if (pressedBits & engine::kJoyDebug) { shell.openMenu(); redraw = true; continue; }
            host.padBits = padBits & ~uint32_t(engine::kJoyDebug);
            const uint32_t padButtons = engine::kJoyJump | engine::kJoyShoot | engine::kJoyWhip | engine::kJoySelect;
            host.padPressed = (pressedBits & padButtons) != 0;
            g->tick(host);
            ticked = true;
        }
        if (ticked || redraw) {
            if (ticked) g->render();
            shell.draw();
            redraw = false;
            void* pixels; int pitch;
            SDL_LockTexture(tex, nullptr, &pixels, &pitch);
            const ph::Palette& pal = g->palette();
            uint32_t lut[256];
            for (int i = 0; i < 256; ++i) { const ph::Rgb c = pal.shown(i); lut[i] = 0xff000000u | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b; }
            const uint8_t* src = g->framebuffer().viewBase(g->viewScrollX());
            for (int y = 0; y < engine::kViewH; ++y) {
                auto* d = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + size_t(y) * pitch);
                for (int x = 0; x < engine::kViewW; ++x) d[x] = lut[src[size_t(y) * engine::kBufW + x]];
            }
            SDL_UnlockTexture(tex);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
        } else {
            SDL_Delay(1);
        }
        if (const int d = g->takeCheatDialog()) {
            // The original shows its own dialogs (with photographs, from PITFALL.EXE's resources),
            // paused; here their words.
            g->setPaused(true);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Pitfall",
                                     d == 0x7f ? "Nice try! This isn't Doom!\n\nTin Guerrero, Andy Glaister, Scott Krager"
                                               : "What a job, eh?\n\nThe Testers!", win);
            g->setPaused(false);
        }
        if (g->quitRequested() || shell.quit) running = false;
    }
    if (dev) SDL_CloseAudioDevice(dev);
    if (joy) SDL_JoystickClose(joy);
    for (SDL_GameController* c : pads) SDL_GameControllerClose(c);
    if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP)) SDL_GetWindowSize(win, &options.windowSizeX, &options.windowSizeY);
    options.save(ini);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
