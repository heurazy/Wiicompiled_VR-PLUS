// SPDX-License-Identifier: GPL-3.0-or-later
#include "vr/body_ik.h"
#include "vr/body_hand_defaults.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace mkw::vr;
using namespace mkw::vr::body_ik;
static void Check(bool ok) { if (!ok) { std::fprintf(stderr,"body IK invariant failed\n");std::abort(); } }
static bool Near(float a,float b) { return std::abs(a-b)<0.0005f; }
int main() {
    for(uint16_t id:{0,2,3,4,5,6,7,15,16,17,18,19}) Check(IsDrivingLegAnimation(id));
    for(uint16_t id:{1,8,9,10,11,12,13,14,20,21,27,28,40,65535}) Check(!IsDrivingLegAnimation(id));
    LegAnimationGate celebration;
    Check(celebration.AllowNative(false,false,true,1000000000ull,false));
    Check(!celebration.AllowNative(false,false,true,1010000000ull,true));
    Check(!celebration.AllowNative(false,false,false,4000000000ull,true)); // boost already ended
    Check(!celebration.AllowNative(false,false,false,4100000000ull,false)); // blend-out
    Check(celebration.AllowNative(false,false,false,4300000000ull,false));
    Check(celebration.AllowNative(false,false,true,4400000000ull,false));
    LegAnimationGate gate;
    Check(gate.AllowNative(false,false,false,1000000000ull));
    Check(gate.AllowNative(false,false,true,1010000000ull)); // drift
    Check(!gate.AllowNative(false,true,false,1020000000ull)); // MT release
    Check(!gate.AllowNative(false,true,false,1100000000ull)); // reward animation
    Check(!gate.AllowNative(false,false,false,1200000000ull)); // settling
    Check(gate.AllowNative(false,false,false,1400000000ull));
    Check(!gate.AllowNative(false,true,false,1500000000ull));
    Check(gate.AllowNative(false,true,true,1510000000ull)); // next drift during boost
    Check(!gate.AllowNative(true,true,true,1520000000ull));
    Check(!gate.AllowNative(false,true,true,1600000000ull)); // trick hold still applies
    Check(gate.AllowNative(false,true,true,1800000000ull));
    // Turning/drifting leg motion survives, but an animated root's rotation
    // and jump translation do not move the seated pelvis or chest.
    auto seated_parent=kIdentityMtx34;seated_parent[7]=0.4f;
    auto animated_parent=Align({}, {0,1,0},{1,0,0});
    animated_parent[3]=4;animated_parent[7]=8;animated_parent[11]=-2;
    auto leg_local=Align({}, {0,-1,0},{0,-1,-1});leg_local[7]=-0.3f;leg_local[11]=-0.1f;
    const auto animated_leg=ComposeMtx(animated_parent,leg_local);
    Mtx34 seated_leg{};
    Check(AnimateSeatedLimb(seated_parent,animated_parent,animated_leg,seated_leg));
    const auto expected_leg=ComposeMtx(seated_parent,leg_local);
    for(unsigned i=0;i<12;++i) Check(Near(seated_leg[i],expected_leg[i]));
    Check(!AnimateSeatedLimb(seated_parent,Mtx34{},animated_leg,seated_leg));
    const auto defaults=DefaultBodyHandCalibrations();
    Check(defaults.size()==24);
    for(const auto& [key,profile]:defaults) {
        Check(!key.empty() && profile.size()==24);
        for(int hand=0;hand<2;++hand) {
            Mtx34 pose{};
            std::copy_n(profile.begin()+hand*12,12,pose.begin());
            Check(detail::IsFiniteMtx34(pose));
            Check(Length(Position(pose))<0.18f);
            for(int column=0;column<3;++column)
                Check(std::abs(Length({pose[column],pose[column+4],pose[column+8]})-1)<0.002f);
        }
    }
    for (float scale: {0.2f,1.0f,2.5f}) {
        // A close controller must not create the old 65cm folded sleeve.
        const float reach_fit=FitArmReach(0.5f*scale,0.2f*scale,scale);
        Check(Near(0.5f*scale*reach_fit,0.30f*scale));
        const auto close=Solve({}, {0.25f*scale*reach_fit,0,0},
            {0.5f*scale*reach_fit,0,0},{0,0,-0.2f*scale},{-0.7f,-1,0.25f});
        Check(close.valid && Near(close.wrist.z,-0.2f*scale));
        Check(Length(close.elbow)<0.16f*scale);
        const auto extended=FitArmReach(0.5f*scale,0.8f*scale,scale);
        Check(0.5f*scale*extended>0.8f*scale);
        const Vec s{0,0,0}, e{scale,0,0}, w{2*scale,0,0};
        for (Vec target: {Vec{scale,scale,0},Vec{100,0,0},Vec{0,0,0},Vec{-scale,0,0}}) {
            const auto arm=Solve(s,e,w,target,{0,-1,0});
            Check(arm.valid);
            Check(Near(Length(Sub(arm.elbow,s)),scale));
            Check(Near(Length(Sub(arm.wrist,arm.elbow)),scale));
            Check(detail::IsFiniteFloat(&arm.elbow.x));
        }
    }
    Check(!Solve({},{},{},{1,0,0},{0,-1,0}).valid);
    Check(!Solve({},{1,0,0},{2,0,0},{std::numeric_limits<float>::quiet_NaN(),0,0},{0,-1,0}).valid);
    const Vec pivot{4,5,6};
    for(Vec direction: {Vec{1,0,0},Vec{-1,0,0},Vec{0,0,-1},Vec{0,1,0}}) {
        auto grip=Align({}, {1,0,0}, direction);
        grip[3]=0.3f;grip[7]=-0.2f;grip[11]=-0.4f;
        const Vec expected{0.015f,-0.03f,0.055f};
        const auto wrist=detail::TransformPoint(grip,expected.x,expected.y,expected.z);
        auto wrist_pose=Align({}, {0,1,0}, direction);
        wrist_pose[3]=wrist.x;wrist_pose[7]=wrist.y;wrist_pose[11]=wrist.z;
        Mtx34 correction{};
        Check(CalibrateGripPose(grip,wrist_pose,correction));
        const auto restored=ComposeMtx(grip,correction);
        for(unsigned i=0;i<12;++i) Check(Near(restored[i],wrist_pose[i]));
        Vec captured{};
        Check(CalibrateGripOffset(grip,wrist,captured));
        Check(Near(captured.x,expected.x) && Near(captured.y,expected.y) && Near(captured.z,expected.z));
        // A saved controller-local offset follows a new orientation and pose.
        auto moved=Align({}, {1,0,0}, {-direction.x,-direction.y,-direction.z});
        moved[3]=-0.5f;moved[7]=0.1f;moved[11]=-0.7f;
        const auto applied=detail::TransformPoint(moved,captured.x,captured.y,captured.z);
        Check(Near(Length(Sub(applied,Position(moved))),Length(expected)));
    }
    Vec rejected{};
    Check(!CalibrateGripOffset(kIdentityMtx34,{1,0,0},rejected));
    Check(!CalibrateGripOffset(Mtx34{}, {},rejected));
    for(float scale: {0.2f,1.0f,2.5f}) {
        for(Vec direction: {Vec{1,0,0},Vec{-1,0,0},Vec{0,0,-1},Vec{0,1,0}}) {
            const auto rotation=Align({}, {1,0,0}, direction);
            const auto wrist=WristForPalm(pivot,rotation,{1,0,0},0.055f*scale);
            const auto palm=Add(wrist,Mul(direction,0.055f*scale));
            Check(Near(palm.x,pivot.x) && Near(palm.y,pivot.y) && Near(palm.z,pivot.z));
        }
    }
    const auto fit=MapSegment(pivot,{1,2,3},{1,0,0},{0,2,0});
    const auto endpoint=detail::TransformPoint(fit,5,5,6);
    Check(Near(endpoint.x,1) && Near(endpoint.y,4) && Near(endpoint.z,3));
    Check(Near(Length(detail::TransformDirection(fit,{0,0,1})),1));
    const auto thin=MapSegment(pivot,{1,2,3},{1,0,0},{0,2,0},0.65f);
    const auto thin_endpoint=detail::TransformPoint(thin,5,5,6);
    Check(Near(thin_endpoint.x,endpoint.x) && Near(thin_endpoint.y,endpoint.y) && Near(thin_endpoint.z,endpoint.z));
    Check(Near(Length(detail::TransformDirection(thin,{0,0,1})),0.65f));
    for (Vec to: {Vec{0,1,0},Vec{-1,0,0},Vec{1,0,0}}) {
        const auto m=Align(pivot,{1,0,0},to);
        const auto p=detail::TransformPoint(m,pivot.x,pivot.y,pivot.z);
        const auto d=detail::TransformDirection(m,{1,0,0});
        Check(Near(p.x,pivot.x) && Near(p.y,pivot.y) && Near(p.z,pivot.z));
        Check(Near(d.x,to.x) && Near(d.y,to.y) && Near(d.z,to.z));
    }
    std::puts("body IK: reach, folded arms, lightning scale and joint rotation passed");
}
