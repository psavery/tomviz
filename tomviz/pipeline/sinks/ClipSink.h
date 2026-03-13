/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineClipSink_h
#define tomvizPipelineClipSink_h

#include "tomviz_pipeline_export.h"

#include "LegacyModuleSink.h"

#include <vtkNew.h>
#include <vtkSmartPointer.h>

class vtkImageData;
class vtkNonOrthoImagePlaneWidget;
class vtkPlane;

namespace tomviz {
namespace pipeline {

/// Clipping-plane visualization sink using vtkNonOrthoImagePlaneWidget.
/// Matches the old ModuleClip: shows an interactive texture-mapped plane
/// and emits the clipping plane geometry for other modules to clip against.
class TOMVIZ_PIPELINE_EXPORT ClipSink : public LegacyModuleSink
{
  Q_OBJECT

public:
  enum Direction { XY = 0, YZ = 1, XZ = 2, Custom = 3 };

  ClipSink(QObject* parent = nullptr);
  ~ClipSink() override;

  QIcon icon() const override;

  void setVisibility(bool visible) override;

  bool initialize(vtkSMViewProxy* view) override;
  bool finalize() override;

  QJsonObject serialize() const override;
  bool deserialize(const QJsonObject& json) override;

  /// Plane orientation preset.
  Direction direction() const;
  void setDirection(Direction dir);

  /// Slice index for orthogonal directions (0-based).
  int slice() const;
  void setSlice(int s);

  /// Plane opacity.
  double opacity() const;
  void setOpacity(double value);

  /// Show/hide the directional arrow on the widget.
  bool showArrow() const;
  void setShowArrow(bool show);

  /// Invert clip direction.
  bool invertPlane() const;
  void setInvertPlane(bool invert);

  /// Custom plane origin and normal.
  void setPlaneOrigin(double x, double y, double z);
  void setPlaneNormal(double nx, double ny, double nz);

  /// Access the clipping plane for use by other sinks/modules.
  vtkPlane* clippingPlane() const;

signals:
  /// Emitted when the clip plane geometry is updated.
  void clipPlaneUpdated();

protected:
  bool consume(const QMap<QString, PortData>& inputs) override;

private:
  void setupWidget();
  void applyDirection();

  vtkSmartPointer<vtkNonOrthoImagePlaneWidget> m_widget;
  vtkNew<vtkPlane> m_clippingPlane;
  Direction m_direction = XY;
  int m_slice = -1;
  double m_opacity = 1.0;
  bool m_showArrow = true;
  bool m_invertPlane = false;
  int m_dims[3] = { 0, 0, 0 };
  double m_bounds[6] = { 0, 0, 0, 0, 0, 0 };
};

} // namespace pipeline
} // namespace tomviz

#endif
