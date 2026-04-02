#include "GamepadManager.h"
#include "hid_device.h"
#include "ps_device.h"
#include "xbox_device.h"
#include "nintendo_device.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <algorithm>

namespace gcpad {

// GamepadManager implementation
class GamepadManagerImpl : public GamepadManager {
public:
    GamepadManagerImpl();
    ~GamepadManagerImpl() override;

    // GamepadManager interface
    bool initialize() override;
    void shutdown() override;

    int getMaxGamepads() const override { return MAX_GAMEPADS; }
    int getConnectedGamepadCount() const override;
    std::vector<int> getConnectedGamepadIndices() const override;

    GamepadDevice* getGamepad(int index) override;
    const GamepadDevice* getGamepad(int index) const override;

    void setGamepadConnectedCallback(GamepadConnectedCallback callback) override;
    void setGamepadDisconnectedCallback(GamepadDisconnectedCallback callback) override;

    void updateAll() override;

    void setRemapper(std::shared_ptr<Remapper> remapper) override;
    std::shared_ptr<Remapper> getRemapper() const override;

    std::string getLastError() const override { return last_error_; }

private:
    static constexpr int MAX_GAMEPADS = 4;

    std::vector<std::unique_ptr<GamepadDevice>> gamepads_;
    std::vector<std::string> connected_device_paths_;
    std::shared_ptr<Remapper> global_remapper_;

    std::thread hotplug_thread_;
    std::atomic<bool> hotplug_running_;
    mutable std::mutex mutex_;

    GamepadConnectedCallback connected_callback_;
    GamepadDisconnectedCallback disconnected_callback_;

    mutable std::string last_error_;

    // Internal methods
    void hotplug_detection_loop();
    void check_for_new_devices();
    void check_for_disconnected_devices();
    int find_available_slot() const;
    int find_slot_for_path(const std::string& path) const;
    bool is_supported_device(uint16_t vendor_id, uint16_t product_id) const;
    std::unique_ptr<GamepadDevice> create_device(std::unique_ptr<internal::HidDevice> hid_device, int slot);
    void apply_remapper_to_all();
};

GamepadManagerImpl::GamepadManagerImpl()
    : hotplug_running_(false) {
    gamepads_.resize(MAX_GAMEPADS);
    connected_device_paths_.resize(MAX_GAMEPADS);
}

GamepadManagerImpl::~GamepadManagerImpl() {
    shutdown();
}

bool GamepadManagerImpl::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto hid_devices = internal::enumerate_hid_devices();

    for (auto& hid_device : hid_devices) {
        auto attributes = hid_device->get_attributes();
        if (!is_supported_device(attributes.vendor_id, attributes.product_id)) {
            continue;
        }

        std::string current_path = hid_device->device_path();
        if (find_slot_for_path(current_path) != -1) {
            continue;
        }

        int slot = find_available_slot();
        if (slot == -1) {
            continue;
        }

        gamepads_[slot] = create_device(std::move(hid_device), slot);
        connected_device_paths_[slot] = current_path;

        if (global_remapper_ && gamepads_[slot]) {
            gamepads_[slot]->setRemapper(global_remapper_);
        }

        if (connected_callback_) {
            connected_callback_(slot);
        }
    }

    hotplug_running_ = true;
    hotplug_thread_ = std::thread(&GamepadManagerImpl::hotplug_detection_loop, this);

    return true;
}

void GamepadManagerImpl::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    hotplug_running_ = false;
    if (hotplug_thread_.joinable()) {
        hotplug_thread_.join();
    }

    for (auto& gamepad : gamepads_) {
        gamepad.reset();
    }
    std::fill(connected_device_paths_.begin(), connected_device_paths_.end(), std::string());
}

int GamepadManagerImpl::getConnectedGamepadCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    int count = 0;
    for (const auto& gamepad : gamepads_) {
        if (gamepad && gamepad->isConnected()) {
            ++count;
        }
    }
    return count;
}

std::vector<int> GamepadManagerImpl::getConnectedGamepadIndices() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<int> indices;
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (gamepads_[i] && gamepads_[i]->isConnected()) {
            indices.push_back(i);
        }
    }
    return indices;
}

