/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PythonSource.h"

#include "ExternalNodeExecutor.h"
#include "OutputPort.h"

namespace tomviz {
namespace pipeline {

PythonSource::PythonSource(QObject* parent) : SourceNode(parent)
{
}

void PythonSource::setJSONDescription(const QString& json)
{
  m_backend.setJSONDescription(json);
  if (!m_backend.defaultLabel().isEmpty()) {
    setLabel(m_backend.defaultLabel());
  }
  setSupportsCancel(m_backend.supportsCancel());
  setSupportsCompletion(m_backend.supportsComplete());
  // Shape mismatch (a transform-shaped description loaded by a source
  // shell) is rejected upstream by the menu-routing reaction; loading
  // a saved state file trusts its serialized type string. Either way,
  // a SourceNode never accepts inputs — silently ignore any inputs[]
  // entries the description still carries.
  m_backend.applyDescription(
    nullptr,
    [this](const QString& name, PortType type) {
      return addOutput(name, type);
    });
}

QString PythonSource::jsonDescription() const
{
  return m_backend.jsonDescription();
}

void PythonSource::setScript(const QString& script)
{
  m_backend.setScript(script);
}

QString PythonSource::scriptSource() const
{
  return m_backend.scriptSource();
}

void PythonSource::setParameter(const QString& name, const QVariant& value)
{
  m_backend.setParameter(name, value);
}

QVariant PythonSource::parameter(const QString& name) const
{
  return m_backend.parameter(name);
}

QMap<QString, QVariant> PythonSource::parameters() const
{
  return m_backend.parameters();
}

QString PythonSource::operatorName() const
{
  return m_backend.operatorName();
}

bool PythonSource::execute()
{
  resetExecutionFlags();
  resetProgress();
  setExecState(NodeExecState::Running);

  auto outputs = m_backend.runSource(this);

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

QJsonObject PythonSource::serialize() const
{
  return m_backend.serializeInto(SourceNode::serialize());
}

bool PythonSource::deserialize(const QJsonObject& json)
{
  m_backend.applySerializedFields(
    json,
    nullptr,
    [this](const QString& name, PortType type) {
      return addOutput(name, type);
    });
  setSupportsCancel(m_backend.supportsCancel());
  setSupportsCompletion(m_backend.supportsComplete());
  if (!SourceNode::deserialize(json)) {
    return false;
  }
  if (!nodeExecutor()) {
    auto envPath = m_backend.externalPythonEnvPath();
    if (!envPath.isEmpty()) {
      setNodeExecutor(new ExternalNodeExecutor(envPath));
    }
  }
  return true;
}

} // namespace pipeline
} // namespace tomviz
