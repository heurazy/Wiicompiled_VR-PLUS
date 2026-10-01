// SPDX-License-Identifier: GPL-3.0-or-later

#include "vr/openxr_driving.h"

#include <mutex>
#include <chrono>

namespace mkw::vr {
// Named rather than anonymous: runtime sources are unity-built in groups.
namespace driving_bridge {

struct Published {
    std::mutex mutex;
    DrivingSnapshot snapshot{};
    BodyHandCalibration calibration{};
    HandWorkshopTracking workshop{};
    bool workshop_active=false;
};

Published& Get() {
    static Published published;
    return published;
}

} // namespace driving_bridge

void OpenXRPublishDriving(const DrivingSnapshot& snapshot) noexcept {
    auto& published = driving_bridge::Get();
    std::lock_guard lock(published.mutex);
    published.snapshot = snapshot;
    if(!snapshot.cockpit_active || snapshot.vehicle_identity!=published.calibration.vehicle) {
        published.calibration.active=false;
        published.calibration.valid={};
    }
    published.snapshot.published_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

DrivingSnapshot OpenXRReadDriving() noexcept {
    auto& published = driving_bridge::Get();
    std::lock_guard lock(published.mutex);
    return published.snapshot;
}

BodyHandCalibration OpenXRReadBodyHandCalibration() {
    auto& p=driving_bridge::Get();std::lock_guard lock(p.mutex);return p.calibration;
}
void OpenXRPublishBodyHandPose(uint64_t vehicle, const std::string& model, unsigned hand,
                             const std::array<float,12>& pose) {
    if(hand>=2) return;
    auto& p=driving_bridge::Get();std::lock_guard lock(p.mutex);
    auto& c=p.calibration;
    if(c.vehicle!=vehicle || c.model!=model) { c={};c.vehicle=vehicle;c.model=model; }
    if(!c.active) { c.valid[hand]=true;c.seat_from_wrist[hand]=pose; }
}
bool OpenXRBeginBodyHandCalibration() {
    auto& p=driving_bridge::Get();std::lock_guard lock(p.mutex);
    auto& c=p.calibration;
    if(c.active || !c.vehicle || c.model.empty() || !c.valid[0] || !c.valid[1] ||
       !p.snapshot.cockpit_active || p.snapshot.vehicle_identity!=c.vehicle) return false;
    c.active=true;c.captured={};return true;
}
void OpenXRMarkBodyHandCaptured(unsigned hand) {
    if(hand>=2) return;
    auto& p=driving_bridge::Get();std::lock_guard lock(p.mutex);
    if(p.calibration.active && p.calibration.valid[hand]) p.calibration.captured[hand]=true;
}
void OpenXREndBodyHandCalibration() {
    auto& p=driving_bridge::Get();std::lock_guard lock(p.mutex);p.calibration.active=false;
}
void OpenXRPublishHandWorkshopTracking(HandWorkshopTracking tracking) {
    auto& p=driving_bridge::Get();std::lock_guard lock(p.mutex);
    tracking.published_ns=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    p.workshop=tracking;
}
HandWorkshopTracking OpenXRReadHandWorkshopTracking() {
    auto& p=driving_bridge::Get();std::lock_guard lock(p.mutex);return p.workshop;
}
void OpenXRSetHandWorkshopActive(bool active) {
    auto& p=driving_bridge::Get();std::lock_guard lock(p.mutex);p.workshop_active=active;
}
bool OpenXRHandWorkshopActive() {
    auto& p=driving_bridge::Get();std::lock_guard lock(p.mutex);return p.workshop_active;
}

} // namespace mkw::vr
