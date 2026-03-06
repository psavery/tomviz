/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineTransformNode_h
#define tomvizPipelineTransformNode_h

#include "tomviz_pipeline_export.h"

#include "Node.h"
#include "PortData.h"

#include <QMap>
#include <QString>

namespace tomviz {
namespace pipeline {

class TOMVIZ_PIPELINE_EXPORT TransformNode : public Node
{
  Q_OBJECT

public:
  TransformNode(QObject* parent = nullptr);
  ~TransformNode() override = default;

  InputPort* addInput(const QString& name, PortTypes acceptedTypes);
  OutputPort* addOutput(const QString& name, PortType type);

  bool execute() override;

protected:
  virtual QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) = 0;
};

} // namespace pipeline
} // namespace tomviz

#endif
