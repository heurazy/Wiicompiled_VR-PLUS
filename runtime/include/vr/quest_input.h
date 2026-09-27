// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <dolphin/pad.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "vr/onboarding.h"
#include "vr/control_settings.h"

namespace mkw::vr {

struct QuestInput {
    std::array<UiHandPose,2> ui_hands{};
    std::array<float,2> ui_grips{};
    UiHandPose ui_pointer{};
    UiHandPose ui_left_pointer{};
    float ui_aspect=1;
    bool steamvr=false;
    bool active = false;
    bool wheel_active = false;
    bool cockpit_controls = false;
    bool reverse = false;
    float wheel_steering = 0;
    float wheel_angle = 0; // Physical visual rotation, independent of saturated steering.
    float steering_x = 0, steering_y = 0;
    float raw_steering_x = 0, raw_steering_y = 0; // Calibration remains available while options block gameplay.
    float tricks_x = 0, tricks_y = 0;
    float accelerate = 0, item = 0, drift = 0;
    bool confirm = false, brake = false, trick = false, look_back = false, pause = false;
};

inline float QuestAxis(float value) noexcept {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u) return 0;
    return std::clamp(value, -1.0f, 1.0f);
}


// SteamVR owns the system/menu button. Keep immediate tricks on X and provide
// a deliberate long-X pause without forwarding a duplicated runtime menu action.
class SteamVrTrickPause {
    int64_t started_=0;
    bool held_=false, blocked_=false, fired_=false;
public:
    struct Result { bool trick=false,pause=false; };
    Result Update(bool active,bool down,bool chord,int64_t time) {
        if(!active) { held_=false;blocked_=true;fired_=false;return {}; }
        if(!down) { held_=blocked_=fired_=false;return {}; }
        if(blocked_) return {};
        if(!held_) { held_=true;started_=time; }
        if(chord || time<started_) { fired_=true;return {true,false}; }
        if(!fired_ && time-started_>=650000000) { fired_=true;return {false,true}; }
        return {!fired_,false};
    }
};

inline float CenterQuestAxis(float value, float center) noexcept {
    center = std::clamp(QuestAxis(center), -0.3f, 0.3f);
    const float offset = QuestAxis(value) - center;
    return std::clamp(offset / (offset >= 0 ? 1.0f - center : 1.0f + center), -1.0f, 1.0f);
}

inline PADStatus MapQuestInput(QuestInput input, const QuestStickCalibration& calibration = {}, QuestButtonMapping mapping = {}) noexcept {
    PADStatus pad{};
    pad.err = input.active ? PAD_ERR_NONE : PAD_ERR_NO_CONTROLLER;
    if (!input.active) return pad;
    if (QuestAxis(input.item) > 0.5f && input.trick) return pad; // X + Y opens VR settings.
    if (mapping.swapItemTrick) {
        const bool item=QuestAxis(input.item)>0.5f;
        input.item=input.trick?1.0f:0.0f;
        input.trick=item;
    }
    if (input.cockpit_controls && mapping.swapCockpitDriftBrake) std::swap(input.confirm,input.brake);
    const float x = CenterQuestAxis(input.steering_x, calibration.center_x);
    const float y = CenterQuestAxis(input.steering_y, calibration.center_y);
    const float length = std::sqrt(x * x + y * y);
    const float deadzone = std::clamp(QuestAxis(calibration.deadzone), 0.0f, 0.4f);
    const float outer = std::clamp(QuestAxis(calibration.outer), 0.6f, 1.0f);
    // Radial deadzone with continuous rescaling preserves full steering range.
    if (length > deadzone) {
        const float scale = std::min((length - deadzone) / (outer - deadzone), 1.0f) * 100.0f / length;
        pad.stickX = static_cast<int8_t>(std::lround(x * scale));
        pad.stickY = static_cast<int8_t>(std::lround(y * scale)); // OpenXR +Y is up, like GC.
    }
    if (input.wheel_active) {
        pad.stickX = static_cast<int8_t>(std::lround(QuestAxis(input.wheel_steering) * 100.0f));
        // Keep item aim (forward/back) independent from wheel steering.
    }
    if (QuestAxis(input.accelerate) > 0.5f || (input.confirm && !input.cockpit_controls)) {
        pad.button |= PAD_BUTTON_A;
        pad.analogA = 255;
    }
    if (input.brake) { pad.button |= PAD_BUTTON_B; pad.analogB = 255; }
    if (QuestAxis(input.item) > 0.5f) { pad.button |= PAD_TRIGGER_L; pad.triggerL = 255; }
    if (input.cockpit_controls ? input.confirm : QuestAxis(input.drift) > 0.5f) {
        pad.button |= PAD_TRIGGER_R; pad.triggerR = 255;
    }
    if (input.look_back) pad.button |= PAD_BUTTON_X;
    if (input.pause) pad.button |= PAD_BUTTON_START;
    if (input.trick || QuestAxis(input.tricks_y) > 0.6f) pad.button |= PAD_BUTTON_UP;
    if (QuestAxis(input.tricks_y) < -0.6f) pad.button |= PAD_BUTTON_DOWN;
    if (QuestAxis(input.tricks_x) > 0.6f) pad.button |= PAD_BUTTON_RIGHT;
    if (QuestAxis(input.tricks_x) < -0.6f) pad.button |= PAD_BUTTON_LEFT;
    if (input.reverse) {
        // Native MKW brakes to a stop, then reverses while B stays held.
        // Override throttle/drift so it also works with the accelerator held.
        pad.button &= ~(PAD_BUTTON_A | PAD_TRIGGER_R);
        pad.button |= PAD_BUTTON_B;
        pad.analogA = pad.triggerR = 0;
        pad.analogB = 255;
    }
    return pad;
}

// F10 must not leak gameplay input, including buttons held while it closes.
class QuestPadFilter {
public:
    PADStatus Apply(PADStatus pad, bool blocked) noexcept {
        if (blocked) {
            suppressed_ = pad.button;
            suppress_stick_ = pad.stickX != 0 || pad.stickY != 0;
            pad = {};
            pad.err = PAD_ERR_NONE;
            return pad;
        }
        suppressed_ &= pad.button;
        pad.button &= ~suppressed_;
        if (!(pad.button & PAD_BUTTON_A)) pad.analogA = 0;
        if (!(pad.button & PAD_BUTTON_B)) pad.analogB = 0;
        if (!(pad.button & PAD_TRIGGER_L)) pad.triggerL = 0;
        if (!(pad.button & PAD_TRIGGER_R)) pad.triggerR = 0;
        if (suppress_stick_) {
            suppress_stick_ = pad.stickX != 0 || pad.stickY != 0;
            pad.stickX = pad.stickY = 0;
        }
        return pad;
    }
private:
    uint16_t suppressed_ = 0;
    bool suppress_stick_ = false;
};

// XR thread publishes; guest PADRead consumes. No OpenXR calls on the guest thread.
void PublishQuestInput(const QuestInput& input) noexcept;
void RequestQuestPausePulse() noexcept;
QuestInput ReadQuestInputSnapshot() noexcept;
void SetQuestStickCalibration(const QuestStickCalibration& calibration) noexcept;
void SetQuestButtonMapping(QuestButtonMapping mapping) noexcept;
bool ReadQuestPad(PADStatus& output, bool blocked) noexcept;

} // namespace mkw::vr
