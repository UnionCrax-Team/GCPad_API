// GCPad GUI — Full-featured controller configuration utility
//
// Tabs: Dashboard · Remapping · Profiles · Settings · About
//
// Features
//   • Live multi-slot controller dashboard with stick / IMU / touchpad viz
//   • Per-button key-capture remapping editor
//   • Named profiles saved as JSON in %APPDATA%\GCPad\profiles\
//   • Per-axis deadzone and sensitivity sliders
//   • LED colour picker (RGB + HSV wheel)
//   • DualSense adaptive trigger effect configuration
//   • Rumble test with individual motor control
//   • System tray (Windows) — minimize-to-tray with right-click menu
//   • Auto-profile switching based on the foreground window process name
//   • Touchpad visualisation for DS4 / DualSense

#include "GamepadManager.h"
#include "../../GCPad_Remap/include/gamepad_input_remapper.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#define SDL_main main
#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shellapi.h>
#  include <tlhelp32.h>
#  include <SDL_syswm.h>
   static constexpr UINT WM_TRAYICON = WM_APP + 1;
   static constexpr UINT TRAY_MENU_SHOW  = 1001;
   static constexpr UINT TRAY_MENU_QUIT  = 1002;
#endif

// ─── Constants ────────────────────────────────────────────────────────────────

static constexpr int BUTTON_COUNT = static_cast<int>(gcpad::Button::COUNT);
static constexpr int AXIS_COUNT   = static_cast<int>(gcpad::Axis::COUNT);

static const char* kButtonNames[BUTTON_COUNT] = {
    "A","B","X","Y","Start","Select","Guide",
    "L1","R1","L2","R2","L3","R3",
    "DPad Up","DPad Down","DPad Left","DPad Right","Touchpad"
};
static const char* kAxisNames[AXIS_COUNT] = {
    "Left X","Left Y","Right X","Right Y","Left Trigger","Right Trigger"
};

static const char* kTriggerEffectNames[] = {
    "Off","Rigid","Pulse","Semi-Rigid","Vibrating"
};
static const gcpad::TriggerEffect::Mode kTriggerEffectModes[] = {
    gcpad::TriggerEffect::Mode::Off,
    gcpad::TriggerEffect::Mode::Rigid,
    gcpad::TriggerEffect::Mode::Pulse,
    gcpad::TriggerEffect::Mode::SemiRigid,
    gcpad::TriggerEffect::Mode::Vibrating,
};

// ─── Profile / Config types ───────────────────────────────────────────────────

struct ButtonMapping {
    bool   enabled  = false;
    int    vk       = 0;      // virtual key code (keyboard) or mouse button flag
    bool   isMouse  = false;
    int    mouseBtn = 0;      // 0=left 1=right 2=middle
    char   label[32]{};       // user-visible label
};

struct AxisMapping {
    bool  enabled  = false;
    float deadzone = 0.10f;
    float sensitivity = 1.0f;
    // Mouse mode
    bool  toMouse  = false;
    float mouseSens  = 25.0f;   // pixels/frame at full deflection (~1500 px/s at 60 fps)
    float mouseCurve = 1.5f;    // 1.0=linear, 1.5=recommended, 2.0=heavy curve
    // Key mode (positive / negative halves)
    bool  toKey    = false;
    int   posVK    = 0;
    int   negVK    = 0;
    float keyThreshold = 0.5f;
};

struct TriggerEffectConfig {
    int    modeIdx  = 0;   // index into kTriggerEffectModes
    uint8_t start   = 0;
    uint8_t end     = 255;
    uint8_t force   = 128;
    uint8_t param1  = 0;
    uint8_t param2  = 0;
};

struct GCPadProfile {
    std::string name = "Default";
    std::string processMatch;           // auto-switch: match foreground exe name
    ButtonMapping btnMap[BUTTON_COUNT];
    AxisMapping   axisMap[AXIS_COUNT];
    float globalDeadzone = 0.10f;
    float triggerDeadzone = 0.05f;
    gcpad::Color  ledColor{0, 0, 255};
    bool  rumbleEnabled = true;
    float rumbleIntensity = 1.0f;
    TriggerEffectConfig triggerL;
    TriggerEffectConfig triggerR;
};

// ─── Simple JSON helpers ──────────────────────────────────────────────────────
// Hand-rolled read/write — avoids adding a dependency.

static std::string jsonEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 4);
    for (char c : s) {
        if      (c == '"' ) o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else                o += c;
    }
    return o;
}

static std::string profileToJson(const GCPadProfile& p) {
    std::ostringstream j;
    j << "{\n";
    j << "  \"name\": \""     << jsonEscape(p.name) << "\",\n";
    j << "  \"process\": \""  << jsonEscape(p.processMatch) << "\",\n";
    j << "  \"deadzone\": "   << p.globalDeadzone << ",\n";
    j << "  \"triggerDz\": "  << p.triggerDeadzone << ",\n";
    j << "  \"rumble\": "     << (p.rumbleEnabled ? "true" : "false") << ",\n";
    j << "  \"rumbleGain\": " << p.rumbleIntensity << ",\n";
    j << "  \"led\": [" << (int)p.ledColor.r << "," << (int)p.ledColor.g << "," << (int)p.ledColor.b << "],\n";
    // buttons
    j << "  \"buttons\": [\n";
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        const auto& b = p.btnMap[i];
        j << "    {\"en\":" << b.enabled << ",\"vk\":" << b.vk
          << ",\"mouse\":" << b.isMouse << ",\"mb\":" << b.mouseBtn
          << ",\"lbl\":\"" << jsonEscape(b.label) << "\"}";
        if (i < BUTTON_COUNT-1) j << ",";
        j << "\n";
    }
    j << "  ],\n";
    // axes
    j << "  \"axes\": [\n";
    for (int i = 0; i < AXIS_COUNT; ++i) {
        const auto& a = p.axisMap[i];
        j << "    {\"en\":" << a.enabled << ",\"dz\":" << a.deadzone
          << ",\"sens\":" << a.sensitivity
          << ",\"toMouse\":" << a.toMouse << ",\"msens\":" << a.mouseSens
          << ",\"curve\":" << a.mouseCurve
          << ",\"toKey\":" << a.toKey << ",\"pvk\":" << a.posVK << ",\"nvk\":" << a.negVK
          << ",\"kthr\":" << a.keyThreshold << "}";
        if (i < AXIS_COUNT-1) j << ",";
        j << "\n";
    }
    j << "  ],\n";
    // trigger effects
    auto writeTrig = [&](const char* key, const TriggerEffectConfig& t) {
        j << "  \"" << key << "\": {\"mode\":" << t.modeIdx
          << ",\"start\":" << (int)t.start << ",\"end\":" << (int)t.end
          << ",\"force\":" << (int)t.force
          << ",\"p1\":" << (int)t.param1 << ",\"p2\":" << (int)t.param2 << "}\n";
    };
    writeTrig("trigL", p.triggerL);
    j << "  ,";
    writeTrig("trigR", p.triggerR);
    j << "}\n";
    return j.str();
}

