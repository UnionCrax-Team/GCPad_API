// GCPad GUI Frontend — Cross-platform reference application
//
// Demonstrates how to use GCPad with a proper graphical interface using
// SDL2 + Dear ImGui.  Shows live controller input, mapping configuration,
// and real-time controller-to-keyboard/mouse translation.
//
// Build: cmake -B build && cmake --build build --config Release
// Requires: SDL2 (vendored on Windows, system package on Linux)

#include "GamepadManager.h"
#include "gamepad_input_remapper.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <SDL.h>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>

// ── Helpers ──────────────────────────────────────────────────────────────────

static const char* buttonNames[] = {
    "A", "B", "X", "Y", "Start", "Select", "Guide",
    "L1", "R1", "L2", "R2", "L3", "R3",
    "DPad Up", "DPad Down", "DPad Left", "DPad Right", "Touchpad"
};

static const char* axisNames[] = {
    "Left X", "Left Y", "Right X", "Right Y", "L Trigger", "R Trigger"
};

// ── Mapping profiles ─────────────────────────────────────────────────────────

struct ProfileDef {
    const char* name;
    struct BtnKey { gcpad::Button btn; int vk; };
    struct AxisKey { gcpad::Axis axis; int vk; float thr; bool neg; };
    struct AxisMouse { gcpad::Axis axis; float sens; float dz; float curve; };
    struct AxisClick { gcpad::Axis axis; int mouse; float thr; }; // mouse: 0=L, 1=R

    std::vector<BtnKey>    btnKeys;
    std::vector<AxisKey>   axisKeys;
    std::vector<AxisMouse> axisMice;
    std::vector<AxisClick> axisClicks;
};

static std::vector<ProfileDef> g_profiles;

static void initProfiles() {
    // FPS profile
    {
        ProfileDef p;
        p.name = "FPS / Shooter";
        p.axisKeys = {
            { gcpad::Axis::LeftY,  'W', 0.5f, true },
            { gcpad::Axis::LeftY,  'S', 0.5f, false },
            { gcpad::Axis::LeftX,  'A', 0.5f, true },
            { gcpad::Axis::LeftX,  'D', 0.5f, false },
        };
        p.axisMice = {
            { gcpad::Axis::RightX, 15.0f, 0.12f, 2.0f },
            { gcpad::Axis::RightY, 15.0f, 0.12f, 2.0f },
        };
        p.axisClicks = {
            { gcpad::Axis::RightTrigger, 0, 0.3f },
            { gcpad::Axis::LeftTrigger,  1, 0.3f },
        };
        p.btnKeys = {
            { gcpad::Button::A, 0x20 },       // Space
            { gcpad::Button::B, 'C' },
            { gcpad::Button::X, 'R' },
            { gcpad::Button::Y, 'E' },
            { gcpad::Button::L1, 'Q' },
            { gcpad::Button::R1, 'F' },
            { gcpad::Button::L3, 0xA0 },      // LShift
            { gcpad::Button::Start, 0x1B },    // Escape
            { gcpad::Button::Select, 0x09 },   // Tab
            { gcpad::Button::DPad_Up, '1' },
            { gcpad::Button::DPad_Right, '2' },
            { gcpad::Button::DPad_Down, '3' },
            { gcpad::Button::DPad_Left, '4' },
        };
        g_profiles.push_back(p);
    }
    // Half-Life 2
    {
        ProfileDef p;
        p.name = "Half-Life 2";
        p.axisKeys = {
            { gcpad::Axis::LeftY,  'W', 0.5f, true },
            { gcpad::Axis::LeftY,  'S', 0.5f, false },
            { gcpad::Axis::LeftX,  'A', 0.5f, true },
            { gcpad::Axis::LeftX,  'D', 0.5f, false },
        };
        p.axisMice = {
            { gcpad::Axis::RightX, 16.0f, 0.10f, 2.2f },
            { gcpad::Axis::RightY, 16.0f, 0.10f, 2.2f },
        };
        p.axisClicks = {
            { gcpad::Axis::RightTrigger, 0, 0.25f },
            { gcpad::Axis::LeftTrigger,  1, 0.25f },
        };
        p.btnKeys = {
            { gcpad::Button::A, 0x20 },       // Space
            { gcpad::Button::B, 0xA2 },       // LCtrl
            { gcpad::Button::X, 'R' },
            { gcpad::Button::Y, 'E' },
            { gcpad::Button::L1, 'F' },
            { gcpad::Button::R1, 'Q' },
            { gcpad::Button::L3, 0xA0 },      // LShift
            { gcpad::Button::R3, 'Z' },
            { gcpad::Button::Start, 0x1B },    // Escape
            { gcpad::Button::Select, 0x09 },   // Tab
            { gcpad::Button::DPad_Up, '1' },
            { gcpad::Button::DPad_Right, '2' },
            { gcpad::Button::DPad_Down, '3' },
            { gcpad::Button::DPad_Left, '4' },
            { gcpad::Button::Guide, '5' },
        };
        g_profiles.push_back(p);
    }
    // Platformer
    {
        ProfileDef p;
        p.name = "2D Platformer";
        p.axisKeys = {
            { gcpad::Axis::LeftY,  0x26, 0.5f, true },  // VK_UP
            { gcpad::Axis::LeftY,  0x28, 0.5f, false },  // VK_DOWN
            { gcpad::Axis::LeftX,  0x25, 0.5f, true },  // VK_LEFT
            { gcpad::Axis::LeftX,  0x27, 0.5f, false },  // VK_RIGHT
        };
        p.btnKeys = {
            { gcpad::Button::A, 'Z' },
            { gcpad::Button::B, 'X' },
            { gcpad::Button::X, 'C' },
            { gcpad::Button::Y, 'V' },
            { gcpad::Button::Start, 0x0D },    // Enter
            { gcpad::Button::Select, 0x1B },   // Escape
        };
        g_profiles.push_back(p);
    }
    // Desktop nav
    {
        ProfileDef p;
        p.name = "Desktop Mouse";
        p.axisMice = {
            { gcpad::Axis::LeftX, 20.0f, 0.1f, 1.5f },
            { gcpad::Axis::LeftY, 20.0f, 0.1f, 1.5f },
        };
        p.axisClicks = {
            { gcpad::Axis::RightTrigger, 0, 0.3f },
            { gcpad::Axis::LeftTrigger,  1, 0.3f },
        };
        p.btnKeys = {
            { gcpad::Button::A, 0x0D },    // Enter
            { gcpad::Button::B, 0x1B },    // Escape
            { gcpad::Button::DPad_Up, 0x26 },    // VK_UP
            { gcpad::Button::DPad_Down, 0x28 },  // VK_DOWN
            { gcpad::Button::DPad_Left, 0x25 },  // VK_LEFT
            { gcpad::Button::DPad_Right, 0x27 }, // VK_RIGHT
        };
        g_profiles.push_back(p);
    }
}

