/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineSourceNode_h
#define tomvizPipelineSourceNode_h

#include "Node.h"
#include "PortData.h"

namespace tomviz {
namespace pipeline {

class SourceNode : public Node
{
  Q_OBJECT

public:
  SourceNode(QObject* parent = nullptr);
  ~SourceNode() override = default;

  QIcon icon() const override;

  OutputPort* addOutput(const QString& name, PortType type);
  void setOutputData(const QString& portName, const PortData& data);

  bool execute() override;
};

} // namespace pipeline
} // namespace tomviz

#endif
