// GCPad Frontend — Reference application for the GCPad library.
//
// Demonstrates how to integrate GCPad into a project with:
//   - Controller detection and live input visualization
//   - Real-time controller-to-keyboard/mouse translation
//   - Customizable mapping profiles with save/load
//   - Rumble and LED testing
//
// Controls:
//   [1-4]         Switch active controller slot
//   [Tab]         Toggle input translation on/off
//   [P]           Cycle through mapping profiles
//   [S]           Save current profile to disk
//   [R]           Send rumble pulse
//   [L]           Cycle LED colour
//   [Q] / Ctrl+C  Quit
//
// Requires Windows 10 build 1511+ for ANSI virtual-terminal support.

#include "GamepadManager.h"
#include "gamepad_input_remapper.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <ctime>
#include <csignal>
#include <cmath>
#include <algorithm>
#include <conio.h>
#include <windows.h>
#include <filesystem>

// ── ANSI / True-color helpers ────────────────────────────────────────────────

static constexpr const char* RST  = "\033[0m";
static constexpr const char* BOLD = "\033[1m";
static constexpr const char* DIM  = "\033[2m";
static constexpr const char* ITAL = "\033[3m";
static constexpr const char* ULINE= "\033[4m";

// Standard colors
static constexpr const char* FG_RED = "\033[31m";
static constexpr const char* FG_GRN = "\033[32m";
static constexpr const char* FG_YLW = "\033[33m";
static constexpr const char* FG_BLU = "\033[34m";
static constexpr const char* FG_MAG = "\033[35m";
static constexpr const char* FG_CYN = "\033[36m";
static constexpr const char* FG_WHT = "\033[37m";
static constexpr const char* FG_GRY = "\033[90m";

// True-color foreground: \033[38;2;R;G;Bm
static std::string tc(int r, int g, int b) {
    return "\033[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}
// True-color background
static std::string tcbg(int r, int g, int b) {
    return "\033[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}

// Accent palette
static const std::string CLR_ACCENT  = tc(0, 200, 220);    // teal
static const std::string CLR_ACCENT2 = tc(80, 160, 220);   // soft blue
static const std::string CLR_WARM    = tc(240, 160, 50);    // warm orange
static const std::string CLR_DIM     = tc(90, 90, 100);     // dim gray
static const std::string CLR_DIMMER  = tc(55, 55, 65);      // darker gray
static const std::string CLR_FRAME   = tc(60, 140, 160);    // frame teal
static const std::string CLR_TITLE   = tc(220, 240, 255);   // bright white-blue
static const std::string CLR_PANEL   = tc(100, 120, 140);   // panel border

// Face button colors (Xbox-style)
static const std::string CLR_BTN_A   = tc(80, 220, 80);     // green
static const std::string CLR_BTN_B   = tc(230, 60, 60);     // red
static const std::string CLR_BTN_X   = tc(60, 120, 240);    // blue
static const std::string CLR_BTN_Y   = tc(240, 220, 50);    // yellow

// Bar gradient colors
static const std::string CLR_BAR_LO  = tc(40, 80, 120);     // dark blue
static const std::string CLR_BAR_MID = tc(60, 180, 180);    // teal
static const std::string CLR_BAR_HI  = tc(100, 240, 200);   // bright cyan-green

// Trigger gradient
static std::string triggerColor(float t) {
    // Interpolate from dim blue-gray to bright orange-red
    int r = static_cast<int>(60 + t * 200);
    int g = static_cast<int>(80 + t * 100 - t * t * 140);
    int b = static_cast<int>(120 - t * 100);
    return tc(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
}

// Battery color
static std::string batteryColor(float level) {
    if (level > 0.6f) return tc(80, 220, 100);
    if (level > 0.25f) return tc(240, 200, 50);
    return tc(240, 60, 60);
}

// ── Console helpers ──────────────────────────────────────────────────────────

static HANDLE g_hCon = INVALID_HANDLE_VALUE;
static constexpr int W = 80; // display width

static void con_init() {
    SetConsoleOutputCP(65001);
    SetConsoleTitleA("GCPad Frontend");
    g_hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(g_hCon, &mode);
    SetConsoleMode(g_hCon, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    CONSOLE_CURSOR_INFO ci = { 1, FALSE };
    SetConsoleCursorInfo(g_hCon, &ci);
}

static void con_restore() {
    CONSOLE_CURSOR_INFO ci = { 1, TRUE };
    SetConsoleCursorInfo(g_hCon, &ci);
    std::cout << RST << "\n" << std::flush;
}

static void con_goto(int x, int y) {
    SetConsoleCursorPosition(g_hCon, { static_cast<SHORT>(x), static_cast<SHORT>(y) });
}

static void con_clear() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hCon, &csbi);
    DWORD cells = static_cast<DWORD>(csbi.dwSize.X) * csbi.dwSize.Y, written;
    COORD home{ 0, 0 };
    FillConsoleOutputCharacterA(g_hCon, ' ', cells, home, &written);
    FillConsoleOutputAttribute(g_hCon, csbi.wAttributes, cells, home, &written);
}

// ── Box-drawing primitives ───────────────────────────────────────────────────

// Outer frame (double-line)
static std::string frameTop() {
    return CLR_FRAME + "\xE2\x95\x94" + std::string(W - 2, '\xCD') + // use raw bytes for ═
           "\xE2\x95\x97" + std::string(RST);
    // Actually let me use proper UTF-8 strings
}

// Let me define the box chars properly as string constants
static const char* BOX_TL  = "\xe2\x95\x94"; // ╔
static const char* BOX_TR  = "\xe2\x95\x97"; // ╗
static const char* BOX_BL  = "\xe2\x95\x9a"; // ╚
static const char* BOX_BR  = "\xe2\x95\x9d"; // ╝
static const char* BOX_H   = "\xe2\x95\x90"; // ═
static const char* BOX_V   = "\xe2\x95\x91"; // ║
static const char* BOX_LT  = "\xe2\x95\xa0"; // ╠
static const char* BOX_RT  = "\xe2\x95\xa3"; // ╣
static const char* BOX_TT  = "\xe2\x95\xa6"; // ╦
static const char* BOX_BT  = "\xe2\x95\xa9"; // ╩

// Single-line box
static const char* SB_TL   = "\xe2\x94\x8c"; // ┌
static const char* SB_TR   = "\xe2\x94\x90"; // ┐
static const char* SB_BL   = "\xe2\x94\x94"; // └
static const char* SB_BR   = "\xe2\x94\x98"; // ┘
static const char* SB_H    = "\xe2\x94\x80"; // ─
static const char* SB_V    = "\xe2\x94\x82"; // │

// Block characters
static const char* BLK_FULL = "\xe2\x96\x88"; // █
static const char* BLK_7    = "\xe2\x96\x89"; // ▉
static const char* BLK_6    = "\xe2\x96\x8a"; // ▊
static const char* BLK_5    = "\xe2\x96\x8b"; // ▋
static const char* BLK_4    = "\xe2\x96\x8c"; // ▌
static const char* BLK_3    = "\xe2\x96\x8d"; // ▍
static const char* BLK_SHADE_L = "\xe2\x96\x91"; // ░
static const char* BLK_SHADE_M = "\xe2\x96\x92"; // ▒
static const char* BLK_SHADE_D = "\xe2\x96\x93"; // ▓

// Special symbols
static const char* SYM_DOT    = "\xc2\xb7";     // ·
static const char* SYM_BULLET = "\xe2\x97\x8f"; // ●
static const char* SYM_CIRCLE = "\xe2\x97\x8b"; // ○
static const char* SYM_UP     = "\xe2\x96\xb2"; // ▲
static const char* SYM_DOWN   = "\xe2\x96\xbc"; // ▼
static const char* SYM_LEFT   = "\xe2\x97\x80"; // ◀
static const char* SYM_RIGHT  = "\xe2\x96\xb6"; // ▶
static const char* SYM_ZAP    = "\xe2\x9a\xa1"; // ⚡
static const char* SYM_SQUARE = "\xe2\x96\xa0"; // ■

// Repeat a UTF-8 box-drawing char n times
static std::string repeatUTF8(const char* ch, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += ch;
    return s;
}

static std::string hDouble(int n) { return repeatUTF8(BOX_H, n); }
static std::string hSingle(int n) { return repeatUTF8(SB_H, n); }

// Frame lines
static std::string fTop()  { return CLR_FRAME + BOX_TL + hDouble(W - 2) + BOX_TR + RST; }
static std::string fBot()  { return CLR_FRAME + BOX_BL + hDouble(W - 2) + BOX_BR + RST; }
static std::string fMid()  { return CLR_FRAME + BOX_LT + hDouble(W - 2) + BOX_RT + RST; }
static std::string fSide() { return CLR_FRAME + std::string(BOX_V) + RST; }

// Pad a visible string to a fixed width (accounting for ANSI escape codes)
// This is approximate — works for our controlled output.
static size_t visibleLength(const std::string& s) {
    size_t len = 0;
    bool inEsc = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033') { inEsc = true; continue; }
        if (inEsc) { if (s[i] == 'm') inEsc = false; continue; }
        // Count UTF-8 characters (lead bytes only)
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80 || c >= 0xC0) len++;
    }
    return len;
}

