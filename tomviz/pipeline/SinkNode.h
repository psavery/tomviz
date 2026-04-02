/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineSinkNode_h
#define tomvizPipelineSinkNode_h

#include "Node.h"
#include "PortData.h"

#include <QMap>
#include <QString>

namespace tomviz {
namespace pipeline {

class SinkNode : public Node
{
  Q_OBJECT

public:
  SinkNode(QObject* parent = nullptr);
  ~SinkNode() override = default;

  InputPort* addInput(const QString& name, PortTypes acceptedTypes);

  bool execute() override;

protected:
  virtual bool consume(const QMap<QString, PortData>& inputs) = 0;
};

} // namespace pipeline
} // namespace tomviz

#endif
