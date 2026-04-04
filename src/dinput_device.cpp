#include "dinput_device.h"

#define DIRECTINPUT_VERSION 0x0800
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <iostream>


namespace gcpad {
namespace internal {

// ── Globals ──────────────────────────────────────────────────────────────────
static IDirectInput8A* g_dinput = nullptr;

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string guid_to_string(const GUID& g) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << g.Data1 << '-'
       << std::setw(4) << g.Data2 << '-'
       << std::setw(4) << g.Data3 << '-';
    for (int i = 0; i < 2; ++i) ss << std::setw(2) << static_cast<unsigned>(g.Data4[i]);
    ss << '-';
    for (int i = 2; i < 8; ++i) ss << std::setw(2) << static_cast<unsigned>(g.Data4[i]);
    return ss.str();
}

static GUID string_to_guid(const std::string& s) {
    GUID g = {};
    // Format: 12345678-1234-1234-1234-123456789012
    if (s.size() < 36) return g;
    unsigned long p1 = 0; unsigned short p2 = 0, p3 = 0;
    sscanf_s(s.c_str(), "%08lx-%04hx-%04hx-", &p1, &p2, &p3);
    g.Data1 = p1; g.Data2 = p2; g.Data3 = p3;
    const char* tail = s.c_str() + 19; // skip past 3rd dash
    for (int i = 0; i < 2; ++i) {
        unsigned int b; sscanf_s(tail + i * 2, "%02x", &b); g.Data4[i] = static_cast<BYTE>(b);
    }
    tail += 5; // skip 4 hex chars + dash
    for (int i = 0; i < 6; ++i) {
        unsigned int b; sscanf_s(tail + i * 2, "%02x", &b); g.Data4[2 + i] = static_cast<BYTE>(b);
    }
    return g;
}

/// Check whether a DirectInput device is really an XInput device.
/// XInput devices embed "IG_" in their device path.  This is the
/// standard Microsoft-recommended way to filter them out.
static bool isXInputDevice(const GUID& guidProduct) {
    // Quick path check: DI device paths for XInput contain "IG_"
    // We do a brute-force WMI check (official MS approach).
    // However the simpler path-based heuristic works well enough:
    // XInput devices have a product GUID whose first DWORD embeds
    // the PID in the high word. Known XInput VID is 0x045E.
    // For simplicity, we check the product GUID string for the
    // standard XInput "pidvid" pattern. This is not 100% accurate
    // but avoids the heavy WMI dependency.
    (void)guidProduct;
    // We will rely on the full WMI path approach below.
    return false;
}

// ── XInput detection via device path ────────────────────────────────────────
// DirectInput enumerates XInput devices too. We detect them by checking if
// "IG_" appears in the device instance path (standard MS recommendation).

struct EnumContext {
    std::vector<DInputDeviceInfo> devices;
};

static BOOL CALLBACK EnumDevicesCallback(
    const DIDEVICEINSTANCEA* instance, VOID* context)
{
    auto* ctx = static_cast<EnumContext*>(context);

    // Filter out XInput devices by checking the instance name/product GUID.
    // XInput controllers embed "IG_" in their HID path. We can detect this
    // by temporarily creating the device and checking its property, or by
    // using the product GUID heuristic. The GUID-based heuristic works:
    // If the device's product GUID matches a known XInput VID/PID pattern,
    // skip it. A simpler approach: check for "IG_" in tszInstanceName.
    // Unfortunately tszInstanceName doesn't always contain it, so we use
    // a different approach: create the device temporarily, get its path.
    IDirectInputDevice8A* tmpDevice = nullptr;
    if (SUCCEEDED(g_dinput->CreateDevice(instance->guidInstance, &tmpDevice, nullptr))) {
        DIPROPGUIDANDPATH guidPath;
        guidPath.diph.dwSize = sizeof(DIPROPGUIDANDPATH);
        guidPath.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        guidPath.diph.dwObj = 0;
        guidPath.diph.dwHow = DIPH_DEVICE;

        bool is_xinput = false;
        if (SUCCEEDED(tmpDevice->GetProperty(DIPROP_GUIDANDPATH, &guidPath.diph))) {
            // Convert wide path to narrow and check for "IG_" or "ig_"
            char path[MAX_PATH] = {};
            WideCharToMultiByte(CP_ACP, 0, guidPath.wszPath, -1, path, MAX_PATH, nullptr, nullptr);
            // XInput devices have "IG_" in their path (case-insensitive)
            std::string pathStr(path);
            for (auto& c : pathStr) c = static_cast<char>(toupper(c));
            if (pathStr.find("IG_") != std::string::npos) {
                is_xinput = true;
            }
        }
        tmpDevice->Release();

        if (is_xinput) {
            return DIENUM_CONTINUE; // Skip XInput devices
        }
    }

    DInputDeviceInfo info;
    info.instance_guid = guid_to_string(instance->guidInstance);
    info.product_name = instance->tszProductName;
    // Extract VID/PID from product GUID (standard DI encoding)
    info.vendor_id  = static_cast<uint16_t>(instance->guidProduct.Data1 & 0xFFFF);
    info.product_id = static_cast<uint16_t>((instance->guidProduct.Data1 >> 16) & 0xFFFF);

    ctx->devices.push_back(std::move(info));
    return DIENUM_CONTINUE;
}

// ── DInputDevice implementation ─────────────────────────────────────────────

class DInputDevice : public GamepadDevice {
public:
    DInputDevice(const std::string& instance_guid, int slot);
    ~DInputDevice() override;

