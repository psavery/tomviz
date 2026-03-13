/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "Node.h"

#include "InputPort.h"
#include "Link.h"
#include "OutputPort.h"

namespace tomviz {
namespace pipeline {

Node::Node(QObject* parent) : QObject(parent) {}

QString Node::label() const
{
  return m_label;
}

void Node::setLabel(const QString& label)
{
  if (m_label != label) {
    m_label = label;
    emit labelChanged();
  }
}

NodeState Node::state() const
{
  return m_state;
}

void Node::markStale()
{
  if (m_state == NodeState::Stale) {
    return;
  }
  m_state = NodeState::Stale;
  emit stateChanged(m_state);

  for (auto* output : m_outputPorts) {
    output->setStale(true);
    for (auto* link : output->links()) {
      if (link->to()) {
        Node* downstream = link->to()->node();
        if (downstream) {
          downstream->markStale();
        }
      }
    }
  }
}

void Node::markCurrent()
{
  m_state = NodeState::Current;
  emit stateChanged(m_state);
}

bool Node::hasBreakpoint() const
{
  return m_breakpoint;
}

void Node::setBreakpoint(bool enabled)
{
  if (m_breakpoint != enabled) {
    m_breakpoint = enabled;
    emit breakpointChanged();
  }
}

QList<InputPort*> Node::inputPorts() const
{
  return m_inputPorts;
}

QList<OutputPort*> Node::outputPorts() const
{
  return m_outputPorts;
}

InputPort* Node::inputPort(const QString& name) const
{
  for (auto* port : m_inputPorts) {
    if (port->name() == name) {
      return port;
    }
  }
  return nullptr;
}

OutputPort* Node::outputPort(const QString& name) const
{
  for (auto* port : m_outputPorts) {
    if (port->name() == name) {
      return port;
    }
  }
  return nullptr;
}

bool Node::allInputsCurrent() const
{
  for (auto* input : m_inputPorts) {
    if (!input->link()) {
      return false;
    }
    Node* upstream = input->link()->from()->node();
    if (upstream && upstream->state() != NodeState::Current) {
      return false;
    }
  }
  return true;
}

bool Node::anyInputStale() const
{
  for (auto* input : m_inputPorts) {
    if (input->isStale()) {
      return true;
    }
  }
  return false;
}

QList<Node*> Node::upstreamNodes() const
{
  QList<Node*> result;
  for (auto* input : m_inputPorts) {
    if (input->link() && input->link()->from()) {
      Node* upstream = input->link()->from()->node();
      if (upstream && !result.contains(upstream)) {
        result.append(upstream);
      }
    }
  }
  return result;
}

QList<Node*> Node::downstreamNodes() const
{
  QList<Node*> result;
  for (auto* output : m_outputPorts) {
    for (auto* link : output->links()) {
      if (link->to()) {
        Node* downstream = link->to()->node();
        if (downstream && !result.contains(downstream)) {
          result.append(downstream);
        }
      }
    }
  }
  return result;
}

void Node::setProperty(const QString& key, const QVariant& value)
{
  m_properties[key] = value;
}

QVariant Node::property(const QString& key,
                        const QVariant& defaultValue) const
{
  return m_properties.value(key, defaultValue);
}

QVariantMap Node::properties() const
{
  return m_properties;
}

QIcon Node::icon() const
{
  return QIcon(QStringLiteral(":/icons/pqInspect.png"));
}

QIcon Node::actionIcon() const
{
  return QIcon();
}

void Node::triggerAction() {}

bool Node::execute()
{
  return true;
}

InputPort* Node::addInputPort(const QString& name, PortTypes acceptedTypes)
{
  auto* port = new InputPort(name, acceptedTypes, this);
  m_inputPorts.append(port);
  return port;
}

OutputPort* Node::addOutputPort(const QString& name, PortType type)
{
  auto* port = new OutputPort(name, type, this);
  m_outputPorts.append(port);
  return port;
}

} // namespace pipeline
} // namespace tomviz
