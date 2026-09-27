#pragma once
#include "vr/quest_input.h"

namespace mkw::vr {
// Hold ownership until trigger release; a tracking loss must not transfer a click.
class GameMenuPointer {
    bool left_=false,held_=false,armed_=false;
public:
    struct Result { bool valid=false,down=false;float x=0,y=0; };
    Result Update(const QuestInput& input,bool enabled,float width,float height,float distance) {
        const bool leftDown=input.reverse,rightDown=input.accelerate>.55f;
        if(!enabled || !input.active) { held_=false;armed_=false;return {}; }
        if(!leftDown && !rightDown) armed_=true;
        float ru=0,rv=0,lu=0,lv=0;
        const bool right=ProjectUiRay(input.ui_pointer,width,height,distance,ru,rv);
        const bool left=ProjectUiRay(input.ui_left_pointer,width,height,distance,lu,lv);
        if(!held_) {
            if(left && leftDown) left_=true;
            else if(right && rightDown) left_=false;
            else if(left_?!left:!right) left_=left;
        }
        held_=left_?leftDown:rightDown;
        const bool hit=left_?left:right;
        // KPAD's normalized screen position: x right, y down, centre at zero.
        return {hit,hit && held_ && armed_,2*(left_?lu:ru)-1,2*(left_?lv:rv)-1};
    }
};
}
