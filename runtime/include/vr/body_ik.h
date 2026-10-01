// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "vr/mkw_vr_first_person.h"

namespace mkw::vr::body_ik {
using Vec = detail::Vec3;
inline Vec Add(Vec a, Vec b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec Sub(Vec a, Vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec Mul(Vec a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Length(Vec a) { return std::sqrt(detail::Dot(a,a)); }
inline Vec Position(const Mtx34& m) { return {m[3],m[7],m[11]}; }
// Controller grip tracks the palm, not the character's wrist joint. Rotate
// the wrist-to-palm offset with the hand so it works in every orientation.
inline Vec WristForPalm(Vec palm, const Mtx34& hand, Vec palm_axis, float distance) {
    auto axis=detail::TransformDirection(hand,palm_axis);
    if(!detail::Normalize(axis)) return palm;
    return Sub(palm,Mul(axis,distance));
}
inline bool CalibrateGripOffset(const Mtx34& grip, Vec wrist, Vec& offset) {
    Mtx34 inverse{};
    if(!InvertMtx(grip,inverse)) return false;
    offset=detail::TransformPoint(inverse,wrist.x,wrist.y,wrist.z);
    return detail::IsFiniteFloat(&offset.x) && detail::IsFiniteFloat(&offset.y) &&
           detail::IsFiniteFloat(&offset.z) && Length(offset)<=0.3f;
}
inline bool CalibrateGripPose(const Mtx34& grip, const Mtx34& wrist, Mtx34& correction) {
    Mtx34 inverse{};
    if(!detail::IsFiniteMtx34(wrist) || !InvertMtx(grip,inverse)) return false;
    correction=ComposeMtx(inverse,wrist);
    return detail::IsFiniteMtx34(correction) && Length(Position(correction))<=0.3f;
}
struct Arm { Vec elbow{}, wrist{}; bool valid=false; };

// Driver animation IDs from the native 24-byte animation table: drive=0,
// dash=1, drift_l/r=2/3, wheelie=4, wait/back=5..7, steering=15..19.
// Both driver animation layers must be driving poses before exposing legs.
inline bool IsDrivingLegAnimation(uint16_t animation) {
    return animation==0 || (animation>=2 && animation<=7) ||
        (animation>=15 && animation<=19);
}

struct LegAnimationGate {
    uint64_t trick_hold_until=0, turbo_hold_until=0;
    bool AllowNative(bool trick, bool mini_turbo, bool drifting, uint64_t now,
                     bool character_action=false) {
        // A new drift may start before the previous boost finishes. Keep its
        // leg articulation usable, but do not release a trick hold early.
        if(trick) trick_hold_until=now+250000000ull;
        if(character_action || (mini_turbo && !drifting)) turbo_hold_until=now+250000000ull;
        return !trick && !character_action && now>=trick_hold_until &&
            (drifting || (!mini_turbo && now>=turbo_hold_until));
    }
};

// Keep a little bend without forcing a long, tightly folded arm when the
// controller is close to the chest. Distances scale with the character/world.
inline float FitArmReach(float natural, float requested, float units_per_meter) {
    if(natural<0.001f || units_per_meter<=0) return 1.0f;
    return std::clamp(std::max(0.30f*units_per_meter,requested*1.04f)/natural,0.25f,3.5f);
}

// Keep joint articulation relative to a stable seated parent. Native root
// translation/rotation (drift lean or a rider jump) cannot move the torso.
inline bool AnimateSeatedLimb(const Mtx34& seated_parent, const Mtx34& animated_parent,
                              const Mtx34& animated_joint, Mtx34& result) {
    Mtx34 inverse{};
    if(!InvertMtx(animated_parent,inverse)) return false;
    result=ComposeMtx(seated_parent,ComposeMtx(inverse,animated_joint));
    return detail::IsFiniteMtx34(result);
}

// Analytic two-bone IK, with a stable elbow pole and no bone stretching.
// All positions and lengths share the character's current scaled world units.
inline Arm Solve(Vec shoulder, Vec elbow, Vec wrist, Vec target, Vec pole) {
    const float upper=Length(Sub(elbow,shoulder)), lower=Length(Sub(wrist,elbow));
    if (!detail::IsFiniteFloat(&upper) || !detail::IsFiniteFloat(&lower) || upper<0.001f || lower<0.001f)
        return {};
    Vec axis=Sub(target,shoulder);
    float distance=Length(axis);
    if (!detail::IsFiniteFloat(&distance)) return {};
    if (!detail::Normalize(axis)) { axis=Sub(wrist,shoulder); if (!detail::Normalize(axis)) return {}; }
    const float epsilon=std::min(upper,lower)*0.001f;
    distance=std::clamp(distance,std::abs(upper-lower)+epsilon,upper+lower-epsilon);
    Vec bend=Sub(pole,Mul(axis,detail::Dot(pole,axis)));
    if (!detail::Normalize(bend)) {
        bend=Sub(Sub(elbow,shoulder),Mul(axis,detail::Dot(Sub(elbow,shoulder),axis)));
        if (!detail::Normalize(bend)) {
            bend=detail::Cross(axis,{0,1,0});
            if (!detail::Normalize(bend)) { bend=detail::Cross(axis,{1,0,0}); detail::Normalize(bend); }
        }
    }
    const float along=(upper*upper-lower*lower+distance*distance)/(2*distance);
    const float height=std::sqrt(std::max(0.0f,upper*upper-along*along));
    return {Add(shoulder,Add(Mul(axis,along),Mul(bend,height))),Add(shoulder,Mul(axis,distance)),true};
}

// Rotate about a joint to align its old segment to the solved one. Opposite
// vectors use a perpendicular axis rather than an undefined cross product.
inline Mtx34 Align(Vec origin, Vec from, Vec to) {
    if (!detail::Normalize(from) || !detail::Normalize(to)) return kIdentityMtx34;
    const float c=std::clamp(detail::Dot(from,to),-1.0f,1.0f);
    Vec axis=detail::Cross(from,to);
    float s=Length(axis);
    if (s<1e-5f) {
        if (c>0) return kIdentityMtx34;
        axis=detail::Cross(from,{0,1,0});
        if (!detail::Normalize(axis)) { axis=detail::Cross(from,{1,0,0}); detail::Normalize(axis); }
        s=0;
    } else axis=Mul(axis,1/s);
    const float x=axis.x,y=axis.y,z=axis.z,k=1-c;
    Mtx34 m{x*x*k+c,x*y*k-z*s,x*z*k+y*s,0,
            y*x*k+z*s,y*y*k+c,y*z*k-x*s,0,
            z*x*k-y*s,z*y*k+x*s,z*z*k+c,0};
    const auto rotated=detail::TransformDirection(m,origin);
    m[3]=origin.x-rotated.x;m[7]=origin.y-rotated.y;m[11]=origin.z-rotated.z;
    return m;
}

// Fit a skinned limb to a new segment without widening its cross section.
inline Mtx34 MapSegment(Vec old_origin, Vec new_origin, Vec old_segment, Vec new_segment,
                       float cross_section_scale=1.0f) {
    const float old_length=Length(old_segment), new_length=Length(new_segment);
    if (old_length<0.0001f || new_length<0.0001f) return kIdentityMtx34;
    Vec axis=Mul(old_segment,1/old_length);
    const float k=new_length/old_length-cross_section_scale;
    Mtx34 stretch=kIdentityMtx34;
    stretch[0]=stretch[5]=stretch[10]=cross_section_scale;
    const float v[3]{axis.x,axis.y,axis.z};
    for(int row=0;row<3;++row) for(int col=0;col<3;++col) stretch[row*4+col]+=k*v[row]*v[col];
    const auto shifted=detail::TransformDirection(stretch,old_origin);
    stretch[3]=old_origin.x-shifted.x;stretch[7]=old_origin.y-shifted.y;stretch[11]=old_origin.z-shifted.z;
    auto result=ComposeMtx(Align(old_origin,old_segment,new_segment),stretch);
    const auto delta=Sub(new_origin,old_origin);
    result[3]+=delta.x;result[7]+=delta.y;result[11]+=delta.z;
    return result;
}
} // namespace mkw::vr::body_ik