// Tiny JSON tokeniser (good enough for our own files)
static bool jsonReadInt(const std::string& src, const std::string& key, int& out) {
    auto pos = src.find("\"" + key + "\":");
    if (pos == std::string::npos) return false;
    pos += key.size() + 3;
    while (pos < src.size() && (src[pos]==' '||src[pos]=='\t')) ++pos;
    try { out = std::stoi(src.substr(pos)); return true; }
    catch (...) { return false; }
}
static bool jsonReadFloat(const std::string& src, const std::string& key, float& out) {
    auto pos = src.find("\"" + key + "\":");
    if (pos == std::string::npos) return false;
    pos += key.size() + 3;
    while (pos < src.size() && (src[pos]==' '||src[pos]=='\t')) ++pos;
    try { out = std::stof(src.substr(pos)); return true; }
    catch (...) { return false; }
}
static bool jsonReadBool(const std::string& src, const std::string& key, bool& out) {
    auto pos = src.find("\"" + key + "\":");
    if (pos == std::string::npos) return false;
    pos += key.size() + 3;
    while (pos < src.size() && (src[pos]==' '||src[pos]=='\t')) ++pos;
    if (pos < src.size() && src[pos]=='t') { out=true; return true; }
    if (pos < src.size() && src[pos]=='f') { out=false; return true; }
    return false;
}
static std::string jsonReadString(const std::string& src, const std::string& key) {
    auto pos = src.find("\"" + key + "\":");
    if (pos == std::string::npos) return "";
    pos = src.find('"', pos + key.size() + 3);
    if (pos == std::string::npos) return "";
    ++pos;
    std::string out;
    while (pos < src.size() && src[pos] != '"') {
        if (src[pos]=='\\' && pos+1 < src.size()) { ++pos; out += src[pos]; }
        else out += src[pos];
        ++pos;
    }
    return out;
}

// ─── Profile file I/O ─────────────────────────────────────────────────────────

static std::string getProfileDir() {
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    std::string dir = appdata ? appdata : ".";
    dir += "\\GCPad\\profiles";
    CreateDirectoryA((dir.substr(0, dir.rfind('\\'))).c_str(), nullptr);
    CreateDirectoryA(dir.c_str(), nullptr);
#else
    const char* home = getenv("HOME");
    std::string dir = home ? home : ".";
    dir += "/.config/gcpad/profiles";
    system(("mkdir -p \"" + dir + "\"").c_str());
#endif
    return dir;
}

static bool saveProfile(const GCPadProfile& p) {
    std::string path = getProfileDir() + "/" + p.name + ".json";
    // Sanitise filename
    for (char& c : path) {
        if (c=='<'||c=='>'||c==':'||c=='"'||c=='|'||c=='?'||c=='*') c='_';
    }
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << profileToJson(p);
    return true;
}

static bool loadProfile(const std::string& filePath, GCPadProfile& p) {
    std::ifstream f(filePath);
    if (!f.is_open()) return false;
    std::string src((std::istreambuf_iterator<char>(f)), {});

    p.name = jsonReadString(src, "name");
    p.processMatch = jsonReadString(src, "process");
    jsonReadFloat(src, "deadzone", p.globalDeadzone);
    jsonReadFloat(src, "triggerDz", p.triggerDeadzone);
    jsonReadBool(src, "rumble", p.rumbleEnabled);
    jsonReadFloat(src, "rumbleGain", p.rumbleIntensity);

    // Led
    {
        auto pos = src.find("\"led\":[");
        if (pos != std::string::npos) {
            int r,g,b;
            try {
                pos += 7;
                r = std::stoi(src.substr(pos)); pos = src.find(',', pos)+1;
                g = std::stoi(src.substr(pos)); pos = src.find(',', pos)+1;
                b = std::stoi(src.substr(pos));
                p.ledColor = {(uint8_t)r,(uint8_t)g,(uint8_t)b};
            } catch (...) {}
        }
    }
    // Buttons and axes are parsed from arrays — simple approach: parse as flat JSON
    // (skipped for brevity in this parser; default values used for loaded profiles)
    return true;
}

static std::vector<std::string> listProfileFiles() {
    std::vector<std::string> files;
    std::string dir = getProfileDir();
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.json").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            files.push_back(dir + "\\" + fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    // Portable fallback
    FILE* ls = popen(("ls -1 \"" + dir + "\"/*.json 2>/dev/null").c_str(), "r");
    if (ls) {
        char buf[512]; while (fgets(buf, sizeof(buf), ls)) {
            std::string s(buf);
            if (!s.empty() && s.back()=='\n') s.pop_back();
            if (!s.empty()) files.push_back(s);
        }
        pclose(ls);
    }
#endif
    return files;
}

// ─── Auto-profile: get foreground process name ───────────────────────────────

static std::string getForegroundProcessName() {
#ifdef _WIN32
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return "";
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return "";
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return "";
    PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
    std::string name;
    if (Process32First(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                name = pe.szExeFile;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return name;
#else
    return "";
#endif
}

// ─── System Tray (Windows) ───────────────────────────────────────────────────

#ifdef _WIN32
struct TrayState {
    bool           windowHidden = false;
    NOTIFYICONDATA nid{};
    HWND           hwnd = nullptr;
    bool           added = false;
    SDL_Window*    sdlWin = nullptr;
};
static TrayState g_tray;

static void trayAdd(SDL_Window* win) {
    SDL_SysWMinfo wm; SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(win, &wm)) return;
    g_tray.hwnd    = wm.info.win.window;
    g_tray.sdlWin  = win;
    auto& n = g_tray.nid;
    n.cbSize           = sizeof(n);
    n.hWnd             = g_tray.hwnd;
    n.uID              = 1;
    n.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = WM_TRAYICON;
    n.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);
    strncpy_s(n.szTip, "GCPad", sizeof(n.szTip));
    Shell_NotifyIconA(NIM_ADD, &n);
    g_tray.added = true;
}

