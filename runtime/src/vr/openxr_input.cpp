// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(MKW_ENABLE_OPENXR)

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "vr/openxr_input.h"
#include "vr/openxr_controller_profiles.h"
#include "vr/mkw_vr_policy.h"
#include "physical_wheel.h"
#include "runtime_config.h"
#include "settings_overlay.h"
#include "vr/mkw_vr_first_person.h"
#include "vr/openxr_diagnostics.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#include <time.h>
#endif

namespace mkw::vr {
namespace {

// SDL holds its joystick lock for as long as an enumeration takes, and the
// Bluetooth Wii Remote rescan (F10 > Controller settings > Keep scanning)
// closes and reopens every HID device twice per scan: 15 ms on a plain desk,
// over 200 ms on a machine carrying several HID devices, such as a Lighthouse
// setup's base-station dongles. The pacing thread must never wait on that,
// because the OpenXR frame it holds open costs the compositor every display
// slot that passes. So it leaves the gamepad here, and the game thread writes
// it to SDL where it already polls controllers.
//
// m_sdl is the lock the SDL work runs under; the pacing thread only ever takes
// m_state, and only for the copy. Both are taken in that order.
class VirtualGamepadRelay {
public:
    struct Pad {
        std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};
        std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    };

    void Attach(SDL_Joystick* joystick) {
        std::scoped_lock lock(m_sdl, m_state);
        m_joystick = joystick;
        m_pending = false;
    }

    void Detach() {
        std::scoped_lock lock(m_sdl, m_state);
        m_joystick = nullptr;
        m_pending = false;
    }

    // Pacing thread.
    void Publish(const Pad& pad) {
        std::lock_guard lock(m_state);
        m_pad = pad;
        m_pending = true;
    }

    // Game thread. Holding m_sdl here is what keeps Detach from closing the
    // joystick underneath the writes.
    void Apply() {
        std::lock_guard sdl(m_sdl);
        Pad pad;
        SDL_Joystick* joystick = nullptr;
        {
            std::lock_guard lock(m_state);
            if (!m_pending || m_joystick == nullptr) {
                return;
            }
            pad = m_pad;
            joystick = m_joystick;
            m_pending = false;
        }
        for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis) {
            SDL_SetJoystickVirtualAxis(joystick, static_cast<SDL_GamepadAxis>(axis),
                                       pad.axes[static_cast<size_t>(axis)]);
        }
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
            SDL_SetJoystickVirtualButton(joystick, static_cast<SDL_GamepadButton>(button),
                                         pad.buttons[static_cast<size_t>(button)]);
        }
    }

private:
    std::mutex m_sdl;
    std::mutex m_state;
    SDL_Joystick* m_joystick = nullptr;
    Pad m_pad;
    bool m_pending = false;
};

VirtualGamepadRelay& Relay() {
    static VirtualGamepadRelay relay;
    return relay;
}

#if defined(__ANDROID__)
// Debug-only remote button presses for headset experiments driven over adb, so
// a menu can be reached without someone wearing the headset:
//   adb shell setprop debug.wiicompiled.inject <sequence>:<button>
// A new sequence number holds the button for kInjectHoldFrames XR frames.
// Buttons: a, b, x, y, start, up, down, left, right, and for the Wii Remote
// presentation also home, c and z (x/y/start press 1/2/+ there, and the
// directions push the Nunchuk stick). `panel` presses the settings panel's
// button (left Y, or both thumbsticks for a gamepad), opening or closing it,
// where `a` then selects. The property is unset in normal use, so this costs
// one property read every few frames.
constexpr uint32_t kInjectHoldFrames = 12;
constexpr uint32_t kInjectPollFrames = 4;

struct InjectedPress {
    long sequence = -1;
    std::string button;
    uint32_t frames_left = 0;
    uint32_t poll_countdown = 0;
};

InjectedPress& Injection() {
    static InjectedPress press;
    return press;
}

void PollInjection() {
    InjectedPress& press = Injection();
    if (press.frames_left > 0) {
        --press.frames_left;
    }
    if (press.poll_countdown > 0) {
        --press.poll_countdown;
        return;
    }
    press.poll_countdown = kInjectPollFrames;
    char value[PROP_VALUE_MAX]{};
    if (__system_property_get("debug.wiicompiled.inject", value) <= 0) {
        return;
    }
    char* end = nullptr;
    const long sequence = std::strtol(value, &end, 10);
    if (end == value || *end != ':' || sequence == press.sequence) {
        return;
    }
    const bool first_read = press.sequence < 0;
    press.sequence = sequence;
    if (first_read) {
        return; // A value left over from an earlier run is not a new press.
    }
    press.button = end + 1;
    press.frames_left = kInjectHoldFrames;
}

bool Injected(const char* button) {
    const InjectedPress& press = Injection();
    return press.frames_left > 0 && press.button == button;
}

using ConvertNowToXrTime = XrResult(XRAPI_PTR*)(XrInstance, const struct timespec*, XrTime*);
#else
void PollInjection() {}
bool Injected(const char*) { return false; }

#if defined(_WIN32)
using ConvertNowToXrTime = XrResult(XRAPI_PTR*)(XrInstance, const LARGE_INTEGER*, XrTime*);
#endif
#endif

constexpr uint32_t kHandCount = 2;

// Re-sent every frame while the game holds the motor on, so a rumble whose stop
// never arrives (or a stalled pacing thread) dies out on its own.
constexpr XrDuration kRumblePulseNs = 50'000'000;

struct Binding {
    XrAction* action;
    const char* path;
};

Sint16 ToAxis(float value) noexcept {
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    return static_cast<Sint16>(std::lround(clamped * 32767.0f));
}

// Trigger axes are reported by SDL gamepads on the positive half only.
Sint16 ToTrigger(float value) noexcept {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<Sint16>(std::lround(clamped * 32767.0f));
}

wii_remote::Pose ToWiiRemotePose(const XrPosef& pose) noexcept {
    return {{pose.position.x, pose.position.y, pose.position.z},
            {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w}};
}

// The adb injection's buttons as the Wii Remote presentation's WPAD bits.
uint32_t InjectedWiiRemoteButtons() {
    uint32_t hold = 0;
    const auto press = [&hold](const char* name, uint32_t bit) {
        if (Injected(name)) {
            hold |= bit;
        }
    };
    press("a", wii_remote::kButtonA);
    press("b", wii_remote::kButtonB);
    press("x", wii_remote::kButtonOne);
    press("y", wii_remote::kButtonTwo);
    press("start", wii_remote::kButtonPlus);
    press("home", wii_remote::kButtonHome);
    press("c", wii_remote::kButtonC);
    press("z", wii_remote::kButtonZ);
    return hold;
}

} // namespace