static void applyProfile(const ProfileDef& p, gcpad::GamepadInputRemapper& remap) {
    remap.clearAllButtonMappings();
    remap.clearAllAxisMappings();
    remap.resetState();
    for (auto& bk : p.btnKeys)
        remap.mapButtonToKey(bk.btn, static_cast<uint16_t>(bk.vk));
    for (auto& ak : p.axisKeys)
        remap.mapAxisToKey(ak.axis, static_cast<uint16_t>(ak.vk), ak.thr, ak.neg);
    for (auto& am : p.axisMice)
        remap.mapAxisToMouse(am.axis, am.sens, am.dz, false, am.curve);
    for (auto& ac : p.axisClicks) {
        auto mb = ac.mouse == 0 ? gcpad::MouseButton::Left : gcpad::MouseButton::Right;
        remap.mapAxisToMouseButton(ac.axis, mb, ac.thr);
    }
}

// ── Custom ImGui styling ─────────────────────────────────────────────────────

static void setupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 8.0f;
    s.ChildRounding     = 6.0f;
    s.FrameRounding     = 5.0f;
    s.GrabRounding      = 4.0f;
    s.PopupRounding     = 6.0f;
    s.TabRounding       = 5.0f;
    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(8, 5);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ScrollbarSize     = 12.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;

    ImVec4* c = s.Colors;
    // Dark theme with accent color
    c[ImGuiCol_WindowBg]          = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.10f, 0.10f, 0.13f, 0.96f);
    c[ImGuiCol_Border]            = ImVec4(0.20f, 0.20f, 0.25f, 0.50f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.25f, 0.25f, 0.32f, 1.00f);
    c[ImGuiCol_TitleBg]           = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    c[ImGuiCol_ScrollbarBg]       = ImVec4(0.08f, 0.08f, 0.10f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
    c[ImGuiCol_CheckMark]         = ImVec4(0.40f, 0.75f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]        = ImVec4(0.40f, 0.75f, 1.00f, 0.80f);
    c[ImGuiCol_SliderGrabActive]  = ImVec4(0.50f, 0.85f, 1.00f, 1.00f);
    c[ImGuiCol_Button]            = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.30f, 0.55f, 0.90f, 0.80f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.25f, 0.50f, 0.85f, 1.00f);
    c[ImGuiCol_Header]            = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.30f, 0.55f, 0.90f, 0.60f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.30f, 0.55f, 0.90f, 0.80f);
    c[ImGuiCol_Tab]               = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.30f, 0.55f, 0.90f, 0.60f);
    c[ImGuiCol_TabSelected]       = ImVec4(0.22f, 0.40f, 0.70f, 1.00f);
    c[ImGuiCol_Separator]         = ImVec4(0.20f, 0.20f, 0.25f, 0.50f);
    c[ImGuiCol_Text]              = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.45f, 0.45f, 0.50f, 1.00f);
    c[ImGuiCol_PlotHistogram]     = ImVec4(0.40f, 0.75f, 1.00f, 0.80f);
}

