// SPDX-License-Identifier: GPL-3.0-or-later
//
// The in-headset settings panel's controller handling, tested without a
// headset: the button that opens and closes it in each controller mode, the
// release latch that keeps a closing press out of the game, selection,
// scrolling, and where a hit lands on the panel's canvas.

#include "vr/openxr_settings_panel.h"

#include <cmath>
#include <iostream>

namespace {

using namespace mkw::vr;
using namespace mkw::vr::settings_panel;

int g_failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++g_failures;
        std::cerr << "FAILED: " << what << '\n';
    }
}

void CheckNear(float actual, float expected, const char* what, float tolerance = 1.0e-3f) {
    if (!(std::fabs(actual - expected) <= tolerance)) {
        ++g_failures;
        std::cerr << "FAILED: " << what << " (expected " << expected << ", got " << actual << ")\n";
    }
}

constexpr float kDt = 1.0f / 90.0f;
constexpr OpenXRControllerMode kWiiRemote = OpenXRControllerMode::WiiRemote;
constexpr OpenXRControllerMode kGamepad = OpenXRControllerMode::Gamepad;

std::array<HandInputs, 2> Released() {
    return {};
}

std::array<HandInputs, 2> Chord() {
    std::array<HandInputs, 2> hands{};
    hands[0].primary = true;
    hands[0].secondary = true;
    return hands;
}

std::array<HandInputs, 2> LeftY() {
    std::array<HandInputs, 2> hands{};
    hands[0].secondary = true;
    return hands;
}

void LeftYOpensAndClosesOnceAsAWiiRemote() {
    Controls controls; bool open=false;
    auto frame=controls.Update(LeftY(),open,kDt,kWiiRemote);
    Check(!open && !frame.withheld,"Y alone is an item, not options");
    controls.Update(Released(),open,.25f,kWiiRemote);
    frame=controls.Update(Chord(),open,kDt,kWiiRemote);
    Check(open && frame.withheld,"X+Y opens options in either controller mode");
    controls.Update(Chord(),open,kDt,kWiiRemote);
    Check(open,"holding X+Y does not toggle twice");
    controls.Update(Released(),open,.25f,kWiiRemote);
    frame=controls.Update(Chord(),open,kDt,kWiiRemote);
    Check(!open && frame.withheld,"closing chord stays out of the game");
    frame=controls.Update(Released(),open,.25f,kWiiRemote);
    Check(!frame.withheld,"input returns after release");
}

void ChordOpensAndClosesOnceAsAGamepad() {
    Controls controls;
    bool open = false;

    Frame frame = controls.Update(LeftY(), open, kDt, kGamepad);
    Check(!open && !frame.withheld, "as a gamepad left Y is GameCube Y, not the panel");
    auto one = Released();
    one[0].thumbstick_click = true;
    frame = controls.Update(one, open, kDt, kGamepad);
    Check(!open && !frame.withheld, "one thumbstick click alone neither opens nor withholds");

    frame = controls.Update(Chord(), open, kDt, kGamepad);
    Check(open && frame.open && frame.withheld, "clicking both thumbsticks opens the panel");
    frame = controls.Update(Chord(), open, kDt, kGamepad);
    Check(open, "holding the chord does not toggle again");
    frame = controls.Update(Released(), open, .25f, kGamepad);
    Check(open && frame.withheld, "the panel stays open and keeps the controllers after the chord is released");

    frame = controls.Update(Chord(), open, kDt, kGamepad);
    Check(!open && !frame.open, "the chord closes the panel again");
    Check(frame.withheld, "the closing chord is still withheld from the game");
    frame = controls.Update(Released(), open, .25f, kGamepad);
    Check(!frame.withheld, "the game gets the controllers back once everything is released");
}

void MenuClosesAndItsPressStaysOutOfTheGame() {
    Controls controls;
    bool open = false;
    controls.Update(Chord(), open, kDt, kWiiRemote);
    controls.Update(Released(), open, .25f, kWiiRemote);

    auto menu = Released();
    menu[0].menu = true;
    Frame frame = controls.Update(menu, open, kDt, kWiiRemote);
    Check(!open, "the left menu button closes an open panel");
    frame = controls.Update(menu, open, kDt, kWiiRemote);
    Check(frame.withheld, "+ held across the close does not reach the game");
    Check(!open, "a held menu button does not reopen the panel");
    frame = controls.Update(Released(), open, .25f, kWiiRemote);
    Check(!frame.withheld, "released, the controllers go back to the game");

    frame = controls.Update(menu, open, kDt, kWiiRemote);
    Check(!open && !frame.withheld, "with the panel closed the menu button is the game's");
}

