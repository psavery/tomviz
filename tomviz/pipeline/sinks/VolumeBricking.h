/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineVolumeBricking_h
#define tomvizPipelineVolumeBricking_h

#include <vtkSmartPointer.h>

class vtkImageData;
class vtkMultiBlockDataSet;

namespace tomviz {
namespace pipeline {

/// Volumes are uploaded to the GPU as a single 3-D texture, which OpenGL caps
/// at GL_MAX_3D_TEXTURE_SIZE (commonly 2048 on integrated/older GPUs).  These
/// helpers split a volume that exceeds that cap into a vtkMultiBlockDataSet of
/// smaller vtkImageData bricks so it can be rendered at full resolution by
/// vtkMultiBlockVolumeMapper (one resident texture per brick), instead of being
/// re-streamed every frame by vtkGPUVolumeRayCastMapper::SetPartitions.

/// Number of bricks needed along an axis of the given voxel length so each
/// brick - including the one-voxel boundary it shares with its neighbor - stays
/// within maxTextureSize.  Returns >= 1 (1 means the axis already fits).
int computeBlockCount(int length, int maxTextureSize);

/// True if any dimension of image exceeds maxTextureSize.
bool exceedsTextureLimit(vtkImageData* image, int maxTextureSize);

/// Split image into a vtkMultiBlockDataSet of vtkImageData bricks, each within
/// maxTextureSize along every axis.  Adjacent bricks share a one-voxel boundary
/// plane (a one-voxel overlap) so trilinear interpolation stays continuous
/// across brick seams.  Origin and spacing are preserved and each brick keeps
/// its global extent indices, so the bricks reassemble into the original volume
/// in world space.  If image already fits, returns a single-block dataset
/// wrapping a shallow copy of image.
vtkSmartPointer<vtkMultiBlockDataSet> brickVolume(vtkImageData* image,
                                                  int maxTextureSize);

} // namespace pipeline
} // namespace tomviz

#endif
