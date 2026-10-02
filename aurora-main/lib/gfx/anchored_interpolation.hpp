#pragma once
#include "stereo_replay.hpp"

namespace aurora::gfx::stereo_replay {
template<class Interpolate>
inline Mat3x4<float> interpolate_anchored_transform(
    const Mat3x4<float>& previous, const Mat3x4<float>& current,
    const Mat3x4<float>& previousAnchor, const Mat3x4<float>& currentAnchor,
    float weight, bool normal, Interpolate interpolate) noexcept {
  const auto before = normal ? compose_normal(previousAnchor, previous) : compose_affine(previousAnchor, previous);
  const auto after = normal ? compose_normal(currentAnchor, current) : compose_affine(currentAnchor, current);
  auto result = after;
  if (weight < 1.f && interpolate(before, after, weight, result)) return result;
  return after;
}
} // namespace aurora::gfx::stereo_replay
