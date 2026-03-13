/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineSetTiltAnglesTransform_h
#define tomvizPipelineSetTiltAnglesTransform_h

#include "tomviz_pipeline_export.h"

#include "TransformNode.h"

#include <QMap>
#include <QVector>

namespace tomviz {
namespace pipeline {

/// Transform that sets tilt angles on a VolumeData.
class TOMVIZ_PIPELINE_EXPORT SetTiltAnglesTransform : public TransformNode
{
  Q_OBJECT

public:
  SetTiltAnglesTransform(QObject* parent = nullptr);
  ~SetTiltAnglesTransform() override = default;

  void setTiltAngles(const QMap<size_t, double>& angles);
  QMap<size_t, double> tiltAnglesMap() const;

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override;

private:
  QMap<size_t, double> m_tiltAngles;
};

} // namespace pipeline
} // namespace tomviz

#endif