    int getIndex() const override { return slot_; }
    std::string getName() const override { return name_; }
    std::string getSerialNumber() const override { return ""; }

    const GamepadState& getState() const override { return state_; }
    GamepadState getRemappedState() const override;
    bool updateState() override;

    bool setLED(const Color&) override { return false; }
    bool setRumble(const Rumble& rumble) override;

    void setRemapper(std::shared_ptr<Remapper> remapper) override;
    bool isConnected() const override { return connected_; }

private:
    std::string instance_guid_str_;
    int slot_;
    std::string name_;
    bool connected_;
    bool acquired_;
    GamepadState state_;
    std::shared_ptr<Remapper> remapper_;

    IDirectInputDevice8A* device_;

    bool acquire();
};

DInputDevice::DInputDevice(const std::string& instance_guid, int slot)
    : instance_guid_str_(instance_guid), slot_(slot),
      name_("DirectInput Controller"), connected_(false), acquired_(false),
      remapper_(nullptr), device_(nullptr)
{
    state_.reset();

    if (!g_dinput) return;

    GUID guid = string_to_guid(instance_guid);
    HRESULT hr = g_dinput->CreateDevice(guid, &device_, nullptr);
    if (FAILED(hr) || !device_) {
        device_ = nullptr;
        return;
    }

    // Set data format to standard joystick
    hr = device_->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) {
        device_->Release();
        device_ = nullptr;
        return;
    }

    // Set cooperative level — background + non-exclusive so we don't steal focus
    // Use nullptr for HWND since we're a DLL; DISCL_BACKGROUND allows reading
    // without a window handle.
    device_->SetCooperativeLevel(nullptr, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);

    // Auto-set axis ranges to [-32768, 32767]
    DIPROPRANGE range;
    range.diph.dwSize = sizeof(DIPROPRANGE);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwObj = 0;
    range.diph.dwHow = DIPH_DEVICE;
    range.lMin = -32768;
    range.lMax = 32767;
    device_->SetProperty(DIPROP_RANGE, &range.diph);

    // Get product name
    DIDEVICEINSTANCEA devInfo;
    devInfo.dwSize = sizeof(devInfo);
    if (SUCCEEDED(device_->GetDeviceInfo(&devInfo))) {
        name_ = std::string("DirectInput: ") + devInfo.tszProductName;
    }
}

DInputDevice::~DInputDevice() {
    if (device_) {
        if (acquired_) device_->Unacquire();
        device_->Release();
        device_ = nullptr;
    }
}

GamepadState DInputDevice::getRemappedState() const {
    if (remapper_) return remapper_->apply(state_);
    return state_;
}

void DInputDevice::setRemapper(std::shared_ptr<Remapper> remapper) {
    remapper_ = std::move(remapper);
}

bool DInputDevice::acquire() {
    if (!device_) return false;
    HRESULT hr = device_->Acquire();
    if (SUCCEEDED(hr) || hr == S_FALSE) { // S_FALSE = already acquired
        acquired_ = true;
        return true;
    }
    // DIERR_INPUTLOST — try re-acquiring
    if (hr == DIERR_INPUTLOST) {
        hr = device_->Acquire();
        if (SUCCEEDED(hr)) {
            acquired_ = true;
            return true;
        }
    }
    return false;
}

