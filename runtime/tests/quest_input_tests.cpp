// SPDX-License-Identifier: GPL-3.0-or-later
#include "vr/quest_input.h"
#include "vr/game_menu_pointer.h"
#include <chrono>
#include <iostream>
#include <thread>

using namespace mkw::vr;
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
}
int main() {
    {
        GameMenuPointer pointer;
        QuestInput q{};q.active=true;q.ui_pointer.valid=true;q.ui_pointer.forward={0,0,-1};
        auto hit=pointer.Update(q,true,2,1,2);
        Check(hit.valid && !hit.down && hit.x==0 && hit.y==0,"menu ray points at native screen centre");
        q.accelerate=1;
        Check(pointer.Update(q,true,2,1,2).down,"right trigger validates pointer target");
        q.ui_pointer.valid=false;q.ui_left_pointer.valid=true;q.ui_left_pointer.forward={0,0,-1};q.reverse=true;
        Check(!pointer.Update(q,true,2,1,2).valid,"tracking loss does not transfer held click to other hand");
        q.accelerate=0;q.reverse=false;pointer.Update(q,true,2,1,2);pointer.Update(q,true,2,1,2);
        q.reverse=true;q.ui_left_pointer.position={.5f,.25f,0};
        hit=pointer.Update(q,true,2,1,2);
        Check(hit.valid && hit.down && hit.x==.5f && hit.y==-.5f,"left trigger and native pointer axes");
        pointer.Update(q,false,2,1,2);
        Check(!pointer.Update(q,true,2,1,2).down,"closing settings while holding trigger cannot click game");
        q.reverse=false;pointer.Update(q,true,2,1,2);q.reverse=true;
        Check(pointer.Update(q,true,2,1,2).down,"fresh trigger works after leaving settings");
        q.ui_left_pointer.position={3,0,0};
        Check(!pointer.Update(q,true,2,1,2).valid,"ray outside screen cannot select menu buttons");
    }
    QuestInput input{};
    Check(MapQuestInput(input).err == PAD_ERR_NO_CONTROLLER, "inactive controllers stay disconnected");
    input.active = true;
    input.steering_x = 0.1f;
    Check(MapQuestInput(input).stickX == 0, "stick noise is inside deadzone");
    input.steering_x = 1;
    input.steering_y = 0;
    Check(MapQuestInput(input).stickX == 100, "full right steering");
    input.steering_x = 0;
    input.steering_y = -1;
    Check(MapQuestInput(input).stickY == -100, "down retains GameCube sign for menus and item aim");
    input.steering_x = 1;
    input.steering_y = 1;
    auto pad = MapQuestInput(input);
    Check(pad.stickX == 71 && pad.stickY == 71, "diagonal is normalized");
    for (int x = 0; x <= 100; ++x) {
        input.steering_x = x / 100.0f; input.steering_y = 0.2f;
        const auto right = MapQuestInput(input).stickX;
        input.steering_x = -x / 100.0f;
        Check(MapQuestInput(input).stickX == -right, "left and right steering are symmetric");
    }
    const QuestStickCalibration calibrated{0.15f, 1.0f, -0.1f, 0.05f};
    input.steering_x = -0.1f; input.steering_y = 0.05f;
    Check(MapQuestInput(input, calibrated).stickX == 0 && MapQuestInput(input, calibrated).stickY == 0,
          "calibrated physical centre produces neutral steering");
    input.steering_x = 1;
    Check(MapQuestInput(input, calibrated).stickX == 100, "centre correction preserves full right");
    input.steering_x = -1;
    Check(MapQuestInput(input, calibrated).stickX == -100, "centre correction preserves full left");
    input.steering_x = 0.8f; input.steering_y = 0;
    Check(MapQuestInput(input, {0.15f, 0.8f, 0, 0}).stickX == 100, "outer calibration restores maximum steering");
    input.steering_x = 0.2f;
    Check(MapQuestInput(input, {0.25f, 1, 0, 0}).stickX == 0, "larger deadzone rejects stronger drift");
    input = {}; input.active = true;
    input.accelerate = 1; input.item = 1; input.drift = 1;
    input.wheel_active = true; input.wheel_steering = 0.05f;
    Check(MapQuestInput(input, calibrated).stickX == 5, "wheel bypasses stick drift calibration and deadzone");
    input.wheel_steering = -1;
    Check(MapQuestInput(input).stickX == -100, "wheel reaches full left lock");
    input.steering_y = -1;
    Check(MapQuestInput(input).stickY == -100, "wheel preserves backward item aim");
    input.steering_y = 1;
    Check(MapQuestInput(input).stickY == 100, "wheel preserves forward item aim");
    input.steering_y = 0;
    input.wheel_active = false;
    pad = MapQuestInput(input);
    Check(pad.button == (PAD_BUTTON_A | PAD_TRIGGER_L | PAD_TRIGGER_R), "accelerate, item and drift work together");
    Check(pad.analogA == 255 && pad.triggerL == 255 && pad.triggerR == 255, "GC analog channels agree with buttons");
    input.trick = true;
    Check(MapQuestInput(input).button == 0, "X + Y opens VR settings without using the item");
    input = {}; input.active = true;
    input.confirm = true; input.brake = true; input.look_back = true; input.pause = true;
    pad = MapQuestInput(input);
    Check(pad.button == (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_START), "face buttons and pause mapping");
    QuestInput cockpit{};cockpit.active=true;cockpit.cockpit_controls=true;
    cockpit.wheel_active=true;cockpit.wheel_steering=0.7f;cockpit.confirm=true;cockpit.accelerate=1;
    pad=MapQuestInput(cockpit);
    Check((pad.button&(PAD_BUTTON_A|PAD_TRIGGER_R))==(PAD_BUTTON_A|PAD_TRIGGER_R)&&pad.stickX==70,
          "cockpit A drifts while accelerating and holding the wheel");
    cockpit.confirm=false;cockpit.drift=1;
    Check(!(MapQuestInput(cockpit).button&PAD_TRIGGER_R),"grips cannot trigger cockpit drift");
    cockpit.accelerate=0;cockpit.confirm=true;
    Check(!(MapQuestInput(cockpit).button&PAD_BUTTON_A),"cockpit A is reserved for drift, not acceleration");
    cockpit.brake=true;
    Check(MapQuestInput(cockpit).button&PAD_BUTTON_B,"cockpit B remains available for braking");
    input.look_back = false; input.trick = true;
    Check((MapQuestInput(input).button & PAD_BUTTON_UP) != 0, "X alone still triggers a trick");
    input = {}; input.active = true;
    input.tricks_x = -1; input.tricks_y = -1;
    Check(MapQuestInput(input).button == (PAD_BUTTON_LEFT | PAD_BUTTON_DOWN), "right stick drives GC trick directions");
    QuestPadFilter filter;
    input = {}; input.active = true; input.accelerate = 1; input.steering_x = 1;
    pad = MapQuestInput(input);
    auto blocked = filter.Apply(pad, true);
    Check(blocked.err == PAD_ERR_NONE && blocked.button == 0 && blocked.stickX == 0, "F10 neutralizes without disconnecting");
    blocked = filter.Apply(pad, false);
    Check(blocked.button == 0 && blocked.analogA == 0 && blocked.stickX == 0, "held input stays suppressed after F10 closes");
    input.accelerate = 0; input.steering_x = 0;
    filter.Apply(MapQuestInput(input), false);
    input.accelerate = 1; input.steering_x = 1;
    blocked = filter.Apply(MapQuestInput(input), false);
    Check(blocked.button == PAD_BUTTON_A && blocked.stickX == 100, "release and repress restores controls");
    input={}; input.active=true; input.trick=true;
    Check(MapQuestInput(input,{}, {true,false}).button==PAD_TRIGGER_L,"remapped X uses item");
    input.item=1;
    Check(MapQuestInput(input,{}, {true,true}).button==0,"menu chord remains available after remapping");
    input={}; input.active=true; input.cockpit_controls=true;input.brake=true;
    Check(MapQuestInput(input,{}, {false,true}).button==PAD_TRIGGER_R,"remapped B drifts in cockpit");
    input.reverse=true; input.accelerate=1;
    Check(MapQuestInput(input,{}, {true,true}).button==PAD_BUTTON_B,"reverse overrides remapped drift and throttle");
    SteamVrTrickPause steam;
    auto gesture=steam.Update(true,true,false,1000000000);
    Check(gesture.trick&&!gesture.pause,"SteamVR X tricks immediately without pausing");
    gesture=steam.Update(true,true,false,1640000000);
    Check(gesture.trick&&!gesture.pause,"short X cannot pause");
    gesture=steam.Update(true,true,false,1650000000);
    Check(!gesture.trick&&gesture.pause,"holding X deliberately pauses after 650 ms");
    Check(!steam.Update(true,true,false,2000000000).pause,"holding X produces only one pause pulse");
    steam.Update(true,false,false,2100000000);
    gesture=steam.Update(true,true,true,2200000000);
    Check(gesture.trick&&!gesture.pause,"X plus Y remains available to VR settings");
    Check(!steam.Update(true,true,false,3200000000).pause,"releasing Y from settings chord cannot pause Mario Kart");
    steam.Update(false,false,false,3300000000);
    Check(!steam.Update(true,true,false,4300000000).pause,"SteamVR focus recovery cannot pause from a stale held X");
    steam.Update(true,false,false,4400000000);
    Check(steam.Update(true,true,false,4500000000).trick,"fresh X works after dashboard focus recovery");
    return failures ? 1 : 0;
}