void SelectWaitsForAReleaseAndFollowsTheTrigger() {
    Controls controls;
    bool open = false;
    // Opened from the settings bar while a trigger is held for the game.
    auto trigger = Released();
    trigger[1].trigger = 1.0f;
    controls.Update(trigger, open, kDt, kWiiRemote);
    open = true;
    Frame frame = controls.Update(trigger, open, kDt, kWiiRemote);
    Check(frame.open && !frame.select, "a trigger held from before the panel opened does not click");
    frame = controls.Update(Released(), open, .25f, kWiiRemote);
    Check(!frame.select, "nothing held, nothing selected");
    frame = controls.Update(trigger, open, kDt, kWiiRemote);
    Check(frame.select && frame.pointing_hand == 1, "a fresh right trigger selects and points with the right hand");

    auto left = Released();
    left[0].trigger = 0.9f;
    controls.Update(Released(), open, .25f, kWiiRemote);
    frame = controls.Update(left, open, kDt, kWiiRemote);
    Check(frame.select && frame.pointing_hand == 0, "pulling the left trigger hands the pointer to the left hand");

    auto button = Released();
    button[1].primary = true;
    controls.Update(Released(), open, .25f, kWiiRemote);
    frame = controls.Update(button, open, kDt, kWiiRemote);
    Check(frame.select && frame.pointing_hand == 0, "A selects without moving the pointer to another hand");
}

void ThumbstickScrolls() {
    Controls controls;
    bool open = true;
    controls.Update(Released(), open, .25f, kWiiRemote);

    auto small = Released();
    small[1].stick_y = 0.2f;
    Check(controls.Update(small, open, kDt, kWiiRemote).wheel == 0.0f, "a resting thumbstick does not scroll");

    auto up = Released();
    up[1].stick_y = 1.0f;
    CheckNear(controls.Update(up, open, 0.5f, kWiiRemote).wheel, kScrollStepsPerSecond * 0.1f,
              "full deflection scrolls up at the full rate, with a long frame clamped");
    auto down = Released();
    down[0].stick_y = -1.0f;
    down[1].stick_y = 0.3f;
    Check(controls.Update(down, open, kDt, kWiiRemote).wheel < 0.0f, "the more deflected stick decides the direction");

    bool closed = false;
    Controls idle;
    Check(idle.Update(up, closed, kDt, kWiiRemote).wheel == 0.0f, "a closed panel does not scroll");
}

void HitsMapOntoTheCanvas() {
    wii_remote::ScreenHit hit{};
    hit.valid = true;
    hit.u = -1.0f;
    hit.v = 1.0f;
    auto point = CanvasPoint(hit);
    CheckNear(point[0], 0.0f, "the panel's left edge is canvas x 0");
    CheckNear(point[1], 0.0f, "the panel's top edge is canvas y 0");
    hit.u = 1.0f;
    hit.v = -1.0f;
    point = CanvasPoint(hit);
    CheckNear(point[0], kSettingsPanelWidthPixels, "the right edge is the canvas width");
    CheckNear(point[1], kSettingsPanelHeightPixels, "the bottom edge is the canvas height");

    const auto extents = HalfExtents(2.4f);
    CheckNear(extents[0], 0.9f, "three quarters of a 2.4 m screen is 1.8 m across");
    CheckNear(extents[1], 0.675f, "and keeps the canvas's 4:3");
}

void BridgeAccumulatesWheelUntilTaken() {
    OpenXRSetSettingsPanelOpen(true);
    Check(OpenXRSettingsPanelOpen(), "the open flag is shared");
    OpenXRPublishSettingsPanelPointer(true, 10.0f, 20.0f, false, 0.5f);
    OpenXRPublishSettingsPanelPointer(true, 11.0f, 21.0f, true, 0.25f);
    OpenXRSettingsPanelPointer pointer = OpenXRTakeSettingsPanelPointer();
    Check(pointer.valid && pointer.select, "the latest pointer state is read");
    CheckNear(pointer.x, 11.0f, "the latest x is read");
    CheckNear(pointer.wheel, 0.75f, "wheel steps from every XR frame are kept");
    pointer = OpenXRTakeSettingsPanelPointer();
    CheckNear(pointer.wheel, 0.0f, "taking the pointer consumes its wheel steps");
    OpenXRSetSettingsPanelOpen(false);
    Check(!OpenXRSettingsPanelOpen(), "and can be cleared again");
}

} // namespace

