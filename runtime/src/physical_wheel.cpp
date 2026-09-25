// SPDX-License-Identifier: GPL-3.0-or-later
// USB steering wheel and pedals for player 1. Ported from heurazy's
// mario-kart-wii-VR-port (GPL-3.0-or-later); see physical_wheel.h.

#include "physical_wheel.h"

#include "input_bindings.h"
#include "runtime_config.h"
#include "runtime_log.h"
#include "vr/openxr_integration.h"

#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_stdinc.h>
#include <imgui.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace physical_wheel {
// Named rather than anonymous: runtime sources are unity-built in groups.
namespace hardware {

using Clock = std::chrono::steady_clock;
struct Device {
    SDL_JoystickID id;
    SDL_Joystick* joystick;
    std::string key, name;
    // SDL knows the model as a steering wheel (Logitech, Thrustmaster, Fanatec...).
    bool wheel;
};
struct Axis {
    std::string device;
    int index = -1, low = 0, center = 0, high = 0;
};
struct Button {
    std::string device;
    int index = -1;
};
enum AxisSlot { kSteering, kThrottle, kBrake, kAxisCount };
enum ButtonSlot { kDrift, kItem, kTrick, kConfirm, kPause, kBack, kButtonCount };
// heurazy's PhysicalWheel.toml has the first five; Back was added here.
constexpr size_t kLegacyButtonCount = 5;

std::vector<Device> devices;
std::array<Axis, kAxisCount> axes;
std::array<Button, kButtonCount> buttons;
constexpr std::array<const char*, kAxisCount> kAxisNames{"Steering wheel", "Accelerator pedal", "Brake / reverse pedal"};
constexpr std::array<const char*, kButtonCount> kButtonNames{
    "Drift (R): RIGHT paddle", "Item (L): LEFT paddle", "Trick / wheelie", "Confirm / accelerate (A)",
    "Pause (Start)", "Back (B, menus)"};
bool loaded = false, enabled = false, vibration = false, ready = false, focused = false, driving = false;
bool armed = false, loggedReady = false;
float deadzone = .02f, strength = .05f;
std::string error;
int learning = -1;
std::vector<std::pair<SDL_JoystickID, int>> previousButtons;
Clock::time_point rumbleTick{}, motorTime{}, settingsUntil{};
bool discoveryRequested = true, discoveryActive = false;
bool motorOn = false;
SDL_Haptic* haptic = nullptr;
SDL_JoystickID hapticId = 0, attemptedHaptic = 0;
bool hapticSubsystem = false;
std::mutex snapshotMutex;
float snapshotSteering = 0;
bool snapshotActive = false;
Clock::time_point snapshotTime{};
PadFilter inputFilter;

std::filesystem::path ConfigPath() {
    return RuntimeConfigFile::ResolveConfigPath().parent_path() / "PhysicalWheel.toml";
}

Device* Find(const std::string& key) {
    Device* match = nullptr;
    for (auto& d : devices) {
        if (d.key == key && SDL_JoystickConnected(d.joystick)) {
            if (match) return nullptr; // Ambiguous identical devices must not control the wrong pedal.
            match = &d;
        }
    }
    return match;
}
bool AxisReady(const Axis& a) {
    const auto* d = Find(a.device);
    return d && a.index >= 0 && a.index < SDL_GetNumJoystickAxes(d->joystick) && std::abs(a.high - a.low) >= 1024;
}
int Raw(const Axis& a) {
    const auto* d = Find(a.device);
    return d && a.index >= 0 && a.index < SDL_GetNumJoystickAxes(d->joystick) ? SDL_GetJoystickAxis(d->joystick, a.index)
                                                                              : 0;
}
bool ButtonReady(const Button& b) {
    const auto* d = Find(b.device);
    return d && b.index >= 0 && b.index < SDL_GetNumJoystickButtons(d->joystick);
}
bool Pressed(const Button& b) {
    const auto* d = Find(b.device);
    return ButtonReady(b) && SDL_GetJoystickButton(d->joystick, b.index);
}
// The steering device's first hat is the D-pad (a G29's, for one).
uint8_t Hat() {
    const auto* d = Find(axes[kSteering].device);
    return d && SDL_GetNumJoystickHats(d->joystick) > 0 ? SDL_GetJoystickHat(d->joystick, 0) : 0;
}
Controls ReadControls() {
    Controls c;
    const auto& s = axes[kSteering];
    c.steering = Steering(Raw(s), s.low, s.center, s.high, deadzone);
    c.throttle = Pedal(Raw(axes[kThrottle]), axes[kThrottle].low, axes[kThrottle].high);
    c.brake = Pedal(Raw(axes[kBrake]), axes[kBrake].low, axes[kBrake].high);
    c.drift = Pressed(buttons[kDrift]);
    c.item = Pressed(buttons[kItem]);
    c.trick = Pressed(buttons[kTrick]);
    c.confirm = Pressed(buttons[kConfirm]);
    c.pause = Pressed(buttons[kPause]);
    c.back = Pressed(buttons[kBack]);
    c.hat = Hat();
    return c;
}

void StopFeedback() {
    if (!motorOn && !haptic) return;
    if (haptic) SDL_StopHapticRumble(haptic);
    if (auto* d = Find(axes[kSteering].device)) SDL_RumbleJoystick(d->joystick, 0, 0, 0);
    motorOn = false;
}
void CloseHaptic() {
    StopFeedback();
    if (haptic) SDL_CloseHaptic(haptic);
    haptic = nullptr;
    hapticId = attemptedHaptic = 0;
}

bool WriteReplacing(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text) || !out.flush()) return false;
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

