// SPDX-License-Identifier: GPL-3.0-or-later

#include "vr/openxr_settings_panel.h"
#include "vr/mkw_vr_policy.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace mkw::vr {
namespace menu_bridge {
std::mutex mutex;
OpenXRUiSnapshot snapshot;
QuestInput controls;
PhysicalOptionsButtons physical_options;
std::chrono::steady_clock::time_point physical_options_time;
std::atomic<bool> introduction{false}, pause{false};
}
void OpenXRPublishPortControls(const QuestInput& input) noexcept {
    std::lock_guard lock(menu_bridge::mutex); menu_bridge::controls=input;
}
QuestInput OpenXRReadPortControls() noexcept {
    std::lock_guard lock(menu_bridge::mutex); return menu_bridge::controls;
}
void OpenXRPublishPhysicalOptionsButtons(PhysicalOptionsButtons buttons) noexcept {
    std::lock_guard lock(menu_bridge::mutex);
    menu_bridge::physical_options=buttons;
    menu_bridge::physical_options_time=std::chrono::steady_clock::now();
}
PhysicalOptionsButtons OpenXRReadPhysicalOptionsButtons() noexcept {
    std::lock_guard lock(menu_bridge::mutex);
    if(std::chrono::steady_clock::now()-menu_bridge::physical_options_time>
       std::chrono::milliseconds(250)) return {};
    return menu_bridge::physical_options;
}
void OpenXRPublishUiSnapshot(const OpenXRUiSnapshot& snapshot) noexcept {
    std::lock_guard lock(menu_bridge::mutex); menu_bridge::snapshot=snapshot;
}
OpenXRUiSnapshot OpenXRReadUiSnapshot() noexcept {
    std::lock_guard lock(menu_bridge::mutex); return menu_bridge::snapshot;
}
void OpenXRSetIntroductionActive(bool active) noexcept { menu_bridge::introduction.store(active); }
bool OpenXRIntroductionActive() noexcept { return menu_bridge::introduction.load(); }
void OpenXRRequestTutorialPause() noexcept { menu_bridge::pause.store(true); }
bool OpenXRTakeTutorialPause(bool remote) noexcept {
    (void)remote;
    return menu_bridge::pause.exchange(false);
}

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

void OpenXRPublishSettingsPanelPointer(bool valid, float x, float y, bool select, float wheel,uint32_t hand) noexcept {
    auto& published = settings_panel_bridge::Published();
    std::lock_guard lock(published.mutex);
    published.pointer.valid = valid;
    published.pointer.x = x;
    published.pointer.y = y;
    published.pointer.select = select;
    published.pointer.wheel += wheel;
    published.pointer.hand=hand<2?hand:1;
}

OpenXRSettingsPanelPointer OpenXRTakeSettingsPanelPointer() noexcept {
    auto& published = settings_panel_bridge::Published();
    std::lock_guard lock(published.mutex);
    OpenXRSettingsPanelPointer pointer = published.pointer;
    published.pointer.wheel = 0.0f;
    return pointer;
}
OpenXRSettingsPanelPointer OpenXRReadSettingsPanelPointer() noexcept {
    auto& published=settings_panel_bridge::Published();
    std::lock_guard lock(published.mutex);return published.pointer;
}

} // namespace mkw::vr
