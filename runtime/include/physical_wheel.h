// SPDX-License-Identifier: GPL-3.0-or-later
// USB steering wheel and pedals for player 1. Ported from heurazy's
// mario-kart-wii-VR-port (GPL-3.0-or-later).
//
// Any SDL joystick works: the steering axis, both pedals and the buttons are
// chosen and calibrated by moving or pressing them (F10 > Controllers > USB
// wheel and pedals), so no per-model table or gamepad mapping is involved, and
// separate pedals, reversed axes and combined pedal axes all calibrate the same
// way. The wheel is a GameCube controller on port 0: in a race it owns
// steering, pedals and the assigned buttons; in menus it adds its confirm,
// back, pause and D-pad (the steering device's first hat) to whatever else
// drives port 0. The settings live in PhysicalWheel.toml beside Config.toml.
//
// The mapping below is pure and tested (tests/physical_wheel_tests.cpp).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <dolphin/pad.h>

union SDL_Event;

namespace physical_wheel {

// Endpoint calibration also handles reversed and combined pedal axes.
inline float Pedal(int raw, int released, int pressed) {
    if (std::abs(pressed - released) < 1024) return 0;
    return std::clamp(float(raw - released) / float(pressed - released), 0.f, 1.f);
}
inline float Steering(int raw, int left, int center, int right, float deadzone) {
    if (std::abs(left - center) < 1024 || std::abs(right - center) < 1024 || (left < center) == (right < center))
        return 0;
    const float direction = float(raw - center) / float(right - center);
    const float x = direction >= 0 ? Pedal(raw, center, right) : -Pedal(raw, center, left);
    deadzone = std::clamp(deadzone, 0.f, .25f);
    return std::copysign(std::max(0.f, (std::abs(x) - deadzone) / (1 - deadzone)), x);
}
inline PADStatus Map(float steering, float throttle, float brake, bool drift, bool item) {
    PADStatus p{};
    p.err = PAD_ERR_NONE;
    p.stickX = static_cast<int8_t>(std::lround(std::clamp(steering, -1.f, 1.f) * 100));
    if (throttle > .1f) {
        p.button |= PAD_BUTTON_A;
        p.analogA = 255;
    }
    if (drift) {
        p.button |= PAD_TRIGGER_R;
        p.triggerR = 255;
    }
    if (item) {
        p.button |= PAD_TRIGGER_L;
        p.triggerL = 255;
    }
    if (brake > .1f) {
        p.button &= ~(PAD_BUTTON_A | PAD_TRIGGER_R);
        p.analogA = p.triggerR = 0;
        p.button |= PAD_BUTTON_B;
        p.analogB = 255;
    }
    return p;
}

// SDL_HAT_UP/RIGHT/DOWN/LEFT bits as the GameCube D-pad.
inline uint16_t HatButtons(uint8_t hat) {
    uint16_t buttons = 0;
    if (hat & 0x01) buttons |= PAD_BUTTON_UP;
    if (hat & 0x02) buttons |= PAD_BUTTON_RIGHT;
    if (hat & 0x04) buttons |= PAD_BUTTON_DOWN;
    if (hat & 0x08) buttons |= PAD_BUTTON_LEFT;
    return buttons;
}

// One sample of the calibrated hardware.
struct Controls {
    float steering = 0, throttle = 0, brake = 0;
    bool drift = false, item = false, trick = false, confirm = false, pause = false, back = false;
    uint8_t hat = 0;
};

// Race: steering on the stick, the accelerator on A, the brake pedal on B
// (braking, then reversing, over the accelerator and drift), R drift, L item,
// the D-pad and the trick button for tricks and wheelies.
inline PADStatus RacePad(const Controls& c) {
    auto p = Map(c.steering, c.throttle, c.brake, c.drift, c.item);
    p.button |= HatButtons(c.hat);
    if (c.trick) p.button |= PAD_BUTTON_UP;
    if (c.confirm && !(p.button & PAD_BUTTON_B)) {
        p.button |= PAD_BUTTON_A;
        p.analogA = 255;
    }
    if (c.pause) p.button |= PAD_BUTTON_START;
    return p;
}

// Menus: only deliberate presses, so a resting foot or a turned wheel never
// moves a cursor or confirms. The pedals and the steering stay out.
inline PADStatus MenuPad(const Controls& c) {
    PADStatus p{};
    p.err = PAD_ERR_NONE;
    p.button = HatButtons(c.hat);
    if (c.confirm) {
        p.button |= PAD_BUTTON_A;
        p.analogA = 255;
    }
    if (c.back) {
        p.button |= PAD_BUTTON_B;
        p.analogB = 255;
    }
    if (c.pause) p.button |= PAD_BUTTON_START;
    return p;
}

// Input that was held while blocked (settings open, not yet armed) stays out of
// the game until it is released.
class PadFilter {
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

// Folds the wheel into port 0. A race gives the wheel the whole pad, keeping
// only the other source's pause and its stick's vertical axis (item aiming);
// menus add the wheel's buttons to it.
inline void Merge(PADStatus& pad, PADStatus wheel, bool race, bool blocked, PadFilter& filter) {
    if (race) {
        const auto pause = pad.button & PAD_BUTTON_START;
        const auto aim = pad.stickY;
        pad = filter.Apply(wheel, blocked);
        pad.button |= pause;
        pad.stickY = blocked ? 0 : aim;
    } else {
        wheel.stickX = wheel.stickY = 0;
        wheel = filter.Apply(wheel, blocked);
        pad.button |= wheel.button;
        pad.analogA = std::max(pad.analogA, wheel.analogA);
        pad.analogB = std::max(pad.analogB, wheel.analogB);
        pad.err = PAD_ERR_NONE;
    }
}

// Device and UI operations run on the game thread (PADRead and the settings
// overlay); the XR pacing thread reads a locked snapshot.
bool ReadPad(PADStatus& pad, bool blocked, bool race);
void DrawSettings();
void HandleSdlEvent(const SDL_Event& event);
// The calibrated steering, -1..1, while the wheel is driving a race.
bool SteeringSnapshot(float& steering);
// PADControlMotor for port 0; false when the wheel does not own it.
bool Motor(int channel, unsigned command);
void Shutdown();

} // namespace physical_wheel
