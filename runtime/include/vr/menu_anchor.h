#pragma once
#include <array>
#include <cmath>

namespace mkw::vr {
struct UprightMenuPose {
    std::array<float,4> orientation{0,0,0,1};
    std::array<float,3> position{};
};
inline bool StartupReferenceInverted(std::array<float,4> q) noexcept {
    const float n=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];
    // A headset worn upright cannot point its local up axis at the floor.
    // Only repair this gross startup inversion, never ordinary head tilt.
    return n>0.00001f && 1-2*(q[0]*q[0]+q[2]*q[2])/n < -.5f;
}
// OpenXR quaternion order XYZW. A screen inherits heading and eye height,
// never pitch/roll or the floor height of the reference space.
inline UprightMenuPose UprightMenuAhead(std::array<float,4> q,
                                      std::array<float,3> eye, float distance) noexcept {
    const float norm=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];
    if (norm>0.00001f) for(auto& v:q) v/=std::sqrt(norm);
    else q={0,0,0,1};
    const float yaw=std::atan2(2*(q[0]*q[2]+q[3]*q[1]),1-2*(q[0]*q[0]+q[1]*q[1]));
    return {{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},
            {eye[0]-std::sin(yaw)*distance,eye[1],eye[2]-std::cos(yaw)*distance}};
}
// Normalize the PC runtime's initial room origin to the tracked seat. Normal
// head pitch/roll must not become the world's horizon.
inline UprightMenuPose StartupMenuOrigin(std::array<float,4> q,
                                        std::array<float,3> eye) noexcept {
    auto origin=UprightMenuAhead(q,eye,0.f);
    if(StartupReferenceInverted(q)) {
        const float norm=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
        for(auto& v:q) v/=norm;
        origin.orientation=q;
    }
    return origin;
}
}
