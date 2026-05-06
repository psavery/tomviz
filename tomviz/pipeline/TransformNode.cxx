/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "TransformNode.h"

#include "InputPort.h"
#include "OutputPort.h"

namespace tomviz {
namespace pipeline {

TransformNode::TransformNode(QObject* parent) : Node(parent) {}

QIcon TransformNode::icon() const
{
  return QIcon(
    QStringLiteral(":/pqWidgets/Icons/pqProgrammableFilter.svg"));
}

InputPort* TransformNode::addInput(const QString& name,
                                   PortTypes acceptedTypes)
{
  return addInputPort(name, acceptedTypes);
}

OutputPort* TransformNode::addOutput(const QString& name, PortType type)
{
  return addOutputPort(name, type);
}

QJsonObject TransformNode::serialize() const
{
  return Node::serialize();
}

bool TransformNode::deserialize(const QJsonObject& json)
{
  return Node::deserialize(json);
}

bool TransformNode::execute()
{
  resetExecutionFlags();
  resetProgress();
  setExecState(NodeExecState::Running);

  for (auto* port : inputPorts()) {
    if (!port->link() || !port->hasData()) {
      setExecState(NodeExecState::Failed);
      return false;
    }
  }

  auto inputs = collectInputs();

  auto outputs = transform(inputs);

  if (isCanceled()) {
    setExecState(NodeExecState::Canceled);
    return false;
  }

  if (outputs.isEmpty() && !outputPorts().isEmpty()) {
    setExecState(NodeExecState::Failed);
    return false;
  }

  applyOutputs(outputs);

  markCurrent();
  setExecState(NodeExecState::Idle);
  return true;
}

} // namespace pipeline
} // namespace tomviz
