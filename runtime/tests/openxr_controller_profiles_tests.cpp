// SPDX-License-Identifier: GPL-3.0-or-later

#include "vr/openxr_controller_profiles.h"
#include "vr/controller_model_connection.h"

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAILED: " << message << '\n';
    }
}

bool HasProfile(std::string_view profile) {
    for (const auto& candidate : mkw::vr::kOpenXRControllerProfiles) {
        if (candidate.interaction_profile == profile) return true;
    }
    return false;
}

size_t ActionCount(const mkw::vr::OpenXRControllerActionPaths& actions) {
    size_t count = 0;
    count += !actions.steering.empty();
    count += !actions.tricks.empty();
    count += !actions.accelerate.empty();
    count += !actions.item[0].empty() || !actions.item[1].empty();
    count += !actions.drift.empty();
    count += !actions.drift_click.empty();
    count += !actions.confirm.empty();
    count += !actions.brake.empty();
    count += !actions.trick.empty();
    count += !actions.look_back.empty();
    count += !actions.pause[0].empty() || !actions.pause[1].empty();
    return count;
}

} // namespace

int main() {
    using namespace mkw::vr;

    // Reproduce the issue's thousands of model-load failures on VDXR without
    // requiring a headset: neither the OpenVR query nor init may be called.
    ControllerModelConnection models;
    int queries=0;
    const auto absent=[&] { ++queries; return false; };
    for(int frame=0;frame<10000;++frame) models.Connect(false,absent);
    Check(queries==0,"VDXR never accesses an OpenVR model client");
    for(int frame=0;frame<10000;++frame) models.Connect(true,absent);
    Check(queries==1,"an unavailable model client is not queried per frame");

    ControllerModelConnection shared;
    queries=0;
    for(int frame=0;frame<10000;++frame)
        Check(shared.Connect(true,[&] { ++queries; return true; }),"an existing client is reused");
    Check(queries==1,"model loading connects to an existing scene client only once");

    Check(kOpenXRControllerProfiles.size() >= 12,
          "the compatibility table covers more than the original three profiles");
    Check(HasProfile("/interaction_profiles/oculus/touch_controller"),
          "legacy Oculus Touch remains supported");
    Check(HasProfile("/interaction_profiles/valve/index_controller"),
          "Valve Index is supported");
    Check(HasProfile("/interaction_profiles/microsoft/motion_controller"),
          "Windows Mixed Reality is supported");
    Check(HasProfile("/interaction_profiles/htc/vive_controller"),
          "HTC Vive is supported");
    Check(HasProfile("/interaction_profiles/meta/touch_pro_controller"),
          "Meta Touch Pro is supported");
    Check(HasProfile("/interaction_profiles/bytedance/pico4_controller"),
          "PICO 4 is supported");
    Check(kTouchControllerActions.item[0] == "/user/hand/left/input/y/click",
          "Quest Y uses the current item");
    Check(kTouchControllerActions.pause[1] == "/user/hand/left/input/trigger/value",
          "Quest left trigger brakes and reverses");
    Check(kTouchControllerActions.trick == "/user/hand/left/input/x/click",
          "Quest X remains available for tricks");

    for (const auto& profile : kOpenXRControllerProfiles) {
        Check(profile.interaction_profile.starts_with("/interaction_profiles/"),
              "profile paths use the OpenXR interaction-profile namespace");
        Check(!profile.left_grip_pose.empty(), "every profile provides the left hand HUD pose");
        Check(ActionCount(profile.actions) != 0, "every profile exposes at least one game action");
    }

    if (!failures) std::cout << "OpenXR controller profile checks passed\n";
    return failures ? 1 : 0;
}
