/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SourceNode.h"

#include "OutputPort.h"

namespace tomviz {
namespace pipeline {

SourceNode::SourceNode(QObject* parent) : Node(parent) {}

OutputPort* SourceNode::addOutput(const QString& name, PortType type)
{
  return addOutputPort(name, type);
}

void SourceNode::setOutputData(const QString& portName, const PortData& data)
{
  auto* port = outputPort(portName);
  if (port) {
    port->setData(data);
    markCurrent();
    // Mark all downstream nodes as stale
    for (auto* downstream : downstreamNodes()) {
      downstream->markStale();
    }
  }
}

bool SourceNode::execute()
{
  // A source node is current if any output has data
  for (auto* port : outputPorts()) {
    if (port->hasData()) {
      markCurrent();
      return true;
    }
  }
  return true;
}

} // namespace pipeline
} // namespace tomviz