void Save() {
    toml::value root(toml::table{});
    root["enabled"] = enabled;
    root["vibration"] = vibration;
    root["deadzone"] = double(deadzone);
    root["strength"] = double(strength);
    toml::array axisList, buttonList;
    for (const auto& a : axes) {
        axisList.emplace_back(
            toml::table{{"device", a.device}, {"axis", a.index}, {"low", a.low}, {"center", a.center}, {"high", a.high}});
    }
    for (const auto& b : buttons) buttonList.emplace_back(toml::table{{"device", b.device}, {"button", b.index}});
    root["axes"] = axisList;
    root["buttons"] = buttonList;
    if (!WriteReplacing(ConfigPath(), toml::format(root))) {
        error = "Could not save PhysicalWheel.toml.";
    } else {
        error.clear();
    }
}

void Load() {
    loaded = true;
    std::error_code ec;
    if (!std::filesystem::exists(ConfigPath(), ec)) return;
    try {
        const auto c = toml::parse(RuntimeConfigFile::PathToUtf8(ConfigPath()));
        auto newAxes = axes;
        auto newButtons = buttons;
        const auto& axisList = toml::find(c, "axes").as_array();
        const auto& buttonList = toml::find(c, "buttons").as_array();
        if (axisList.size() != kAxisCount ||
            (buttonList.size() != kButtonCount && buttonList.size() != kLegacyButtonCount)) {
            throw std::runtime_error("Invalid wheel configuration");
        }
        for (size_t i = 0; i < kAxisCount; ++i) {
            auto& a = newAxes[i];
            a.device = toml::find<std::string>(axisList[i], "device");
            a.index = toml::find<int>(axisList[i], "axis");
            a.low = std::clamp(toml::find<int>(axisList[i], "low"), -32768, 32767);
            a.center = std::clamp(toml::find<int>(axisList[i], "center"), -32768, 32767);
            a.high = std::clamp(toml::find<int>(axisList[i], "high"), -32768, 32767);
        }
        for (size_t i = 0; i < buttonList.size(); ++i) {
            newButtons[i].device = toml::find<std::string>(buttonList[i], "device");
            newButtons[i].index = toml::find<int>(buttonList[i], "button");
        }
        axes = newAxes;
        buttons = newButtons;
        enabled = toml::find_or<bool>(c, "enabled", false);
        vibration = toml::find_or<bool>(c, "vibration", false);
        const double dz = toml::find_or<double>(c, "deadzone", .02), gain = toml::find_or<double>(c, "strength", .05);
        deadzone = std::isfinite(dz) ? std::clamp(float(dz), 0.f, .25f) : .02f;
        strength = std::isfinite(gain) ? std::clamp(float(gain), 0.f, .15f) : .05f;
    } catch (const std::exception& e) {
        enabled = false;
        error = std::string("Wheel configuration: ") + e.what();
        RT_LOG(RT_TAG_CONFIG) << "PhysicalWheel.toml ignored: " << e.what() << std::endl;
    }
}

