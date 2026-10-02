#include "stereo_interpolation.hpp"
#include "gfx/anchored_interpolation.hpp"
#include "gx/frame_interpolation.hpp"
#include <gtest/gtest.h>
#include <cmath>

TEST(StereoInterpolation, ContinuousMotionAcross60HzScenesAtHeadsetRates) {
  constexpr uint64_t interval = 16'666'667;
  // A camera/object moving one unit per guest frame must advance uniformly,
  // even at 72/90 Hz where many samples are neither midpoints nor endpoints.
  for (uint64_t hz : {72u, 90u, 120u, 144u}) {
    double previousPosition = -1;
    for (uint64_t sample = 1; sample <= hz; ++sample) {
      const uint64_t displayTime = 1'000'000'000 + sample * 1'000'000'000 / hz;
      const uint64_t scene = (displayTime - 1'000'000'000) / interval;
      const uint64_t boundary = 1'000'000'000 + scene * interval;
      const float weight = aurora::stereo::interpolation_weight(displayTime, boundary, interval);
      const double position = static_cast<double>(scene) + weight;
      if (sample > 1)
        EXPECT_NEAR(position - previousPosition, 1'000'000'000.0 / hz / interval, 1e-5);
      previousPosition = position;
    }
  }
}

TEST(StereoInterpolation, KartStaysRigidWhenRecordedCameraTurnsAndMoves) {
  using namespace aurora;
  using namespace aurora::gfx::stereo_replay;
  Mat3x4<float> previousAnchor{}, currentAnchor{}, currentCamera{}, kart{};
  previousAnchor.m0[0]=previousAnchor.m1[1]=previousAnchor.m2[2]=1;
  kart=previousAnchor; kart.m0[3]=25; kart.m1[3]=-50; kart.m2[3]=-75;
  const float c=std::cos(.65f),s=std::sin(.65f);
  currentAnchor.m0[0]=c; currentAnchor.m0[2]=s; currentAnchor.m0[3]=1200;
  currentAnchor.m1[1]=1; currentAnchor.m1[3]=40;
  currentAnchor.m2[0]=-s; currentAnchor.m2[2]=c; currentAnchor.m2[3]=200;
  currentCamera.m0[0]=c; currentCamera.m0[2]=-s; currentCamera.m0[3]=-c*1200+s*200;
  currentCamera.m1[1]=1; currentCamera.m1[3]=-40;
  currentCamera.m2[0]=s; currentCamera.m2[2]=c; currentCamera.m2[3]=-s*1200-c*200;
  const auto currentKart=compose_affine(currentCamera,kart);
  for(float weight : {0.f,.25f,.5f,.75f,1.f}) {
    const auto result=interpolate_anchored_transform(kart,currentKart,previousAnchor,currentAnchor,
        weight,false,gx::interpolate_transform);
    for(size_t col=0;col<4;++col) {
      EXPECT_NEAR(result.m0[col],kart.m0[col],.001f);
      EXPECT_NEAR(result.m1[col],kart.m1[col],.001f);
      EXPECT_NEAR(result.m2[col],kart.m2[col],.001f);
    }
  }
  const auto fallback=interpolate_anchored_transform(kart,currentKart,previousAnchor,currentAnchor,
      .5f,false,[](const auto&,const auto&,float,auto& out) {out={};return false;});
  EXPECT_NEAR(fallback.m0[3],kart.m0[3],.001f);
}

TEST(StereoInterpolation, MissingTimingAndStallsDoNotExtrapolate) {
  using aurora::stereo::interpolation_weight;
  EXPECT_FLOAT_EQ(interpolation_weight(0, 100, 10), 1);
  EXPECT_FLOAT_EQ(interpolation_weight(105, 0, 10), 1);
  EXPECT_FLOAT_EQ(interpolation_weight(105, 100, 0), 1);
  EXPECT_FLOAT_EQ(interpolation_weight(95, 100, 10), 0);
  EXPECT_FLOAT_EQ(interpolation_weight(105, 100, 10), 0.5);
  EXPECT_FLOAT_EQ(interpolation_weight(500, 100, 10), 1);
}
