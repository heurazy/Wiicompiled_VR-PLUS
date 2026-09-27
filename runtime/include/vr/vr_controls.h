// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mkw::vr {

enum class CameraMode { Game, FirstPerson, Far };

inline constexpr CameraMode NextCameraMode(CameraMode mode) noexcept {
    return mode == CameraMode::Game ? CameraMode::FirstPerson
         : mode == CameraMode::FirstPerson ? CameraMode::Far : CameraMode::Game;
}

// Require a release after focus/tracking loss. A held button cannot cycle
// repeatedly or trigger a camera change when the runtime regains focus.
class CameraClickLatch {
public:
    bool Update(bool active, bool pressed) noexcept {
        const bool clicked = active && armed_ && pressed;
        armed_ = active && !pressed;
        return clicked;
    }
private:
    bool armed_ = false;
};

} // namespace mkw::vr