GamepadDevice* GamepadManagerImpl::getGamepad(int index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (index >= 0 && index < MAX_GAMEPADS && gamepads_[index]) {
        return gamepads_[index].get();
    }
    return nullptr;
}

const GamepadDevice* GamepadManagerImpl::getGamepad(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (index >= 0 && index < MAX_GAMEPADS && gamepads_[index]) {
        return gamepads_[index].get();
    }
    return nullptr;
}

void GamepadManagerImpl::setGamepadConnectedCallback(GamepadConnectedCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_callback_ = callback;
}

void GamepadManagerImpl::setGamepadDisconnectedCallback(GamepadDisconnectedCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnected_callback_ = callback;
}

void GamepadManagerImpl::updateAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& gamepad : gamepads_) {
        if (gamepad) {
            if (!gamepad->updateState()) {
                // fail safe: device disconnected, cleanup handled in hotplug checker
            }
        }
    }
}

void GamepadManagerImpl::setRemapper(std::shared_ptr<Remapper> remapper) {
    std::lock_guard<std::mutex> lock(mutex_);
    global_remapper_ = std::move(remapper);
    apply_remapper_to_all();
}

std::shared_ptr<Remapper> GamepadManagerImpl::getRemapper() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return global_remapper_;
}

void GamepadManagerImpl::apply_remapper_to_all() {
    for (auto& gamepad : gamepads_) {
        if (gamepad && global_remapper_) {
            gamepad->setRemapper(global_remapper_);
        }
    }
}

void GamepadManagerImpl::hotplug_detection_loop() {
    while (hotplug_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        std::lock_guard<std::mutex> lock(mutex_);

        check_for_new_devices();
        check_for_disconnected_devices();
    }
}

void GamepadManagerImpl::check_for_new_devices() {
    auto hid_devices = internal::enumerate_hid_devices();

    for (auto& hid_device : hid_devices) {
        auto path = hid_device->device_path();
        if (find_slot_for_path(path) != -1) {
            continue;
        }

        auto attributes = hid_device->get_attributes();
        if (!is_supported_device(attributes.vendor_id, attributes.product_id)) {
            continue;
        }

        int slot = find_available_slot();
        if (slot == -1) {
            continue;
        }

        gamepads_[slot] = create_device(std::move(hid_device), slot);
        connected_device_paths_[slot] = path;

        if (global_remapper_ && gamepads_[slot]) {
            gamepads_[slot]->setRemapper(global_remapper_);
        }

        if (connected_callback_) {
            connected_callback_(slot);
        }
    }
}

void GamepadManagerImpl::check_for_disconnected_devices() {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (gamepads_[i] && !gamepads_[i]->isConnected()) {
            gamepads_[i].reset();
            connected_device_paths_[i].clear();
            if (disconnected_callback_) {
                disconnected_callback_(i);
            }
        }
    }
}

int GamepadManagerImpl::find_available_slot() const {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (!gamepads_[i]) {
            return i;
        }
    }
    return -1;
}

int GamepadManagerImpl::find_slot_for_path(const std::string& path) const {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (!connected_device_paths_[i].empty() && connected_device_paths_[i] == path) {
            return i;
        }
    }
    return -1;
}

bool GamepadManagerImpl::is_supported_device(uint16_t vendor_id, uint16_t product_id) const {
    auto ps_infos = internal::getPlayStationDeviceInfos();
    for (const auto& info : ps_infos) {
        if (info.vendor_id == vendor_id && info.product_id == product_id) {
            return true;
        }
    }

    auto xbox_infos = internal::getXboxDeviceInfos();
    for (const auto& info : xbox_infos) {
        if (info.vendor_id == vendor_id && info.product_id == product_id) {
            return true;
        }
    }

    auto nintendo_infos = internal::getNintendoDeviceInfos();
    for (const auto& info : nintendo_infos) {
        if (info.vendor_id == vendor_id && info.product_id == product_id) {
            return true;
        }
    }

    return false;
}

