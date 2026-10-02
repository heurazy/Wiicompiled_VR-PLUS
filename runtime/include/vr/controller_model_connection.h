// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mkw::vr {
// Model loading is optional. Only borrow an existing SteamVR client; never
// initialize a second VR client or repeatedly query an unavailable one.
class ControllerModelConnection {
    bool attempted_ = false;
    bool connected_ = false;
public:
    template<class QueryExisting>
    bool Connect(bool steamvr, QueryExisting query) {
        if (!steamvr) return false;
        if (!attempted_) {
            attempted_ = true;
            // SDL/the OpenXR runtime may already own this process's client.
            // Reinitializing it can invalidate its interfaces and input state.
            connected_ = query();
        }
        return connected_;
    }
};
} // namespace mkw::vr