OpenXRInput::OpenXRInput(OpenXRLogCallback logger) : m_logger(std::move(logger)) {}

OpenXRInput::~OpenXRInput() {
    Destroy();
}

bool OpenXRInput::Create(OpenXRRuntime& runtime) {
    m_last_error.clear();
    if (m_created) {
        return true;
    }
    if (!runtime.IsInitialized() || !runtime.HasSession()) {
        m_last_error = "OpenXR input needs an initialized runtime with a session";
        return false;
    }
    m_runtime = &runtime;

    if (!Check(xrStringToPath(runtime.Instance(), "/user/hand/left", &m_hand_paths[0]),
               "xrStringToPath(/user/hand/left)") ||
        !Check(xrStringToPath(runtime.Instance(), "/user/hand/right", &m_hand_paths[1]),
               "xrStringToPath(/user/hand/right)")) {
        Destroy();
        return false;
    }
    if (!CreateActions() || !SuggestBindings()) {
        Destroy();
        return false;
    }

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &m_action_set;
    if (!Check(xrAttachSessionActionSets(runtime.Session(), &attach), "xrAttachSessionActionSets")) {
        Destroy();
        return false;
    }
    m_created = true;
    CreatePoseSpaces();
    LoadInputClock();
    if (!AttachVirtualGamepad()) {
        Log(OpenXRLogLevel::Warning,
            "SDL refused the virtual gamepad; OpenXR controllers will not reach the game");
    }
    Log(OpenXRLogLevel::Info, OpenXRGetControllerMode() == OpenXRControllerMode::WiiRemote
                                  ? "OpenXR controller actions attached (Wii Remote + Nunchuk)"
                                  : "OpenXR controller actions attached (gamepad)");
    return true;
}