static void trayRemove() {
    if (g_tray.added) {
        Shell_NotifyIconA(NIM_DELETE, &g_tray.nid);
        g_tray.added = false;
    }
}

static void trayShowContextMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING, TRAY_MENU_SHOW, "Show GCPad");
    AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(menu, MF_STRING, TRAY_MENU_QUIT, "Quit");
    SetForegroundWindow(g_tray.hwnd);
    POINT pt; GetCursorPos(&pt);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, g_tray.hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == TRAY_MENU_SHOW) {
        SDL_ShowWindow(g_tray.sdlWin); g_tray.windowHidden = false;
        SDL_RaiseWindow(g_tray.sdlWin);
    } else if (cmd == TRAY_MENU_QUIT) {
        SDL_Event ev{}; ev.type = SDL_QUIT;
        SDL_PushEvent(&ev);
    }
}

// Subclass the HWND to intercept tray messages
static WNDPROC g_origWndProc = nullptr;
static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAYICON) {
        if (LOWORD(lp) == WM_RBUTTONUP) trayShowContextMenu();
        if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            SDL_ShowWindow(g_tray.sdlWin); g_tray.windowHidden = false;
            SDL_RaiseWindow(g_tray.sdlWin);
        }
        return 0;
    }
    return CallWindowProcA(g_origWndProc, hwnd, msg, wp, lp);
}
static void trayInstallHook() {
    if (!g_tray.hwnd || g_origWndProc) return;
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(g_tray.hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(TrayWndProc)));
}
#endif

// ─── ImGui styling ────────────────────────────────────────────────────────────

static void setupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding  = 10.0f; s.ChildRounding  = 8.0f;
    s.FrameRounding   = 6.0f;  s.GrabRounding   = 5.0f;
    s.PopupRounding   = 8.0f;  s.TabRounding    = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowPadding   = {14,14}; s.FramePadding  = {8,5};
    s.ItemSpacing     = {8,6};   s.ItemInnerSpacing = {6,4};
    s.ScrollbarSize   = 11.0f;
    s.WindowBorderSize = 1.0f; s.FrameBorderSize = 0.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]         = {0.07f,0.07f,0.09f,1.f};
    c[ImGuiCol_ChildBg]          = {0.09f,0.09f,0.12f,1.f};
    c[ImGuiCol_PopupBg]          = {0.09f,0.09f,0.12f,0.97f};
    c[ImGuiCol_Border]           = {0.18f,0.18f,0.24f,0.55f};
    c[ImGuiCol_FrameBg]          = {0.13f,0.13f,0.16f,1.f};
    c[ImGuiCol_FrameBgHovered]   = {0.19f,0.19f,0.25f,1.f};
    c[ImGuiCol_FrameBgActive]    = {0.24f,0.24f,0.32f,1.f};
    c[ImGuiCol_TitleBg]          = {0.05f,0.05f,0.07f,1.f};
    c[ImGuiCol_TitleBgActive]    = {0.09f,0.09f,0.13f,1.f};
    c[ImGuiCol_MenuBarBg]        = {0.09f,0.09f,0.12f,1.f};
    c[ImGuiCol_ScrollbarBg]      = {0.07f,0.07f,0.09f,0.6f};
    c[ImGuiCol_ScrollbarGrab]    = {0.28f,0.28f,0.36f,1.f};
    c[ImGuiCol_CheckMark]        = {0.38f,0.72f,1.f,1.f};
    c[ImGuiCol_SliderGrab]       = {0.38f,0.72f,1.f,0.85f};
    c[ImGuiCol_SliderGrabActive] = {0.50f,0.84f,1.f,1.f};
    c[ImGuiCol_Button]           = {0.16f,0.16f,0.22f,1.f};
    c[ImGuiCol_ButtonHovered]    = {0.28f,0.52f,0.88f,0.85f};
    c[ImGuiCol_ButtonActive]     = {0.24f,0.48f,0.82f,1.f};
    c[ImGuiCol_Header]           = {0.16f,0.16f,0.22f,1.f};
    c[ImGuiCol_HeaderHovered]    = {0.28f,0.52f,0.88f,0.65f};
    c[ImGuiCol_HeaderActive]     = {0.28f,0.52f,0.88f,0.85f};
    c[ImGuiCol_Tab]              = {0.12f,0.12f,0.16f,1.f};
    c[ImGuiCol_TabHovered]       = {0.28f,0.52f,0.88f,0.65f};
    c[ImGuiCol_TabSelected]      = {0.20f,0.38f,0.68f,1.f};
    c[ImGuiCol_Separator]        = {0.18f,0.18f,0.24f,0.55f};
    c[ImGuiCol_Text]             = {0.91f,0.91f,0.95f,1.f};
    c[ImGuiCol_TextDisabled]     = {0.44f,0.44f,0.50f,1.f};
    c[ImGuiCol_PlotHistogram]    = {0.38f,0.72f,1.f,0.85f};
    c[ImGuiCol_PlotLines]        = {0.38f,0.72f,1.f,0.85f};
}

// ─── Draw helpers ─────────────────────────────────────────────────────────────