// Emit a line with frame borders, content padded to fill width
static std::string fLine(const std::string& content) {
    size_t vis = visibleLength(content);
    int pad = static_cast<int>(W - 2) - static_cast<int>(vis);
    if (pad < 0) pad = 0;
    return fSide() + content + std::string(pad, ' ') + fSide();
}

// ── App state ────────────────────────────────────────────────────────────────

static volatile bool g_running = true;
static void sig_handler(int) { g_running = false; }

static std::atomic<int>  g_slot{ 0 };
static std::atomic<bool> g_translating{ false };
static int g_frame_counter = 0;

// Event log
static std::mutex              g_log_mtx;
static std::deque<std::string> g_log;
static constexpr int           LOG_ROWS = 5;

static void log_push(std::string msg) {
    char tbuf[10];
    auto t = std::time(nullptr);
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", std::localtime(&t));
    std::lock_guard<std::mutex> lk(g_log_mtx);
    g_log.push_front(std::string("[") + tbuf + "] " + std::move(msg));
    if (static_cast<int>(g_log.size()) > LOG_ROWS)
        g_log.pop_back();
}

// LED colours for cycling
static const gcpad::Color LED_CYCLE[] = {
    { 255,   0,   0 }, { 0, 255,   0 }, { 0,   0, 255 },
    { 255, 128,   0 }, { 128, 0, 255 }, { 0, 255, 255 },
    { 255, 255, 255 }, { 0,   0,   0 },
};
static int g_led_idx = 0;

// ── Mapping profiles ─────────────────────────────────────────────────────────

struct MappingProfile {
    std::string name;
    struct ButtonMap      { gcpad::Button button; uint16_t vk; };
    struct ButtonMouseMap { gcpad::Button button; gcpad::MouseButton mouse; };
    struct AxisMouseMap   { gcpad::Axis axis; float sensitivity; float deadzone; bool invert; float curve; };
    struct AxisKeyMap     { gcpad::Axis axis; uint16_t vk; float threshold; bool negative; };
    struct AxisMouseBtnMap{ gcpad::Axis axis; gcpad::MouseButton mouse; float threshold; };