bool OpenXRInput::CreateActions() {
    XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(set_info.actionSetName, "mkw_gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(set_info.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    set_info.priority = 0;
    if (!Check(xrCreateActionSet(m_runtime->Instance(), &set_info, &m_action_set), "xrCreateActionSet")) {
        return false;
    }

    struct Spec {
        XrAction* action;
        const char* name;
        const char* localized;
        XrActionType type;
    };
    const std::array<Spec, 10> specs{{
        {&m_thumbstick, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT},
        {&m_thumbstick_click, "thumbstick_click", "Thumbstick Click", XR_ACTION_TYPE_BOOLEAN_INPUT},
        {&m_trigger, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT},
        {&m_squeeze, "squeeze", "Grip", XR_ACTION_TYPE_FLOAT_INPUT},
        {&m_button_primary, "button_primary", "A / X", XR_ACTION_TYPE_BOOLEAN_INPUT},
        {&m_button_secondary, "button_secondary", "B / Y", XR_ACTION_TYPE_BOOLEAN_INPUT},
        {&m_menu, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT},
        {&m_aim_pose, "aim_pose", "Pointer", XR_ACTION_TYPE_POSE_INPUT},
        {&m_grip_pose, "grip_pose", "Motion", XR_ACTION_TYPE_POSE_INPUT},
        {&m_haptic, "haptic", "Haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT},
    }};
    for (const Spec& spec : specs) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType = spec.type;
        std::strncpy(info.actionName, spec.name, XR_MAX_ACTION_NAME_SIZE - 1);
        std::strncpy(info.localizedActionName, spec.localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        info.countSubactionPaths = kHandCount;
        info.subactionPaths = m_hand_paths;
        if (!Check(xrCreateAction(m_action_set, &info, spec.action), spec.name)) {
            return false;
        }
    }
    // Separate left-hand actions avoid old SteamVR game bindings which may
    // have mapped the shared A/X or B/Y action only to the right controller.
    for (const Spec& spec : std::array<Spec,2>{{
        {&m_options_x,"vr_options_x","VR options X",XR_ACTION_TYPE_BOOLEAN_INPUT},
        {&m_options_y,"vr_options_y","VR options Y",XR_ACTION_TYPE_BOOLEAN_INPUT}}}) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType=spec.type;
        std::strncpy(info.actionName,spec.name,XR_MAX_ACTION_NAME_SIZE-1);
        std::strncpy(info.localizedActionName,spec.localized,XR_MAX_LOCALIZED_ACTION_NAME_SIZE-1);
        info.countSubactionPaths=1;info.subactionPaths=m_hand_paths;
        if(!Check(xrCreateAction(m_action_set,&info,spec.action),spec.name)) return false;
    }
    return true;
}

bool OpenXRInput::SuggestBindings() {
    bool accepted=false;
    const auto suggest = [&](const char* profile, const std::vector<Binding>& bindings, bool required) {
        XrPath profile_path = XR_NULL_PATH;
        if (!Check(xrStringToPath(m_runtime->Instance(), profile, &profile_path), profile)) {
            return false;
        }
        std::vector<XrActionSuggestedBinding> suggested;
        if(std::string_view(profile)=="/interaction_profiles/htc/vive_controller") m_vive_profile=profile_path;
        suggested.reserve(bindings.size());
        for (const Binding& binding : bindings) {
            XrPath path = XR_NULL_PATH;
            if (XR_FAILED(xrStringToPath(m_runtime->Instance(), binding.path, &path))) {
                continue;
            }
            suggested.push_back({*binding.action, path});
            if(std::string_view(binding.path).find("/left/")!=std::string_view::npos) {
                if(binding.action==&m_button_primary) suggested.push_back({m_options_x,path});
                if(binding.action==&m_button_secondary) suggested.push_back({m_options_y,path});
            }
        }
        XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        info.interactionProfile = profile_path;
        info.countSuggestedBindings = static_cast<uint32_t>(suggested.size());
        info.suggestedBindings = suggested.data();
        const XrResult result = xrSuggestInteractionProfileBindings(m_runtime->Instance(), &info);
        m_runtime->ObserveResult(result);
        if (XR_FAILED(result)) {
            std::ostringstream message;
            message << "xrSuggestInteractionProfileBindings(" << profile << ") failed (" << result << ')';
            if (required) {
                m_last_error = message.str();
                Log(OpenXRLogLevel::Error, m_last_error);
                return false;
            }
            Log(OpenXRLogLevel::Warning, message.str());
            return false;
        }
        accepted=true;
        return true;
    };

    // Meta Quest Touch controllers (Quest 2 / 3 / Pro all expose this profile).
    const std::vector<Binding> touch{
        {&m_thumbstick, "/user/hand/left/input/thumbstick"},
        {&m_thumbstick, "/user/hand/right/input/thumbstick"},
        {&m_thumbstick_click, "/user/hand/left/input/thumbstick/click"},
        {&m_thumbstick_click, "/user/hand/right/input/thumbstick/click"},
        {&m_trigger, "/user/hand/left/input/trigger/value"},
        {&m_trigger, "/user/hand/right/input/trigger/value"},
        {&m_squeeze, "/user/hand/left/input/squeeze/value"},
        {&m_squeeze, "/user/hand/right/input/squeeze/value"},
        {&m_button_primary, "/user/hand/left/input/x/click"},
        {&m_button_primary, "/user/hand/right/input/a/click"},
        {&m_button_secondary, "/user/hand/left/input/y/click"},
        {&m_button_secondary, "/user/hand/right/input/b/click"},
        {&m_menu, "/user/hand/left/input/menu/click"},
        {&m_aim_pose, "/user/hand/left/input/aim/pose"},
        {&m_aim_pose, "/user/hand/right/input/aim/pose"},
        {&m_grip_pose, "/user/hand/left/input/grip/pose"},
        {&m_grip_pose, "/user/hand/right/input/grip/pose"},
        {&m_haptic, "/user/hand/left/output/haptic"},
        {&m_haptic, "/user/hand/right/output/haptic"},
    };
    suggest("/interaction_profiles/oculus/touch_controller", touch, false);
    // The port's advertised controller families. Paths are explicit; rejected
    // optional profiles do not prevent another runtime's bindings from loading.
    for (const auto& profile : kOpenXRControllerProfiles) {
        const auto id=profile.interaction_profile;
        if(id=="/interaction_profiles/oculus/touch_controller" || id=="/interaction_profiles/khr/simple_controller") continue;
        auto bindings=touch;
        for(auto& binding:bindings) {
            const bool left=std::string(binding.path).find("/left/")!=std::string::npos;
            if(binding.action==&m_thumbstick) binding.path=(left?profile.actions.steering:profile.actions.tricks).data();
            if(binding.action==&m_button_primary) binding.path=(left?profile.actions.trick:profile.actions.confirm).data();
            if(binding.action==&m_button_secondary) binding.path=(left?profile.actions.item[0]:profile.actions.brake).data();
            if(binding.action==&m_thumbstick_click && !left) binding.path=profile.camera_click.data();
            if(binding.action==&m_trigger && !left) binding.path=profile.actions.accelerate.data();
            if(binding.action==&m_squeeze) {
                if(id.find("index")!=std::string_view::npos) binding.path=left?"/user/hand/left/input/squeeze/value":"/user/hand/right/input/squeeze/value";
                else if(id.find("motion")!=std::string_view::npos || id.find("odyssey")!=std::string_view::npos || id=="/interaction_profiles/htc/vive_controller")
                    binding.path=left?"/user/hand/left/input/squeeze/click":"/user/hand/right/input/squeeze/click";
            }
            if(binding.action==&m_menu) binding.path=id.find("index")!=std::string_view::npos?nullptr:profile.actions.pause[0].data();
            const bool wand=id=="/interaction_profiles/htc/vive_controller";
            const bool motion=id.find("motion")!=std::string_view::npos || id.find("odyssey")!=std::string_view::npos;
            if(wand || motion) {
                // These controllers have no X/Y/A/B cluster. Keep left/right
                // actions on their own hand and avoid unsupported Touch paths.
                if(binding.action==&m_button_primary && left) binding.path="/user/hand/left/input/menu/click";
                if(binding.action==&m_button_secondary && left) binding.path="/user/hand/left/input/trackpad/click";
                if(binding.action==&m_button_primary && !left && motion) binding.path="/user/hand/right/input/trackpad/click";
                if(binding.action==&m_thumbstick_click) binding.path=left?nullptr:(wand?"/user/hand/right/input/trackpad/click":"/user/hand/right/input/thumbstick/click");
            }
        }
        bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[](const Binding& b){return !b.path || !b.path[0];}),bindings.end());
        suggest(id.data(),bindings,false);
    }
    // Minimal fallback so an unfamiliar runtime still offers a select, a menu
    // and something to point with.
    const std::vector<Binding> simple{
        {&m_button_primary, "/user/hand/right/input/select/click"},
        {&m_button_secondary, "/user/hand/left/input/select/click"},
        {&m_menu, "/user/hand/left/input/menu/click"},
        {&m_aim_pose, "/user/hand/left/input/aim/pose"},
        {&m_aim_pose, "/user/hand/right/input/aim/pose"},
        {&m_grip_pose, "/user/hand/left/input/grip/pose"},
        {&m_grip_pose, "/user/hand/right/input/grip/pose"},
        {&m_haptic, "/user/hand/left/output/haptic"},
        {&m_haptic, "/user/hand/right/output/haptic"},
    };
    suggest("/interaction_profiles/khr/simple_controller", simple, false);
    if(!accepted) m_last_error="No supported controller interaction profile could be bound.";
    return accepted;
}

void OpenXRInput::CreatePoseSpaces() {
    bool logged = false;
    for (uint32_t hand = 0; hand < kHandCount; ++hand) {
        for (auto [action, spaces] : {std::pair{m_aim_pose, m_aim_spaces}, std::pair{m_grip_pose, m_grip_spaces}}) {
            XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            info.action = action;
            info.subactionPath = m_hand_paths[hand];
            info.poseInActionSpace.orientation.w = 1.0f;
            const XrResult result = xrCreateActionSpace(m_runtime->Session(), &info, &spaces[hand]);
            m_runtime->ObserveResult(result);
            if (XR_FAILED(result)) {
                spaces[hand] = XR_NULL_HANDLE;
                if (!logged) {
                    logged = true;
                    std::ostringstream message;
                    message << "xrCreateActionSpace failed (" << result
                            << "); the Wii Remote will have no motion or pointer";
                    Log(OpenXRLogLevel::Warning, message.str());
                }
            }
        }
    }
}

void OpenXRInput::DestroyPoseSpaces() {
    for (uint32_t hand = 0; hand < kHandCount; ++hand) {
        for (XrSpace* space : {&m_aim_spaces[hand], &m_grip_spaces[hand]}) {
            if (*space != XR_NULL_HANDLE) {
                xrDestroySpace(*space);
                *space = XR_NULL_HANDLE;
            }
        }
    }
}

