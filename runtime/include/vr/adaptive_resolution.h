#pragma once
#include <algorithm>
namespace mkw::vr {
// Conservative, slow steps avoid reallocating eye targets on individual spikes.
class AdaptiveResolution {
    float scale_=1;
    unsigned goodWindows_=0;
public:
    float Scale() const { return scale_; }
    float Observe(float newFps,float targetHz,bool enabled) {
        if(!enabled) { scale_=1;goodWindows_=0;return scale_; }
        if(targetHz<=0||newFps<=0) return scale_;
        if(newFps<targetHz*.85f) { scale_=std::max(.7f,scale_-.1f);goodWindows_=0; }
        else if(newFps>targetHz*.97f) {
            if(++goodWindows_>=3) { scale_=std::min(1.0f,scale_+.1f);goodWindows_=0; }
        } else goodWindows_=0;
        return scale_;
    }
};
}
