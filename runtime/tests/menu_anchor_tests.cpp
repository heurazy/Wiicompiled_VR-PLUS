#include "vr/menu_anchor.h"
#include "vr/reference_space_changes.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
void near(float a,float b) { if(std::fabs(a-b)>.0001f) std::abort(); }
int main() {
    struct Change { int64_t change_time; bool external; };
    std::vector<Change> changes{{0,false},{200,true},{300,true}};
    bool external=true;
    if(!mkw::vr::ConsumeReferenceSpaceChanges(changes,100,&external) || external || changes.size()!=2)
        std::abort(); // Applying our startup origin cannot undo its repair.
    if(mkw::vr::ConsumeReferenceSpaceChanges(changes,150,&external) || external) std::abort();
    if(!mkw::vr::ConsumeReferenceSpaceChanges(changes,200,&external) || !external || changes.size()!=1)
        std::abort(); // External recenter clears the startup correction only when due.
    changes.push_back({0,false});
    if(!mkw::vr::ConsumeReferenceSpaceChanges(changes,300,&external) || !external || !changes.empty())
        std::abort(); // Mixed self/runtime events must not hide a runtime recenter.
    if(!mkw::vr::StartupReferenceInverted({0,0,1,0}) ||
       mkw::vr::StartupReferenceInverted({0,.7071068f,0,.7071068f}) ||
       mkw::vr::StartupReferenceInverted({.3f,0,0,.953939f})) std::abort();
    const std::array<float,3> eye{3,1.7f,8};
    const auto origin=mkw::vr::StartupMenuOrigin({0,0,0,1},eye);
    for(int axis=0;axis<3;++axis) near(origin.position[axis],eye[axis]);
    near(origin.orientation[3],1);
    // A head pitched down starts at eye height without tilting the world's up.
    const auto tilted=mkw::vr::StartupMenuOrigin({.34202014f,0,0,.93969262f},eye);
    near(tilted.orientation[0],0); near(tilted.orientation[3],1);
    const auto inverted=mkw::vr::StartupMenuOrigin({0,0,2,0},eye);
    near(inverted.orientation[2],1); near(inverted.position[1],1.7f);
    for(float yaw:{0.f,1.5707963f,3.1415926f,-1.5707963f}) {
        const float pitch=.7f,roll=.6f;
        // yaw * pitch * roll, matching a freely oriented headset.
        const float sy=std::sin(yaw/2),cy=std::cos(yaw/2),
          sp=std::sin(pitch/2),cp=std::cos(pitch/2),sr=std::sin(roll/2),cr=std::cos(roll/2);
        const auto p=mkw::vr::UprightMenuAhead({cy*sp*cr+sy*cp*sr,sy*cp*cr-cy*sp*sr,
          cy*cp*sr-sy*sp*cr,cy*cp*cr+sy*sp*sr},eye,2);
        near(p.position[0],eye[0]-2*std::sin(yaw));
        near(p.position[1],eye[1]);near(p.position[2],eye[2]-2*std::cos(yaw));
        near(p.orientation[0],0);near(p.orientation[2],0);
    }
    const auto p=mkw::vr::UprightMenuAhead({0,0,0,0},eye,2);
    near(p.position[2],6);near(p.orientation[3],1);
    std::cout<<"Upright menu anchor tests passed\n";
}