std::unique_ptr<GamepadDevice> GamepadManagerImpl::create_device(std::unique_ptr<internal::HidDevice> hid_device, int slot) {
    auto attributes = hid_device->get_attributes();

    auto ps_infos = internal::getPlayStationDeviceInfos();
    if (std::any_of(ps_infos.begin(), ps_infos.end(), [&](const internal::PlayStationDeviceInfo& info) {
            return info.vendor_id == attributes.vendor_id && info.product_id == attributes.product_id;
        })) {
        return internal::createPlayStationDevice(std::move(hid_device), slot);
    }

    auto xbox_infos = internal::getXboxDeviceInfos();
    if (std::any_of(xbox_infos.begin(), xbox_infos.end(), [&](const internal::XboxDeviceInfo& info) {
            return info.vendor_id == attributes.vendor_id && info.product_id == attributes.product_id;
        })) {
        return internal::createXboxDevice(std::move(hid_device), slot);
    }

    auto nintendo_infos = internal::getNintendoDeviceInfos();
    if (std::any_of(nintendo_infos.begin(), nintendo_infos.end(), [&](const internal::NintendoDeviceInfo& info) {
            return info.vendor_id == attributes.vendor_id && info.product_id == attributes.product_id;
        })) {
        return internal::createNintendoDevice(std::move(hid_device), slot);
    }

    return nullptr;
}

// Factory function
std::unique_ptr<GamepadManager> GamepadManager::create() {
    return std::make_unique<GamepadManagerImpl>();
}

// Convenience functions
std::unique_ptr<GamepadManager> createGamepadManager() {
    return GamepadManager::create();
}

std::vector<HidDeviceInfo> getAllHidDevices() {
    std::vector<HidDeviceInfo> infos;
    // Enumerate ALL HID devices
    auto devices = internal::enumerate_hid_devices();
    
    std::cerr << "[DEBUG] enumerate_hid_devices returned " << devices.size() << " devices" << std::endl;
    
    for (size_t i = 0; i < devices.size(); ++i) {
        auto& dev = devices[i];

        // Try to open the device to retrieve attributes
        bool opened = dev->open();
        std::cerr << "[DEBUG] Device " << i << " open: " << (opened ? "YES" : "NO")
                  << " path=" << dev->device_path() << std::endl;

        if (!opened) {
            // Skip devices we cannot read, first ones usually not controllers
            continue;
        }

        auto attrs = dev->get_attributes();
        std::string product_str = dev->get_product_string();
        std::cerr << "[DEBUG] Device " << i << " VID:PID = " << std::hex << attrs.vendor_id
                  << ":" << attrs.product_id << std::dec << std::endl;
        std::cerr << "[DEBUG] Device " << i << " Product: '" << product_str << "'" << std::endl;

        dev->close();

        if (attrs.vendor_id == 0 && attrs.product_id == 0) {
            // Ignore non-controller or generic HID endpoints
            continue;
        }

        infos.push_back({
            attrs.vendor_id,
            attrs.product_id,
            product_str,
            dev->device_path()
        });
    }
    return infos;
}

bool initializeGamepadManager(GamepadManager* manager) {
    return manager ? manager->initialize() : false;
}

void shutdownGamepadManager(GamepadManager* manager) {
    if (manager) {
        manager->shutdown();
    }
}

int getConnectedGamepadCount(GamepadManager* manager) {
    return manager ? manager->getConnectedGamepadCount() : 0;
}

GamepadDevice* getGamepad(GamepadManager* manager, int index) {
    return manager ? manager->getGamepad(index) : nullptr;
}

bool updateGamepad(GamepadDevice* gamepad) {
    return gamepad ? gamepad->updateState() : false;
}

const GamepadState& getGamepadState(GamepadDevice* gamepad) {
    static GamepadState empty_state;
    return gamepad ? gamepad->getState() : empty_state;
}

void setGlobalRemapper(GamepadManager* manager, std::shared_ptr<Remapper> remapper) {
    if (manager) {
        manager->setRemapper(std::move(remapper));
    }
}

std::shared_ptr<Remapper> getGlobalRemapper(GamepadManager* manager) {
    return manager ? manager->getRemapper() : nullptr;
}

} // namespace gcpad