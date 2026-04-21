/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "Node.h"

#include "InputPort.h"
#include "Link.h"
#include "OutputPort.h"

#include <QJsonArray>

namespace tomviz {
namespace pipeline {

namespace {

QJsonArray portTypesToJson(PortTypes types)
{
  QJsonArray arr;
  for (PortType t : { PortType::ImageData, PortType::TiltSeries,
                      PortType::Volume, PortType::Image, PortType::Scalar,
                      PortType::Array, PortType::Table, PortType::Molecule }) {
    if (types.testFlag(t)) {
      arr.append(portTypeToString(t));
    }
  }
  return arr;
}

QString nodeStateToString(NodeState state)
{
  switch (state) {
    case NodeState::New:
      return QStringLiteral("New");
    case NodeState::Stale:
      return QStringLiteral("Stale");
    case NodeState::Current:
      return QStringLiteral("Current");
  }
  return QStringLiteral("New");
}

} // namespace

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

void Node::setStateNoCascade(NodeState state)
{
  if (m_state != state) {
    m_state = state;
    emit stateChanged(m_state);
  }
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
  return QIcon(QStringLiteral(":/pipeline/pqInspect.png"));
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

QJsonObject Node::serialize() const
{
  QJsonObject json;
  json[QStringLiteral("label")] = m_label;
  if (m_state != NodeState::New) {
    // Defaults to New on load; only persist when different.
    json[QStringLiteral("state")] = nodeStateToString(m_state);
  }
  if (m_breakpoint) {
    json[QStringLiteral("breakpoint")] = true;
  }
  if (!m_properties.isEmpty()) {
    json[QStringLiteral("properties")] =
      QJsonObject::fromVariantMap(m_properties);
  }
  if (!m_typeInferenceSources.isEmpty()) {
    QJsonObject tis;
    for (auto it = m_typeInferenceSources.constBegin();
         it != m_typeInferenceSources.constEnd(); ++it) {
      tis[it.key()] = it.value();
    }
    json[QStringLiteral("typeInferenceSources")] = tis;
  }
  if (!m_outputPorts.isEmpty()) {
    QJsonObject outputs;
    for (auto* port : m_outputPorts) {
      QJsonObject entry;
      entry[QStringLiteral("type")] =
        portTypeToString(port->declaredType());
      entry[QStringLiteral("persistent")] = !port->isTransient();
      // Delegate payload serialization to the port itself; base OutputPort
      // dispatches on PortData's payload type. Node stays agnostic.
      auto metadata = port->serialize();
      if (!metadata.isEmpty()) {
        entry[QStringLiteral("metadata")] = metadata;
      }
      outputs[port->name()] = entry;
    }
    json[QStringLiteral("outputPorts")] = outputs;
  }
  if (!m_inputPorts.isEmpty()) {
    QJsonObject inputs;
    for (auto* port : m_inputPorts) {
      QJsonObject entry;
      entry[QStringLiteral("type")] =
        portTypesToJson(port->acceptedTypes());
      inputs[port->name()] = entry;
    }
    json[QStringLiteral("inputPorts")] = inputs;
  }
  return json;
}

bool Node::deserialize(const QJsonObject& json)
{
  if (json.contains(QStringLiteral("label"))) {
    setLabel(json.value(QStringLiteral("label")).toString());
  }
  if (json.contains(QStringLiteral("state"))) {
    auto s = json.value(QStringLiteral("state")).toString();
    if (s == QLatin1String("Stale")) {
      markStale();
    } else if (s == QLatin1String("Current")) {
      markCurrent();
    }
    // "New" is the default; nothing to do.
  }
  if (json.contains(QStringLiteral("breakpoint"))) {
    setBreakpoint(json.value(QStringLiteral("breakpoint")).toBool());
  }
  if (json.contains(QStringLiteral("properties"))) {
    m_properties =
      json.value(QStringLiteral("properties")).toObject().toVariantMap();
  }
  if (json.contains(QStringLiteral("typeInferenceSources"))) {
    m_typeInferenceSources.clear();
    auto tis =
      json.value(QStringLiteral("typeInferenceSources")).toObject();
    for (auto it = tis.constBegin(); it != tis.constEnd(); ++it) {
      m_typeInferenceSources[it.key()] = it.value().toString();
    }
  }
  if (json.contains(QStringLiteral("outputPorts"))) {
    auto outputs = json.value(QStringLiteral("outputPorts")).toObject();
    for (auto it = outputs.constBegin(); it != outputs.constEnd(); ++it) {
      auto* port = outputPort(it.key());
      if (!port) {
        continue;
      }
      auto entry = it.value().toObject();
      // Restore declaredType so a reader-style source whose effective
      // type was promoted at execute time (e.g. ImageData → TiltSeries)
      // lands on the saved type *before* links are resolved, letting
      // downstream inference pick it up.
      if (entry.contains(QStringLiteral("type"))) {
        PortType t =
          portTypeFromString(entry.value(QStringLiteral("type")).toString());
        if (t != PortType::None && t != port->declaredType()) {
          port->setDeclaredType(t);
        }
      }
      if (entry.contains(QStringLiteral("persistent"))) {
        port->setTransient(
          !entry.value(QStringLiteral("persistent")).toBool());
      }
      if (entry.contains(QStringLiteral("metadata"))) {
        port->deserialize(
          entry.value(QStringLiteral("metadata")).toObject());
      }
    }
  }
  // inputPorts: types are intrinsic to the Node subclass, so we do not
  // restore acceptedTypes here. Subclasses with dynamic inputs (e.g.
  // SinkGroupNode) are expected to create their ports before calling
  // Node::deserialize so per-port state can apply.
  return true;
}

} // namespace pipeline
} // namespace tomviz