bool DInputDevice::updateState() {
    if (!device_) {
        connected_ = false;
        state_.is_connected = false;
        return false;
    }

    if (!acquired_) {
        if (!acquire()) {
            connected_ = false;
            state_.is_connected = false;
            return false;
        }
    }

    HRESULT hr = device_->Poll();
    if (FAILED(hr)) {
        // Try re-acquiring
        acquired_ = false;
        if (!acquire()) {
            connected_ = false;
            state_.is_connected = false;
            return false;
        }
        hr = device_->Poll();
        if (FAILED(hr)) {
            connected_ = false;
            state_.is_connected = false;
            return false;
        }
    }

    DIJOYSTATE2 js;
    memset(&js, 0, sizeof(js));
    hr = device_->GetDeviceState(sizeof(DIJOYSTATE2), &js);
    if (FAILED(hr)) {
        acquired_ = false;
        connected_ = false;
        state_.is_connected = false;
        return false;
    }

    connected_ = true;
    state_.is_connected = true;
    state_.setTimestamp(std::chrono::steady_clock::now());

    // Axes: DI axes are LONG in [-32768, 32767] (we set DIPROP_RANGE)
    state_.axes[static_cast<size_t>(Axis::LeftX)]  = js.lX / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::LeftY)]  = js.lY / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::RightX)] = js.lZ / 32767.0f;  // Often Z = right stick X
    state_.axes[static_cast<size_t>(Axis::RightY)] = js.lRz / 32767.0f; // Rz = right stick Y

    // Triggers: DirectInput often maps triggers to Rx/Ry or combined Z axis.
    // We'll use Rx and Ry as individual triggers (range [-32768, 32767]).
    // Map from [-32768, 32767] to [0, 1]
    state_.axes[static_cast<size_t>(Axis::LeftTrigger)]  = (js.lRx + 32768.0f) / 65535.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] = (js.lRy + 32768.0f) / 65535.0f;

    // Digital trigger flags
    state_.buttons[static_cast<size_t>(Button::L2)] = state_.axes[static_cast<size_t>(Axis::LeftTrigger)] > 0.5f;
    state_.buttons[static_cast<size_t>(Button::R2)] = state_.axes[static_cast<size_t>(Axis::RightTrigger)] > 0.5f;

    // D-Pad: DI uses POV hat (0-35999 in hundredths of degrees, -1 = centered)
    DWORD pov = js.rgdwPOV[0];
    bool pov_centered = (LOWORD(pov) == 0xFFFF);
    state_.buttons[static_cast<size_t>(Button::DPad_Up)]    = !pov_centered && (pov >= 31500 || pov <= 4500);
    state_.buttons[static_cast<size_t>(Button::DPad_Right)] = !pov_centered && (pov >= 4500 && pov <= 13500);
    state_.buttons[static_cast<size_t>(Button::DPad_Down)]  = !pov_centered && (pov >= 13500 && pov <= 22500);
    state_.buttons[static_cast<size_t>(Button::DPad_Left)]  = !pov_centered && (pov >= 22500 && pov <= 31500);

    // Buttons: DI supports up to 128 buttons. Map first 14 to our layout.
    // Standard mapping (varies by controller, but this covers most gamepads):
    //   0=A/Cross, 1=B/Circle, 2=X/Square, 3=Y/Triangle
    //   4=LB, 5=RB, 6=Back/Select, 7=Start
    //   8=L3, 9=R3, 10=Guide
    state_.buttons[static_cast<size_t>(Button::A)]      = (js.rgbButtons[0] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::B)]      = (js.rgbButtons[1] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::X)]      = (js.rgbButtons[2] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::Y)]      = (js.rgbButtons[3] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::L1)]     = (js.rgbButtons[4] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::R1)]     = (js.rgbButtons[5] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::Select)] = (js.rgbButtons[6] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::Start)]  = (js.rgbButtons[7] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::L3)]     = (js.rgbButtons[8] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::R3)]     = (js.rgbButtons[9] & 0x80) != 0;
    state_.buttons[static_cast<size_t>(Button::Guide)]  = (js.rgbButtons[10] & 0x80) != 0;

    return true;
}

bool DInputDevice::setRumble(const Rumble& /*rumble*/) {
    // DirectInput force-feedback requires creating effect objects.
    // This is complex and controller-specific. Return false for now.
    // TODO: Implement DIEFFECT-based rumble for FF-capable devices.
    return false;
}

// ── Public API ──────────────────────────────────────────────────────────────

bool initializeDInput() {
    if (g_dinput) return true; // Already initialized

    HRESULT hr = DirectInput8Create(
        GetModuleHandle(nullptr),
        DIRECTINPUT_VERSION,
        IID_IDirectInput8A,
        reinterpret_cast<void**>(&g_dinput),
        nullptr);

    return SUCCEEDED(hr) && g_dinput != nullptr;
}

void shutdownDInput() {
    if (g_dinput) {
        g_dinput->Release();
        g_dinput = nullptr;
    }
}

std::vector<DInputDeviceInfo> enumerateDInputDevices() {
    std::vector<DInputDeviceInfo> result;
    if (!g_dinput) return result;

    EnumContext ctx;
    g_dinput->EnumDevices(
        DI8DEVCLASS_GAMECTRL,
        EnumDevicesCallback,
        &ctx,
        DIEDFL_ATTACHEDONLY);

    return ctx.devices;
}

std::unique_ptr<GamepadDevice> createDInputDevice(
    const std::string& instance_guid, int slot)
{
    return std::make_unique<DInputDevice>(instance_guid, slot);
}

} // namespace internal
} // namespace gcpad