// Poses for input are located at the measured current time, not the frame's
// predicted display time: that lies tens of milliseconds ahead, and the runtime
// extrapolates a fast wrist turn that far past where the hand really is, which
// sprays the pointer and invents acceleration (DolphinXR's fast-motion fix).
void OpenXRInput::LoadInputClock() {
    const auto& extensions = m_runtime->EnabledExtensions();
    const auto enabled = [&](const char* name) {
        return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
    };
    PFN_xrVoidFunction function = nullptr;
#if defined(_WIN32)
    if (enabled("XR_KHR_win32_convert_performance_counter_time")) {
        m_runtime->GetInstanceProcAddress("xrConvertWin32PerformanceCounterToTimeKHR", &function);
    }
#elif defined(__ANDROID__)
    if (enabled("XR_KHR_convert_timespec_time")) {
        m_runtime->GetInstanceProcAddress("xrConvertTimespecTimeToTimeKHR", &function);
    }
#else
    (void)enabled;
#endif
    m_convert_now_to_xr_time = function;
    if (m_convert_now_to_xr_time == nullptr) {
        Log(OpenXRLogLevel::Info,
            "OpenXR offers no clock conversion; controller motion is sampled at display time");
    }
}

XrTime OpenXRInput::InputSampleTime(XrTime predicted_display_time) const {
    if (m_convert_now_to_xr_time == nullptr) {
        return predicted_display_time;
    }
    XrTime now = 0;
#if defined(_WIN32)
    LARGE_INTEGER counter{};
    if (QueryPerformanceCounter(&counter) == 0 ||
        XR_FAILED(reinterpret_cast<ConvertNowToXrTime>(m_convert_now_to_xr_time)(m_runtime->Instance(),
                                                                                  &counter, &now))) {
        return predicted_display_time;
    }
#elif defined(__ANDROID__)
    timespec spec{};
    if (clock_gettime(CLOCK_MONOTONIC, &spec) != 0 ||
        XR_FAILED(reinterpret_cast<ConvertNowToXrTime>(m_convert_now_to_xr_time)(m_runtime->Instance(),
                                                                                  &spec, &now))) {
        return predicted_display_time;
    }
#endif
    return now > 0 ? (std::min)(predicted_display_time, now) : predicted_display_time;
}

void OpenXRApplyVirtualGamepad() noexcept {
    Relay().Apply();
}

bool OpenXRInput::AttachVirtualGamepad() {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) |
                       (1u << SDL_GAMEPAD_BUTTON_WEST) | (1u << SDL_GAMEPAD_BUTTON_NORTH) |
                       (1u << SDL_GAMEPAD_BUTTON_START) | (1u << SDL_GAMEPAD_BUTTON_LEFT_STICK) |
                       (1u << SDL_GAMEPAD_BUTTON_RIGHT_STICK) |
                       (1u << SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) |
                       (1u << SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY) |
                     (1u << SDL_GAMEPAD_AXIS_RIGHTX) | (1u << SDL_GAMEPAD_AXIS_RIGHTY) |
                     (1u << SDL_GAMEPAD_AXIS_LEFT_TRIGGER) | (1u << SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    desc.name = "OpenXR Touch Controllers";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        m_last_error = std::string("SDL_AttachVirtualJoystick failed: ") + SDL_GetError();
        Log(OpenXRLogLevel::Warning, m_last_error);
        return false;
    }
    SDL_Joystick* joystick = SDL_OpenJoystick(id);
    if (joystick == nullptr) {
        m_last_error = std::string("SDL_OpenJoystick failed: ") + SDL_GetError();
        Log(OpenXRLogLevel::Warning, m_last_error);
        SDL_DetachVirtualJoystick(id);
        return false;
    }
    m_joystick_id = id;
    m_joystick = joystick;
    Relay().Attach(joystick);
    return true;
}

void OpenXRInput::DetachVirtualGamepad() {
    // Before the handle goes: the game thread may be writing through it.
    Relay().Detach();
    if (m_joystick != nullptr) {
        SDL_CloseJoystick(static_cast<SDL_Joystick*>(m_joystick));
        m_joystick = nullptr;
    }
    if (m_joystick_id != 0) {
        SDL_DetachVirtualJoystick(m_joystick_id);
        m_joystick_id = 0;
    }
}

void OpenXRInput::Destroy() {
    // The game must stop reading a remote whose controllers are going away.
    OpenXRWithdrawWiiRemote();
    ResetDriving();
    if (m_created) {
        StopRumble();
    }
    DetachVirtualGamepad();
    DestroyPoseSpaces();
    if (m_action_set != XR_NULL_HANDLE) {
        // Destroying the set destroys every action created from it.
        xrDestroyActionSet(m_action_set);
        m_action_set = XR_NULL_HANDLE;
    }
    m_thumbstick = m_thumbstick_click = m_trigger = m_squeeze = XR_NULL_HANDLE;
    m_button_primary = m_button_secondary = m_menu = m_haptic = XR_NULL_HANDLE;
    m_options_x = m_options_y = XR_NULL_HANDLE;
    m_options_previous=0;
    m_aim_pose = m_grip_pose = XR_NULL_HANDLE;
    m_hand_paths[0] = m_hand_paths[1] = XR_NULL_PATH;
    m_convert_now_to_xr_time = nullptr;
    for (auto& motion : m_motion) {
        motion.Rest();
    }
    m_pointer.Reset();
    m_horizon = {1.0f, 0.0f};
    OpenXRPublishUiSnapshot({});
    OpenXRPublishPortControls({});
    m_panel_controls.Reset();
    m_last_input_time = 0;
    m_panel_select_held = false;
    m_created = false;
    m_runtime = nullptr;
}

void OpenXRInput::Idle() {
    if (!m_created) {
        return;
    }
    for (auto& motion : m_motion) {
        motion.Rest();
    }
    m_pointer.Reset();
    m_horizon = {1.0f, 0.0f};
    OpenXRPublishPortControls({});
    OpenXRPublishWiiRemote(m_joystick_id, OpenXRWiiRemoteSample{});
    // The panel stays as it was; only what the controllers were holding is forgotten.
    m_panel_controls.Reset();
    m_last_input_time = 0;
    m_panel_select_held = false;
    OpenXRPublishSettingsPanelPointer(false, 0.0f, 0.0f, false, 0.0f);
    m_first_person_click.Reset();
    ResetDriving();
    StopRumble();
    // Nothing stays held on the gamepad either while input is away.
    if (m_joystick != nullptr) {
        Relay().Publish({});
    }
}