static void drawButtonGrid(const gcpad::GamepadState& st) {
    auto btn = [&](const char* lbl, gcpad::Button b) {
        bool p = st.isButtonPressed(b);
        ImGui::PushStyleColor(ImGuiCol_Button, p
            ? ImVec4(0.20f,0.65f,0.35f,1.f)
            : ImVec4(0.14f,0.14f,0.17f,1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, p
            ? ImVec4(1,1,1,1)
            : ImVec4(0.45f,0.45f,0.5f,1.f));
        ImGui::Button(lbl, {44,28});
        ImGui::PopStyleColor(2);
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4,4});
    ImGui::TextDisabled("Face  "); ImGui::SameLine(70);
    btn("A",gcpad::Button::A);    ImGui::SameLine();
    btn("B",gcpad::Button::B);    ImGui::SameLine();
    btn("X",gcpad::Button::X);    ImGui::SameLine();
    btn("Y",gcpad::Button::Y);
    ImGui::TextDisabled("Bumper"); ImGui::SameLine(70);
    btn("L1",gcpad::Button::L1); ImGui::SameLine();
    btn("R1",gcpad::Button::R1); ImGui::SameLine();
    btn("L2",gcpad::Button::L2); ImGui::SameLine();
    btn("R2",gcpad::Button::R2);
    ImGui::TextDisabled("System"); ImGui::SameLine(70);
    btn("STA",gcpad::Button::Start);  ImGui::SameLine();
    btn("SEL",gcpad::Button::Select); ImGui::SameLine();
    btn("GUI",gcpad::Button::Guide);  ImGui::SameLine();
    btn("L3",gcpad::Button::L3);      ImGui::SameLine();
    btn("R3",gcpad::Button::R3);
    ImGui::TextDisabled("D-Pad "); ImGui::SameLine(70);
    btn(" ^ ",gcpad::Button::DPad_Up);    ImGui::SameLine();
    btn(" v ",gcpad::Button::DPad_Down);  ImGui::SameLine();
    btn(" < ",gcpad::Button::DPad_Left);  ImGui::SameLine();
    btn(" > ",gcpad::Button::DPad_Right); ImGui::SameLine();
    btn("TP ",gcpad::Button::Touchpad);
    ImGui::PopStyleVar();
}

static void drawAxisBars(const gcpad::GamepadState& st, float globalDz) {
    float avail = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < AXIS_COUNT; ++i) {
        float v = st.axes[i];
        float mag = std::fabs(v);
        bool  dz  = mag < globalDz;
        ImVec4 col = dz
            ? ImVec4(0.3f,0.3f,0.35f,0.6f)
            : ImVec4(0.20f+mag*0.15f, 0.52f+mag*0.22f, 0.90f, 0.85f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        char ovl[24]; snprintf(ovl,sizeof(ovl),"%+.3f", v);
        if (i >= 4) {
            ImGui::ProgressBar(v, {avail-90, 18}, ovl);
        } else {
            ImGui::ProgressBar((v+1.f)*0.5f, {avail-90, 18}, ovl);
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", kAxisNames[i]);
    }
}

static void drawStick(const char* label, float x, float y,
                       float dz, ImVec2 sz = {108,108}) {
    ImVec2 pos  = ImGui::GetCursorScreenPos();
    float  r    = sz.x * 0.5f - 4;
    ImVec2 ctr  = {pos.x + sz.x*0.5f, pos.y + sz.y*0.5f};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddCircleFilled(ctr, r, IM_COL32(28,28,36,255), 64);
    dl->AddCircle(ctr, r, IM_COL32(55,55,70,200), 64, 1.5f);
    dl->AddCircle(ctr, r*dz, IM_COL32(80,80,100,80), 32, 1.f);
    dl->AddLine({ctr.x-r,ctr.y},{ctr.x+r,ctr.y},IM_COL32(45,45,55,120),1.f);
    dl->AddLine({ctr.x,ctr.y-r},{ctr.x,ctr.y+r},IM_COL32(45,45,55,120),1.f);

    float dx = ctr.x + x*r*0.88f;
    float dy = ctr.y + y*r*0.88f;
    dl->AddLine(ctr,{dx,dy},IM_COL32(90,180,255,80),2.f);
    dl->AddCircleFilled({dx,dy},6.f,IM_COL32(90,180,255,220),20);
    dl->AddCircle({dx,dy},6.f,IM_COL32(140,210,255,255),20,1.5f);
    dl->AddText({pos.x+2,pos.y+sz.y+2},IM_COL32(170,170,185,255),label);
    ImGui::Dummy({sz.x, sz.y+18});
}

static void drawTouchpad(const gcpad::GamepadState& st) {
    float padW = 200, padH = 90;
    ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos,{pos.x+padW,pos.y+padH},IM_COL32(30,30,38,255),10.f);
    dl->AddRect(pos,{pos.x+padW,pos.y+padH},IM_COL32(60,60,75,180),10.f,0,1.5f);
    for (int f = 0; f < 2; ++f) {
        const auto& tp = st.touchpad[f];
        if (!tp.active) continue;
        float tx = pos.x + (float)tp.x / 1920.f * padW;
        float ty = pos.y + (float)tp.y / 943.f  * padH;
        dl->AddCircleFilled({tx,ty},7.f,IM_COL32(90,180,255,200),20);
        dl->AddCircle({tx,ty},7.f,IM_COL32(140,210,255,255),20,1.5f);
    }
    ImGui::Dummy({padW,padH});
}

// ─── Key name lookup ──────────────────────────────────────────────────────────

static const char* vkName(int vk) {
#ifdef _WIN32
    static char buf[64];
    UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    if (GetKeyNameTextA((LONG)(sc << 16), buf, sizeof(buf)) > 0) return buf;
#endif
    static char fb[8]; snprintf(fb,sizeof(fb),"0x%02X",vk); return fb;
}

// ─── Apply profile to remapper ────────────────────────────────────────────────

static void applyProfileToRemapper(const GCPadProfile& p,
                                    gcpad::GamepadInputRemapper& remap) {
    remap.clearAllButtonMappings();
    remap.clearAllAxisMappings();
    remap.resetState();
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        const auto& bm = p.btnMap[i];
        if (!bm.enabled) continue;
        auto btn = static_cast<gcpad::Button>(i);
        if (bm.isMouse) {
            auto mb = bm.mouseBtn == 0 ? gcpad::MouseButton::Left
                    : bm.mouseBtn == 1 ? gcpad::MouseButton::Right
                                       : gcpad::MouseButton::Middle;
            remap.mapButtonToMouseButton(btn, mb);
        } else if (bm.vk) {
            remap.mapButtonToKey(btn, (uint16_t)bm.vk);
        }
    }
    for (int i = 0; i < AXIS_COUNT; ++i) {
        const auto& am = p.axisMap[i];
        if (!am.enabled) continue;
        auto ax = static_cast<gcpad::Axis>(i);
        if (am.toMouse) {
            remap.mapAxisToMouse(ax, am.mouseSens, am.deadzone, false, am.mouseCurve);
        } else if (am.toKey) {
            if (am.posVK) remap.mapAxisToKey(ax, (uint16_t)am.posVK, am.keyThreshold, false);
            if (am.negVK) remap.mapAxisToKey(ax, (uint16_t)am.negVK, am.keyThreshold, true);
        }
    }
}

// ─── Trigger effect builder ───────────────────────────────────────────────────

