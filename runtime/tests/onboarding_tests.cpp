#include "vr/onboarding.h"
#include <iostream>
#include <limits>
using namespace mkw::vr;
int main() {
    int failures=0;
    const auto check=[&](bool result,const char* message) { if(!result) { ++failures;std::cerr<<message<<'\n'; } };
    UiHandPose ray;ray.valid=true;float u=0,v=0;
    check(ProjectUiRay(ray,2,1,2,u,v)&&u==.5f&&v==.5f,"forward ray hits centre");
    ray.position={1,.5f,0};
    check(ProjectUiRay(ray,2,1,2,u,v)&&u==1&&v==0,"ray maps top right correctly");
    ray.position[0]=1.1f;check(!ProjectUiRay(ray,2,1,2,u,v),"outside panel rejected");
    ray={};ray.valid=true;ray.forward={0,0,1};check(!ProjectUiRay(ray,2,1,2,u,v),"backward ray rejected");
    ray.forward={0,0,-1};ray.position[0]=std::numeric_limits<float>::quiet_NaN();
    check(!ProjectUiRay(ray,2,1,2,u,v),"invalid tracking rejected");
    for(bool first : {false,true}) {
        TutorialFlow flow;
        check(!flow.Update(true,first,false,0,0),"guide waits for playable race");
        check(flow.Update(true,first,false,0,2),"first active camera requests pause");
        check(flow.stage!=TutorialFlow::Stage::Showing,"guide cannot show before pause acknowledgement");
        check(!flow.Update(true,first,true,0,2.1)&&flow.stage==TutorialFlow::Stage::Showing,"actual pause opens guide");
        const unsigned completed=flow.bit;flow={};
        check(!flow.Update(true,first,false,completed,3),"seen controls do not repeat");
        flow.Update(true,!first,false,completed,4);
        check(flow.Update(true,!first,false,completed,6),"other driving mode requests its own guide");
        flow.Update(false,!first,false,completed,7);
        check(flow.stage==TutorialFlow::Stage::Idle,"race exit resets pending tutorial");
    }
    TutorialFlow flow;
    check(!flow.Update(true,false,true,0,0),"existing player pause is not claimed by tutorial");
    int requests=0;
    for(int i=1;i<=60;++i) requests+=flow.Update(true,false,false,0,i);
    check(requests==8&&flow.stage!=TutorialFlow::Stage::Showing,"unpausable session has bounded retries and no blocking guide");
    check(TutorialBit(false)==1&&TutorialBit(true)==2,"third person/diorama and cockpit have independent persistence bits");
    SettingsPauseFlow settings;
    check(!settings.Update(false,true,false,0),"menus never inject a race pause");
    check(settings.Update(true,true,false,1),"opening settings requests race pause");
    check(!settings.Update(true,true,false,1.1),"pause request is not repeated every frame");
    check(!settings.Update(true,true,true,1.2)&&settings.owned,"pause acknowledged and owned");
    check(!settings.Update(true,true,true,2),"open settings keep race paused");
    check(settings.Update(true,false,true,3),"closing settings resumes its own pause");
    check(!settings.Update(true,false,false,3.1)&&!settings.owned,"resume acknowledgement clears ownership");
    check(!settings.Update(true,true,true,4),"already paused race remains paused");
    check(!settings.Update(true,false,true,5),"closing settings never resumes player-owned pause");
    settings={};requests=0;
    for(int i=0;i<20;++i) requests+=settings.Update(true,true,false,i);
    check(requests==3,"unpausable online race does not receive endless pause requests");
    settings={};settings.Update(true,true,false,0);
    check(settings.Update(true,false,true,.1),"closing while pause acknowledgement arrives still resumes");
    check(!settings.Update(false,false,true,.2)&&!settings.owned,"leaving race cancels ownership");
    std::cout<<(failures?"FAIL":"PASS")<<": onboarding\n";
    return failures?1:0;
}