    std::vector<ButtonMap>       button_keys;
    std::vector<ButtonMouseMap>  button_mice;
    std::vector<AxisMouseMap>    axis_mice;
    std::vector<AxisKeyMap>      axis_keys;
    std::vector<AxisMouseBtnMap> axis_mouse_btns;

    void applyTo(gcpad::GamepadInputRemapper& remap) const {
        remap.clearAllButtonMappings();
        remap.clearAllAxisMappings();
        remap.resetState();
        for (auto& bk : button_keys)     remap.mapButtonToKey(bk.button, bk.vk);
        for (auto& bm : button_mice)     remap.mapButtonToMouseButton(bm.button, bm.mouse);
        for (auto& am : axis_mice)       remap.mapAxisToMouse(am.axis, am.sensitivity, am.deadzone, am.invert, am.curve);
        for (auto& ak : axis_keys)       remap.mapAxisToKey(ak.axis, ak.vk, ak.threshold, ak.negative);
        for (auto& ab : axis_mouse_btns) remap.mapAxisToMouseButton(ab.axis, ab.mouse, ab.threshold);
    }
};

static const char* buttonName(gcpad::Button b) {
    static const char* n[] = {
        "A","B","X","Y","Start","Select","Guide",
        "L1","R1","L2","R2","L3","R3",
        "DPad Up","DPad Dn","DPad Lt","DPad Rt","Touch"
    };
    int i = static_cast<int>(b);
    return (i >= 0 && i < static_cast<int>(gcpad::Button::COUNT)) ? n[i] : "?";
}

static const char* axisName(gcpad::Axis a) {
    static const char* n[] = { "LX","LY","RX","RY","LT","RT" };
    int i = static_cast<int>(a);
    return (i >= 0 && i < static_cast<int>(gcpad::Axis::COUNT)) ? n[i] : "?";
}

static const char* mouseButtonName(gcpad::MouseButton mb) {
    switch (mb) {
        case gcpad::MouseButton::Left:   return "LMB";
        case gcpad::MouseButton::Right:  return "RMB";
        case gcpad::MouseButton::Middle: return "MMB";
    }
    return "?";
}

// ── Built-in profiles ────────────────────────────────────────────────────────

static MappingProfile makeProfileFPS() {
    MappingProfile p;
    p.name = "FPS / Shooter";
    p.axis_keys.push_back({ gcpad::Axis::LeftY,  'W', 0.5f, true  });
    p.axis_keys.push_back({ gcpad::Axis::LeftY,  'S', 0.5f, false });
    p.axis_keys.push_back({ gcpad::Axis::LeftX,  'A', 0.5f, true  });
    p.axis_keys.push_back({ gcpad::Axis::LeftX,  'D', 0.5f, false });
    p.axis_mice.push_back({ gcpad::Axis::RightX, 15.0f, 0.12f, false, 2.0f });
    p.axis_mice.push_back({ gcpad::Axis::RightY, 15.0f, 0.12f, false, 2.0f });
    p.axis_mouse_btns.push_back({ gcpad::Axis::LeftTrigger,  gcpad::MouseButton::Right, 0.3f });
    p.axis_mouse_btns.push_back({ gcpad::Axis::RightTrigger, gcpad::MouseButton::Left,  0.3f });
    p.button_keys.push_back({ gcpad::Button::A, VK_SPACE });
    p.button_keys.push_back({ gcpad::Button::B, 'C' });
    p.button_keys.push_back({ gcpad::Button::X, 'R' });
    p.button_keys.push_back({ gcpad::Button::Y, 'E' });
    p.button_keys.push_back({ gcpad::Button::L1, 'Q' });
    p.button_keys.push_back({ gcpad::Button::R1, 'F' });
    p.button_keys.push_back({ gcpad::Button::L3, VK_LSHIFT });
    p.button_keys.push_back({ gcpad::Button::R3, 'V' });
    p.button_keys.push_back({ gcpad::Button::DPad_Up,    '1' });
    p.button_keys.push_back({ gcpad::Button::DPad_Right, '2' });
    p.button_keys.push_back({ gcpad::Button::DPad_Down,  '3' });
    p.button_keys.push_back({ gcpad::Button::DPad_Left,  '4' });
    p.button_keys.push_back({ gcpad::Button::Start,  VK_ESCAPE });
    p.button_keys.push_back({ gcpad::Button::Select, VK_TAB });
    return p;
}

static MappingProfile makeProfilePlatformer() {
    MappingProfile p;
    p.name = "Platformer";
    p.axis_keys.push_back({ gcpad::Axis::LeftY,  VK_UP,    0.5f, true  });
    p.axis_keys.push_back({ gcpad::Axis::LeftY,  VK_DOWN,  0.5f, false });
    p.axis_keys.push_back({ gcpad::Axis::LeftX,  VK_LEFT,  0.5f, true  });
    p.axis_keys.push_back({ gcpad::Axis::LeftX,  VK_RIGHT, 0.5f, false });
    p.button_keys.push_back({ gcpad::Button::A, 'Z' });
    p.button_keys.push_back({ gcpad::Button::B, 'X' });
    p.button_keys.push_back({ gcpad::Button::X, 'C' });
    p.button_keys.push_back({ gcpad::Button::Y, 'V' });
    p.button_keys.push_back({ gcpad::Button::L1, 'A' });
    p.button_keys.push_back({ gcpad::Button::R1, 'S' });
    p.button_keys.push_back({ gcpad::Button::L2, 'Q' });
    p.button_keys.push_back({ gcpad::Button::R2, 'W' });
    p.button_keys.push_back({ gcpad::Button::Start,  VK_RETURN });
    p.button_keys.push_back({ gcpad::Button::Select, VK_ESCAPE });
    return p;
}

