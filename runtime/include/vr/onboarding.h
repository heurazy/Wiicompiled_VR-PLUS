// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
namespace mkw::vr {
struct UiHandPose {
    std::array<float,3> position{},right{1,0,0},up{0,1,0},forward{0,0,-1};
    bool valid=false;
};
inline std::array<float,3> RotateUiVector(float x,float y,float z,float w,std::array<float,3> v) {
    const std::array<float,3> t{2*(y*v[2]-z*v[1]),2*(z*v[0]-x*v[2]),2*(x*v[1]-y*v[0])};
    return {v[0]+w*t[0]+y*t[2]-z*t[1],v[1]+w*t[1]+z*t[0]-x*t[2],v[2]+w*t[2]+x*t[1]-y*t[0]};
}
inline bool ProjectUiRay(const UiHandPose& pose,float width,float height,float distance,float& u,float& v) {
    const auto finite=[](float f) { uint32_t bits;std::memcpy(&bits,&f,4);return (bits&0x7f800000u)!=0x7f800000u; };
    if(!finite(width)||!finite(height)||!finite(distance)) return false;
    for(int i=0;i<3;++i) if(!finite(pose.position[i])||!finite(pose.forward[i])) return false;
    if(!pose.valid || width<=0 || height<=0 || distance<=0 || pose.forward[2]>=-0.001f) return false;
    const float t=(-distance-pose.position[2])/pose.forward[2];
    if(t<0 || t>20) return false;
    u=.5f+(pose.position[0]+t*pose.forward[0])/width;
    v=.5f-(pose.position[1]+t*pose.forward[1])/height;
    return u>=0 && u<=1 && v>=0 && v<=1;
}
// Placement adapted from CircuitLord's BigWalkVR VrControllerTooltips.Callout.
// See THIRD-PARTY-NOTICES.md. Native C++ rendering replaces Unity components.
inline std::array<float,3> ControllerCalloutAnchor(const UiHandPose& hand,float side) {
    auto p=hand.position;
    for(int i=0;i<3;++i) p[i]+=hand.forward[i]*.08f+hand.up[i]*.06f+hand.right[i]*(side*.09f);
    return p;
}
inline unsigned TutorialBit(bool cockpit) { return cockpit?2u:1u; }
// Own only pauses requested by the VR settings, never an existing player pause.
class SettingsPauseFlow {
public:
    bool requested=false,owned=false,wasOpen=false;
    unsigned attempts=0;
    double lastRequest=-1;
    bool Update(bool race,bool open,bool paused,double now) {
        if(!race) { *this={};return false; }
        if(open && !wasOpen) { requested=false;owned=false;attempts=0;lastRequest=-1; }
        if(paused && requested) owned=true;
        if(!open && wasOpen) { attempts=0;lastRequest=-1; }
        wasOpen=open;
        if(open) {
            if(paused) return false;
        } else {
            if(!owned || !paused) { *this={};return false; }
        }
        if(attempts>=3 || (lastRequest>=0 && now-lastRequest<1)) return false;
        ++attempts;lastRequest=now;
        if(open) requested=true;
        return true;
    }
};
class TutorialFlow {
public:
    enum class Stage { Idle,RequestPause,Showing };
    Stage stage=Stage::Idle;
    unsigned bit=0;
    double eligibleSince=-1,lastRequest=-1;
    unsigned attempts=0;
    bool Update(bool race,bool cockpit,bool paused,unsigned completed,double now) {
        if(!race) { *this={};return false; }
        if(stage==Stage::Showing) return false;
        const unsigned wanted=TutorialBit(cockpit);
        if(completed&wanted) { *this={};return false; }
        if(bit!=wanted) { *this={};bit=wanted; }
        if(stage==Stage::RequestPause && paused) { stage=Stage::Showing;return false; }
        if(paused) { eligibleSince=-1;return false; }
        if(eligibleSince<0) eligibleSince=now;
        if(attempts<8 && now-eligibleSince>=2 && (lastRequest<0 || now-lastRequest>=2)) {
            stage=Stage::RequestPause;lastRequest=now;++attempts;return true;
        }
        return false;
    }
};
}
