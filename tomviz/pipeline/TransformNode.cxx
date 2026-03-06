/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "TransformNode.h"

#include "InputPort.h"
#include "OutputPort.h"

namespace tomviz {
namespace pipeline {

TransformNode::TransformNode(QObject* parent) : Node(parent) {}

InputPort* TransformNode::addInput(const QString& name,
                                   PortTypes acceptedTypes)
{
  return addInputPort(name, acceptedTypes);
}

OutputPort* TransformNode::addOutput(const QString& name, PortType type)
{
  return addOutputPort(name, type);
}

bool TransformNode::execute()
{
  emit executionStarted();

  QMap<QString, PortData> inputs;
  for (auto* port : inputPorts()) {
    inputs[port->name()] = port->data();
  }

  auto outputs = transform(inputs);

  for (auto it = outputs.constBegin(); it != outputs.constEnd(); ++it) {
    auto* port = outputPort(it.key());
    if (port) {
      port->setData(it.value());
    }
  }

  markCurrent();
  emit executionFinished(true);
  return true;
}

} // namespace pipeline
} // namespace tomviz
