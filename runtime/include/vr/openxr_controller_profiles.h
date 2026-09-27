// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace mkw::vr {

// These action slots are deliberately data-only. OpenXR action creation and
// path conversion stay in openxr_runtime.cpp, while this table remains easy
// to review and test without an OpenXR loader or headset.
enum class OpenXRControllerAction : size_t {
    Steering,
    Tricks,
    Accelerate,
    Item,
    Drift,
    DriftClick,
    Confirm,
    Brake,
    Trick,
    LookBack,
    Pause,
    Reverse,
};

inline constexpr size_t kOpenXRControllerActionCount = 12;

struct OpenXRControllerActionPaths {
    std::string_view steering{};
    std::string_view tricks{};
    std::string_view accelerate{};
    std::array<std::string_view, 2> item{};
    std::string_view drift{};
    std::string_view drift_click{};
    std::string_view confirm{};
    std::string_view brake{};
    std::string_view trick{};
    std::string_view look_back{};
    std::array<std::string_view, 2> pause{};
};

struct OpenXRControllerProfileBindings {
    std::string_view interaction_profile;
    std::string_view camera_click;
    std::string_view left_grip_pose;
    OpenXRControllerActionPaths actions;
};

// Quest Touch, PICO, and the Meta legacy profiles share the same physical
// layout. The table keeps the bindings explicit per profile so a runtime can
// accept only the paths that its active interaction profile actually exposes.
inline constexpr OpenXRControllerActionPaths kTouchControllerActions{
    "/user/hand/left/input/thumbstick",
    "/user/hand/right/input/thumbstick",
    "/user/hand/right/input/trigger/value",
    {"/user/hand/left/input/y/click", {}},
    "/user/hand/right/input/squeeze/value",
    {},
    "/user/hand/right/input/a/click",
    "/user/hand/right/input/b/click",
    "/user/hand/left/input/x/click",
    {},
    {"/user/hand/left/input/menu/click", "/user/hand/left/input/trigger/value"},
};

inline constexpr OpenXRControllerActionPaths kIndexControllerActions{
    "/user/hand/left/input/thumbstick",
    "/user/hand/right/input/thumbstick",
    "/user/hand/right/input/trigger/value",
    {"/user/hand/left/input/b/click", {}},
    "/user/hand/right/input/squeeze/value",
    {},
    "/user/hand/right/input/a/click",
    "/user/hand/right/input/b/click",
    "/user/hand/left/input/a/click",
    {},
    {"/user/hand/left/input/system/click", "/user/hand/left/input/trigger/value"},
};

inline constexpr OpenXRControllerActionPaths kMotionControllerActions{
    "/user/hand/left/input/thumbstick",
    "/user/hand/right/input/thumbstick",
    "/user/hand/right/input/trigger/value",
    {"/user/hand/left/input/squeeze/click", {}},
    {},
    "/user/hand/right/input/squeeze/click",
    "/user/hand/right/input/thumbstick/click",
    "/user/hand/right/input/menu/click",
    {},
    "/user/hand/right/input/thumbstick/click",
    {"/user/hand/left/input/menu/click", "/user/hand/left/input/trigger/value"},
};

inline constexpr OpenXRControllerActionPaths kViveControllerActions{
    "/user/hand/left/input/trackpad",
    "/user/hand/right/input/trackpad",
    "/user/hand/right/input/trigger/value",
    {"/user/hand/left/input/squeeze/click", {}},
    {},
    "/user/hand/right/input/squeeze/click",
    "/user/hand/right/input/trigger/click",
    "/user/hand/right/input/menu/click",
    {},
    "/user/hand/right/input/trackpad/click",
    {"/user/hand/left/input/menu/click", "/user/hand/left/input/trigger/value"},
};

// A simple controller has no analog controls, but still supports menu
// navigation and confirmation on runtimes that expose this fallback profile.
inline constexpr OpenXRControllerActionPaths kSimpleControllerActions{
    {},
    {},
    {},
    {"/user/hand/left/input/select/click", {}},
    {},
    {},
    "/user/hand/right/input/select/click",
    "/user/hand/right/input/menu/click",
    {},
    "/user/hand/left/input/menu/click",
    {"/user/hand/left/input/menu/click", {}},
};

inline constexpr OpenXRControllerActionPaths kPicoG3ControllerActions{
    "/user/hand/left/input/thumbstick",
    "/user/hand/right/input/thumbstick",
    "/user/hand/right/input/trigger/value",
    {"/user/hand/left/input/y/click", {}},
    "/user/hand/right/input/squeeze/value",
    {},
    "/user/hand/right/input/trigger/click",
    "/user/hand/right/input/menu/click",
    "/user/hand/left/input/x/click",
    "/user/hand/right/input/thumbstick/click",
    {"/user/hand/left/input/menu/click", "/user/hand/left/input/trigger/value"},
};

inline constexpr std::array kOpenXRControllerProfiles{
    OpenXRControllerProfileBindings{
        "/interaction_profiles/khr/simple_controller",
        "/user/hand/right/input/select/click",
        "/user/hand/left/input/grip/pose",
        kSimpleControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/htc/vive_controller",
        {},
        "/user/hand/left/input/grip/pose",
        kViveControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/microsoft/motion_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kMotionControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/samsung/odyssey_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kMotionControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/oculus/touch_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/valve/index_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kIndexControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/meta/touch_controller_plus",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/meta/touch_plus_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/meta/touch_pro_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/meta/touch_controller_rift_cv1",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/meta/touch_controller_quest_1_rift_s",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/meta/touch_controller_quest_2",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/bytedance/pico_neo3_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/bytedance/pico4_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/bytedance/pico_g3_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kPicoG3ControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/bytedance/pico_ultra_controller_bd",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/htc/vive_cosmos_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
    OpenXRControllerProfileBindings{
        "/interaction_profiles/htc/vive_focus3_controller",
        "/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/grip/pose",
        kTouchControllerActions,
    },
};

} // namespace mkw::vr