void OpenXRInput::Sync(XrTime predicted_display_time, const OpenXRPointerScreen& screen,
                       const OpenXRPointerScreen& settings_panel, const driving::SeatFrame& seat) {
    if (!m_created || m_runtime == nullptr) {
        return;
    }
    if (!m_runtime->IsSessionFocused()) {
        Idle();
        return;
    }
    XrActiveActionSet active{m_action_set, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    const XrResult result = diagnostics::Measure(diagnostics::Stage::SyncActions, [&] {
        return xrSyncActions(m_runtime->Session(), &sync);
    });
    m_runtime->ObserveResult(result);
    if (XR_FAILED(result)) {
        if (!m_logged_sync_failure) {
            m_logged_sync_failure = true;
            std::ostringstream message;
            message << "xrSyncActions failed (" << result << ')';
            Log(OpenXRLogLevel::Warning, message.str());
        }
        Idle();
        return;
    }

    const auto boolean = [&](XrAction action, uint32_t hand) {
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = m_hand_paths[hand];
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        return XR_SUCCEEDED(xrGetActionStateBoolean(m_runtime->Session(), &info, &state)) &&
               state.isActive == XR_TRUE && state.currentState == XR_TRUE;
    };
    const auto scalar = [&](XrAction action, uint32_t hand) {
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = m_hand_paths[hand];
        XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_FAILED(xrGetActionStateFloat(m_runtime->Session(), &info, &state)) ||
            state.isActive != XR_TRUE) {
            return 0.0f;
        }
        return state.currentState;
    };
    const auto vector = [&](XrAction action, uint32_t hand) {
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = m_hand_paths[hand];
        XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_FAILED(xrGetActionStateVector2f(m_runtime->Session(), &info, &state)) ||
            state.isActive != XR_TRUE) {
            return XrVector2f{0.0f, 0.0f};
        }
        return state.currentState;
    };

    std::array<wii_remote::HandInputs, kHands> hands{};
    for (uint32_t hand = 0; hand < kHands; ++hand) {
        wii_remote::HandInputs& inputs = hands[hand];
        inputs.primary = boolean(m_button_primary, hand);
        inputs.secondary = boolean(m_button_secondary, hand);
        inputs.menu = boolean(m_menu, hand);
        inputs.thumbstick_click = boolean(m_thumbstick_click, hand);
        inputs.trigger = scalar(m_trigger, hand);
        inputs.squeeze = scalar(m_squeeze, hand);
        const XrVector2f stick = vector(m_thumbstick, hand);
        inputs.stick_x = stick.x;
        inputs.stick_y = stick.y;
    }

    OpenXRUiSnapshot ui{}; ui.active=true; ui.buttons=hands;
    const auto locate=[&](XrSpace space, UiHandPose& out) {
        if (space==XR_NULL_HANDLE) return;
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        const auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (XR_FAILED(xrLocateSpace(space,m_runtime->AppSpace(),predicted_display_time,&location)) ||
            (location.locationFlags&valid)!=valid) return;
        const auto& q=location.pose.orientation; const auto& p=location.pose.position;
        out.valid=true; out.position={p.x,p.y,p.z};
        out.right=RotateUiVector(q.x,q.y,q.z,q.w,{1,0,0});
        out.up=RotateUiVector(q.x,q.y,q.z,q.w,{0,1,0});
        out.forward=RotateUiVector(q.x,q.y,q.z,q.w,{0,0,-1});
    };
    for(int hand=0;hand<2;++hand) { locate(m_grip_spaces[hand],ui.hands[hand]); locate(m_aim_spaces[hand],ui.aims[hand]); }
    OpenXRPublishUiSnapshot(ui);

    PollInjection();
    if (Injected("up")) {
        hands[0].stick_y = 1.0f;
    } else if (Injected("down")) {
        hands[0].stick_y = -1.0f;
    } else if (Injected("left")) {
        hands[0].stick_x = -1.0f;
    } else if (Injected("right")) {
        hands[0].stick_x = 1.0f;
    }

    const XrTime input_time = InputSampleTime(predicted_display_time);
    const float dt_seconds =
        m_last_input_time != 0 && input_time > m_last_input_time
            ? static_cast<float>(input_time - m_last_input_time) * 1.0e-9f
            : 0.0f;
    m_last_input_time = input_time;
    std::array<wii_remote::HandInputs, kHands> panel_hands = hands;
    panel_hands[0].primary |= boolean(m_options_x,0);
    panel_hands[0].secondary |= boolean(m_options_y,0);
    const auto physical=m_runtime->RuntimeInfo().runtime_name.find("SteamVR")!=std::string::npos ?
        OpenXRReadPhysicalOptionsButtons() : PhysicalOptionsButtons{};
    if(physical.valid) {
        panel_hands[0].primary |= physical.x;
        panel_hands[0].secondary |= physical.y;
    }
    if(physical.valid!=m_native_options_valid) {
        m_native_options_valid=physical.valid;
        Log(OpenXRLogLevel::Info,physical.valid ? "VR options: native SteamVR face buttons available" :
            "VR options: native SteamVR face buttons unavailable; using OpenXR actions");
    }
    const uint8_t options=(panel_hands[0].primary?1:0)|(panel_hands[0].secondary?2:0);
    if(options!=m_options_previous) {
        m_options_previous=options;
        Log(OpenXRLogLevel::Info,"VR options left buttons: X="+
            std::to_string(bool(options&1))+", Y="+std::to_string(bool(options&2))+
            ", native="+std::to_string(physical.valid)+", native-X="+std::to_string(physical.x)+
            ", native-Y="+std::to_string(physical.y));
    }
    if (Injected("panel")) {
        // The panel button in either controller mode.
        panel_hands[0].primary = true;
        panel_hands[0].secondary = true;
        panel_hands[0].thumbstick_click = true;
        panel_hands[1].thumbstick_click = true;
    }
    if (Injected("a")) {
        panel_hands[1].primary = true;
    }
    // The game thread may open or close the panel too; only a change made here
    // is written back.
    const bool was_open = OpenXRSettingsPanelOpen() || OpenXRIntroductionActive();
    bool open = was_open;
    settings_panel::Frame panel =
        m_panel_controls.Update(panel_hands, open, dt_seconds, OpenXRGetControllerMode());
    if (OpenXRIntroductionActive()) { panel.open=true; panel.withheld=true; }
    // Pointer first: the game thread reads it as soon as it sees the panel open.
    PublishSettingsPanel(input_time, settings_panel, panel);
    if (OpenXRIntroductionActive()) open = true;
    if (open != was_open) {
        Log(OpenXRLogLevel::Info, open ? "VR settings opened with controller chord" : "VR settings closed with controller chord");
        OpenXRSetSettingsPanelOpen(open);
    }

    // A clean right-thumbstick click toggles the first-person camera. It fires
    // on release, so the two-thumbstick panel chord never toggles it, and
    // never while the panel has the controllers.
    if (m_first_person_click.Update(hands[1].thumbstick_click, hands[0].thumbstick_click,
                                    panel.open || panel.withheld) &&
        RuntimeConfigFile::VrFirstPersonToggleClick()) {
        settings_overlay::RequestFirstPersonToggle();
        constexpr XrDuration kToggleTickNs = 20'000'000;
        ApplyHaptic(1, 0.35f, kToggleTickNs);
    }

    // The cockpit's wheel before the game reads the controllers: a held wheel
    // steers through the left stick and keeps its grips from the game.
    UpdateDriving(predicted_display_time, seat, hands, panel.withheld);

    QuestInput port{};
    port.active=true;
    if (screen.valid) {
        const auto& q=screen.pose.orientation;
        const auto local=[&](const UiHandPose& aim) {
            UiHandPose ray=aim;
            const auto& p=screen.pose.position;
            ray.position=RotateUiVector(-q.x,-q.y,-q.z,q.w,
                {aim.position[0]-p.x,aim.position[1]-p.y,aim.position[2]-p.z});
            ray.position[2]-=RuntimeConfigFile::VrHudDistanceMeters();
            ray.forward=RotateUiVector(-q.x,-q.y,-q.z,q.w,aim.forward);
            return ray;
        };
        port.ui_pointer=local(ui.aims[1]);port.ui_left_pointer=local(ui.aims[0]);
        port.ui_aspect=screen.half_height_meters>0 ? screen.half_width_meters/screen.half_height_meters : 16.f/9.f;
    }
    port.steamvr=m_runtime->RuntimeInfo().runtime_name.find("SteamVR")!=std::string::npos;
    port.cockpit_controls=MkwVRGetCameraMode()==CameraMode::FirstPerson && MkwVRPolicyGetSnapshot().scene.mode==VRSceneMode::Race;
    port.raw_steering_x=ui.buttons[0].stick_x;port.raw_steering_y=ui.buttons[0].stick_y;
    port.steering_x=hands[0].stick_x;port.steering_y=hands[0].stick_y;
    port.tricks_x=hands[1].stick_x;port.tricks_y=hands[1].stick_y;
    port.accelerate=hands[1].trigger;port.reverse=hands[0].trigger>.5f;
    port.confirm=hands[1].primary;port.brake=hands[1].secondary;
    XrInteractionProfileState rightProfile{XR_TYPE_INTERACTION_PROFILE_STATE};
    if(port.cockpit_controls && m_vive_profile!=XR_NULL_PATH &&
       XR_SUCCEEDED(xrGetCurrentInteractionProfile(m_runtime->Session(),m_hand_paths[1],&rightProfile)) &&
       rightProfile.interactionProfile==m_vive_profile) {
        // Vive wands have no face-button cluster. Right Menu drifts in the
        // cockpit; the left trigger remains the brake/reverse control.
        port.confirm=hands[1].secondary;port.brake=false;
    }
    port.item=hands[0].secondary?1.f:0.f;port.trick=hands[0].primary;
    port.drift=ui.buttons[1].squeeze;
    port.wheel_active=m_driving.held[0]||m_driving.held[1];
    port.wheel_steering=m_driving.steering_input;
    const auto pause=m_trick_pause.Update(port.active && !panel.withheld,port.trick,
                                         hands[0].secondary,predicted_display_time);
    if(port.steamvr) {
        port.trick=pause.trick;
        if(pause.pause && !panel.withheld) OpenXRRequestTutorialPause();
    } else port.pause=hands[0].menu;
    if(panel.withheld) {
        const auto rawX=port.raw_steering_x,rawY=port.raw_steering_y;
        port=QuestInput{.active=true};port.raw_steering_x=rawX;port.raw_steering_y=rawY;
    }
    OpenXRPublishPortControls(port);
    // While the panel has the controllers, the game sees them idle.
    static const std::array<wii_remote::HandInputs, kHands> kIdleHands{};
    const auto& game_hands = panel.withheld ? kIdleHands : hands;
    const wii_remote::HandInputs& left = game_hands[0];
    const wii_remote::HandInputs& right = game_hands[1];
    const auto injected = [&panel](const char* button) { return !panel.withheld && Injected(button); };

    if (m_joystick != nullptr) {
        VirtualGamepadRelay::Pad pad;
        // OpenXR thumbsticks report +Y up; SDL gamepads report +Y down.
        pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = ToAxis(left.stick_x);
        pad.axes[SDL_GAMEPAD_AXIS_LEFTY] = ToAxis(-left.stick_y);
        pad.axes[SDL_GAMEPAD_AXIS_RIGHTX] = ToAxis(right.stick_x);
        pad.axes[SDL_GAMEPAD_AXIS_RIGHTY] = ToAxis(-right.stick_y);
        pad.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = ToTrigger(left.trigger);
        pad.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = ToTrigger(right.trigger);

        pad.buttons[SDL_GAMEPAD_BUTTON_SOUTH] = right.primary || injected("a");
        pad.buttons[SDL_GAMEPAD_BUTTON_EAST] = right.secondary || injected("b");
        pad.buttons[SDL_GAMEPAD_BUTTON_WEST] = left.primary || injected("x");
        pad.buttons[SDL_GAMEPAD_BUTTON_NORTH] = left.secondary || injected("y");
        pad.buttons[SDL_GAMEPAD_BUTTON_START] = left.menu || injected("start");
        pad.buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK] = left.thumbstick_click;
        pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_STICK] = right.thumbstick_click;
        pad.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = left.squeeze > 0.5f;
        pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = right.squeeze > 0.5f;
        Relay().Publish(pad);
    }

    PublishWiiRemote(input_time, screen, game_hands, panel.withheld ? 0u : InjectedWiiRemoteButtons(),
                     panel.withheld);
    UpdateRumble();
}

