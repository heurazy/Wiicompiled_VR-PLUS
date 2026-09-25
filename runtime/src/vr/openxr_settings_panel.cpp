// SPDX-License-Identifier: GPL-3.0-or-later

#include "vr/openxr_settings_panel.h"
#include "vr/mkw_vr_policy.h"

#include <atomic>
#include <mutex>

namespace mkw::vr {
// Named rather than anonymous: runtime sources are unity-built in groups.
namespace settings_panel_bridge {

std::atomic_bool g_open{false};

struct PublishedPointer {
    std::mutex mutex;
    OpenXRSettingsPanelPointer pointer{};
};

PublishedPointer& Published() {
    static PublishedPointer published;
    return published;
}

} // namespace settings_panel_bridge

void OpenXRSetSettingsPanelOpen(bool open) noexcept {
    settings_panel_bridge::g_open.store(open, std::memory_order_release);
    MkwVRPolicySetSettingsVisible(open);
}

bool OpenXRSettingsPanelOpen() noexcept {
    return settings_panel_bridge::g_open.load(std::memory_order_acquire);
}

void OpenXRPublishSettingsPanelPointer(bool valid, float x, float y, bool select, float wheel) noexcept {
    auto& published = settings_panel_bridge::Published();
    std::lock_guard lock(published.mutex);
    published.pointer.valid = valid;
    published.pointer.x = x;
    published.pointer.y = y;
    published.pointer.select = select;
    published.pointer.wheel += wheel;
}

OpenXRSettingsPanelPointer OpenXRTakeSettingsPanelPointer() noexcept {
    auto& published = settings_panel_bridge::Published();
    std::lock_guard lock(published.mutex);
    OpenXRSettingsPanelPointer pointer = published.pointer;
    published.pointer.wheel = 0.0f;
    return pointer;
}

} // namespace mkw::vr
