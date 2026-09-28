/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <gtest/gtest.h>

#include "sinks/VolumeBricking.h"

#include <vtkFloatArray.h>
#include <vtkImageData.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>

#include <array>
#include <set>

using tomviz::pipeline::brickVolume;
using tomviz::pipeline::computeBlockCount;
using tomviz::pipeline::exceedsTextureLimit;

namespace {

// Build a volume whose scalar at (x,y,z) is a unique, reproducible value so we
// can check that bricks carry the right data at the right global indices.
vtkSmartPointer<vtkImageData> makeRampVolume(int nx, int ny, int nz)
{
  auto image = vtkSmartPointer<vtkImageData>::New();
  image->SetDimensions(nx, ny, nz);
  image->SetSpacing(1.0, 1.0, 1.0);
  image->SetOrigin(0.0, 0.0, 0.0);

  vtkNew<vtkFloatArray> scalars;
  scalars->SetName("ImageScalars");
  scalars->SetNumberOfComponents(1);
  scalars->SetNumberOfTuples(static_cast<vtkIdType>(nx) * ny * nz);
  for (int z = 0; z < nz; ++z) {
    for (int y = 0; y < ny; ++y) {
      for (int x = 0; x < nx; ++x) {
        vtkIdType id = static_cast<vtkIdType>(z) * ny * nx +
                       static_cast<vtkIdType>(y) * nx + x;
        scalars->SetValue(id, static_cast<float>(x * 1000000 + y * 1000 + z));
      }
    }
  }
  image->GetPointData()->SetScalars(scalars);
  return image;
}

float expectedValue(int x, int y, int z)
{
  return static_cast<float>(x * 1000000 + y * 1000 + z);
}

} // namespace

TEST(VolumeBrickingTest, BlockCount)
{
  // Fits exactly -> a single block, no split.
  EXPECT_EQ(computeBlockCount(2048, 2048), 1);
  EXPECT_EQ(computeBlockCount(1, 2048), 1);
  EXPECT_EQ(computeBlockCount(2049, 2048), 2);
  // One-voxel overlap means two 2048 bricks share a boundary plane and so
  // cover only 2*2047 + 1 = 4095 voxels. 4095 fits in two; 4096 needs three.
  EXPECT_EQ(computeBlockCount(4095, 2048), 2);
  EXPECT_EQ(computeBlockCount(4096, 2048), 3);
  EXPECT_EQ(computeBlockCount(5000, 2048), 3);
}

TEST(VolumeBrickingTest, ComputedBricksNeverExceedCap)
{
  // Whatever the count, each brick (step + 1 shared-boundary voxels) must fit.
  for (int maxTex : { 4, 7, 16, 100, 2048 }) {
    for (int length = 1; length < 6 * maxTex; ++length) {
      int n = computeBlockCount(length, maxTex);
      ASSERT_GE(n, 1);
      // Largest brick under an even split.
      int span = length - 1;
      int maxStep = (n > 0) ? (span + n - 1) / n : span; // ceil(span / n)
      EXPECT_LE(maxStep + 1, maxTex)
        << "length=" << length << " maxTex=" << maxTex << " n=" << n;
    }
  }
}

TEST(VolumeBrickingTest, SingleBlockWhenItFits)
{
  auto image = makeRampVolume(8, 8, 8);
  EXPECT_FALSE(exceedsTextureLimit(image, 2048));

  auto blocks = brickVolume(image, 2048);
  ASSERT_EQ(blocks->GetNumberOfBlocks(), 1u);

  auto* brick = vtkImageData::SafeDownCast(blocks->GetBlock(0));
  ASSERT_NE(brick, nullptr);
  int dims[3];
  brick->GetDimensions(dims);
  EXPECT_EQ(dims[0], 8);
  EXPECT_EQ(dims[1], 8);
  EXPECT_EQ(dims[2], 8);
}

TEST(VolumeBrickingTest, SplitsLongAxisOnly)
{
  // Long in x only; y and z fit. Expect a 3 x 1 x 1 brick grid.
  auto image = makeRampVolume(100, 10, 10);
  const int maxTex = 40;
  EXPECT_TRUE(exceedsTextureLimit(image, maxTex));

  auto blocks = brickVolume(image, maxTex);
  EXPECT_EQ(blocks->GetNumberOfBlocks(),
            static_cast<unsigned>(computeBlockCount(100, maxTex)));

  for (unsigned i = 0; i < blocks->GetNumberOfBlocks(); ++i) {
    auto* brick = vtkImageData::SafeDownCast(blocks->GetBlock(i));
    ASSERT_NE(brick, nullptr);
    int dims[3];
    brick->GetDimensions(dims);
    EXPECT_LE(dims[0], maxTex);
    EXPECT_EQ(dims[1], 10); // y not split
    EXPECT_EQ(dims[2], 10); // z not split
  }
}

TEST(VolumeBrickingTest, BricksCoverVolumeWithOverlapAndCorrectValues)
{
  auto image = makeRampVolume(100, 60, 5);
  const int maxTex = 32;
  auto blocks = brickVolume(image, maxTex);
  ASSERT_GT(blocks->GetNumberOfBlocks(), 1u);

  // Every global voxel index must be covered by at least one brick, and the
  // scalar there must match the original. Track coverage to confirm no gaps.
  std::set<std::array<int, 3>> covered;

  for (unsigned i = 0; i < blocks->GetNumberOfBlocks(); ++i) {
    auto* brick = vtkImageData::SafeDownCast(blocks->GetBlock(i));
    ASSERT_NE(brick, nullptr);

    int ext[6];
    brick->GetExtent(ext);
    // Each brick keeps its global extent indices and the shared origin/spacing.
    auto* scalars = brick->GetPointData()->GetScalars();
    ASSERT_NE(scalars, nullptr);

    for (int z = ext[4]; z <= ext[5]; ++z) {
      for (int y = ext[2]; y <= ext[3]; ++y) {
        for (int x = ext[0]; x <= ext[1]; ++x) {
          double val = brick->GetScalarComponentAsDouble(x, y, z, 0);
          EXPECT_FLOAT_EQ(static_cast<float>(val), expectedValue(x, y, z))
            << "brick " << i << " at (" << x << "," << y << "," << z << ")";
          covered.insert({ x, y, z });
        }
      }
    }
  }

  EXPECT_EQ(covered.size(), static_cast<size_t>(100 * 60 * 5));

  // Confirm there is genuine overlap: summed brick voxel counts exceed the
  // volume's voxel count (adjacent bricks share a boundary plane).
  vtkIdType totalBrickVoxels = 0;
  for (unsigned i = 0; i < blocks->GetNumberOfBlocks(); ++i) {
    totalBrickVoxels +=
      vtkImageData::SafeDownCast(blocks->GetBlock(i))->GetNumberOfPoints();
  }
  EXPECT_GT(totalBrickVoxels, static_cast<vtkIdType>(100 * 60 * 5));
}

TEST(VolumeBrickingTest, NullInputIsSafe)
{
  EXPECT_FALSE(exceedsTextureLimit(nullptr, 2048));
  auto blocks = brickVolume(nullptr, 2048);
  ASSERT_NE(blocks, nullptr);
  EXPECT_EQ(blocks->GetNumberOfBlocks(), 0u);
}