int main() {
    {
        Controls controls;bool open=false;
        auto sticks=Released();sticks[0].thumbstick_click=sticks[1].thumbstick_click=true;
        controls.Update(sticks,open,kDt,kGamepad);
        Check(open,"both stick clicks provide the upstream options shortcut");
        for(int i=0;i<30;++i) controls.Update(sticks,open,kDt,kGamepad);
        Check(open,"held stick shortcut opens once only");
        controls.Update(Released(),open,.25f,kGamepad);
        controls.Update(sticks,open,kDt,kGamepad);
        Check(!open,"both stick clicks close options after release");
        controls.Update(Released(),open,.25f,kGamepad);
        sticks[0].thumbstick_click=false;
        const auto frame=controls.Update(sticks,open,kDt,kGamepad);
        Check(!open && !frame.withheld,"one camera stick click cannot open options");
    }
    {
        Controls controls;bool open=false;
        auto x=Released();x[0].primary=true;
        controls.Update(x,open,kDt,kGamepad);
        Check(!open,"X alone remains a trick");
        controls.Update(Released(),open,kDt,kGamepad);
        controls.Update(LeftY(),open,kDt,kGamepad);
        Check(open,"serialized X then Y samples open VR settings");
        for(int i=0;i<30;++i) controls.Update(LeftY(),open,kDt,kGamepad);
        Check(open,"holding the second button cannot toggle repeatedly");
        for(int i=0;i<30;++i) controls.Update(Released(),open,kDt,kGamepad);
        controls.Update(LeftY(),open,kDt,kGamepad);
        controls.Update(Released(),open,kDt,kGamepad);
        controls.Update(x,open,kDt,kGamepad);
        Check(!open,"serialized Y then X samples close VR settings");
        controls.Reset();open=false;
        controls.Update(x,open,kDt,kGamepad);
        for(int i=0;i<30;++i) controls.Update(Released(),open,kDt,kGamepad);
        controls.Update(LeftY(),open,kDt,kGamepad);
        Check(!open,"ordinary trick then later item must not open options");
    }
    {
        Controls controls;bool open=false;
        controls.Update(Chord(),open,kDt,kGamepad);
        controls.Update(Released(),open,.25f,kGamepad);
        controls.Update(Chord(),open,kDt,kGamepad);
        Check(!open,"second chord closes settings");
        for(int i=0;i<30;++i) {
            controls.Update(Released(),open,kDt,kGamepad);
            controls.Update(Chord(),open,kDt,kGamepad);
        }
        Check(!open,"brief SteamVR empty samples cannot reopen a closing chord");
        controls.Update(LeftY(),open,.25f,kGamepad);
        controls.Update(Chord(),open,kDt,kGamepad);
        Check(!open,"partial button release cannot rearm the chord");
        controls.Update(Released(),open,.25f,kGamepad);
        controls.Update(Chord(),open,kDt,kGamepad);
        Check(open,"a stable release allows the next deliberate chord");
    }
    LeftYOpensAndClosesOnceAsAWiiRemote();
    ChordOpensAndClosesOnceAsAGamepad();
    MenuClosesAndItsPressStaysOutOfTheGame();
    SelectWaitsForAReleaseAndFollowsTheTrigger();
    ThumbstickScrolls();
    HitsMapOntoTheCanvas();
    BridgeAccumulatesWheelUntilTaken();
    {
        OpenXRPublishPhysicalOptionsButtons({true,true,true});
        const auto physical=OpenXRReadPhysicalOptionsButtons();
        Check(physical.valid && physical.x && physical.y,"SteamVR publishes both physically held buttons together");
        auto hands=Released();hands[0].primary=physical.x;hands[0].secondary=physical.y;
        Controls controls;bool open=false;
        const auto frame=controls.Update(hands,open,kDt,kGamepad);
        OpenXRSetSettingsPanelOpen(open);
        Check(OpenXRSettingsPanelOpen() && frame.open && frame.withheld,
            "physical SteamVR chord reaches the actual headset menu flag and blocks game input");
        OpenXRPublishPhysicalOptionsButtons({});
        Check(!OpenXRReadPhysicalOptionsButtons().valid,"lost native controller cannot fabricate an options chord");
        OpenXRSetSettingsPanelOpen(false);
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "vr_settings_panel_tests: all checks passed\n";
    return 0;
}
