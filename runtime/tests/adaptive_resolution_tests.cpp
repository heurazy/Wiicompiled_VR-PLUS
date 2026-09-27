// SPDX-License-Identifier: GPL-3.0-or-later
#include "vr/adaptive_resolution.h"
#include <cmath>
#include <cstdlib>
using namespace mkw::vr;
int main() {
    AdaptiveResolution scale;
    auto check=[](bool ok){if(!ok)std::abort();};
    check(scale.Observe(60,90,true)<1);
    for(int i=0;i<10;++i) scale.Observe(60,90,true);
    check(std::abs(scale.Scale()-.7f)<.001f);
    const auto low=scale.Scale();
    scale.Observe(90,90,true);scale.Observe(90,90,true);
    check(scale.Scale()==low);
    check(scale.Observe(90,90,true)>low);
    check(scale.Observe(0,90,true)<1);
    check(scale.Observe(0,90,false)==1);
    check(scale.Observe(60,60,true)==1); // native 60 Hz rendering is healthy without interpolation
}
