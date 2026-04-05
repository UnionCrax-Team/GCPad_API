// gcpad_example.cpp — GCPad API Test Frontend
//
// Full-screen console diagnostic tool for the GCPad library.
// Shows live input state for up to 4 gamepads with axis visualisation,
// hotplug events, and interactive rumble/LED testing.
//
// Controls:
//   Left / Right arrow  (or  < / >)  — switch active pad slot
//   R                                — send a short rumble pulse
//   L                                — cycle LED colour (DS4 / DualSense)
//   Q  /  Ctrl+C                     — quit
//
// Requires Windows 10 build 1511+ for ANSI virtual-terminal support.

#include "GamepadManager.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <deque>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <ctime>
#include <csignal>
#include <conio.h>
#include <windows.h>

// ── Console helpers ───────────────────────────────────────────────────────────

static HANDLE g_hCon = INVALID_HANDLE_VALUE;

static void con_init() {
    SetConsoleOutputCP(65001); // UTF-8
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
    std::cout << "\033[0m\n" << std::flush;
}

static void con_goto(int x, int y) {
    SetConsoleCursorPosition(g_hCon, { static_cast<SHORT>(x), static_cast<SHORT>(y) });
}

static void con_clear() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hCon, &csbi);
    DWORD cells = static_cast<DWORD>(csbi.dwSize.X) * csbi.dwSize.Y, written;
    COORD home { 0, 0 };
    FillConsoleOutputCharacterA(g_hCon, ' ', cells, home, &written);
    FillConsoleOutputAttribute(g_hCon, csbi.wAttributes, cells, home, &written);
}

// ANSI codes
static constexpr const char* RST  = "\033[0m";
static constexpr const char* BOLD = "\033[1m";
static constexpr const char* DIM  = "\033[2m";
static constexpr const char* RED  = "\033[31m";
static constexpr const char* GRN  = "\033[32m";
static constexpr const char* YLW  = "\033[33m";
static constexpr const char* CYN  = "\033[36m";

// ── App state ─────────────────────────────────────────────────────────────────

static volatile bool    g_running = true;
static std::atomic<int> g_slot { 0 };

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

static void sig_handler(int) { g_running = false; }

// ── LED colours for cycling ───────────────────────────────────────────────────

static const gcpad::Color LED_CYCLE[] = {
    { 255,   0,   0 },  // red
    {   0, 255,   0 },  // green
    {   0,   0, 255 },  // blue
    { 255, 128,   0 },  // orange
    { 128,   0, 255 },  // purple
    {   0, 255, 255 },  // cyan
    { 255, 255, 255 },  // white
    {   0,   0,   0 },  // off
};
static int g_led_idx = 0;

// ── Drawing primitives ────────────────────────────────────────────────────────

// Renders a button widget: bold green when pressed, dim when released.
static std::string btn(const char* label, bool pressed) {
    std::string s;
    s += pressed ? (std::string(BOLD) + GRN) : DIM;
    s += '[';
    s += label;
    s += ']';
    s += RST;
    return s;
}

// 24-character axis bar.
//   bidir=true  (sticks):   center | moves left or right
//   bidir=false (triggers): fills left-to-right from 0
static std::string axis_bar(float v, bool bidir) {
    constexpr int N = 24;
    char buf[N + 1];
    std::fill(buf, buf + N, '-');
    buf[N] = '\0';
    if (bidir) {
        int mid = N / 2;
        buf[mid] = '|';
        int pos = mid + static_cast<int>(v * mid);
        pos = pos < 0 ? 0 : (pos >= N ? N - 1 : pos);
        if (pos < mid)
            for (int i = pos; i < mid; ++i) buf[i] = '=';
        else
            for (int i = mid + 1; i <= pos; ++i) buf[i] = '=';
    } else {
        int filled = static_cast<int>(v * N);
        filled = filled < 0 ? 0 : (filled > N ? N : filled);
        for (int i = 0; i < filled; ++i) buf[i] = '=';
    }
    return std::string("[") + buf + "]";
}

