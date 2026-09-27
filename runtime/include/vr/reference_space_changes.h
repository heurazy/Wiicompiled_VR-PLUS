#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
namespace mkw::vr {
// Preserve future runtime events and distinguish them from our own origin
// replacement. Otherwise applying a repair can immediately undo itself.
template<class Change>
bool ConsumeReferenceSpaceChanges(std::vector<Change>& pending,int64_t display_time,
                                 bool* external=nullptr) {
    bool consumed=false;
    if(external) *external=false;
    std::erase_if(pending,[&](const Change& change) {
        const bool due=change.change_time==0 || display_time>=change.change_time;
        consumed |= due;
        if(external && due && change.external) *external=true;
        return due;
    });
    return consumed;
}
}