static MappingProfile makeProfileDesktop() {
    MappingProfile p;
    p.name = "Desktop Nav";
    p.axis_mice.push_back({ gcpad::Axis::LeftX, 20.0f, 0.1f, false, 1.5f });
    p.axis_mice.push_back({ gcpad::Axis::LeftY, 20.0f, 0.1f, false, 1.5f });
    p.axis_mouse_btns.push_back({ gcpad::Axis::RightTrigger, gcpad::MouseButton::Left,  0.3f });
    p.axis_mouse_btns.push_back({ gcpad::Axis::LeftTrigger,  gcpad::MouseButton::Right, 0.3f });
    p.button_keys.push_back({ gcpad::Button::A, VK_RETURN });
    p.button_keys.push_back({ gcpad::Button::B, VK_ESCAPE });
    p.button_keys.push_back({ gcpad::Button::X, VK_BACK });
    p.button_keys.push_back({ gcpad::Button::Y, VK_TAB });
    p.button_keys.push_back({ gcpad::Button::DPad_Up,    VK_UP });
    p.button_keys.push_back({ gcpad::Button::DPad_Down,  VK_DOWN });
    p.button_keys.push_back({ gcpad::Button::DPad_Left,  VK_LEFT });
    p.button_keys.push_back({ gcpad::Button::DPad_Right, VK_RIGHT });
    p.button_keys.push_back({ gcpad::Button::L1, VK_PRIOR });
    p.button_keys.push_back({ gcpad::Button::R1, VK_NEXT });
    return p;
}

static MappingProfile makeProfileHalfLife() {
    MappingProfile p;
    p.name = "Half-Life";
    // Left stick -> WASD
    p.axis_keys.push_back({ gcpad::Axis::LeftY,  'W', 0.5f, true  }); // forward
    p.axis_keys.push_back({ gcpad::Axis::LeftY,  'S', 0.5f, false }); // backward
    p.axis_keys.push_back({ gcpad::Axis::LeftX,  'A', 0.5f, true  }); // strafe left
    p.axis_keys.push_back({ gcpad::Axis::LeftX,  'D', 0.5f, false }); // strafe right
    // Right stick -> mouse look
    p.axis_mice.push_back({ gcpad::Axis::RightX, 14.0f, 0.12f, false, 2.0f });
    p.axis_mice.push_back({ gcpad::Axis::RightY, 14.0f, 0.12f, false, 2.0f });
    // Triggers -> fire
    p.axis_mouse_btns.push_back({ gcpad::Axis::RightTrigger, gcpad::MouseButton::Left,  0.3f }); // primary
    p.axis_mouse_btns.push_back({ gcpad::Axis::LeftTrigger,  gcpad::MouseButton::Right, 0.3f }); // secondary
    // Face buttons
    p.button_keys.push_back({ gcpad::Button::A, VK_SPACE });    // jump
    p.button_keys.push_back({ gcpad::Button::B, VK_LCONTROL }); // crouch
    p.button_keys.push_back({ gcpad::Button::X, 'R' });         // reload
    p.button_keys.push_back({ gcpad::Button::Y, 'E' });         // use
    // Bumpers
    p.button_keys.push_back({ gcpad::Button::L1, 'Q' });        // last weapon
    p.button_keys.push_back({ gcpad::Button::R1, 'F' });        // flashlight
    // Stick clicks
    p.button_keys.push_back({ gcpad::Button::L3, VK_LSHIFT });  // walk
    p.button_keys.push_back({ gcpad::Button::R3, 'G' });        // spray
    // D-pad -> weapon slots
    p.button_keys.push_back({ gcpad::Button::DPad_Up,    '1' }); // crowbar / melee
    p.button_keys.push_back({ gcpad::Button::DPad_Right, '2' }); // pistols
    p.button_keys.push_back({ gcpad::Button::DPad_Down,  '3' }); // shotguns / SMG
    p.button_keys.push_back({ gcpad::Button::DPad_Left,  '4' }); // explosives
    // System
    p.button_keys.push_back({ gcpad::Button::Start,  VK_ESCAPE }); // menu
    p.button_keys.push_back({ gcpad::Button::Select, VK_TAB });    // scoreboard
    return p;
}

static MappingProfile makeProfileHalfLife2() {
    MappingProfile p;
    p.name = "Half-Life 2";
    // Left stick -> WASD
    p.axis_keys.push_back({ gcpad::Axis::LeftY,  'W', 0.5f, true  }); // forward
    p.axis_keys.push_back({ gcpad::Axis::LeftY,  'S', 0.5f, false }); // backward
    p.axis_keys.push_back({ gcpad::Axis::LeftX,  'A', 0.5f, true  }); // strafe left
    p.axis_keys.push_back({ gcpad::Axis::LeftX,  'D', 0.5f, false }); // strafe right
    // Right stick -> mouse look (slightly higher sens for Source engine)
    p.axis_mice.push_back({ gcpad::Axis::RightX, 16.0f, 0.10f, false, 2.2f });
    p.axis_mice.push_back({ gcpad::Axis::RightY, 16.0f, 0.10f, false, 2.2f });
    // Triggers -> fire (lower threshold for responsive gravity gun)
    p.axis_mouse_btns.push_back({ gcpad::Axis::RightTrigger, gcpad::MouseButton::Left,  0.25f }); // primary / grab
    p.axis_mouse_btns.push_back({ gcpad::Axis::LeftTrigger,  gcpad::MouseButton::Right, 0.25f }); // secondary / punt
    // Face buttons
    p.button_keys.push_back({ gcpad::Button::A, VK_SPACE });    // jump
    p.button_keys.push_back({ gcpad::Button::B, VK_LCONTROL }); // crouch
    p.button_keys.push_back({ gcpad::Button::X, 'R' });         // reload
    p.button_keys.push_back({ gcpad::Button::Y, 'E' });         // use / pick up
    // Bumpers
    p.button_keys.push_back({ gcpad::Button::L1, 'F' });        // flashlight
    p.button_keys.push_back({ gcpad::Button::R1, 'Q' });        // last weapon / quick-switch
    // Stick clicks
    p.button_keys.push_back({ gcpad::Button::L3, VK_LSHIFT });  // sprint
    p.button_keys.push_back({ gcpad::Button::R3, 'Z' });        // suit zoom
    // D-pad -> weapon slots
    p.button_keys.push_back({ gcpad::Button::DPad_Up,    '1' }); // crowbar / gravity gun
    p.button_keys.push_back({ gcpad::Button::DPad_Right, '2' }); // pistol / magnum
    p.button_keys.push_back({ gcpad::Button::DPad_Down,  '3' }); // SMG / pulse rifle
    p.button_keys.push_back({ gcpad::Button::DPad_Left,  '4' }); // shotgun / crossbow
    // System
    p.button_keys.push_back({ gcpad::Button::Start,  VK_ESCAPE }); // menu
    p.button_keys.push_back({ gcpad::Button::Select, VK_TAB });    // scoreboard
    p.button_keys.push_back({ gcpad::Button::Guide,  '5' });       // RPG / grenades
    return p;
}

