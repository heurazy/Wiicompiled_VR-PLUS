#pragma once
namespace mkw::vr {
struct QuestStickCalibration {
    float deadzone=.15f, outer=1.f, center_x=0.f, center_y=0.f;
};
struct QuestButtonMapping { bool swapItemTrick=false, swapCockpitDriftBrake=false; };
}