void Scan() {
    // Keep open handles stable; closing/reopening a wheel can disturb its driver.
    int count = 0;
    auto* ids = SDL_GetJoysticks(&count);
    if (!ids) return;
    for (auto it = devices.begin(); it != devices.end();) {
        if (!SDL_JoystickConnected(it->joystick)) {
            if (hapticId == it->id || attemptedHaptic == it->id) CloseHaptic();
            SDL_CloseJoystick(it->joystick);
            it = devices.erase(it);
        } else {
            ++it;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (std::any_of(devices.begin(), devices.end(), [&](const auto& d) { return d.id == ids[i]; })) continue;
        auto* j = SDL_OpenJoystick(ids[i]);
        if (!j) continue;
        char guid[33]{};
        SDL_GUIDToString(SDL_GetJoystickGUID(j), guid, sizeof(guid));
        const char* serial = SDL_GetJoystickSerial(j);
        const char* path = SDL_GetJoystickPath(j);
        std::string key = guid;
        key += '|';
        key += serial && *serial ? serial : path ? path : "";
        const char* name = SDL_GetJoystickName(j);
        devices.push_back({ids[i], j, key, name ? name : "Unnamed device",
                           SDL_GetJoystickType(j) == SDL_JOYSTICK_TYPE_WHEEL});
    }
    SDL_free(ids);
}

void Feedback() {
    if (!enabled || !ready || !focused || !driving || InputBindings::InputBlocked() || Clock::now() < settingsUntil ||
        !vibration || Clock::now() - motorTime > std::chrono::milliseconds(250)) {
        StopFeedback();
        return;
    }
    if (!motorOn) return;
    if (Clock::now() - rumbleTick < std::chrono::milliseconds(40)) return;
    rumbleTick = Clock::now();
    auto* d = Find(axes[kSteering].device);
    if (!d) return;
    // Short bounded effects only. No spring, damper or constant steering torque.
    const auto amplitude = static_cast<Uint16>(std::clamp(strength, 0.f, .15f) * 65535);
    if (SDL_RumbleJoystick(d->joystick, amplitude, amplitude, 100)) return;
    if (!haptic && attemptedHaptic != d->id) {
        attemptedHaptic = d->id;
        if (!hapticSubsystem) hapticSubsystem = SDL_InitSubSystem(SDL_INIT_HAPTIC);
        if (hapticSubsystem && SDL_IsJoystickHaptic(d->joystick)) {
            haptic = SDL_OpenHapticFromJoystick(d->joystick);
            hapticId = d->id;
            if (haptic && (!SDL_SetHapticGain(haptic, 15) || !SDL_InitHapticRumble(haptic))) {
                SDL_CloseHaptic(haptic);
                haptic = nullptr;
            }
        }
        if (!haptic) error = "This driver does not expose rumble. Driving still works.";
    }
    if (haptic) SDL_PlayHapticRumble(haptic, std::clamp(strength, 0.f, .15f), 100);
}

void PublishSnapshot(bool active, float steering) {
    std::lock_guard lock(snapshotMutex);
    snapshotActive = active;
    snapshotSteering = active ? steering : 0;
    snapshotTime = Clock::now();
}

void Poll() {
    if (!loaded) Load();
    // SDL device enumeration may probe HID or Bluetooth drivers and stall a
    // frame. Enumerate only while the wheel is enabled or its settings page is
    // open; then refresh on hot-plug instead of rescanning every second.
    const bool discover = enabled || Clock::now() < settingsUntil;
    if (!discover) {
        if (discoveryActive) {
            CloseHaptic();
            for (auto& device : devices) SDL_CloseJoystick(device.joystick);
            devices.clear();
        }
        discoveryActive = false;
        discoveryRequested = true;
        ready = false;
        armed = false;
        inputFilter = {};
        PublishSnapshot(false, 0);
        return;
    }
    if (!discoveryActive) discoveryRequested = true;
    discoveryActive = true;
    if (discoveryRequested) {
        Scan();
        discoveryRequested = false;
    }
    // In VR the headset has the player's attention even when the desktop
    // window does not have the keyboard.
    focused = SDL_GetKeyboardFocus() != nullptr || mkw::vr::OpenXRIsRunning();
    const auto& s = axes[kSteering];
    ready = AxisReady(s) && AxisReady(axes[kThrottle]) && AxisReady(axes[kBrake]) && std::abs(s.low - s.center) >= 1024 &&
            std::abs(s.high - s.center) >= 1024 && (s.low < s.center) != (s.high < s.center) &&
            ButtonReady(buttons[kDrift]) && ButtonReady(buttons[kItem]) &&
            (buttons[kDrift].device != buttons[kItem].device || buttons[kDrift].index != buttons[kItem].index);
    if (enabled && ready != loggedReady) {
        loggedReady = ready;
        const auto* d = Find(s.device);
        RT_LOG(RT_TAG_RUNTIME) << "[wheel] USB wheel " << (ready ? "ready: " : "not ready (setup incomplete or disconnected)")
                               << (ready && d ? d->name : std::string()) << std::endl;
    }
    const bool active = enabled && ready && focused && driving && !InputBindings::InputBlocked() &&
                        Clock::now() >= settingsUntil;
    PublishSnapshot(active, active ? Steering(Raw(s), s.low, s.center, s.high, deadzone) : 0);
    Feedback();
}

void HandleSdlEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_JOYSTICK_ADDED || event.type == SDL_EVENT_JOYSTICK_REMOVED)
        discoveryRequested = true;
}

} // namespace hardware