// The pointing hand's aim ray against the whole panel, in canvas pixels, with a
// short tick in that hand when a selection starts.
void OpenXRInput::PublishSettingsPanel(XrTime input_time, const OpenXRPointerScreen& panel,
                                       const settings_panel::Frame& frame) {
    if (!frame.open) {
        // Nothing may still read as held when the panel next opens.
        m_panel_select_held = false;
        OpenXRPublishSettingsPanelPointer(false, 0.0f, 0.0f, false, 0.0f);
        return;
    }
    constexpr XrSpaceLocationFlags kPoseValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    bool valid = false;
    std::array<float, 2> point{};
    const XrSpace aim_space = m_aim_spaces[frame.pointing_hand];
    if (panel.valid && aim_space != XR_NULL_HANDLE) {
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrLocateSpace(aim_space, m_runtime->AppSpace(), input_time, &location)) &&
            (location.locationFlags & kPoseValid) == kPoseValid) {
            wii_remote::Screen target{};
            target.pose = ToWiiRemotePose(panel.pose);
            target.half_width = panel.half_width_meters;
            target.half_height = panel.half_height_meters;
            const wii_remote::ScreenHit hit = wii_remote::RaycastScreen(ToWiiRemotePose(location.pose), target);
            if (hit.valid) {
                valid = true;
                point = settings_panel::CanvasPoint(hit);
            }
        }
    }
    if (frame.select && !m_panel_select_held) {
        constexpr XrDuration kTickNs = 15'000'000;
        ApplyHaptic(frame.pointing_hand, 0.35f, kTickNs);
    }
    m_panel_select_held = frame.select;
    OpenXRPublishSettingsPanelPointer(valid, point[0], point[1], frame.select, frame.wheel);
}

