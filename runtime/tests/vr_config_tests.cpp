// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime_config.h"
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

static void Require(bool condition) {
    if (!condition) std::abort();
}

static RuntimeUserConfig Parse(const std::string& text) {
    std::istringstream input(text);
    return RuntimeConfigFile::ParseConfig(input);
}

int main() {
    const auto defaults = Parse("[vr]\n");
    Require(!defaults.vrWelcomeComplete && defaults.vrTutorialCompleted == 0);
    Require(defaults.vrHandHud && defaults.vrMenuShaderQuality == 3);
    const auto imported = Parse("[vr]\nwelcome_complete = true\ntutorial_completed = 3\ndefault_camera = 2\ncamera_mode = 2\ndiorama_distance = 1800\ndiorama_height = 1000\ndiorama_units_per_meter = 600\nhand_hud = false\nmenu_shader_quality = 1\n");
    Require(imported.vrWelcomeComplete && imported.vrTutorialCompleted == 3);
    Require(imported.vrDefaultCamera == 2 && imported.vrCameraMode == 2);
    Require(imported.vrDioramaDistance == 1800 && imported.vrDioramaHeight == 1000);
    Require(imported.vrDioramaUnitsPerMeter == 600 && !imported.vrHandHud);
    Require(imported.vrMenuShaderQuality == 1);
    const auto bounded = Parse("[vr]\ndiorama_distance = -1\ndiorama_height = 99999\nmenu_shader_quality = 99\n");
    Require(bounded.vrDioramaDistance == 200 && bounded.vrDioramaHeight == 4000);
    Require(bounded.vrMenuShaderQuality == 3);
    // [vr] foveation: the Quest's foveated rendering level, index-matched to
    // aurora_set_stereo_foveation.
    for (std::string_view level : RuntimeConfigFile::kVrFoveationLevels) {
        Require(Parse("[vr]\nfoveation = \"" + std::string(level) + "\"\n").vrFoveation == std::string(level));
    }
    Require(!Parse("[vr]\nfoveation = \"ultra\"\n").vrFoveation.has_value());
    Require(!Parse("[vr]\nfoveation = 2\n").vrFoveation.has_value());
    Require(!Parse("[vr]\n").vrFoveation.has_value());
    Require(std::string_view(RuntimeConfigFile::kVrFoveationDefault) == "off");
    Require(RuntimeConfigFile::VrFoveationLevelIndex("off") == 0);
    Require(RuntimeConfigFile::VrFoveationLevelIndex("low") == 1);
    Require(RuntimeConfigFile::VrFoveationLevelIndex("medium") == 2);
    Require(RuntimeConfigFile::VrFoveationLevelIndex("high") == 3);
    Require(RuntimeConfigFile::VrFoveationLevelIndex("ultra") == 0);

    // [vr] single_pass_eyes: each eye replayed in one render pass.
    Require(Parse("[vr]\nsingle_pass_eyes = true\n").vrSinglePassEyes == true);
    Require(Parse("[vr]\nsingle_pass_eyes = false\n").vrSinglePassEyes == false);
    Require(!Parse("[vr]\n").vrSinglePassEyes.has_value());

    // [vr] immersive_window and flat_screen: one race view in two keys, Flat
    // Screen mode winning, so a file that predates the window reads as before.
    using RuntimeConfigFile::VrRaceView;
    using RuntimeConfigFile::VrRaceViewOf;
    Require(Parse("[vr]\nimmersive_window = true\n").vrImmersiveWindow == true);
    Require(!Parse("[vr]\n").vrImmersiveWindow.has_value());
    Require(VrRaceViewOf(Parse("[vr]\n")) == VrRaceView::Immersive);
    Require(VrRaceViewOf(Parse("[vr]\nflat_screen = false\n")) == VrRaceView::Immersive);
    Require(VrRaceViewOf(Parse("[vr]\nflat_screen = true\n")) == VrRaceView::FlatScreen);
    Require(VrRaceViewOf(Parse("[vr]\nimmersive_window = true\n")) == VrRaceView::ImmersiveWindow);
    Require(VrRaceViewOf(Parse("[vr]\nflat_screen = true\nimmersive_window = true\n")) == VrRaceView::FlatScreen);
    Require(VrRaceViewOf(Parse("[vr]\nflat_screen = false\nimmersive_window = false\n")) == VrRaceView::Immersive);
    Require(!Parse("[controller]\nwii_continuous_scan = true\n").wiiContinuousScan.value_or(false));
    Require(Parse("[controller]\nwii_continuous_scan_opt_in = true\n").wiiContinuousScan.value_or(false));
    Require(Parse("[vr]\nadaptive_resolution = true\n").vrAdaptiveResolution);
    const auto stick=Parse("[vr]\nstick_deadzone = 0.2\nstick_outer = 0.8\nstick_center_x = 0.1\n");
    Require(stick.vrStickCalibration.deadzone == 0.2f && stick.vrStickCalibration.outer == 0.8f);
    Require(stick.vrStickCalibration.center_x == 0.1f);
    Require(!Parse("[vr]\n").vrForceSteamVr);
    Require(Parse("[vr]\nforce_steamvr = true\n").vrForceSteamVr);
    Require(!Parse("[vr]\nforce_steamvr = false\n").vrForceSteamVr);
    return 0;
}
