/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineContourSink_h
#define tomvizPipelineContourSink_h

#include "tomviz_pipeline_export.h"

#include "LegacyModuleSink.h"

#include <vtkNew.h>

class vtkActor;
class vtkColorTransferFunction;
class vtkDataSetMapper;
class vtkFlyingEdges3D;
class vtkProperty;

namespace tomviz {
namespace pipeline {

/// Isosurface/contour visualization sink using vtkFlyingEdges3D.
/// Matches the old ModuleContour pipeline with lighting, representation
/// mode, opacity, and color map support.
class TOMVIZ_PIPELINE_EXPORT ContourSink : public LegacyModuleSink
{
  Q_OBJECT

public:
  ContourSink(QObject* parent = nullptr);
  ~ContourSink() override;

  QIcon icon() const override;

  void setVisibility(bool visible) override;
  bool isColorMapNeeded() const override;

  bool initialize(vtkSMViewProxy* view) override;
  bool finalize() override;

  QJsonObject serialize() const override;
  bool deserialize(const QJsonObject& json) override;

  double isoValue() const;
  void setIsoValue(double value);

  double opacity() const;
  void setOpacity(double value);

  /// Phong lighting parameters.
  double ambient() const;
  void setAmbient(double value);
  double diffuse() const;
  void setDiffuse(double value);
  double specular() const;
  void setSpecular(double value);
  double specularPower() const;
  void setSpecularPower(double value);

  /// Representation: VTK_SURFACE (2), VTK_WIREFRAME (1), VTK_POINTS (0).
  int representation() const;
  void setRepresentation(int rep);

  /// Solid color (used when mapScalars is false).
  void color(double rgb[3]) const;
  void setColor(double r, double g, double b);

  /// Cached scalar range from the last consume().
  void scalarRange(double range[2]) const;

  QWidget* createPropertiesWidget(QWidget* parent) override;

  /// Toggle scalar color mapping on the surface.
  bool mapScalars() const;
  void setMapScalars(bool map);

protected:
  bool consume(const QMap<QString, PortData>& inputs) override;
  void updateColorMap() override;

private:
  vtkNew<vtkFlyingEdges3D> m_flyingEdges;
  vtkNew<vtkDataSetMapper> m_mapper;
  vtkNew<vtkActor> m_actor;
  vtkNew<vtkProperty> m_property;
  double m_isoValue = 0.0;
  bool m_isoValueSet = false;
  bool m_mapScalars = true;
  double m_scalarRange[2] = { 0.0, 1.0 };
};

} // namespace pipeline
} // namespace tomviz

#endif