static std::vector<MappingProfile> g_profiles;
static int g_active_profile = 0;
static gcpad::GamepadInputRemapper g_remapper;

static void initProfiles() {
    g_profiles.push_back(makeProfileHalfLife());
    g_profiles.push_back(makeProfileHalfLife2());
    g_profiles.push_back(makeProfileFPS());
    g_profiles.push_back(makeProfilePlatformer());
    g_profiles.push_back(makeProfileDesktop());
    g_profiles[0].applyTo(g_remapper);
}

static void cycleProfile() {
    g_active_profile = (g_active_profile + 1) % static_cast<int>(g_profiles.size());
    g_profiles[g_active_profile].applyTo(g_remapper);
    log_push("Profile: " + g_profiles[g_active_profile].name);
}

static void saveProfile(const MappingProfile& profile) {
    std::string dir = "config";
    std::filesystem::create_directories(dir);
    std::string path = dir + "/" + profile.name + ".gcpad_profile";
    std::ofstream f(path);
    if (!f.is_open()) { log_push("Save failed: " + path); return; }
    f << "# GCPad Mapping Profile\nname=" << profile.name << "\n\n";
    for (auto& bk : profile.button_keys)
        f << "button_key " << buttonName(bk.button) << " 0x"
          << std::hex << bk.vk << std::dec
          << " # " << gcpad::GamepadInputRemapper::virtualKeyName(bk.vk) << "\n";
    for (auto& bm : profile.button_mice)
        f << "button_mouse " << buttonName(bm.button) << " " << mouseButtonName(bm.mouse) << "\n";
    for (auto& am : profile.axis_mice)
        f << "axis_mouse " << axisName(am.axis) << " sens=" << am.sensitivity
          << " dz=" << am.deadzone << " inv=" << am.invert << " curve=" << am.curve << "\n";
    for (auto& ak : profile.axis_keys)
        f << "axis_key " << axisName(ak.axis) << " 0x" << std::hex << ak.vk << std::dec
          << " thr=" << ak.threshold << " neg=" << ak.negative << "\n";
    for (auto& ab : profile.axis_mouse_btns)
        f << "axis_mbtn " << axisName(ab.axis) << " " << mouseButtonName(ab.mouse)
          << " thr=" << ab.threshold << "\n";
    log_push("Saved: " + path);
}

// ── Visual widgets ───────────────────────────────────────────────────────────

// Color-coded face button widget
static std::string faceBtn(const char* label, bool pressed, const std::string& color) {
    if (pressed) return color + BOLD + "[" + label + "]" + RST;
    return CLR_DIMMER + "[" + std::string(label) + "]" + RST;
}

// Generic button widget
static std::string btnWidget(const char* label, bool pressed) {
    if (pressed) return std::string(BOLD) + FG_WHT + "[" + label + "]" + RST;
    return CLR_DIMMER + "[" + std::string(label) + "]" + RST;
}

// Trigger bar with gradient blocks (20 chars wide)
static std::string triggerBar(const char* label, float value) {
    constexpr int BAR_W = 20;
    int filled = static_cast<int>(value * BAR_W);
    filled = std::clamp(filled, 0, BAR_W);

    std::ostringstream s;
    s << CLR_DIM << label << " ";

    for (int i = 0; i < BAR_W; ++i) {
        float t = static_cast<float>(i) / BAR_W;
        if (i < filled) {
            s << triggerColor(t) << BLK_FULL;
        } else {
            s << CLR_DIMMER << BLK_SHADE_L;
        }
    }
    s << RST << CLR_DIM << " " << std::fixed << std::setprecision(2) << value << RST;
    return s.str();
}

