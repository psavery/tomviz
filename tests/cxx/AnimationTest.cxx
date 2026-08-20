/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <gtest/gtest.h>

#include "CameraViewpoints.h"
#include "sinks/ClipSink.h"

#include <vtkCamera.h>
#include <vtkNew.h>

#include <cmath>

using tomviz::CameraViewpoints;
using tomviz::Viewpoint;
using tomviz::pipeline::planeTravelRange;

namespace {

Viewpoint viewpointAt(double x, double duration, bool eased)
{
  Viewpoint viewpoint;
  viewpoint.position = { x, 0, 10 };
  viewpoint.focalPoint = { x, 0, 0 };
  viewpoint.duration = duration;
  viewpoint.eased = eased;
  return viewpoint;
}

// The viewpoint list is shared by the whole application, so each test
// starts from an empty one.
class AnimationTest : public ::testing::Test
{
protected:
  void SetUp() override { CameraViewpoints::instance().clear(); }
  void TearDown() override { CameraViewpoints::instance().clear(); }

  CameraViewpoints& viewpoints() { return CameraViewpoints::instance(); }
};

} // namespace

TEST_F(AnimationTest, PlaneTravelRangeCoversTheDataAlongTheNormal)
{
  double bounds[6] = { 0, 10, 0, 20, 0, 30 };

  double minDistance = 0;
  double maxDistance = 0;
  double z[3] = { 0, 0, 1 };
  planeTravelRange(bounds, z, minDistance, maxDistance);
  EXPECT_DOUBLE_EQ(minDistance, -15.0);
  EXPECT_DOUBLE_EQ(maxDistance, 15.0);

  // Along a diagonal the box is wider than along either of its axes, and
  // the normal does not have to arrive normalized.
  double diagonal[3] = { 3, 3, 0 };
  planeTravelRange(bounds, diagonal, minDistance, maxDistance);
  double expected = (5.0 + 10.0) / std::sqrt(2.0);
  EXPECT_NEAR(maxDistance, expected, 1e-9);
  EXPECT_NEAR(minDistance, -expected, 1e-9);

  // A plane with no normal has nowhere to travel.
  double degenerate[3] = { 0, 0, 0 };
  planeTravelRange(bounds, degenerate, minDistance, maxDistance);
  EXPECT_DOUBLE_EQ(minDistance, 0.0);
  EXPECT_DOUBLE_EQ(maxDistance, 0.0);
}

TEST_F(AnimationTest, SegmentDurationsDivideUpTheAnimation)
{
  EXPECT_TRUE(viewpoints().stops().isEmpty());

  viewpoints().append(viewpointAt(0, 1.0, false));
  EXPECT_TRUE(viewpoints().stops().isEmpty()) << "one viewpoint is not a path";

  viewpoints().append(viewpointAt(1, 3.0, false));
  viewpoints().append(viewpointAt(2, 1.0, false));

  // The first leg is a quarter as long as the second, so it is over a
  // quarter of the way through.
  auto stops = viewpoints().stops();
  ASSERT_EQ(stops.size(), 3);
  EXPECT_DOUBLE_EQ(stops[0], 0.0);
  EXPECT_DOUBLE_EQ(stops[1], 0.25);
  EXPECT_DOUBLE_EQ(stops[2], 1.0);

  // Durations that leave no time at all fall back to equal legs.
  viewpoints().replace(0, viewpointAt(0, 0.0, false));
  viewpoints().replace(1, viewpointAt(1, 0.0, false));
  stops = viewpoints().stops();
  ASSERT_EQ(stops.size(), 3);
  EXPECT_DOUBLE_EQ(stops[1], 0.5);
}

