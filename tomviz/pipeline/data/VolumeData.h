/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineVolumeData_h
#define tomvizPipelineVolumeData_h

#include "tomviz_pipeline_export.h"

#include <QString>

#include <vtkSmartPointer.h>

#include <array>
#include <memory>

class vtkImageData;
class vtkDataArray;

namespace tomviz {
namespace pipeline {

/// Lightweight wrapper around vtkImageData providing essential metadata
/// accessors for pipeline port data. This is a value-semantic type
/// (shared via shared_ptr) with no QObject overhead.
class TOMVIZ_PIPELINE_EXPORT VolumeData
{
public:
  VolumeData();
  explicit VolumeData(vtkSmartPointer<vtkImageData> imageData);
  ~VolumeData();

  VolumeData(const VolumeData&) = default;
  VolumeData& operator=(const VolumeData&) = default;
  VolumeData(VolumeData&&) = default;
  VolumeData& operator=(VolumeData&&) = default;

  /// Access the underlying vtkImageData
  vtkImageData* imageData() const;
  void setImageData(vtkSmartPointer<vtkImageData> data);

  /// Returns true if the underlying image data is non-null
  bool isValid() const;

  // -- Geometry accessors --

  /// Get the dimensions (number of voxels along each axis)
  std::array<int, 3> dimensions() const;

  /// Get the spacing (physical size of each voxel)
  std::array<double, 3> spacing() const;
  void setSpacing(double x, double y, double z);

  /// Get/set the origin
  std::array<double, 3> origin() const;
  void setOrigin(double x, double y, double z);

  /// Get the extent [xmin, xmax, ymin, ymax, zmin, zmax]
  std::array<int, 6> extent() const;

  /// Get the physical bounds [xmin, xmax, ymin, ymax, zmin, zmax]
  std::array<double, 6> bounds() const;

  // -- Scalar data accessors --

  /// Get the active scalars array
  vtkDataArray* scalars() const;

  /// Get the number of scalar components
  int numberOfComponents() const;

  /// Get the scalar range [min, max] of the active scalars
  std::array<double, 2> scalarRange() const;

  // -- Metadata --

  /// A user-visible label
  QString label() const;
  void setLabel(const QString& label);

  /// Units string (e.g. "nm", "angstrom")
  QString units() const;
  void setUnits(const QString& units);

private:
  vtkSmartPointer<vtkImageData> m_imageData;
  QString m_label;
  QString m_units;
};

/// Convenience type for sharing VolumeData through ports via std::any
using VolumeDataPtr = std::shared_ptr<VolumeData>;

} // namespace pipeline
} // namespace tomviz

#endif
