/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SinkGroupNode.h"

#include "InputPort.h"
#include "Link.h"
#include "PassthroughOutputPort.h"
#include "SinkNode.h"

namespace tomviz {
namespace pipeline {

SinkGroupNode::SinkGroupNode(QObject* parent) : Node(parent)
{
  setLabel("Modules");
}

void SinkGroupNode::addPassthrough(const QString& name, PortType type)
{
  auto* input = addInputPort(name, PortTypes(type));
  auto* output = new PassthroughOutputPort(name, type, this);
  addOutputPort(output);
  setTypeInferenceSource(name, name);

  // Keep the proxy wired to the current upstream output port.
  connect(input, &InputPort::connectionChanged, this,
          [input, output]() {
            if (input->link() && input->link()->from()) {
              output->setSource(input->link()->from());
            } else {
              output->setSource(nullptr);
            }
          });
}

bool SinkGroupNode::execute()
{
  for (auto* port : inputPorts()) {
    if (!port->link() || !port->hasData()) {
      setExecState(NodeExecState::Failed);
      return false;
    }
  }
  markCurrent();
  setExecState(NodeExecState::Idle);
  return true;
}

QList<SinkNode*> SinkGroupNode::sinks() const
{
  QList<SinkNode*> result;
  for (auto* port : outputPorts()) {
    for (auto* link : port->links()) {
      if (link->to()) {
        auto* sink = qobject_cast<SinkNode*>(link->to()->node());
        if (sink && !result.contains(sink)) {
          result.append(sink);
        }
      }
    }
  }
  return result;
}

} // namespace pipeline
} // namespace tomviz