TEST_F(AnimationTest, RemappingProgressLeavesTheViewpointsWhereTheyAre)
{
  viewpoints().append(viewpointAt(0, 3.0, true));
  viewpoints().append(viewpointAt(1, 1.0, true));
  viewpoints().append(viewpointAt(2, 1.0, true));

  // Easing changes the pacing inside a leg, never when a leg ends.
  for (auto stop : viewpoints().stops()) {
    EXPECT_NEAR(viewpoints().remapProgress(stop), stop, 1e-12);
  }

  double previous = -1;
  for (int i = 0; i <= 100; ++i) {
    double remapped = viewpoints().remapProgress(i / 100.0);
    EXPECT_GE(remapped, previous) << "time ran backwards at " << i;
    EXPECT_GE(remapped, 0.0);
    EXPECT_LE(remapped, 1.0);
    previous = remapped;
  }

  // Out-of-range progress clamps rather than running off the path.
  EXPECT_DOUBLE_EQ(viewpoints().remapProgress(-0.5), 0.0);
  EXPECT_DOUBLE_EQ(viewpoints().remapProgress(1.5), 1.0);
}

TEST_F(AnimationTest, EasingSlowsTheEndsOfALegAndNotItsMiddle)
{
  viewpoints().append(viewpointAt(0, 1.0, false));
  viewpoints().append(viewpointAt(1, 1.0, false));

  // Without easing the camera moves at a constant rate.
  EXPECT_DOUBLE_EQ(viewpoints().remapProgress(0.25), 0.25);
  EXPECT_DOUBLE_EQ(viewpoints().remapProgress(0.75), 0.75);

  viewpoints().replace(0, viewpointAt(0, 1.0, true));

  // With it, the camera has covered less of the leg by the first quarter
  // and more by the last, and the two ends stay symmetric.
  double quarter = viewpoints().remapProgress(0.25);
  double threeQuarters = viewpoints().remapProgress(0.75);
  EXPECT_LT(quarter, 0.25);
  EXPECT_GT(threeQuarters, 0.75);
  EXPECT_NEAR(quarter, 1.0 - threeQuarters, 1e-12);
  EXPECT_DOUBLE_EQ(viewpoints().remapProgress(0.5), 0.5);
}

TEST_F(AnimationTest, ThePathPassesThroughEveryViewpoint)
{
  viewpoints().append(viewpointAt(0, 1.0, true));
  viewpoints().append(viewpointAt(5, 3.0, true));
  viewpoints().append(viewpointAt(9, 1.0, true));

  auto stops = viewpoints().stops();
  ASSERT_EQ(stops.size(), 3);

  vtkNew<vtkCamera> camera;
  for (int i = 0; i < 3; ++i) {
    viewpoints().interpolate(stops[i], camera);
    double position[3];
    camera->GetPosition(position);
    EXPECT_NEAR(position[0], viewpoints().at(i).position[0], 1e-6)
      << "viewpoint " << i << " was not reached";
  }
}

TEST_F(AnimationTest, ViewpointsSurviveAStateFileRoundTrip)
{
  Viewpoint saved;
  saved.position = { 1, 2, 3 };
  saved.focalPoint = { 4, 5, 6 };
  saved.viewUp = { 0, 0, 1 };
  saved.viewAngle = 45;
  saved.parallelScale = 2.5;
  saved.parallelProjection = true;
  saved.duration = 2.5;
  saved.eased = false;

  viewpoints().append(saved);
  viewpoints().append(viewpointAt(7, 1.0, true));
  auto json = viewpoints().serialize();

  viewpoints().clear();
  ASSERT_TRUE(viewpoints().deserialize(json));
  ASSERT_EQ(viewpoints().size(), 2);

  const auto& restored = viewpoints().at(0);
  EXPECT_EQ(restored.position, saved.position);
  EXPECT_EQ(restored.focalPoint, saved.focalPoint);
  EXPECT_EQ(restored.viewUp, saved.viewUp);
  EXPECT_DOUBLE_EQ(restored.viewAngle, saved.viewAngle);
  EXPECT_DOUBLE_EQ(restored.parallelScale, saved.parallelScale);
  EXPECT_TRUE(restored.parallelProjection);
  EXPECT_DOUBLE_EQ(restored.duration, saved.duration);
  EXPECT_FALSE(restored.eased);

  // A state file with no viewpoints in it is not a list of none.
  EXPECT_FALSE(viewpoints().deserialize(QJsonObject()));
}
