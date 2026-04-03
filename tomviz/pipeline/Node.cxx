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

NodeExecState Node::execState() const
{
  return m_execState;
}

void Node::setExecState(NodeExecState state)
{
  if (m_execState != state) {
    m_execState = state;
    emit execStateChanged(m_execState);
  }
}

bool Node::isEditing() const
{
  return m_editing;
}

void Node::setEditing(bool editing)
{
  if (m_editing != editing) {
    m_editing = editing;
    emit editingChanged(m_editing);
  }
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

int Node::totalProgressSteps() const
{
  return m_totalProgressSteps;
}

void Node::setTotalProgressSteps(int steps)
{
  m_totalProgressSteps = steps;
  emit totalProgressStepsChanged(steps);
}

int Node::progressStep() const
{
  return m_progressStep;
}

void Node::setProgressStep(int step)
{
  m_progressStep = step;
  emit progressStepChanged(step);
}

QString Node::progressMessage() const
{
  return m_progressMessage;
}

void Node::setProgressMessage(const QString& message)
{
  m_progressMessage = message;
  emit progressMessageChanged(message);
}

void Node::resetProgress()
{
  m_totalProgressSteps = 0;
  m_progressStep = 0;
  m_progressMessage.clear();
}

bool Node::supportsCancelingMidExecution() const
{
  return m_supportsCancel;
}

bool Node::supportsCompletionMidExecution() const
{
  return m_supportsCompletion;
}

bool Node::isCanceled() const
{
  return m_canceled.load();
}

bool Node::isCompleted() const
{
  return m_completed.load();
}

void Node::cancelExecution()
{
  m_canceled = true;
  emit executionCanceled();
}

void Node::completeExecution()
{
  m_completed = true;
  emit executionCompleted();
}

void Node::resetExecutionFlags()
{
  m_canceled = false;
  m_completed = false;
}

void Node::setSupportsCancel(bool b)
{
  m_supportsCancel = b;
}

void Node::setSupportsCompletion(bool b)
{
  m_supportsCompletion = b;
}

void Node::setTypeInferenceSource(const QString& outputPortName,
                                  const QString& inputPortName)
{
  m_typeInferenceSources[outputPortName] = inputPortName;
}

void Node::recomputeEffectiveTypes()
{
  for (auto* output : m_outputPorts) {
    if (output->declaredType() != PortType::ImageData) {
      // Concrete types (TiltSeries, Volume, Table, etc.) are never inferred.
      output->setEffectiveType(output->declaredType());
      continue;
    }

    // Find the driving input port for this output.
    InputPort* driver = nullptr;
    auto it = m_typeInferenceSources.constFind(output->name());
    if (it != m_typeInferenceSources.constEnd()) {
      // Explicit mapping
      driver = inputPort(it.value());
    } else {
      // Default: first ImageData input port
      for (auto* input : m_inputPorts) {
        if (input->acceptedTypes().testFlag(PortType::ImageData)) {
          driver = input;
          break;
        }
      }
    }

    if (driver && driver->link() && driver->link()->from()) {
      // Inherit effective type from the upstream output
      output->setEffectiveType(driver->link()->from()->type());
    } else {
      // No connection — revert to declared type
      output->setEffectiveType(PortType::ImageData);
    }
  }
}

bool Node::hasInvalidInputLinks() const
{
  for (auto* input : m_inputPorts) {
    if (input->link() && !input->link()->isValid()) {
      return true;
    }
  }
  return false;
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

void Node::addOutputPort(OutputPort* port)
{
  port->setParent(this);
  m_outputPorts.append(port);
}

} // namespace pipeline
} // namespace tomviz
