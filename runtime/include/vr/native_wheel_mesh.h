// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from heurazy's mario-kart-wii-VR-port (GPL-3.0-or-later).
#pragma once
#include "vr/mkw_vr_first_person.h"
#include "vr/native_wheel_topology.h"
#include <vector>

namespace mkw::vr {

// Hand targets locate the wheel, but are not its centre/radius: Daisy holds
// the same Standard Kart higher than Mario, and Baby Mario grips inside the
// rim. Identify a complete rim component and fit its own plane and bounds.
// Rotate whole connected pieces only, so neither a rim nor a chassis triangle
// can stretch across the selection boundary. Work on a render copy.
inline unsigned RotateNativeWheelVertices(std::vector<detail::Vec3> &points, NativeWheelTopology &topology,
                                          detail::Vec3 gripCenter, float gripRadius, float angle,
                                          const Mtx34 *bodyCorrection = nullptr,
                                          const Mtx34 &bodyFromVertices = kIdentityMtx34) {
    if (!(gripRadius > 4 && gripRadius < 100) || !detail::IsFiniteFloat(&angle) ||
        topology.parents.size() != points.size() || topology.used.size() != points.size() ||
        topology.rootOwned.size() != points.size())
        return 0;
    if (bodyCorrection && !detail::IsFiniteMtx34(*bodyCorrection))
        return 0;
    // Some karts (Baby Booster) author the body in rotated bone coordinates.
    // Fit/turn in the kart frame, then convert only selected vertices back.
    Mtx34 verticesFromBody;
    if (!detail::IsFiniteMtx34(bodyFromVertices) || !InvertMtx(bodyFromVertices, verticesFromBody))
        return 0;
    auto bodyPoints = points;
    for (auto &p : bodyPoints)
        p = detail::TransformPoint(bodyFromVertices, p.x, p.y, p.z);
    struct Piece {
        detail::Vec3 min{INFINITY, INFINITY, INFINITY}, max{-INFINITY, -INFINITY, -INFINITY};
        float sumY = 0, sumZ = 0;
        unsigned count = 0;
        bool selected = true;
    };
    std::vector<Piece> pieces(points.size());
    for (uint32_t i = 0; i < points.size(); ++i)
        if (topology.used[i]) {
            const auto &p = bodyPoints[i];
            auto &piece = pieces[topology.Root(i)];
            piece.min = {std::min(piece.min.x, p.x), std::min(piece.min.y, p.y), std::min(piece.min.z, p.z)};
            piece.max = {std::max(piece.max.x, p.x), std::max(piece.max.y, p.y), std::max(piece.max.z, p.z)};
            piece.sumY += p.y;
            piece.sumZ += p.z;
            ++piece.count;
            if (!topology.rootOwned[i])
                piece.selected = false;
        }
    uint32_t rim = uint32_t(points.size());
    float bestScore = INFINITY, rimSlope = 0, rimRadius = 0;
    detail::Vec3 center{};
    for (uint32_t component = 0; component < pieces.size(); ++component) {
        const auto &piece = pieces[component];
        const float radius = (piece.max.x - piece.min.x) * 0.5f;
        const detail::Vec3 mid{(piece.min.x + piece.max.x) * 0.5f, (piece.min.y + piece.max.y) * 0.5f, 0};
        if (!piece.selected || piece.count < 8 || radius < gripRadius * 0.65f || radius > gripRadius * 2.2f ||
            std::abs(mid.x - gripCenter.x) > gripRadius * 0.35f || std::abs(mid.y - gripCenter.y) > gripRadius * 1.5f)
            continue;
        const float meanY = piece.sumY / piece.count, meanZ = piece.sumZ / piece.count;
        float yy = 0, yz = 0;
        for (uint32_t i = 0; i < points.size(); ++i)
            if (topology.used[i] && topology.Root(i) == component) {
                yy += (bodyPoints[i].y - meanY) * (bodyPoints[i].y - meanY);
                yz += (bodyPoints[i].y - meanY) * (bodyPoints[i].z - meanZ);
            }
        if (yy < radius * radius)
            continue;
        const float slope = yz / yy;
        if (std::abs(slope) > 1.0f)
            continue;
        const float inv = 1.0f / std::sqrt(1.0f + slope * slope);
        const float height = (piece.max.y - piece.min.y) / inv;
        const float z = meanZ + slope * (mid.y - meanY);
        if (height < radius * 1.3f || height > radius * 2.6f || std::abs(z - gripCenter.z) > gripRadius)
            continue;
        bool planar = true;
        for (uint32_t i = 0; i < points.size(); ++i)
            if (topology.used[i] && topology.Root(i) == component) {
                if (std::abs((bodyPoints[i].z - meanZ - slope * (bodyPoints[i].y - meanY)) * inv) > radius * 0.3f)
                    planar = false;
            }
        if (!planar)
            continue;
        // Prefer the enclosing rim over the smaller spoke assembly.
        const float score = -radius;
        if (score >= bestScore)
            continue;
        bestScore = score;
        rim = component;
        rimSlope = slope;
        rimRadius = std::max(radius, height * 0.5f);
        center = {mid.x, mid.y, z};
    }
    if (rim == points.size())
        return 0;
    const float inv = 1.0f / std::sqrt(1.0f + rimSlope * rimSlope);
    const detail::Vec3 up{0, inv, rimSlope * inv}, normal{0, -rimSlope * inv, inv};
    for (uint32_t i = 0; i < points.size(); ++i)
        if (topology.used[i]) {
            const auto &p = bodyPoints[i];
            const detail::Vec3 delta{p.x - center.x, p.y - center.y, p.z - center.z};
            const float y = detail::Dot(delta, up), z = detail::Dot(delta, normal);
            // Domed hubs (Royal Racer) protrude further than the rim's thin slab.
            if (delta.x * delta.x + y * y > rimRadius * rimRadius * 1.21f || std::abs(z) > rimRadius * 0.45f)
                pieces[topology.Root(i)].selected = false;
        }
    // The entire rim is selected even if its polygonal corners exceed a circle.
    pieces[rim].selected = true;
    const float c = std::cos(angle), s = std::sin(angle);
    unsigned changed = 0;
    for (uint32_t i = 0; i < points.size(); ++i)
        if (topology.used[i] && pieces[topology.Root(i)].selected) {
            auto p = bodyPoints[i];
            const detail::Vec3 delta{p.x - center.x, p.y - center.y, p.z - center.z};
            const float y = detail::Dot(delta, up), z = detail::Dot(delta, normal);
            const float rx = c * delta.x - s * y, ry = s * delta.x + c * y;
            p = {center.x + rx, center.y + up.y * ry + normal.y * z, center.z + up.z * ry + normal.z * z};
            if (bodyCorrection)
                p = detail::TransformPoint(*bodyCorrection, p.x, p.y, p.z);
            points[i] = detail::TransformPoint(verticesFromBody, p.x, p.y, p.z);
            ++changed;
        }
    return changed;
}
} // namespace mkw::vr