// 2D stick visualizer (9 wide x 5 tall, returns vector of 5 strings)
static std::vector<std::string> stickViz(const char* label, float x, float y) {
    constexpr int GW = 9;  // grid width (internal)
    constexpr int GH = 5;  // grid height (internal)

    // Map stick to grid position
    int px = static_cast<int>((x + 1.0f) * 0.5f * (GW - 1) + 0.5f);
    int py = static_cast<int>((y + 1.0f) * 0.5f * (GH - 1) + 0.5f);
    px = std::clamp(px, 0, GW - 1);
    py = std::clamp(py, 0, GH - 1);

    // Calculate distance from center for color
    float dist = std::sqrt(x * x + y * y);
    dist = std::min(dist, 1.0f);
    std::string dotColor = tc(
        static_cast<int>(80 + dist * 175),
        static_cast<int>(220 - dist * 100),
        static_cast<int>(220 - dist * 80)
    );

    std::vector<std::string> lines;

    // Label
    std::ostringstream lbl;
    lbl << CLR_DIM << "  " << label << RST;
    // Pad label to match grid width
    lines.push_back(lbl.str());

    // Top border
    lines.push_back(CLR_PANEL + "  " + SB_TL + hSingle(GW * 2 + 1) + SB_TR + RST);

    // Grid rows
    for (int row = 0; row < GH; ++row) {
        std::ostringstream r;
        r << CLR_PANEL << "  " << SB_V << RST;
        for (int col = 0; col < GW; ++col) {
            r << " ";
            if (col == px && row == py) {
                r << dotColor << BOLD << SYM_BULLET << RST;
            } else if (col == GW / 2 && row == GH / 2) {
                // Center crosshair
                r << CLR_DIM << "+" << RST;
            } else if (col == GW / 2 || row == GH / 2) {
                // Crosshair lines
                r << CLR_DIMMER << SYM_DOT << RST;
            } else {
                r << " ";
            }
        }
        r << " " << CLR_PANEL << SB_V << RST;
        lines.push_back(r.str());
    }

    // Bottom border
    lines.push_back(CLR_PANEL + "  " + SB_BL + hSingle(GW * 2 + 1) + SB_BR + RST);

    return lines;
}

// Battery bar widget
static std::string batteryWidget(float level, bool charging) {
    constexpr int BAR_W = 10;
    int filled = static_cast<int>(level * BAR_W);
    filled = std::clamp(filled, 0, BAR_W);
    int pct = static_cast<int>(level * 100.0f);

    std::ostringstream s;
    s << CLR_DIM << "BAT " << RST;
    std::string col = batteryColor(level);
    for (int i = 0; i < BAR_W; ++i) {
        if (i < filled) s << col << BLK_FULL;
        else s << CLR_DIMMER << BLK_SHADE_L;
    }
    s << RST << " " << col << pct << "%" << RST;
    if (charging) s << " " << CLR_WARM << SYM_ZAP << RST;
    return s.str();
}

// ── Main draw ────────────────────────────────────────────────────────────────

