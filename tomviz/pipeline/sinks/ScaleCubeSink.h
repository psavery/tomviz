/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineScaleCubeSink_h
#define tomvizPipelineScaleCubeSink_h

#include "tomviz_pipeline_export.h"

#include "LegacyModuleSink.h"

#include <vtkNew.h>

class vtkActor;
class vtkBillboardTextActor3D;
class vtkCubeSource;
class vtkPolyDataMapper;
class vtkProperty;

namespace tomviz {
namespace pipeline {

/// Scale cube annotation visualization sink.
/// Displays a cube of known size for spatial reference with optional
/// text annotation. Matches the old ModuleScaleCube.
class TOMVIZ_PIPELINE_EXPORT ScaleCubeSink : public LegacyModuleSink
{
  Q_OBJECT

public:
  ScaleCubeSink(QObject* parent = nullptr);
  ~ScaleCubeSink() override;

  QIcon icon() const override;

  void setVisibility(bool visible) override;

  bool initialize(vtkSMViewProxy* view) override;
  bool finalize() override;

  QJsonObject serialize() const override;
  bool deserialize(const QJsonObject& json) override;

  double sideLength() const;
  void setSideLength(double length);

  void setPosition(double x, double y, double z);

  /// Adaptive sizing: set side length to ~10% of the volume extent.
  bool adaptiveScaling() const;
  void setAdaptiveScaling(bool adaptive);

  /// Box color.
  void setColor(double r, double g, double b);

  /// Annotation text (label below the cube).
  bool showAnnotation() const;
  void setShowAnnotation(bool show);

  /// Annotation label text.
  QString annotationText() const;
  void setAnnotationText(const QString& text);

protected:
  bool consume(const QMap<QString, PortData>& inputs) override;

private:
  void updateAnnotation();

  vtkNew<vtkCubeSource> m_cubeSource;
  vtkNew<vtkPolyDataMapper> m_mapper;
  vtkNew<vtkActor> m_actor;
  vtkNew<vtkProperty> m_property;
  vtkNew<vtkBillboardTextActor3D> m_textActor;
  double m_sideLength = 1.0;
  bool m_adaptiveScaling = true;
  bool m_showAnnotation = true;
  QString m_annotationText;
  bool m_firstConsume = true;
};

} // namespace pipeline
} // namespace tomviz

#endif