// ── Draw helpers ─────────────────────────────────────────────────────────────

static void drawButtonGrid(const gcpad::GamepadState& st) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

    auto btnWidget = [&](const char* label, gcpad::Button b) {
        bool pressed = st.isButtonPressed(b);
        if (pressed) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text,   ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.14f, 0.17f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text,   ImVec4(0.45f, 0.45f, 0.50f, 1.0f));
        }
        ImGui::Button(label, ImVec2(44, 28));
        ImGui::PopStyleColor(2);
    };

    // Face buttons
    ImGui::Text("Face");
    ImGui::SameLine(70);
    btnWidget("A", gcpad::Button::A); ImGui::SameLine();
    btnWidget("B", gcpad::Button::B); ImGui::SameLine();
    btnWidget("X", gcpad::Button::X); ImGui::SameLine();
    btnWidget("Y", gcpad::Button::Y);

    // Shoulders
    ImGui::Text("Bumper");
    ImGui::SameLine(70);
    btnWidget("L1", gcpad::Button::L1); ImGui::SameLine();
    btnWidget("R1", gcpad::Button::R1); ImGui::SameLine();
    btnWidget("L2", gcpad::Button::L2); ImGui::SameLine();
    btnWidget("R2", gcpad::Button::R2);

    // System
    ImGui::Text("System");
    ImGui::SameLine(70);
    btnWidget("STA", gcpad::Button::Start); ImGui::SameLine();
    btnWidget("SEL", gcpad::Button::Select); ImGui::SameLine();
    btnWidget("GUI", gcpad::Button::Guide); ImGui::SameLine();
    btnWidget("L3", gcpad::Button::L3); ImGui::SameLine();
    btnWidget("R3", gcpad::Button::R3);

    // D-pad
    ImGui::Text("D-Pad");
    ImGui::SameLine(70);
    btnWidget(" ^ ", gcpad::Button::DPad_Up); ImGui::SameLine();
    btnWidget(" v ", gcpad::Button::DPad_Down); ImGui::SameLine();
    btnWidget(" < ", gcpad::Button::DPad_Left); ImGui::SameLine();
    btnWidget(" > ", gcpad::Button::DPad_Right);

    ImGui::PopStyleVar();
}

static void drawAxisBars(const gcpad::GamepadState& st) {
    float width = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < static_cast<int>(gcpad::Axis::COUNT); ++i) {
        float val = st.axes[i];
        bool isTrigger = (i >= 4);

        ImGui::Text("%-10s", axisNames[i]);
        ImGui::SameLine(90);

        // Color based on magnitude
        float mag = std::abs(val);
        ImVec4 barCol(0.25f + mag * 0.15f, 0.55f + mag * 0.20f, 0.90f, 0.80f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);

        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%+.3f", val);

        if (isTrigger) {
            ImGui::ProgressBar(val, ImVec2(width - 100, 18), overlay);
        } else {
            // Bidirectional: remap -1..1 to 0..1
            float display = (val + 1.0f) * 0.5f;
            ImGui::ProgressBar(display, ImVec2(width - 100, 18), overlay);
        }
        ImGui::PopStyleColor();
    }
}

