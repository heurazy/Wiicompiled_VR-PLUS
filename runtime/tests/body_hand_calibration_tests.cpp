// SPDX-License-Identifier: GPL-3.0-or-later
#include "vr/openxr_driving.h"
#include <cstdio>
#include <cstdlib>
using namespace mkw::vr;
static void Check(bool value) { if(!value) {std::fputs("hand calibration state failed\n",stderr);std::abort();} }
int main() {
    Check(!OpenXRBeginBodyHandCalibration());
    DrivingSnapshot tracking{};tracking.cockpit_active=true;tracking.vehicle_identity=123;
    OpenXRPublishDriving(tracking);
    auto pose=tracking.hands[0].seat_from_grip;
    pose[3]=0.2f;
    OpenXRPublishBodyHandPose(123,"mario_body",0,pose);
    Check(!OpenXRBeginBodyHandCalibration()); // both hands required
    OpenXRPublishBodyHandPose(123,"mario_body",1,pose);
    Check(OpenXRBeginBodyHandCalibration());
    const auto frozen=OpenXRReadBodyHandCalibration().seat_from_wrist;
    pose[3]=0.8f;
    OpenXRPublishBodyHandPose(123,"mario_body",0,pose);
    Check(OpenXRReadBodyHandCalibration().seat_from_wrist==frozen);
    OpenXRMarkBodyHandCaptured(0);
    Check(OpenXRReadBodyHandCalibration().captured[0]);
    OpenXREndBodyHandCalibration();
    OpenXRMarkBodyHandCaptured(1);
    Check(!OpenXRReadBodyHandCalibration().captured[1]);
    OpenXRPublishBodyHandPose(123,"mario_body",0,pose);
    Check(OpenXRReadBodyHandCalibration().seat_from_wrist[0][3]==0.8f);
    Check(OpenXRBeginBodyHandCalibration());
    OpenXRPublishBodyHandPose(123,"donkey_body",0,pose);
    Check(!OpenXRReadBodyHandCalibration().active);
    Check(!OpenXRBeginBodyHandCalibration()); // changed character needs both new poses
    OpenXRPublishBodyHandPose(123,"donkey_body",1,pose);
    Check(OpenXRBeginBodyHandCalibration());
    OpenXRPublishDriving({}); // tracking/session loss cancels, no stale capture
    Check(!OpenXRReadBodyHandCalibration().active);
    Check(!OpenXRBeginBodyHandCalibration());
    std::puts("hand calibration: frozen poses, capture, cancel and tracking loss passed");
    OpenXRSetHandWorkshopActive(true);
    Check(OpenXRHandWorkshopActive());
    HandWorkshopTracking workshop{};workshop.tracked={true,true};
    workshop.panel_from_grip[0][3]=-.23f;
    OpenXRPublishHandWorkshopTracking(workshop);
    const auto published=OpenXRReadHandWorkshopTracking();
    Check(published.tracked[0] && published.tracked[1] && published.published_ns);
    Check(published.panel_from_grip[0][3]==-.23f);
    OpenXRPublishHandWorkshopTracking({});
    Check(!OpenXRReadHandWorkshopTracking().tracked[0]);
    OpenXRSetHandWorkshopActive(false);
    Check(!OpenXRHandWorkshopActive());
}