static void draw(gcpad::GamepadManager* mgr) {
    con_goto(0, 0);
    std::ostringstream out;
    int lineCount = 0;

    auto emit = [&](const std::string& s = "") {
        out << s << "\033[K\n";
        lineCount++;
    };

    // ── Header ───────────────────────────────────────────────────────────────
    emit(fTop());

    // Title line with gradient
    {
        std::ostringstream h;
        h << "  " << tc(0, 180, 200) << BOLD << SYM_SQUARE << " "
          << tc(60, 200, 220) << "G"
          << tc(80, 210, 230) << "C"
          << tc(100, 220, 240) << "P"
          << tc(120, 230, 245) << "a"
          << tc(140, 235, 248) << "d"
          << RST
          << "  " << CLR_DIM << SB_V << RST
          << "  " << CLR_DIM << "Profile: " << RST << BOLD << CLR_WARM
          << g_profiles[g_active_profile].name << RST
          << "  " << CLR_DIM << SB_V << RST
          << "  ";
        if (g_translating.load()) {
            // Pulsing dot effect
            bool bright = (g_frame_counter / 15) % 2 == 0;
            h << (bright ? tc(50, 255, 100) : tc(30, 180, 70)) << BOLD << SYM_BULLET
              << " ACTIVE" << RST;
        } else {
            h << CLR_DIMMER << SYM_CIRCLE << " OFF" << RST;
        }
        emit(fLine(h.str()));
    }
    emit(fMid());

    // ── Slot selector ────────────────────────────────────────────────────────
    {
        std::ostringstream tabs;
        for (int i = 0; i < mgr->getMaxGamepads(); ++i) {
            auto* p = mgr->getGamepad(i);
            bool conn = p && p->isConnected();
            bool active = (i == g_slot);

            if (i > 0) tabs << " ";

            if (active && conn) {
                std::string name = p->getName();
                if (name.size() > 16) name = name.substr(0, 16);
                tabs << CLR_ACCENT << BOLD << " " << SYM_RIGHT << " "
                     << (i + 1) << ": " << name << " " << RST;
            } else if (active && !conn) {
                tabs << CLR_DIM << BOLD << " " << SYM_RIGHT << " "
                     << (i + 1) << ": " << SYM_DOT << SYM_DOT << " " << RST;
            } else if (conn) {
                std::string name = p->getName();
                if (name.size() > 12) name = name.substr(0, 12);
                tabs << CLR_DIM << "  " << (i + 1) << ": " << name << " " << RST;
            } else {
                tabs << CLR_DIMMER << "  " << (i + 1) << ": " << SYM_DOT << SYM_DOT << " " << RST;
            }
        }
        emit(fLine("  " + tabs.str()));
    }
    emit(fMid());

    // ── Controller display ───────────────────────────────────────────────────
    int slot = g_slot.load();
    auto* pad = mgr->getGamepad(slot);

    if (!pad || !pad->isConnected()) {
        emit(fLine(""));
        emit(fLine(""));
        emit(fLine(""));
        std::string msg = "    " + CLR_DIM + std::string(ITAL)
                        + "No controller in slot " + std::to_string(slot + 1)
                        + "  " + SYM_DOT + "  Connect a gamepad or press 1-4 to switch" + RST;
        emit(fLine(msg));
        // Pad to same height as connected view
        for (int i = 0; i < 15; ++i) emit(fLine(""));
    } else {
        const auto& st = pad->getState();

        // Device name + serial
        {
            std::ostringstream s;
            s << "  " << CLR_TITLE << BOLD << pad->getName() << RST;
            std::string serial = pad->getSerialNumber();
            if (!serial.empty())
                s << "  " << CLR_DIMMER << "(" << serial << ")" << RST;
            emit(fLine(s.str()));
        }
        emit(fLine(""));

        // ── Buttons row 1: Bumpers ──────────────────────────────────────────
        {
            std::ostringstream s;
            s << "  "
              << btnWidget("L1", st.isButtonPressed(gcpad::Button::L1)) << " "
              << btnWidget("L2", st.isButtonPressed(gcpad::Button::L2))
              << "                              "
              << btnWidget("R2", st.isButtonPressed(gcpad::Button::R2)) << " "
              << btnWidget("R1", st.isButtonPressed(gcpad::Button::R1));
            emit(fLine(s.str()));
        }

        // ── Buttons row 2: D-pad + Face buttons ────────────────────────────
        {
            std::ostringstream s;
            s << "                "
              << btnWidget("^", st.isButtonPressed(gcpad::Button::DPad_Up))
              << "                              "
              << faceBtn("Y", st.isButtonPressed(gcpad::Button::Y), CLR_BTN_Y);
            emit(fLine(s.str()));
        }
        {
            std::ostringstream s;
            s << "  "
              << btnWidget("SEL", st.isButtonPressed(gcpad::Button::Select))
              << "  "
              << btnWidget("<", st.isButtonPressed(gcpad::Button::DPad_Left))
              << " "
              << btnWidget(">", st.isButtonPressed(gcpad::Button::DPad_Right))
              << "  "
              << btnWidget("GUI", st.isButtonPressed(gcpad::Button::Guide))
              << "             "
              << faceBtn("X", st.isButtonPressed(gcpad::Button::X), CLR_BTN_X)
              << "   "
              << faceBtn("B", st.isButtonPressed(gcpad::Button::B), CLR_BTN_B)
              << "  "
              << btnWidget("STA", st.isButtonPressed(gcpad::Button::Start));
            emit(fLine(s.str()));
        }
        {
            std::ostringstream s;
            s << "                "
              << btnWidget("v", st.isButtonPressed(gcpad::Button::DPad_Down))
              << "                              "
              << faceBtn("A", st.isButtonPressed(gcpad::Button::A), CLR_BTN_A);
            emit(fLine(s.str()));
        }

        emit(fLine(""));

        // ── Stick visualizers (side by side) ────────────────────────────────
        auto leftStick  = stickViz("L Stick",
                                   st.getAxis(gcpad::Axis::LeftX),
                                   st.getAxis(gcpad::Axis::LeftY));
        auto rightStick = stickViz("R Stick",
                                   st.getAxis(gcpad::Axis::RightX),
                                   st.getAxis(gcpad::Axis::RightY));

        // Stick visuals are 7 lines each. Render them side by side with
        // trigger bars on the right.
        for (size_t row = 0; row < leftStick.size() && row < rightStick.size(); ++row) {
            std::ostringstream s;
            s << leftStick[row];

            // Pad between sticks (account for visible width)
            size_t lvis = visibleLength(leftStick[row]);
            int gap = 28 - static_cast<int>(lvis);
            if (gap > 0) s << std::string(gap, ' ');

            s << rightStick[row];

            // Right column: triggers and battery
            size_t rvis = visibleLength(s.str());
            int rgap = 50 - static_cast<int>(rvis);
            if (rgap > 0) s << std::string(rgap, ' ');

            if (row == 1) s << triggerBar("LT", st.getAxis(gcpad::Axis::LeftTrigger));
            if (row == 3) s << triggerBar("RT", st.getAxis(gcpad::Axis::RightTrigger));
            if (row == 5) s << batteryWidget(st.battery_level, st.is_charging);
            if (row == 6) {
                // Stick click indicators
                s << "  "
                  << btnWidget("L3", st.isButtonPressed(gcpad::Button::L3))
                  << "            "
                  << btnWidget("R3", st.isButtonPressed(gcpad::Button::R3));
            }

            emit(fLine(s.str()));
        }

        // ── IMU data (if available) ──────────────────────────────────────────
        bool has_imu = (st.gyro.x != 0.0f || st.gyro.y != 0.0f || st.gyro.z != 0.0f
                     || st.accel.x != 0.0f || st.accel.y != 0.0f || st.accel.z != 0.0f);
        if (has_imu) {
            std::ostringstream imu;
            imu << "  " << CLR_DIM
                << "Gyro " << std::fixed << std::setprecision(0)
                << std::showpos << st.gyro.x << " " << st.gyro.y << " " << st.gyro.z
                << "   Accel "
                << std::setprecision(1)
                << st.accel.x << " " << st.accel.y << " " << st.accel.z
                << std::noshowpos << RST;
            emit(fLine(imu.str()));
        } else {
            emit(fLine(""));
        }
    }

    // ── Mapping panel ────────────────────────────────────────────────────────
    emit(fMid());
    {
        const auto& prof = g_profiles[g_active_profile];
        std::ostringstream ms;
        ms << "  " << CLR_ACCENT2 << BOLD << "Mappings" << RST
           << "  " << CLR_DIM
           << prof.button_keys.size() << " btn" << SYM_RIGHT << "key  "
           << prof.axis_keys.size() << " axis" << SYM_RIGHT << "key  "
           << prof.axis_mice.size() << " axis" << SYM_RIGHT << "mouse  "
           << prof.axis_mouse_btns.size() << " axis" << SYM_RIGHT << "click" << RST;
        emit(fLine(ms.str()));

        // Show mappings in a compact colored row
        std::ostringstream d;
        d << "  " << CLR_DIM;
        int shown = 0;
        for (auto& bk : prof.button_keys) {
            if (shown >= 8) { d << " " << SYM_DOT << SYM_DOT << SYM_DOT; break; }
            if (shown > 0) d << "  ";
            d << RST << CLR_PANEL << buttonName(bk.button) << RST
              << CLR_DIMMER << SYM_RIGHT << RST
              << CLR_DIM << gcpad::GamepadInputRemapper::virtualKeyName(bk.vk) << RST;
            ++shown;
        }
        emit(fLine(d.str()));
    }

    // ── Hotkey bar ───────────────────────────────────────────────────────────
    emit(fMid());
    {
        std::ostringstream hk;
        hk << "  "
           << CLR_ACCENT << "1-4" << RST << CLR_DIM << " Slot" << RST << "  "
           << CLR_DIM << SB_V << RST << "  "
           << CLR_ACCENT << "Tab" << RST << CLR_DIM << " Translate" << RST << "  "
           << CLR_DIM << SB_V << RST << "  "
           << CLR_ACCENT << "P" << RST << CLR_DIM << " Profile" << RST << "  "
           << CLR_DIM << SB_V << RST << "  "
           << CLR_ACCENT << "S" << RST << CLR_DIM << " Save" << RST << "  "
           << CLR_DIM << SB_V << RST << "  "
           << CLR_ACCENT << "R" << RST << CLR_DIM << " Rumble" << RST << "  "
           << CLR_DIM << SB_V << RST << "  "
           << CLR_ACCENT << "L" << RST << CLR_DIM << " LED" << RST << "  "
           << CLR_DIM << SB_V << RST << "  "
           << CLR_ACCENT << "Q" << RST << CLR_DIM << " Quit" << RST;
        emit(fLine(hk.str()));
    }

    // ── Event log ────────────────────────────────────────────────────────────
    emit(fMid());
    {
        std::ostringstream lh;
        lh << "  " << CLR_ACCENT2 << "Events" << RST;
        emit(fLine(lh.str()));
    }
    {
        std::lock_guard<std::mutex> lk(g_log_mtx);
        for (int i = 0; i < LOG_ROWS; ++i) {
            if (i < static_cast<int>(g_log.size())) {
                emit(fLine("  " + CLR_DIM + g_log[i] + RST));
            } else {
                emit(fLine(""));
            }
        }
    }
    emit(fBot());

    std::cout << out.str() << std::flush;
}