// 10-character battery bar.
static std::string bat_bar(float v) {
    constexpr int N = 10;
    int filled = static_cast<int>(v * N);
    filled = filled < 0 ? 0 : (filled > N ? N : filled);
    return "[" + std::string(filled, '=') + std::string(N - filled, '-') + "]";
}

// ── Main draw ─────────────────────────────────────────────────────────────────
//
// Draws EXACTLY TOTAL_LINES lines every call so the screen height is stable
// and we never need to clear — just overwrite from row 0 each frame.
// Each line is terminated with \033[K (erase-to-EOL) before the newline
// so stale characters from longer previous lines are always erased.

static constexpr int TOTAL_LINES = 32;

static void draw(gcpad::GamepadManager* mgr, const gcpad::BuildMetadata& meta) {
    con_goto(0, 0);

    std::ostringstream out;

    // Appends one display line (content + erase-to-EOL + newline).
    auto emit = [&](const std::string& s = "") {
        out << s << "\033[K\n";
    };
    auto hline = [&](char c = '=') {
        emit(std::string(72, c));
    };

    // ── Rows 1-3: header ──────────────────────────────────────────────────────
    hline();
    {
        std::ostringstream h;
        h << "  " << BOLD << CYN
          << "GCPad API Test Frontend  v" << meta.package_version
          << "  (commit: " << meta.git_commit << ")"
          << RST;
        emit(h.str());
    }
    hline();

    // ── Row 4: slot tabs ──────────────────────────────────────────────────────
    {
        std::ostringstream tabs;
        tabs << "  Slots: ";
        for (int i = 0; i < mgr->getMaxGamepads(); ++i) {
            auto* p  = mgr->getGamepad(i);
            bool conn   = p && p->isConnected();
            bool active = (i == g_slot);
            if (active) tabs << BOLD;
            if (conn) {
                std::string name = p->getName();
                if (name.size() > 14) name = name.substr(0, 14);
                tabs << (active ? GRN : DIM)
                     << (active ? " >>>" : "    ")
                     << "[" << i << ": " << name << "]";
            } else {
                tabs << DIM << (active ? " >>>" : "    ")
                     << "[" << i << ": --]";
            }
            tabs << RST << "  ";
        }
        tabs << "  < > switch";
        emit(tabs.str());
    }

    // ── Row 5: separator ──────────────────────────────────────────────────────
    hline('-');

    // ── Rows 6-22: active pad detail (always exactly 17 lines) ───────────────
    int slot = g_slot.load();
    auto* pad = mgr->getGamepad(slot);

    if (!pad || !pad->isConnected()) {
        emit(std::string("  ") + DIM
             + "No controller in slot " + std::to_string(slot)
             + " — plug one in or press < > to switch" + RST);
        for (int i = 0; i < 16; ++i) emit(); // blank filler keeps total stable
    } else {
        const auto& st = pad->getState();

        // Row 6: device name + battery
        {
            std::ostringstream s;
            s << "  Device: " << BOLD << pad->getName() << RST;
            if (st.battery_level > 0.0f) {
                int pct = static_cast<int>(st.battery_level * 100.0f);
                const char* col = pct > 60 ? GRN : (pct > 25 ? YLW : RED);
                s << "    BAT: " << col << bat_bar(st.battery_level)
                  << " " << pct << "%" << RST;
                if (st.is_charging) s << " [charging]";
            }
            emit(s.str());
        }

        emit(); // Row 7: blank

        // Row 8: face buttons
        emit(std::string("  Face:   ")
             + btn("A", st.isButtonPressed(gcpad::Button::A)) + " "
             + btn("B", st.isButtonPressed(gcpad::Button::B)) + " "
             + btn("X", st.isButtonPressed(gcpad::Button::X)) + " "
             + btn("Y", st.isButtonPressed(gcpad::Button::Y)));

        // Row 9: shoulder + system buttons
        emit(std::string("  Shldr:  ")
             + btn("L1", st.isButtonPressed(gcpad::Button::L1)) + " "
             + btn("R1", st.isButtonPressed(gcpad::Button::R1)) + " "
             + btn("L2", st.isButtonPressed(gcpad::Button::L2)) + " "
             + btn("R2", st.isButtonPressed(gcpad::Button::R2))
             + "    "
             + btn("SEL", st.isButtonPressed(gcpad::Button::Select)) + " "
             + btn("STA", st.isButtonPressed(gcpad::Button::Start)) + " "
             + btn("GUI", st.isButtonPressed(gcpad::Button::Guide)));

        // Row 10: stick clicks + d-pad
        emit(std::string("  Stick:  ")
             + btn("L3", st.isButtonPressed(gcpad::Button::L3)) + " "
             + btn("R3", st.isButtonPressed(gcpad::Button::R3))
             + "    D-pad: "
             + btn("^", st.isButtonPressed(gcpad::Button::DPad_Up))
             + btn("v", st.isButtonPressed(gcpad::Button::DPad_Down))
             + btn("<", st.isButtonPressed(gcpad::Button::DPad_Left))
             + btn(">", st.isButtonPressed(gcpad::Button::DPad_Right)));

        emit(); // Row 11: blank

        // Rows 12-17: analog axes
        auto axis_line = [&](const char* name, float v, bool bidir) {
            std::ostringstream s;
            s << "  " << std::left << std::setw(4) << name
              << axis_bar(v, bidir) << "  "
              << std::right << std::fixed << std::setprecision(3)
              << std::setw(7) << std::showpos << v << std::noshowpos;
            emit(s.str());
        };
        axis_line("LX",  st.getAxis(gcpad::Axis::LeftX),        true);
        axis_line("LY",  st.getAxis(gcpad::Axis::LeftY),        true);
        axis_line("RX",  st.getAxis(gcpad::Axis::RightX),       true);
        axis_line("RY",  st.getAxis(gcpad::Axis::RightY),       true);
        axis_line("LT",  st.getAxis(gcpad::Axis::LeftTrigger),  false);
        axis_line("RT",  st.getAxis(gcpad::Axis::RightTrigger), false);

        emit(); // Row 18: blank

        // Rows 19-20: IMU (shows zeros if not available)
        auto imu_line = [&](const char* label, float x, float y, float z) {
            std::ostringstream s;
            s << "  " << std::left << std::setw(8) << label
              << std::fixed << std::setprecision(0)
              << "X:" << std::right << std::setw(7) << std::showpos << x
              << "  Y:" << std::setw(7) << y
              << "  Z:" << std::setw(7) << z << std::noshowpos;
            emit(s.str());
        };
        bool has_imu = (st.gyro.x != 0.0f || st.gyro.y != 0.0f || st.gyro.z != 0.0f
                     || st.accel.x != 0.0f || st.accel.y != 0.0f || st.accel.z != 0.0f);
        if (has_imu) {
            imu_line("Gyro:",  st.gyro.x,  st.gyro.y,  st.gyro.z);
            imu_line("Accel:", st.accel.x, st.accel.y, st.accel.z);
        } else {
            emit(std::string("  ") + DIM + "Gyro / Accel: not available for this device" + RST);
            emit();
        }

        emit(); // Row 21: blank

        // Row 22: serial number (dim, only if non-empty)
        std::string serial = pad->getSerialNumber();
        if (!serial.empty())
            emit(std::string("  ") + DIM + "Serial: " + serial + RST);
        else
            emit();
    }

    // ── Row 23: separator ─────────────────────────────────────────────────────
    hline('-');

    // ── Row 24: key hints ─────────────────────────────────────────────────────
    emit(std::string("  ") + DIM
         + "[Q] Quit   [R] Rumble pulse   [L] Cycle LED   [< >]/[arrows] Switch pad"
         + RST);

    // ── Row 25: separator ─────────────────────────────────────────────────────
    hline();

    // ── Row 26: log header ────────────────────────────────────────────────────
    emit(std::string("  ") + BOLD + "Event log:" + RST);

    // ── Rows 27-31: log entries ───────────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lk(g_log_mtx);
        for (int i = 0; i < LOG_ROWS; ++i) {
            if (i < static_cast<int>(g_log.size()))
                emit(std::string("    ") + DIM + g_log[i] + RST);
            else
                emit();
        }
    }

    // Row 32: trailing blank so the hints bar doesn't merge into the shell prompt on exit
    emit();

    std::cout << out.str() << std::flush;
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    std::signal(SIGINT, sig_handler);
    con_init();
    con_clear();
    con_goto(0, 0);

    auto manager = gcpad::createGamepadManager();

    // Keep callbacks simple — do NOT call back into manager (mutex is already held
    // by the hotplug thread when callbacks fire, so calling getGamepad() would deadlock).
    manager->setGamepadConnectedCallback([](int idx) {
        log_push("Connected at slot " + std::to_string(idx));
    });

    manager->setGamepadDisconnectedCallback([](int idx) {
        log_push("Disconnected from slot " + std::to_string(idx));
    });

    if (!manager->initialize()) {
        con_restore();
        std::cerr << "Failed to initialize GamepadManager: "
                  << manager->getLastError() << "\n";
        return 1;
    }

    // Seed log + pick first active slot from main thread (safe, no hotplug lock held)
    for (int i = 0; i < manager->getMaxGamepads(); ++i) {
        auto* p = manager->getGamepad(i);
        if (p && p->isConnected()) {
            log_push("Connected at slot " + std::to_string(i) + " (" + p->getName() + ")");
            g_slot = i;
            break;
        }
    }

    auto meta     = gcpad::getBuildMetadata();
    int max_slots = manager->getMaxGamepads();

    while (g_running) {
        manager->updateAll();

        // ── Non-blocking keyboard input ───────────────────────────────────────
        while (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') { g_running = false; break; }

            // Extended key prefix (function keys, arrows): read second byte
            if (ch == 0 || ch == 0xE0) {
                ch = _getch();
                if (ch == 0x4B) // left arrow
                    g_slot = (g_slot + max_slots - 1) % max_slots;
                else if (ch == 0x4D) // right arrow
                    g_slot = (g_slot + 1) % max_slots;
                continue;
            }

            if (ch == ',' || ch == '<') {
                g_slot = (g_slot + max_slots - 1) % max_slots;
            } else if (ch == '.' || ch == '>') {
                g_slot = (g_slot + 1) % max_slots;
            } else if (ch == 'r' || ch == 'R') {
                auto* p = manager->getGamepad(g_slot.load());
                if (p && p->isConnected()) {
                    p->setRumble({ 180, 180 });
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    p->setRumble({ 0, 0 });
                    log_push("Rumble on slot " + std::to_string(g_slot.load()));
                } else {
                    log_push("No controller in slot " + std::to_string(g_slot.load()) + " to rumble");
                }
            } else if (ch == 'l' || ch == 'L') {
                auto* p = manager->getGamepad(g_slot.load());
                if (p && p->isConnected()) {
                    p->setLED(LED_CYCLE[g_led_idx]);
                    g_led_idx = (g_led_idx + 1) % static_cast<int>(sizeof(LED_CYCLE) / sizeof(LED_CYCLE[0]));
                    log_push("LED cycled on slot " + std::to_string(g_slot.load()));
                } else {
                    log_push("No controller in slot " + std::to_string(g_slot.load()) + " to set LED");
                }
            }
        }

        draw(manager.get(), meta);

        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 Hz
    }

    con_clear();
    con_goto(0, 0);
    con_restore();
    std::cout << "GCPad Test Frontend exited.\n" << std::flush;

    manager->shutdown();
    return 0;
}