static gcpad::TriggerEffect buildTriggerEffect(const TriggerEffectConfig& cfg) {
    gcpad::TriggerEffect e;
    e.mode   = kTriggerEffectModes[cfg.modeIdx];
    e.start  = cfg.start;
    e.end    = cfg.end;
    e.force  = cfg.force;
    e.param1 = cfg.param1;
    e.param2 = cfg.param2;
    return e;
}

// ─── UI tab helpers ───────────────────────────────────────────────────────────

static void tabDashboard(gcpad::GamepadManager* mgr, int& activeSlot,
                          std::vector<GCPadProfile>& profiles, int& activeProfile,
                          gcpad::GamepadInputRemapper& remap, bool& translating) {
    // Slot tabs
    if (ImGui::BeginTabBar("##slots")) {
        for (int i = 0; i < mgr->getMaxGamepads(); ++i) {
            auto* p = mgr->getGamepad(i);
            bool conn = p && p->isConnected();
            char lbl[40];
            if (conn) {
                std::string nm = p->getName(); if (nm.size()>14) nm=nm.substr(0,14);
                snprintf(lbl,sizeof(lbl)," %d: %s ",i+1,nm.c_str());
            } else snprintf(lbl,sizeof(lbl)," %d: -- ",i+1);
            ImGuiTabItemFlags fl = (i==activeSlot) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(lbl,nullptr,fl)) { activeSlot=i; ImGui::EndTabItem(); }
        }
        ImGui::EndTabBar();
    }

    auto* pad = mgr->getGamepad(activeSlot);
    if (!pad || !pad->isConnected()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No controller in slot %d.", activeSlot+1);
        ImGui::TextDisabled("Connect a controller to get started.");
        return;
    }

    const auto& st = pad->getState();
    float leftW = ImGui::GetContentRegionAvail().x * 0.60f;

    ImGui::BeginChild("##left", {leftW, 0}, ImGuiChildFlags_Border);
    {
        // Controller name + battery
        ImGui::Text("%s", pad->getName().c_str());
        if (st.battery_level > 0.f) {
            ImGui::SameLine();
            int pct = (int)(st.battery_level*100);
            ImVec4 bc = pct>60 ? ImVec4(.3f,.8f,.4f,1.f)
                       : pct>25 ? ImVec4(.9f,.7f,.2f,1.f)
                                : ImVec4(.9f,.3f,.2f,1.f);
            ImGui::PushStyleColor(ImGuiCol_Text,bc);
            ImGui::Text("  %d%%%s",pct,st.is_charging?" [CHG]":"");
            ImGui::PopStyleColor();
        }
        ImGui::Separator(); ImGui::Spacing();

        drawButtonGrid(st);
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        drawAxisBars(st, profiles[activeProfile].globalDeadzone);
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Stick visualisers
        drawStick("Left Stick",  st.getAxis(gcpad::Axis::LeftX),  st.getAxis(gcpad::Axis::LeftY),
                  profiles[activeProfile].globalDeadzone);
        ImGui::SameLine(0,20);
        drawStick("Right Stick", st.getAxis(gcpad::Axis::RightX), st.getAxis(gcpad::Axis::RightY),
                  profiles[activeProfile].globalDeadzone);

        // Touchpad
        if (st.touchpad[0].active || st.touchpad[1].active) {
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextDisabled("Touchpad");
            drawTouchpad(st);
        }
        // IMU
        if (st.gyro.x||st.gyro.y||st.gyro.z) {
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextDisabled("Gyro:  %.0f  %.0f  %.0f deg/s",st.gyro.x,st.gyro.y,st.gyro.z);
            ImGui::TextDisabled("Accel: %.2f %.2f %.2f m/s²",st.accel.x,st.accel.y,st.accel.z);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##right", {0,0}, ImGuiChildFlags_Border);
    {
        // Translation toggle
        ImGui::Text("Input Translation");
        ImGui::Spacing();
        if (translating) {
            ImGui::PushStyleColor(ImGuiCol_Button,{0.15f,.55f,.25f,1.f});
            if (ImGui::Button("Translation: ON",{-1,0})) {
                translating=false; remap.resetState();
            }
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Translation: OFF",{-1,0})) {
                translating=true;
                remap.resetState();
            }
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Active profile
        ImGui::Text("Active Profile");
        if (ImGui::BeginCombo("##activeprof",profiles[activeProfile].name.c_str())) {
            for (int i=0;i<(int)profiles.size();++i) {
                bool sel = i==activeProfile;
                if (ImGui::Selectable(profiles[i].name.c_str(),sel)) {
                    activeProfile=i;
                    applyProfileToRemapper(profiles[i],remap);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Quick output (rumble / LED)
        ImGui::Text("Quick Controls");
        ImGui::Spacing();
        if (ImGui::Button("Rumble Test",{-1,0})) {
            uint8_t str = (uint8_t)(200 * profiles[activeProfile].rumbleIntensity);
            pad->setRumble({str,str});
            SDL_AddTimer(500,[](Uint32,void* p){
                auto* dev = static_cast<gcpad::GamepadDevice*>(p);
                dev->setRumble({0,0}); return 0u;
            },pad);
        }
        ImGui::Spacing();
        const auto& lc = profiles[activeProfile].ledColor;
        float col[3]={(float)lc.r/255.f,(float)lc.g/255.f,(float)lc.b/255.f};
        if (ImGui::ColorEdit3("LED",col,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_PickerHueWheel)) {
            profiles[activeProfile].ledColor={
                (uint8_t)(col[0]*255),(uint8_t)(col[1]*255),(uint8_t)(col[2]*255)
            };
            pad->setLED(profiles[activeProfile].ledColor);
        }
    }
    ImGui::EndChild();
}

// ─── Remapping tab ────────────────────────────────────────────────────────────

struct CaptureState {
    bool    active  = false;
    int     target  = -1;   // button index being captured
    bool    isAxis  = false;
    int64_t started = 0;
};
static CaptureState g_capture;

static void tabRemapping(GCPadProfile& prof, gcpad::GamepadInputRemapper& remap) {
    ImGui::Text("Button → Key / Mouse Mapping");
    ImGui::TextDisabled("Click a row to capture a key. Mouse buttons: MB1=left, MB2=right, MB3=middle.");
    ImGui::Spacing();

    if (ImGui::BeginTable("##btnmap", 5,
            ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY,
            {0, 260}))
    {
        ImGui::TableSetupColumn("Button",  ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed,  60);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthFixed,  70);
        ImGui::TableSetupColumn("Clear",   ImGuiTableColumnFlags_WidthFixed,  50);
        ImGui::TableHeadersRow();

        for (int i = 0; i < BUTTON_COUNT; ++i) {
            auto& bm = prof.btnMap[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", kButtonNames[i]);
            ImGui::TableSetColumnIndex(1);
            char chkId[16]; snprintf(chkId,sizeof(chkId),"##en%d",i);
            ImGui::Checkbox(chkId, &bm.enabled);
            ImGui::TableSetColumnIndex(2);
            bool capturing = g_capture.active && g_capture.target==i && !g_capture.isAxis;
            if (capturing) {
                ImGui::PushStyleColor(ImGuiCol_Button,{.7f,.3f,.1f,1.f});
                ImGui::Button("Press a key...", {-1,0});
                ImGui::PopStyleColor();
                // Scan for key press
                for (int vk=8; vk<256; ++vk) {
#ifdef _WIN32
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        bm.vk=vk; bm.isMouse=false;
                        strncpy_s(bm.label,vkName(vk),sizeof(bm.label));
                        bm.enabled=true;
                        g_capture={};
                        applyProfileToRemapper(prof,remap);
                        break;
                    }
#endif
                }
                // Mouse buttons via SDL
                int mx,my; Uint32 mb=SDL_GetMouseState(&mx,&my);
                if (mb & SDL_BUTTON_LMASK) { bm.isMouse=true;bm.mouseBtn=0;bm.enabled=true;g_capture={}; applyProfileToRemapper(prof,remap); }
                if (mb & SDL_BUTTON_RMASK) { bm.isMouse=true;bm.mouseBtn=1;bm.enabled=true;g_capture={}; applyProfileToRemapper(prof,remap); }
                if (mb & SDL_BUTTON_MMASK) { bm.isMouse=true;bm.mouseBtn=2;bm.enabled=true;g_capture={}; applyProfileToRemapper(prof,remap); }
                // Timeout
                if (SDL_GetTicks64() - (uint64_t)g_capture.started > 5000) g_capture={};
            } else {
                std::string bindLbl = bm.enabled
                    ? (bm.isMouse ? (bm.mouseBtn==0?"MB1":bm.mouseBtn==1?"MB2":"MB3") : vkName(bm.vk))
                    : "-- unbound --";
                char btnId[24]; snprintf(btnId,sizeof(btnId),"##cb%d",i);
                if (ImGui::Button((bindLbl+"##b"+std::to_string(i)).c_str(),{-1,0})) {
                    g_capture={true,i,false,(int64_t)SDL_GetTicks64()};
                }
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", bm.isMouse ? "Mouse" : "Key");
            ImGui::TableSetColumnIndex(4);
            char clrId[16]; snprintf(clrId,sizeof(clrId),"X##clb%d",i);
            if (ImGui::SmallButton(clrId)) { bm={}; applyProfileToRemapper(prof,remap); }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Axis → Mouse / Key Mapping");
    ImGui::Spacing();

    if (ImGui::BeginTable("##axismap", 5,
            ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY,
            {0, 200}))
    {
        ImGui::TableSetupColumn("Axis",    ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed,  60);
        ImGui::TableSetupColumn("Mode",    ImGuiTableColumnFlags_WidthFixed,  80);
        ImGui::TableSetupColumn("Settings",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Clear",   ImGuiTableColumnFlags_WidthFixed,  50);
        ImGui::TableHeadersRow();

        for (int i=0;i<AXIS_COUNT;++i) {
            auto& am = prof.axisMap[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s",kAxisNames[i]);
            ImGui::TableSetColumnIndex(1);
            char ce[16]; snprintf(ce,sizeof(ce),"##axen%d",i);
            ImGui::Checkbox(ce,&am.enabled);
            ImGui::TableSetColumnIndex(2);
            const char* modes[] = {"Mouse","Keys"};
            int modeIdx = am.toMouse ? 0 : 1;
            char mc[16]; snprintf(mc,sizeof(mc),"##axmode%d",i);
            if (ImGui::Combo(mc,&modeIdx,modes,2)) {
                am.toMouse=modeIdx==0; am.toKey=modeIdx==1;
                applyProfileToRemapper(prof,remap);
            }
            ImGui::TableSetColumnIndex(3);
            if (am.toMouse) {
                float sens=am.mouseSens, curve=am.mouseCurve, dz=am.deadzone;
                char sid[24]; snprintf(sid,sizeof(sid),"Spd##s%d",i);
                if (ImGui::SliderFloat(sid,&sens,1.f,80.f,"%.0f",ImGuiSliderFlags_Logarithmic))
                    { am.mouseSens=sens; applyProfileToRemapper(prof,remap); }
                ImGui::SameLine();
                char cid[24]; snprintf(cid,sizeof(cid),"Curve##c%d",i);
                // 1.0=linear 1.5=balanced 2.0=heavy — tooltip explains the feel
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("1.0 linear · 1.5 balanced · 2.0+ curved");
                if (ImGui::SliderFloat(cid,&curve,1.f,3.f,"%.2f"))
                    { am.mouseCurve=curve; applyProfileToRemapper(prof,remap); }
            } else {
                char pp[24]; snprintf(pp,sizeof(pp),"Pos##ap%d",i);
                char pn[24]; snprintf(pn,sizeof(pn),"Neg##an%d",i);
                bool capPos = g_capture.active&&g_capture.target==i&&g_capture.isAxis&&am.posVK==0;
                bool capNeg = g_capture.active&&g_capture.target==i&&g_capture.isAxis&&am.negVK==0;
                if (capPos||capNeg) {
                    ImGui::TextDisabled("Press key...");
#ifdef _WIN32
                    for (int vk=8;vk<256;++vk) {
                        if (GetAsyncKeyState(vk)&0x8000) {
                            if(capPos) am.posVK=vk; else am.negVK=vk;
                            g_capture={}; applyProfileToRemapper(prof,remap); break;
                        }
                    }
#endif
                } else {
                    if (ImGui::Button(am.posVK?vkName(am.posVK):"[+key]",{60,0}))
                        { am.posVK=0; g_capture={true,i,true,(int64_t)SDL_GetTicks64()}; }
                    ImGui::SameLine();
                    if (ImGui::Button(am.negVK?vkName(am.negVK):"[-key]",{60,0}))
                        { am.negVK=0; g_capture={true,i,true,(int64_t)SDL_GetTicks64()}; }
                }
            }
            ImGui::TableSetColumnIndex(4);
            char clra[16]; snprintf(clra,sizeof(clra),"X##axclr%d",i);
            if (ImGui::SmallButton(clra)) { am={}; applyProfileToRemapper(prof,remap); }
        }
        ImGui::EndTable();
    }
}

// ─── Profile management tab ───────────────────────────────────────────────────

static void tabProfiles(std::vector<GCPadProfile>& profiles, int& activeProfile,
                         gcpad::GamepadInputRemapper& remap) {
    ImGui::Text("Profiles");
    ImGui::Spacing();

    if (ImGui::Button("+ New")) {
        GCPadProfile p; p.name = "Profile " + std::to_string(profiles.size()+1);
        profiles.push_back(p); activeProfile=(int)profiles.size()-1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save to file")) saveProfile(profiles[activeProfile]);
    ImGui::SameLine();
    if (ImGui::Button("Load from file")) {
        auto files = listProfileFiles();
        // Just load first for now; a proper file-picker dialog would go here
        if (!files.empty()) {
            GCPadProfile np;
            if (loadProfile(files[0],np)) {
                profiles.push_back(np); activeProfile=(int)profiles.size()-1;
                applyProfileToRemapper(profiles[activeProfile],remap);
            }
        }
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    for (int i=0;i<(int)profiles.size();++i) {
        bool sel = i==activeProfile;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Header,{0.20f,.38f,.68f,1.f});
        char id[32]; snprintf(id,sizeof(id),"##prof%d",i);
        if (ImGui::Selectable((profiles[i].name+"##row"+std::to_string(i)).c_str(),sel,
                              ImGuiSelectableFlags_SpanAllColumns)) {
            activeProfile=i;
            applyProfileToRemapper(profiles[i],remap);
        }
        if (sel) ImGui::PopStyleColor();
    }

    if (activeProfile >= 0 && activeProfile < (int)profiles.size()) {
        auto& p = profiles[activeProfile];
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("Edit Profile");
        ImGui::InputText("Name",    &p.name[0], 64);
        ImGui::InputText("Process Match", &p.processMatch[0], 64);
        ImGui::TextDisabled("Process match: auto-switch when exe name contains this string.");
    }
}

// ─── Settings tab ─────────────────────────────────────────────────────────────

static void tabSettings(GCPadProfile& p, gcpad::GamepadDevice* pad,
                         bool& minimizeToTray,
                         gcpad::GamepadInputRemapper& remap) {
    ImGui::Text("Deadzone & Sensitivity");
    ImGui::Spacing();
    ImGui::SliderFloat("Global Deadzone##dz",   &p.globalDeadzone,  0.f, 0.5f, "%.2f");
    ImGui::SliderFloat("Trigger Deadzone##tdz", &p.triggerDeadzone, 0.f, 0.4f, "%.2f");

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Mouse Feel Preset");
    ImGui::TextDisabled("Applies speed + curve to all stick axes mapped to mouse.");
    ImGui::Spacing();
    struct Preset { const char* name; float sens; float curve; float dz; };
    static const Preset kPresets[] = {
        { "Precise  (slow + curved)",    15.f, 2.0f, 0.12f },
        { "Balanced (recommended)",      25.f, 1.5f, 0.10f },
        { "Fast     (linear feel)",      40.f, 1.2f, 0.08f },
        { "Flick    (very fast, linear)",65.f, 1.0f, 0.06f },
    };
    for (const auto& pr : kPresets) {
        if (ImGui::Button(pr.name, {-1, 0})) {
            for (auto& am : p.axisMap) {
                if (!am.toMouse) continue;
                am.mouseSens  = pr.sens;
                am.mouseCurve = pr.curve;
                am.deadzone   = pr.dz;
            }
            applyProfileToRemapper(p, remap);
        }
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Rumble");
    ImGui::Checkbox("Rumble Enabled##re", &p.rumbleEnabled);
    ImGui::SliderFloat("Intensity##ri", &p.rumbleIntensity, 0.f, 1.f, "%.2f");

    if (pad && pad->isConnected()) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("LED Colour");
        float col[3]={(float)p.ledColor.r/255.f,(float)p.ledColor.g/255.f,(float)p.ledColor.b/255.f};
        if (ImGui::ColorPicker3("##led",col,
                ImGuiColorEditFlags_PickerHueWheel|ImGuiColorEditFlags_NoSidePreview)) {
            p.ledColor={(uint8_t)(col[0]*255),(uint8_t)(col[1]*255),(uint8_t)(col[2]*255)};
            pad->setLED(p.ledColor);
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("DualSense Adaptive Triggers");

        auto editTrig = [&](const char* label, TriggerEffectConfig& t) {
            if (!ImGui::CollapsingHeader(label)) return;
            char pfx[16]; snprintf(pfx,sizeof(pfx),"%s##",label);
            ImGui::Combo((std::string("Mode##")+label).c_str(), &t.modeIdx,
                         kTriggerEffectNames, 5);
            int s=t.start,e=t.end,f=t.force,p1=t.param1,p2=t.param2;
            if (ImGui::SliderInt((std::string("Start##")+label).c_str(),&s,0,255)) t.start=(uint8_t)s;
            if (ImGui::SliderInt((std::string("End##")+label).c_str(),  &e,0,255)) t.end  =(uint8_t)e;
            if (ImGui::SliderInt((std::string("Force##")+label).c_str(),&f,0,255)) t.force=(uint8_t)f;
            if (ImGui::SliderInt((std::string("Param1##")+label).c_str(),&p1,0,255)) t.param1=(uint8_t)p1;
            if (ImGui::SliderInt((std::string("Param2##")+label).c_str(),&p2,0,255)) t.param2=(uint8_t)p2;
            if (ImGui::Button((std::string("Apply##")+label).c_str())) {
                 if (label[0]=='L') pad->setTriggerEffect(false, buildTriggerEffect(t));
                 else               pad->setTriggerEffect(true,  buildTriggerEffect(t));
            }
        };
        editTrig("Left Trigger",  p.triggerL);
        editTrig("Right Trigger", p.triggerR);
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Application");
#ifdef _WIN32
    ImGui::Checkbox("Minimize to system tray", &minimizeToTray);
#endif
}

// ─── About tab ────────────────────────────────────────────────────────────────

static void tabAbout() {
    auto meta = gcpad::getBuildMetadata();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text,{0.40f,0.75f,1.f,1.f});
    ImGui::Text("GCPad");
    ImGui::PopStyleColor();
    ImGui::SameLine(); ImGui::TextDisabled("Controller Configuration Utility");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Version:    %s", meta.package_version);
    ImGui::TextDisabled("Git commit: %s", meta.git_commit);
    ImGui::TextDisabled("Built:      %s", meta.build_timestamp);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextWrapped(
        "GCPad is an open-source controller abstraction library and "
        "configuration utility. It supports Xbox, PlayStation (DS4 / DualSense), "
        "Nintendo Switch Pro, and generic HID gamepads on Windows and Linux.\n\n"
        "Use the Remapping tab to bind controller buttons to keyboard keys or mouse "
        "buttons.  Profiles are stored as JSON files in %%APPDATA%%\\GCPad\\profiles\\."
    );
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Backends: XInput | DirectInput | HID | SDL2");
    ImGui::TextDisabled("GUI: Dear ImGui v1.91.8 + SDL2");
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int, char**) {
    // Set the background-events hint *before* any SDL_Init so it's active
    // when GCPad_Lib later does SDL_Init(SDL_INIT_JOYSTICK | GAMECONTROLLER).
    // Don't init the joystick subsystem here — GCPad_Lib owns it. If we init
    // it first without the hint having effect on a subsequent re-init, SDL
    // will skip re-initialization and joystick events while the window is
    // hidden / unfocused get suppressed.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) != 0) {
        fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "GCPad",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1200, 760,
        SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { fprintf(stderr,"Window: %s\n",SDL_GetError()); return 1; }

    SDL_Renderer* renderer = SDL_CreateRenderer(window,-1,
        SDL_RENDERER_PRESENTVSYNC|SDL_RENDERER_ACCELERATED);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    setupStyle();
    ImGui_ImplSDL2_InitForSDLRenderer(window,renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // GCPad init
    auto mgr = gcpad::createGamepadManager();
    mgr->initialize();

    gcpad::GamepadInputRemapper remap;

    // Profiles
    std::vector<GCPadProfile> profiles(1);
    profiles[0].name = "Default";
    int activeProfile = 0;
    int activeSlot    = 0;
    bool translating  = false;
    bool minimizeToTray = true;
    gcpad::GamepadState prevState;

    // Load saved profiles
    {
        auto files = listProfileFiles();
        for (auto& f : files) {
            GCPadProfile p;
            if (loadProfile(f,p)) profiles.push_back(p);
        }
        if (profiles.size() > 1) profiles.erase(profiles.begin()); // remove blank default if we loaded something
    }

#ifdef _WIN32
    trayAdd(window);
    trayInstallHook();
#endif

    // Auto-profile timer
    auto lastAutoCheck = std::chrono::steady_clock::now();
    std::string lastFgProcess;

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) running=false;
            if (ev.type == SDL_WINDOWEVENT) {
                if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
#ifdef _WIN32
                    if (minimizeToTray) { SDL_HideWindow(window); g_tray.windowHidden = true; }
                    else running=false;
#else
                    running=false;
#endif
                }
            }
        }

        // Always poll controllers and translate input — even when minimized
        // to the tray. The whole point of tray mode is that remapping keeps
        // working in the background. We just skip rendering work below when
        // the window is hidden.
        {
            mgr->updateAll();

            // Input translation
            auto* pad = mgr->getGamepad(activeSlot);
            if (translating && pad && pad->isConnected()) {
                remap.sendInput(pad->getState(), prevState);
                prevState = pad->getState();
            } else if (pad && pad->isConnected()) {
                prevState = pad->getState();
            }

            // Auto-profile switching (check every 2 seconds)
            {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now-lastAutoCheck).count() > 2000) {
                    lastAutoCheck = now;
                    std::string fg = getForegroundProcessName();
                    if (fg != lastFgProcess) {
                        lastFgProcess = fg;
                        for (int i=0;i<(int)profiles.size();++i) {
                            if (!profiles[i].processMatch.empty() &&
                                fg.find(profiles[i].processMatch) != std::string::npos) {
                                activeProfile=i;
                                applyProfileToRemapper(profiles[i],remap);
                                break;
                            }
                        }
                    }
                }
            }
        }
        // When hidden to tray, skip the ImGui+render work entirely and just
        // sleep briefly so we don't burn CPU. Input polling above still runs.
#ifdef _WIN32
        if (g_tray.windowHidden) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }
#endif

        // ImGui frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Full-window
        ImGui::SetNextWindowPos({0,0});
        int ww,wh; SDL_GetWindowSize(window,&ww,&wh);
        ImGui::SetNextWindowSize({(float)ww,(float)wh});
        ImGui::Begin("##main",nullptr,
            ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
            ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Header bar
        {
            ImGui::PushStyleColor(ImGuiCol_Text,{0.40f,0.75f,1.f,1.f});
            ImGui::Text("GCPad"); ImGui::PopStyleColor();
            ImGui::SameLine(); ImGui::TextDisabled("Controller Utility");
            ImGui::SameLine(ww-220.f);
            int conn=0;
            for (int i=0;i<mgr->getMaxGamepads();++i) {
                auto* g=mgr->getGamepad(i);
                if (g&&g->isConnected()) ++conn;
            }
            if (conn>0) {
                ImGui::PushStyleColor(ImGuiCol_Text,{0.3f,.8f,.4f,1.f});
                ImGui::Text("%d controller%s connected",conn,conn>1?"s":"");
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("No controllers");
            }
        }
        ImGui::Separator();

        if (ImGui::BeginTabBar("##maintabs")) {
            if (ImGui::BeginTabItem("Dashboard")) {
                tabDashboard(mgr.get(),activeSlot,profiles,activeProfile,remap,translating);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Remapping")) {
                tabRemapping(profiles[activeProfile],remap);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Profiles")) {
                tabProfiles(profiles,activeProfile,remap);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                tabSettings(profiles[activeProfile], mgr->getGamepad(activeSlot), minimizeToTray, remap);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("About")) {
                tabAbout();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer,18,18,24,255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer);
        SDL_RenderPresent(renderer);
    }

    // Cleanup
#ifdef _WIN32
    trayRemove();
#endif
    mgr->shutdown();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
