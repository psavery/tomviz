/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SinkNode.h"

#include "InputPort.h"

namespace tomviz {
namespace pipeline {

SinkNode::SinkNode(QObject* parent) : Node(parent) {}

InputPort* SinkNode::addInput(const QString& name, PortTypes acceptedTypes)
{
  return addInputPort(name, acceptedTypes);
}

bool SinkNode::execute()
{
  emit executionStarted();

  QMap<QString, PortData> inputs;
  for (auto* port : inputPorts()) {
    inputs[port->name()] = port->data();
  }

  bool success = consume(inputs);

  if (success) {
    markCurrent();
  }

  emit executionFinished(success);
  return success;
}

} // namespace pipeline
} // namespace tomviz