bool ReadPad(PADStatus& pad, bool blocked, bool race) {
    using namespace hardware;
    if (!loaded) Load();
    driving = race;
    blocked = blocked || Clock::now() < settingsUntil;
    Poll();
    if (!enabled) {
        // Nothing is opened or read until the wheel is turned on.
        armed = false;
        inputFilter = {};
        PublishSnapshot(false, 0);
        return false;
    }
    if (!ready || !focused) {
        armed = false;
        StopFeedback();
        inputFilter = {};
        if (race) {
            // An incomplete or disconnected setup is neutral in a race, never
            // a stale steering angle.
            const auto pause = pad.button & PAD_BUTTON_START;
            pad = {};
            pad.err = PAD_ERR_NONE;
            pad.button = pause;
        }
        return true;
    }
    const Controls controls = ReadControls();
    PADStatus wheel = race ? RacePad(controls) : MenuPad(controls);
    if (!armed) {
        // Arm only once every pedal and button is released, so enabling the
        // wheel or reconnecting it never fires a held input.
        armed = RacePad(controls).button == 0 && HatButtons(controls.hat) == 0 && !controls.back && !blocked;
        if (!armed) {
            wheel = {};
            wheel.err = PAD_ERR_NONE;
            StopFeedback();
        }
    }
    Merge(pad, wheel, race, blocked, inputFilter);
    return true;
}

bool SteeringSnapshot(float& steering) {
    using namespace hardware;
    std::lock_guard lock(snapshotMutex);
    steering = snapshotSteering;
    return snapshotActive && Clock::now() - snapshotTime < std::chrono::milliseconds(250);
}

bool Motor(int channel, unsigned command) {
    using namespace hardware;
    if (channel != 0 || !enabled) return false;
    motorTime = Clock::now();
    if (command != PAD_MOTOR_RUMBLE) {
        StopFeedback();
    } else {
        motorOn = true;
        Feedback();
    }
    return true;
}

void Shutdown() {
    using namespace hardware;
    CloseHaptic();
    for (auto& d : devices) SDL_CloseJoystick(d.joystick);
    devices.clear();
    discoveryActive = false;
    discoveryRequested = true;
    if (hapticSubsystem) SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    hapticSubsystem = false;
    ready = false;
    PublishSnapshot(false, 0);
}