// ── Entry point ──────────────────────────────────────────────────────────────

int main() {
    std::signal(SIGINT, sig_handler);
    con_init();
    con_clear();

    initProfiles();
    log_push("Loaded " + std::to_string(g_profiles.size()) + " mapping profiles");

    auto manager = gcpad::createGamepadManager();

    manager->setGamepadConnectedCallback([](int idx) {
        log_push("Connected: slot " + std::to_string(idx + 1));
    });
    manager->setGamepadDisconnectedCallback([](int idx) {
        log_push("Disconnected: slot " + std::to_string(idx + 1));
    });

    if (!manager->initialize()) {
        con_restore();
        std::cerr << "Failed to initialize GCPad: "
                  << manager->getLastError() << "\n";
        return 1;
    }

    log_push("GCPad initialized");

    for (int i = 0; i < manager->getMaxGamepads(); ++i) {
        auto* p = manager->getGamepad(i);
        if (p && p->isConnected()) {
            g_slot = i;
            log_push("Auto-selected slot " + std::to_string(i + 1)
                      + " (" + p->getName() + ")");
            break;
        }
    }

    gcpad::GamepadState prev_state;

    while (g_running) {
        manager->updateAll();
        g_frame_counter++;

        // Input translation
        if (g_translating.load()) {
            auto* pad = manager->getGamepad(g_slot.load());
            if (pad && pad->isConnected()) {
                const auto& cur = pad->getState();
                g_remapper.sendInput(cur, prev_state);
                prev_state = cur;
            }
        } else {
            auto* pad = manager->getGamepad(g_slot.load());
            if (pad && pad->isConnected())
                prev_state = pad->getState();
        }

        // Keyboard input
        while (_kbhit()) {
            int ch = _getch();
            if (ch == 0 || ch == 0xE0) { _getch(); continue; }

            if (ch == 'q' || ch == 'Q') { g_running = false; break; }

            if (ch >= '1' && ch <= '4') {
                int ns = ch - '1';
                if (ns < manager->getMaxGamepads()) {
                    g_slot = ns;
                    auto* pad = manager->getGamepad(ns);
                    if (pad && pad->isConnected()) prev_state = pad->getState();
                    else prev_state = gcpad::GamepadState{};
                    g_remapper.resetState();
                    log_push("Slot " + std::to_string(ns + 1));
                }
            }

            if (ch == '\t') {
                bool was = g_translating.load();
                g_translating = !was;
                if (!was) {
                    auto* pad = manager->getGamepad(g_slot.load());
                    if (pad && pad->isConnected()) prev_state = pad->getState();
                    g_remapper.resetState();
                }
                log_push(std::string("Translation ") + (!was ? "ENABLED" : "DISABLED"));
            }

            if (ch == 'p' || ch == 'P') cycleProfile();
            if (ch == 's' || ch == 'S') saveProfile(g_profiles[g_active_profile]);

            if (ch == 'r' || ch == 'R') {
                auto* p = manager->getGamepad(g_slot.load());
                if (p && p->isConnected()) {
                    p->setRumble({ 180, 180 });
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    p->setRumble({ 0, 0 });
                    log_push("Rumble " + std::to_string(g_slot.load() + 1));
                }
            }

            if (ch == 'l' || ch == 'L') {
                auto* p = manager->getGamepad(g_slot.load());
                if (p && p->isConnected()) {
                    p->setLED(LED_CYCLE[g_led_idx]);
                    g_led_idx = (g_led_idx + 1) % (sizeof(LED_CYCLE) / sizeof(LED_CYCLE[0]));
                    log_push("LED cycled");
                }
            }
        }

        draw(manager.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    con_clear();
    con_goto(0, 0);
    con_restore();
    std::cout << "GCPad Frontend exited.\n" << std::flush;
    manager->shutdown();
    return 0;
}