void OpenXRInput::PublishWiiRemote(XrTime input_time, const OpenXRPointerScreen& screen,
                                   const std::array<wii_remote::HandInputs, kHands>& hands,
                                   uint32_t injected_buttons, bool withheld) {
    constexpr XrSpaceLocationFlags kPoseValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

    OpenXRWiiRemoteSample sample{};
    sample.hold = wii_remote::RemoteButtons(hands[0], hands[1]) | injected_buttons;
    sample.stick = wii_remote::NunchukStick(hands[0]);

    // Left is the Nunchuk, right is the remote.
    std::array<wii_remote::Pose, kHands> aims{};
    std::array<bool, kHands> aim_valid{};
    for (uint32_t hand = 0; hand < kHands; ++hand) {
        if (m_aim_spaces[hand] != XR_NULL_HANDLE) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(xrLocateSpace(m_aim_spaces[hand], m_runtime->AppSpace(), input_time, &location)) &&
                (location.locationFlags & kPoseValid) == kPoseValid) {
                aims[hand] = ToWiiRemotePose(location.pose);
                aim_valid[hand] = true;
            }
        }
        wii_remote::Vec3 grip_position{};
        wii_remote::Vec3 grip_velocity{};
        bool position_valid = false;
        bool velocity_valid = false;
        if (m_grip_spaces[hand] != XR_NULL_HANDLE) {
            XrSpaceVelocity velocity{XR_TYPE_SPACE_VELOCITY};
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            location.next = &velocity;
            if (XR_SUCCEEDED(xrLocateSpace(m_grip_spaces[hand], m_runtime->AppSpace(), input_time, &location))) {
                position_valid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
                velocity_valid = (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
                grip_position = {location.pose.position.x, location.pose.position.y, location.pose.position.z};
                grip_velocity = {velocity.linearVelocity.x, velocity.linearVelocity.y, velocity.linearVelocity.z};
            }
        }
        const wii_remote::Vec3 acc =
            m_motion[hand].Update(aim_valid[hand] ? &aims[hand].orientation : nullptr,
                                  position_valid ? &grip_position : nullptr,
                                  velocity_valid ? &grip_velocity : nullptr, input_time);
        (hand == 0 ? sample.nunchuk_acc : sample.acc) = acc;
    }

    if (withheld) {
        // Waving a controller around the panel must not trick or wheelie.
        sample.acc = OpenXRWiiRemoteSample{}.acc;
        sample.nunchuk_acc = OpenXRWiiRemoteSample{}.nunchuk_acc;
        m_pointer.Reset();
        OpenXRPublishWiiRemote(m_joystick_id, sample);
        return;
    }

    wii_remote::Screen target{};
    wii_remote::ScreenHit hit{};
    if (screen.valid && aim_valid[1]) {
        target.pose = ToWiiRemotePose(screen.pose);
        target.half_width = screen.half_width_meters;
        target.half_height = screen.half_height_meters;
        hit = wii_remote::RaycastScreen(aims[1], target);
    }
    if (screen.valid && aim_valid[1]) {
        m_horizon = wii_remote::Horizon(aims[1], target);
    }
    const wii_remote::ScreenHit pointer = m_pointer.Update(hit, input_time);
    if (pointer.valid) {
        sample.pointer_valid = true;
        sample.pointer = wii_remote::KpadPosition(pointer);
        // Held with the position through a tracking blip.
        sample.horizon = m_horizon;
        sample.distance_meters = pointer.distance_meters;
        if (!m_logged_pointer) {
            m_logged_pointer = true;
            Log(OpenXRLogLevel::Info, "OpenXR Wii Remote pointer reached the virtual screen");
        }
    }
    OpenXRPublishWiiRemote(m_joystick_id, sample);
}

void OpenXRInput::ResetDriving() {
    m_wheel = {};
    WheelGeometry unused{};
    m_wheel_reference.Resolve(unused, false, false, false, false, 0, 0.0f);
    m_wheel_visual.Reset();
    m_wheel_held = {};
    m_wheel_time = 0;
    m_driving = {};
    OpenXRPublishDriving(m_driving);
}

