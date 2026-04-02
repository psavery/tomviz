/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineVolumeData_h
#define tomvizPipelineVolumeData_h

#include <QString>

#include <vtkNew.h>
#include <vtkSmartPointer.h>

#include <QList>
#include <QVector>

#include <array>
#include <memory>

class vtkColorTransferFunction;
class vtkImageData;
class vtkDataArray;
class vtkPiecewiseFunction;
class vtkSMProxy;

namespace tomviz {
namespace pipeline {

/// Lightweight wrapper around vtkImageData providing essential metadata
/// accessors for pipeline port data. This is a value-semantic type
/// (shared via shared_ptr) with no QObject overhead.
class VolumeData
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

  /// Display transform: additional visual translation applied to actors
  /// (separate from vtkImageData origin).
  std::array<double, 3> displayPosition() const;
  void setDisplayPosition(double x, double y, double z);

  /// Display transform: Euler angles applied to actors
  /// (vtkImageData has no rotation support).
  std::array<double, 3> displayOrientation() const;
  void setDisplayOrientation(double x, double y, double z);

  // -- Scalar data accessors --

  /// Get the active scalars array
  vtkDataArray* scalars() const;

  /// Get the number of scalar components
  int numberOfComponents() const;

  /// Get the scalar range [min, max] of the active scalars
  std::array<double, 2> scalarRange() const;

  // -- Color/Opacity map --

  /// Returns true if the color map has been initialized.
  bool hasColorMap() const;

  /// Create the SM proxy and cache client-side VTK objects. Must be called
  /// on the main thread. Safe to call multiple times (no-op if already done).
  void initColorMap();

  /// Get the color transfer function proxy. Calls initColorMap() if needed.
  vtkSMProxy* colorMap();

  /// Get the scalar opacity function sub-proxy from the color map.
  vtkSMProxy* opacityMap();

  /// Direct access to the cached client-side VTK objects.
  /// These are populated by initColorMap() and can be used for thread-safe
  /// copy/rescale operations without going through SM proxies.
  vtkColorTransferFunction* colorTransferFunction() const;
  vtkPiecewiseFunction* scalarOpacity() const;

  /// Get the gradient opacity function.
  vtkPiecewiseFunction* gradientOpacity() const;

  /// Rescale the color and opacity maps to the current scalarRange().
  /// Uses cached VTK objects directly (no SM proxy interaction).
  void rescaleColorMap();

  /// Copy color/opacity map control points from another VolumeData as-is
  /// (preserving the source's X positions). Uses cached VTK objects directly.
  /// Both this and source must have initColorMap() called first.
  void copyColorMapFrom(const VolumeData& source);

  /// Convenience: copyColorMapFrom() + rescaleColorMap().
  void copyAndRescaleColorMapFrom(const VolumeData& source);

  // -- Metadata --

  /// A user-visible label
  QString label() const;
  void setLabel(const QString& label);

  /// Units string (e.g. "nm", "angstrom")
  QString units() const;
  void setUnits(const QString& units);

  // -- Tilt angle accessors --

  bool hasTiltAngles() const;
  QVector<double> tiltAngles() const;
  void setTiltAngles(const QVector<double>& angles);

  /// Static utility: check if a vtkImageData has tilt angles stored
  /// in its field data (array named "tilt_angles").
  static bool hasTiltAngles(vtkImageData* image);
  static QVector<double> getTiltAngles(vtkImageData* image);

  // -- Time series support --

  struct TimeStep {
    QString label;
    vtkSmartPointer<vtkImageData> image;
    double time = 0.0;
  };

  void setTimeSteps(const QList<TimeStep>& steps);
  QList<TimeStep> timeSteps() const;
  bool hasTimeSteps() const;
  int currentTimeStepIndex() const;
  void switchTimeStep(int index);

private:
  vtkSmartPointer<vtkImageData> m_imageData;
  vtkSmartPointer<vtkSMProxy> m_colorMap;
  vtkNew<vtkPiecewiseFunction> m_gradientOpacity;
  // Cached client-side VTK objects from the SM proxy
  vtkColorTransferFunction* m_ctf = nullptr;
  vtkPiecewiseFunction* m_opacity = nullptr;
  QString m_label;
  QString m_units;
  std::array<double, 3> m_displayPosition = { 0.0, 0.0, 0.0 };
  std::array<double, 3> m_displayOrientation = { 0.0, 0.0, 0.0 };
  QList<TimeStep> m_timeSteps;
  int m_currentTimeStep = 0;
};

/// Convenience type for sharing VolumeData through ports via std::any
using VolumeDataPtr = std::shared_ptr<VolumeData>;

} // namespace pipeline
} // namespace tomviz

#endif