void DrawSettings() {
    using namespace hardware;
    settingsUntil = Clock::now() + std::chrono::milliseconds(200);
    Poll();
    if (ImGui::Button("Refresh USB devices")) discoveryRequested = true;
    ImGui::TextWrapped("%s",
                       "USB steering wheel and pedals for player 1 (by heurazy). Calibrate each axis, then assign the "
                       "RIGHT paddle to drift and the LEFT paddle to items. Separate USB pedals and combined pedal axes "
                       "are supported.");
    if (ImGui::Checkbox("Enable USB wheel", &enabled)) {
        armed = false;
        loggedReady = false;
        StopFeedback();
        Save();
    }
    ImGui::TextWrapped("%s",
                       "The wheel is a GameCube controller: press its Confirm button at the title screen so the game "
                       "uses a GameCube controller. Its D-pad, Confirm and Back also work the menus. In VR, set the VR "
                       "controllers to Gamepad to steer menus and aim items with them too.");
    ImGui::TextWrapped("%s", "After enabling or reconnecting, close settings and release all pedals and buttons to arm "
                             "driving.");
    ImGui::TextWrapped("%s", ready ? "Devices and required bindings ready."
                                   : "Setup incomplete or device disconnected. Calibrate all axes and assign both "
                                     "paddles. The wheel stays neutral in a race until ready.");
    for (size_t i = 0; i < kAxisCount; ++i) {
        ImGui::PushID(int(i));
        auto& a = axes[i];
        ImGui::Separator();
        ImGui::TextUnformatted(kAxisNames[i]);
        auto* selected = Find(a.device);
        if (ImGui::BeginCombo("Device", selected ? selected->name.c_str() : "Select device")) {
            for (const auto& d : devices) {
                ImGui::PushID(int(d.id));
                const std::string label = d.wheel ? d.name + " (wheel)" : d.name;
                if (ImGui::Selectable(label.c_str(), a.device == d.key)) {
                    if (i == kSteering) CloseHaptic();
                    a = {d.key, -1, 0, 0, 0};
                    Save();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        selected = Find(a.device);
        if (selected) {
            const std::string label = a.index < 0 ? "Select axis" : std::to_string(a.index);
            if (ImGui::BeginCombo("Axis", label.c_str())) {
                for (int n = 0; n < SDL_GetNumJoystickAxes(selected->joystick); ++n) {
                    const std::string text =
                        std::to_string(n) + " : " + std::to_string(SDL_GetJoystickAxis(selected->joystick, n));
                    if (ImGui::Selectable(text.c_str(), n == a.index)) {
                        a.index = n;
                        a.low = a.center = a.high = 0;
                        Save();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Text("Raw: %d", Raw(a));
            if (ImGui::Button(i == kSteering ? "Set full LEFT" : "Set RELEASED")) {
                a.low = Raw(a);
                Save();
            }
            ImGui::SameLine();
            if (ImGui::Button(i == kSteering ? "Set full RIGHT" : "Set fully PRESSED")) {
                a.high = Raw(a);
                Save();
            }
            if (i == kSteering) {
                ImGui::SameLine();
                if (ImGui::Button("Set CENTER")) {
                    a.center = Raw(a);
                    Save();
                }
            }
            const float value = i == kSteering ? Steering(Raw(a), a.low, a.center, a.high, deadzone)
                                               : Pedal(Raw(a), a.low, a.high);
            ImGui::Text("Calibrated: %.2f", value);
            if (i == kSteering) {
                ImGui::TextDisabled("%s", "Full lock is wherever you record full left and right: turn the wheel as far "
                                          "as you want full steering to take (90 degrees each way matches the VR "
                                          "cockpit's wheel).");
                if (SDL_GetNumJoystickHats(selected->joystick) > 0) {
                    ImGui::TextDisabled("D-pad: this device's hat (menus, tricks).");
                }
            }
        }
        ImGui::PopID();
    }
    std::vector<std::pair<SDL_JoystickID, int>> down;
    for (const auto& d : devices) {
        for (int n = 0; n < SDL_GetNumJoystickButtons(d.joystick); ++n) {
            if (SDL_GetJoystickButton(d.joystick, n)) down.emplace_back(d.id, n);
        }
    }
    if (learning >= 0) {
        for (const auto& press : down) {
            if (std::find(previousButtons.begin(), previousButtons.end(), press) != previousButtons.end()) continue;
            for (const auto& d : devices) {
                if (d.id == press.first) {
                    buttons[learning] = {d.key, press.second};
                    learning = -1;
                    Save();
                    break;
                }
            }
            break;
        }
    }
    previousButtons = down;
    ImGui::Separator();
    for (size_t i = 0; i < kButtonCount; ++i) {
        ImGui::PushID(100 + int(i));
        ImGui::TextUnformatted(kButtonNames[i]);
        ImGui::SameLine();
        if (ImGui::Button(learning == int(i) ? "Press the hardware button..." : "Assign")) learning = int(i);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            buttons[i] = {};
            learning = -1;
            Save();
        }
        if (const auto* d = Find(buttons[i].device)) ImGui::Text("%s / button %d", d->name.c_str(), buttons[i].index);
        ImGui::PopID();
    }
    if (learning >= 0 && ImGui::Button("Cancel assignment")) learning = -1;
    ImGui::TextDisabled("%s", "A shifter's gears are buttons too; a gear stays pressed while engaged.");
    if (ImGui::SliderFloat("Wheel deadzone", &deadzone, 0, .25f)) Save();
    if (ImGui::Checkbox("Light game vibration (off by default)", &vibration)) {
        CloseHaptic();
        Save();
    }
    if (ImGui::SliderFloat("Vibration strength (maximum 15%)", &strength, 0, .15f)) Save();
    ImGui::TextWrapped("%s",
                       "No constant force or centering effect: turn on the centering spring in your wheel's own software "
                       "if you want one. Vibration follows Mario Kart's original rumble events; hardware and driver "
                       "support varies.");
    if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
}

void HandleSdlEvent(const SDL_Event& event) { hardware::HandleSdlEvent(event); }

} // namespace physical_wheel