void OpenXRInput::UpdateDriving(XrTime display_time, const driving::SeatFrame& seat,
                                std::array<wii_remote::HandInputs, kHands>& hands, bool withheld) {
    const FirstPersonAnchor anchor = MkwVRFirstPersonGetAnchor();
    if (!seat.valid || !anchor.valid || !anchor.cockpit) {
        if (m_driving.cockpit_active || m_wheel_time != 0) {
            ResetDriving();
        }
        return;
    }
    const WheelTuning tuning = RuntimeConfigFile::VrWheelTuning();
    const bool hand_steering = RuntimeConfigFile::VrHandSteering();
    const bool steering_wheel = RuntimeConfigFile::VrSteeringWheel();
    const bool native_steering_wheel = RuntimeConfigFile::VrNativeSteeringWheel();
    const float dt = m_wheel_time != 0 && display_time > m_wheel_time
                         ? static_cast<float>(display_time - m_wheel_time) * 1.0e-9f
                         : 1.0f / 90.0f;
    m_wheel_time = display_time;

    DrivingSnapshot snapshot{};
    snapshot.cockpit_active = true;
    snapshot.hand_steering = hand_steering;
    snapshot.bike = anchor.bike;
    // The vehicle's own control is the one turning (or none is shown at all),
    // so the overlay adds no separate wheel.
    snapshot.synthetic_control =
        steering_wheel && !(native_steering_wheel && anchor.native_mesh_prepared);

    // Which control the hands reach for: the vehicle's own wherever its
    // geometry is known and no separate wheel is drawn, a handlebar always
    // (held over a brief gap while gripped), otherwise the VR wheel in front
    // of the seat.
    WheelGeometry geometry = anchor.native_wheel;
    const bool geometry_valid = m_wheel_reference.Resolve(geometry, true, geometry.valid,
                                                          m_wheel_held[0] || m_wheel_held[1], anchor.bike,
                                                          anchor.vehicle_identity, dt);
    if (anchor.bike && !geometry_valid) {
        geometry = {};
        geometry.center = {0.0f, SteeringWheel::Height, SteeringWheel::Depth};
        geometry.right = {1.0f, 0.0f, 0.0f};
        geometry.up = {0.0f, 0.0f, -1.0f};
        geometry.normal = {0.0f, 1.0f, 0.0f};
        geometry.radius = 0.25f;
        geometry.valid = true;
    }
    const bool uses_geometry = anchor.bike || (geometry_valid && !snapshot.synthetic_control);
    if (uses_geometry != m_wheel_uses_geometry || anchor.bike != m_wheel_bike) {
        m_wheel = {};
        m_wheel_uses_geometry = uses_geometry;
        m_wheel_bike = anchor.bike;
    }
    snapshot.control = geometry;

    constexpr XrSpaceLocationFlags kPoseValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    std::array<WheelHand, kHands> wheel_hands{};
    for (uint32_t hand = 0; hand < kHands; ++hand) {
        bool tracked = false;
        std::array<float, 12> seat_from_grip = snapshot.hands[hand].seat_from_grip;
        if (m_grip_spaces[hand] != XR_NULL_HANDLE) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(xrLocateSpace(m_grip_spaces[hand], m_runtime->AppSpace(), display_time, &location)) &&
                (location.locationFlags & kPoseValid) == kPoseValid) {
                tracked = true;
                const auto& pose = location.pose;
                seat_from_grip = driving::SeatFromApp(seat, {pose.position.x, pose.position.y, pose.position.z},
                                                      {pose.orientation.x, pose.orientation.y,
                                                       pose.orientation.z, pose.orientation.w});
            }
        }
        const float squeeze = hands[hand].squeeze;
        // Hands are shown only while they can steer.
        snapshot.hands[hand] = {tracked && hand_steering, false, squeeze, seat_from_grip};
        wheel_hands[hand] = {seat_from_grip[3], seat_from_grip[7], seat_from_grip[11], squeeze, tracked};
        if (uses_geometry) {
            wheel_hands[hand] = geometry.ToWheel(wheel_hands[hand]);
        }
    }
    // A USB wheel drives the race through the GameCube pad: it steers, the
    // cockpit's wheel shows its angle, and the hands cannot take hold.
    float hardware_steering = 0.0f;
    const bool hardware_wheel = physical_wheel::SteeringSnapshot(hardware_steering);
    const bool active = hand_steering && !withheld && !hardware_wheel;
    const WheelState wheel = m_wheel.Update(wheel_hands, active, dt,
                                            uses_geometry ? geometry.radius : SteeringWheel::Radius,
                                            anchor.bike, tuning);
    for (uint32_t hand = 0; hand < kHands; ++hand) {
        if (wheel.held[hand] != m_wheel_held[hand] && active && tuning.haptics) {
            constexpr XrDuration kGrabPulseNs = 25'000'000;
            constexpr XrDuration kReleasePulseNs = 15'000'000;
            ApplyHaptic(hand, wheel.held[hand] ? 0.25f : 0.12f, wheel.held[hand] ? kGrabPulseNs : kReleasePulseNs);
        }
        snapshot.hands[hand].held = wheel.held[hand];
    }
    m_wheel_held = wheel.held;
    snapshot.held = wheel.held;
    driving::ApplyHandSteering(hands, wheel);
    const float max_angle = driving::MaxWheelAngle(anchor.bike, tuning);
    if (hardware_wheel) {
        snapshot.steering_input = std::clamp(hardware_steering, -1.0f, 1.0f);
        // The hardware wheel is already smooth; follow it directly.
        snapshot.visual_angle = m_wheel_visual.Update(true, snapshot.steering_input * max_angle, 0.0f, max_angle, dt);
    } else {
        snapshot.steering_input = withheld ? 0.0f : hands[0].stick_x;
        snapshot.visual_angle = m_wheel_visual.Update(wheel.held[0] || wheel.held[1], wheel.visualAngle,
                                                      snapshot.steering_input, max_angle, dt);
    }
    m_driving = snapshot;
    OpenXRPublishDriving(snapshot);
}

void OpenXRInput::UpdateRumble() {
    if (!OpenXRWiiRemoteRumbleRequested() || !OpenXRWiiRemoteOwnsGamepad(m_joystick_id)) {
        StopRumble();
        return;
    }
    for (uint32_t hand = 0; hand < kHandCount; ++hand) {
        ApplyHaptic(hand, 1.0f, kRumblePulseNs);
        m_haptics_active[hand] = true;
    }
}

void OpenXRInput::StopRumble() {
    for (uint32_t hand = 0; hand < kHandCount; ++hand) {
        if (m_haptics_active[hand]) {
            ApplyHaptic(hand, 0.0f, 0);
            m_haptics_active[hand] = false;
        }
    }
}

void OpenXRInput::ApplyHaptic(uint32_t hand, float amplitude, XrDuration duration) {
    if (!m_created || m_runtime == nullptr || hand >= kHandCount || m_haptic == XR_NULL_HANDLE) {
        return;
    }
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = std::clamp(amplitude, 0.0f, 1.0f);
    vibration.duration = duration;
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = m_haptic;
    info.subactionPath = m_hand_paths[hand];
    if (vibration.amplitude <= 0.0f) {
        xrStopHapticFeedback(m_runtime->Session(), &info);
        return;
    }
    xrApplyHapticFeedback(m_runtime->Session(), &info,
                          reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}

bool OpenXRInput::Check(XrResult result, const char* operation) {
    if (m_runtime != nullptr) {
        m_runtime->ObserveResult(result);
    }
    if (XR_SUCCEEDED(result)) {
        return true;
    }
    std::ostringstream message;
    message << operation << " failed (" << result << ')';
    m_last_error = message.str();
    Log(OpenXRLogLevel::Error, m_last_error);
    return false;
}

void OpenXRInput::Log(OpenXRLogLevel level, const std::string& message) const noexcept {
    if (!m_logger) {
        return;
    }
    try {
        m_logger(level, message);
    } catch (...) {
    }
}

} // namespace mkw::vr

#endif // defined(MKW_ENABLE_OPENXR)
