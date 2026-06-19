/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "VolumeBricking.h"

#include <vtkExtractVOI.h>
#include <vtkImageData.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkNew.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tomviz {
namespace pipeline {

int computeBlockCount(int length, int maxTextureSize)
{
  if (maxTextureSize < 2) {
    // Pathological cap; can't honor an overlap, so one brick per voxel.
    return std::max(1, length);
  }
  if (length <= maxTextureSize) {
    return 1;
  }
  // Bricks share a one-voxel boundary, so a brick of size S advances the
  // covered span by (S - 1).  With S capped at maxTextureSize, each brick
  // covers at most (maxTextureSize - 1) of the (length - 1) span between the
  // first and last voxel.
  const int span = length - 1;
  const int step = maxTextureSize - 1;
  return (span + step - 1) / step; // ceil(span / step)
}

bool exceedsTextureLimit(vtkImageData* image, int maxTextureSize)
{
  if (!image) {
    return false;
  }
  int dims[3];
  image->GetDimensions(dims);
  return dims[0] > maxTextureSize || dims[1] > maxTextureSize ||
         dims[2] > maxTextureSize;
}

namespace {

// Brick boundaries along one axis as global extent indices: count+1 monotonic
// cut points from e0..e1 distributed as evenly as possible.  Consecutive bricks
// share a cut point (the one-voxel overlap).
std::vector<int> axisCuts(int e0, int e1, int count)
{
  std::vector<int> cuts(count + 1);
  const int span = e1 - e0;
  for (int k = 0; k <= count; ++k) {
    // llround keeps the distribution balanced and guarantees cuts[0] == e0
    // and cuts[count] == e1.
    cuts[k] = e0 + static_cast<int>(std::llround(
                     static_cast<double>(span) * k / count));
  }
  return cuts;
}

} // namespace

vtkSmartPointer<vtkMultiBlockDataSet> brickVolume(vtkImageData* image,
                                                  int maxTextureSize)
{
  auto blocks = vtkSmartPointer<vtkMultiBlockDataSet>::New();
  if (!image) {
    return blocks;
  }

  int extent[6];
  image->GetExtent(extent);

  if (!exceedsTextureLimit(image, maxTextureSize)) {
    // Already fits: hand back a single shallow-copied block.
    vtkNew<vtkImageData> single;
    single->ShallowCopy(image);
    blocks->SetNumberOfBlocks(1);
    blocks->SetBlock(0, single);
    return blocks;
  }

  std::array<std::vector<int>, 3> cuts;
  for (int axis = 0; axis < 3; ++axis) {
    const int e0 = extent[2 * axis];
    const int e1 = extent[2 * axis + 1];
    const int length = e1 - e0 + 1;
    cuts[axis] = axisCuts(e0, e1, computeBlockCount(length, maxTextureSize));
  }

  for (size_t kz = 0; kz + 1 < cuts[2].size(); ++kz) {
    for (size_t ky = 0; ky + 1 < cuts[1].size(); ++ky) {
      for (size_t kx = 0; kx + 1 < cuts[0].size(); ++kx) {
        vtkNew<vtkExtractVOI> voi;
        voi->SetInputData(image);
        voi->SetVOI(cuts[0][kx], cuts[0][kx + 1], cuts[1][ky], cuts[1][ky + 1],
                    cuts[2][kz], cuts[2][kz + 1]);
        voi->Update();

        vtkNew<vtkImageData> brick;
        brick->ShallowCopy(voi->GetOutput());
        blocks->SetBlock(blocks->GetNumberOfBlocks(), brick);
      }
    }
  }

  return blocks;
}

} // namespace pipeline
} // namespace tomviz
