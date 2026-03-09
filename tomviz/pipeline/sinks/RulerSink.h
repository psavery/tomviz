/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineRulerSink_h
#define tomvizPipelineRulerSink_h

#include "tomviz_pipeline_export.h"

#include "LegacyModuleSink.h"

#include <vtkNew.h>

class vtkActor;
class vtkLineSource;
class vtkPolyDataMapper;
class vtkProperty;

namespace tomviz {
namespace pipeline {

/// Ruler/measurement annotation visualization sink.
/// Displays a measurement line between two user-defined points.
class TOMVIZ_PIPELINE_EXPORT RulerSink : public LegacyModuleSink
{
  Q_OBJECT

public:
  RulerSink(QObject* parent = nullptr);
  ~RulerSink() override;

  bool initialize(vtkSMViewProxy* view) override;
  bool finalize() override;

  QJsonObject serialize() const override;
  bool deserialize(const QJsonObject& json) override;

  void setPoint1(double x, double y, double z);
  void setPoint2(double x, double y, double z);
  double length() const;

protected:
  bool consume(const QMap<QString, PortData>& inputs) override;

private:
  vtkNew<vtkLineSource> m_lineSource;
  vtkNew<vtkPolyDataMapper> m_mapper;
  vtkNew<vtkActor> m_actor;
  vtkNew<vtkProperty> m_property;
  bool m_firstConsume = true;
};

} // namespace pipeline
} // namespace tomviz

#endif
