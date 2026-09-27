// SPDX-License-Identifier: GPL-3.0-or-later

#include "vr/openxr_wii_remote.h"

#include <atomic>
#include <mutex>

namespace mkw::vr {
// Named rather than anonymous: runtime sources are unity-built in groups, and
// anonymous namespaces from other VR files would share this one's scope.
namespace wii_remote_bridge {

struct PublishedSample {
    std::mutex mutex;
    OpenXRWiiRemoteSample sample{};
    bool available = false;
};

PublishedSample& Published() {
    static PublishedSample published;
    return published;
}

std::atomic<OpenXRControllerMode> g_mode{OpenXRControllerMode::Gamepad};
// SDL_JoystickID of the virtual gamepad the samples belong to; 0 when none.
std::atomic<uint32_t> g_joystick_id{0};
std::atomic<bool> g_rumble{false};

} // namespace wii_remote_bridge

void OpenXRSetControllerMode(OpenXRControllerMode mode) noexcept {
    wii_remote_bridge::g_mode.store(mode, std::memory_order_relaxed);
    if (mode != OpenXRControllerMode::WiiRemote) {
        // The game stops addressing the remote's motor once it is gone.
        wii_remote_bridge::g_rumble.store(false, std::memory_order_relaxed);
    }
}

OpenXRControllerMode OpenXRGetControllerMode() noexcept {
    return wii_remote_bridge::g_mode.load(std::memory_order_relaxed);
}

bool OpenXRWiiRemoteOwnsGamepad(uint32_t sdl_joystick_id) noexcept {
    return sdl_joystick_id != 0 && OpenXRGetControllerMode() == OpenXRControllerMode::WiiRemote &&
           wii_remote_bridge::g_joystick_id.load(std::memory_order_relaxed) == sdl_joystick_id;
}

bool OpenXRIsControllerGamepad(uint32_t sdl_joystick_id) noexcept {
    return sdl_joystick_id != 0 && wii_remote_bridge::g_joystick_id.load(std::memory_order_relaxed) == sdl_joystick_id;
}

bool OpenXRReadWiiRemote(OpenXRWiiRemoteSample& sample) noexcept {
    auto& published = wii_remote_bridge::Published();
    std::lock_guard lock(published.mutex);
    if (!published.available) {
        return false;
    }
    sample = published.sample;
    return true;
}

void OpenXRSetWiiRemoteRumble(bool active) noexcept {
    wii_remote_bridge::g_rumble.store(active, std::memory_order_relaxed);
}

void OpenXRPublishWiiRemote(uint32_t sdl_joystick_id, const OpenXRWiiRemoteSample& sample) noexcept {
    auto& published = wii_remote_bridge::Published();
    {
        std::lock_guard lock(published.mutex);
        published.sample = sample;
        published.available = true;
    }
    wii_remote_bridge::g_joystick_id.store(sdl_joystick_id, std::memory_order_relaxed);
}

void OpenXRWithdrawWiiRemote() noexcept {
    wii_remote_bridge::g_joystick_id.store(0, std::memory_order_relaxed);
    wii_remote_bridge::g_rumble.store(false, std::memory_order_relaxed);
    auto& published = wii_remote_bridge::Published();
    std::lock_guard lock(published.mutex);
    published.available = false;
}

bool OpenXRWiiRemoteRumbleRequested() noexcept {
    return OpenXRGetControllerMode() == OpenXRControllerMode::WiiRemote &&
           wii_remote_bridge::g_rumble.load(std::memory_order_relaxed);
}

} // namespace mkw::vr