static void drawStickXY(const char* label, float x, float y) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float radius = 50.0f;
    ImVec2 center(pos.x + radius + 4, pos.y + radius + 4);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background circle
    dl->AddCircleFilled(center, radius, IM_COL32(30, 30, 38, 255), 48);
    dl->AddCircle(center, radius, IM_COL32(60, 60, 75, 200), 48, 1.5f);

    // Deadzone ring
    dl->AddCircle(center, radius * 0.15f, IM_COL32(80, 80, 100, 100), 24, 1.0f);

    // Crosshair
    dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(50, 50, 60, 150), 1.0f);
    dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(50, 50, 60, 150), 1.0f);

    // Stick position dot
    float dotX = center.x + x * radius * 0.9f;
    float dotY = center.y + y * radius * 0.9f;
    dl->AddCircleFilled(ImVec2(dotX, dotY), 6.0f, IM_COL32(100, 190, 255, 230), 16);
    dl->AddCircle(ImVec2(dotX, dotY), 6.0f, IM_COL32(140, 210, 255, 255), 16, 1.5f);

    // Trail line from center
    dl->AddLine(center, ImVec2(dotX, dotY), IM_COL32(100, 190, 255, 100), 2.0f);

    // Label
    dl->AddText(ImVec2(pos.x, pos.y + radius * 2 + 8), IM_COL32(180, 180, 195, 255), label);

    ImGui::Dummy(ImVec2(radius * 2 + 8, radius * 2 + 24));
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int, char**) {
    // SDL init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "GCPad GUI Frontend",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1100, 700,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr; // Don't save layout

    setupStyle();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // GCPad init
    auto manager = gcpad::createGamepadManager();
    manager->initialize();

    // Remapper + profiles
    initProfiles();
    gcpad::GamepadInputRemapper remapper;
    int activeProfile = 0;
    applyProfile(g_profiles[0], remapper);

    int activeSlot = 0;
    bool translating = false;
    gcpad::GamepadState prevState;

    // Main loop
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE)
                running = false;
        }

        // Update controllers
        manager->updateAll();

        // Translation
        auto* pad = manager->getGamepad(activeSlot);
        if (translating && pad && pad->isConnected()) {
            remapper.sendInput(pad->getState(), prevState);
            prevState = pad->getState();
        } else if (pad && pad->isConnected()) {
            prevState = pad->getState();
        }

        // ImGui frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // ── Full window ──────────────────────────────────────────────────────
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(ww), static_cast<float>(wh)));
        ImGui::Begin("##main", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // ── Title bar ────────────────────────────────────────────────────────
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.75f, 1.0f, 1.0f));
            ImGui::Text("GCPad");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("GUI Frontend");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180);

            // Translation toggle
            if (translating) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.25f, 1.0f));
                if (ImGui::Button("Translation: ON", ImVec2(140, 0))) {
                    translating = false;
                    remapper.resetState();
                }
                ImGui::PopStyleColor();
            } else {
                if (ImGui::Button("Translation: OFF", ImVec2(140, 0))) {
                    translating = true;
                    if (pad && pad->isConnected()) prevState = pad->getState();
                    remapper.resetState();
                }
            }
        }
        ImGui::Separator();

        // ── Slot tabs ────────────────────────────────────────────────────────
        if (ImGui::BeginTabBar("##slots")) {
            for (int i = 0; i < manager->getMaxGamepads(); ++i) {
                auto* p = manager->getGamepad(i);
                bool conn = p && p->isConnected();

                char tabLabel[64];
                if (conn) {
                    std::string name = p->getName();
                    if (name.size() > 16) name = name.substr(0, 16);
                    snprintf(tabLabel, sizeof(tabLabel), " %d: %s ", i + 1, name.c_str());
                } else {
                    snprintf(tabLabel, sizeof(tabLabel), " %d: -- ", i + 1);
                }

                ImGuiTabItemFlags flags = 0;
                if (i == activeSlot) flags |= ImGuiTabItemFlags_SetSelected;

                if (ImGui::BeginTabItem(tabLabel, nullptr, flags)) {
                    activeSlot = i;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::Spacing();

        // ── Main content ─────────────────────────────────────────────────────
        pad = manager->getGamepad(activeSlot);

        if (!pad || !pad->isConnected()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No controller in slot %d", activeSlot + 1);
            ImGui::TextDisabled("Connect a controller to get started.");
        } else {
            const auto& st = pad->getState();
            float leftWidth = ImGui::GetContentRegionAvail().x * 0.62f;

            // Left column: input display
            ImGui::BeginChild("##input", ImVec2(leftWidth, 0), ImGuiChildFlags_Border);
            {
                // Header: name + battery
                ImGui::Text("%s", pad->getName().c_str());
                if (st.battery_level > 0.0f) {
                    ImGui::SameLine();
                    int pct = static_cast<int>(st.battery_level * 100);
                    ImVec4 batCol = pct > 60 ? ImVec4(0.3f, 0.8f, 0.4f, 1.0f) :
                                    pct > 25 ? ImVec4(0.9f, 0.7f, 0.2f, 1.0f) :
                                               ImVec4(0.9f, 0.3f, 0.2f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, batCol);
                    ImGui::Text("  BAT: %d%%%s", pct, st.is_charging ? " [charging]" : "");
                    ImGui::PopStyleColor();
                }

                ImGui::Separator();
                ImGui::Spacing();

                // Buttons
                drawButtonGrid(st);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Axes
                drawAxisBars(st);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Stick visualizers
                drawStickXY("Left Stick",
                    st.getAxis(gcpad::Axis::LeftX),
                    st.getAxis(gcpad::Axis::LeftY));
                ImGui::SameLine(0, 20);
                drawStickXY("Right Stick",
                    st.getAxis(gcpad::Axis::RightX),
                    st.getAxis(gcpad::Axis::RightY));

                // IMU
                if (st.gyro.x != 0 || st.gyro.y != 0 || st.gyro.z != 0) {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::TextDisabled("Gyro:  %.0f  %.0f  %.0f  deg/s", st.gyro.x, st.gyro.y, st.gyro.z);
                    ImGui::TextDisabled("Accel: %.1f  %.1f  %.1f  m/s2", st.accel.x, st.accel.y, st.accel.z);
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Right column: profile / settings
            ImGui::BeginChild("##settings", ImVec2(0, 0), ImGuiChildFlags_Border);
            {
                ImGui::Text("Mapping Profile");
                ImGui::Spacing();

                // Profile selector
                if (ImGui::BeginCombo("##profile", g_profiles[activeProfile].name)) {
                    for (int i = 0; i < static_cast<int>(g_profiles.size()); ++i) {
                        bool selected = (i == activeProfile);
                        if (ImGui::Selectable(g_profiles[i].name, selected)) {
                            activeProfile = i;
                            applyProfile(g_profiles[i], remapper);
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Mapping summary
                auto& prof = g_profiles[activeProfile];
                ImGui::TextDisabled("Button -> Key:    %d", static_cast<int>(prof.btnKeys.size()));
                ImGui::TextDisabled("Axis -> Key:      %d", static_cast<int>(prof.axisKeys.size()));
                ImGui::TextDisabled("Axis -> Mouse:    %d", static_cast<int>(prof.axisMice.size()));
                ImGui::TextDisabled("Axis -> Click:    %d", static_cast<int>(prof.axisClicks.size()));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Mappings detail
                ImGui::Text("Button Mappings");
                for (auto& bk : prof.btnKeys) {
                    int idx = static_cast<int>(bk.btn);
                    if (idx >= 0 && idx < 18) {
                        ImGui::TextDisabled("  %s -> 0x%02X", buttonNames[idx], bk.vk);
                    }
                }

                ImGui::Spacing();
                ImGui::Text("Axis Mappings");
                for (auto& ak : prof.axisKeys) {
                    int idx = static_cast<int>(ak.axis);
                    if (idx >= 0 && idx < 6) {
                        ImGui::TextDisabled("  %s %s -> 0x%02X",
                            axisNames[idx], ak.neg ? "-" : "+", ak.vk);
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Controls
                ImGui::Text("Controls");
                ImGui::Spacing();

                if (ImGui::Button("Rumble", ImVec2(-1, 0))) {
                    pad->setRumble({ 180, 180 });
                    // Will need to stop later; simplified for example
                }
                if (ImGui::Button("LED: Red", ImVec2(-1, 0)))   pad->setLED({ 255, 0, 0 });
                if (ImGui::Button("LED: Green", ImVec2(-1, 0))) pad->setLED({ 0, 255, 0 });
                if (ImGui::Button("LED: Blue", ImVec2(-1, 0)))  pad->setLED({ 0, 0, 255 });
                if (ImGui::Button("LED: Off", ImVec2(-1, 0)))   pad->setLED({ 0, 0, 0 });
            }
            ImGui::EndChild();
        }

        ImGui::End();

        // Render
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 20, 26, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // Cleanup
    manager->shutdown();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
