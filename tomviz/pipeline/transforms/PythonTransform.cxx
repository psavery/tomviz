/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PythonTransform.h"

#include "CustomNodeWidgetRegistry.h"
#include "CustomPythonNodeWidget.h"
#include "ExternalNodeExecutor.h"
#include "ParameterBindingUtils.h"
#include "PythonNodeEditorWidget.h"

#include <QString>

namespace tomviz {
namespace pipeline {

PythonTransform::PythonTransform(QObject* parent) : TransformNode(parent)
{
}

void PythonTransform::setJSONDescription(const QString& json)
{
  m_backend.setJSONDescription(json);
  if (!m_backend.defaultLabel().isEmpty()) {
    setLabel(m_backend.defaultLabel());
  }
  setSupportsCancel(m_backend.supportsCancel());
  setSupportsCompletion(m_backend.supportsComplete());
  m_backend.applyDescription(
    [this](const QString& name, PortType type) {
      addInput(name, type);
    },
    [this](const QString& name, PortType type) {
      return addOutput(name, type);
    });
}

QString PythonTransform::jsonDescription() const
{
  return m_backend.jsonDescription();
}

void PythonTransform::setScript(const QString& script)
{
  m_backend.setScript(script);
}

QString PythonTransform::scriptSource() const
{
  return m_backend.scriptSource();
}

void PythonTransform::setParameter(const QString& name, const QVariant& value)
{
  m_backend.setParameter(name, value);
}

QVariant PythonTransform::parameter(const QString& name) const
{
  return m_backend.parameter(name);
}

QMap<QString, QVariant> PythonTransform::parameters() const
{
  return m_backend.parameters();
}

QString PythonTransform::operatorName() const
{
  return m_backend.operatorName();
}

QString PythonTransform::customWidgetID() const
{
  return m_backend.customWidgetID();
}

bool PythonTransform::hasPropertiesWidget() const
{
  return true;
}

bool PythonTransform::propertiesWidgetNeedsInput() const
{
  auto id = m_backend.customWidgetID();
  if (id.isEmpty()) {
    return false;
  }
  const auto* info = findCustomNodeWidget(id);
  return info && info->needsData;
}

EditNodeWidget* PythonTransform::createPropertiesWidget(QWidget* parent)
{
  CustomPythonNodeWidget* customWidget = nullptr;

  auto widgetID = m_backend.customWidgetID();
  if (!widgetID.isEmpty()) {
    if (const auto* info = findCustomNodeWidget(widgetID)) {
      if (info->create) {
        customWidget = info->create(collectInputs(), parent);
        if (customWidget) {
          customWidget->setValues(m_backend.parameters());
          customWidget->setScript(m_backend.scriptSource());
        }
      }
    }
  }

  QString currentType;
  QString currentEnvPath;
  if (auto* ext = qobject_cast<ExternalNodeExecutor*>(nodeExecutor())) {
    currentType = ExternalNodeExecutor::typeString();
    currentEnvPath = ext->envPath();
  }

  auto* widget = new PythonNodeEditorWidget(
    label(), m_backend.scriptSource(), m_backend.jsonDescription(),
    m_backend.parameters(), currentType, currentEnvPath, customWidget,
    parent);

  connect(widget, &PythonNodeEditorWidget::applied, this,
          [this, customWidget](const QString& newLabel,
                               const QString& newScript,
                               const QMap<QString, QVariant>& values,
                               const QString& executorType,
                               const QString& executorEnvPath) {
            bool changed = false;

            if (label() != newLabel) {
              setLabel(newLabel);
              changed = true;
            }

            if (m_backend.scriptSource() != newScript) {
              m_backend.setScript(newScript);
              changed = true;
            }

            // Custom widget supplies its own parameter values; the
            // auto-generated form's values are still used as a base
            // (default values for fields the custom widget doesn't
            // touch), then the custom widget overlays its own.
            QMap<QString, QVariant> finalValues = values;
            if (customWidget) {
              customWidget->getValues(finalValues);
              customWidget->writeSettings();
            }

            for (auto it = finalValues.constBegin();
                 it != finalValues.constEnd(); ++it) {
              if (m_backend.parameter(it.key()) != it.value()) {
                changed = true;
              }
              m_backend.setParameter(it.key(), it.value());
            }

            auto* currentExternal =
              qobject_cast<ExternalNodeExecutor*>(nodeExecutor());
            if (executorType.isEmpty()) {
              if (nodeExecutor() != nullptr) {
                setNodeExecutor(nullptr);
                changed = true;
              }
            } else if (executorType == ExternalNodeExecutor::typeString()) {
              if (currentExternal) {
                if (currentExternal->envPath() != executorEnvPath) {
                  currentExternal->setEnvPath(executorEnvPath);
                  changed = true;
                }
              } else {
                setNodeExecutor(new ExternalNodeExecutor(executorEnvPath));
                changed = true;
              }
            }

            if (changed) {
              emit parametersApplied();
            }
          });

  wireParameterBindings(this, widget, m_backend.parameterBindings());

  return widget;
}

QJsonObject PythonTransform::serialize() const
{
  return m_backend.serializeInto(TransformNode::serialize());
}

bool PythonTransform::deserialize(const QJsonObject& json)
{
  // Apply description first so addInput / addOutput callbacks create
  // the ports before TransformNode::deserialize replays per-port
  // state from the saved JSON.
  m_backend.applySerializedFields(
    json,
    [this](const QString& name, PortType type) {
      addInput(name, type);
    },
    [this](const QString& name, PortType type) {
      return addOutput(name, type);
    });
  setSupportsCancel(m_backend.supportsCancel());
  setSupportsCompletion(m_backend.supportsComplete());
  if (!TransformNode::deserialize(json)) {
    return false;
  }
  // Legacy compatibility: an operator description may carry a
  // `tomviz_pipeline_env` field that pre-dates the per-node executor
  // serialization. Synthesize an ExternalNodeExecutor when set and
  // Node::deserialize didn't already install one.
  if (!nodeExecutor()) {
    auto envPath = m_backend.externalPythonEnvPath();
    if (!envPath.isEmpty()) {
      setNodeExecutor(new ExternalNodeExecutor(envPath));
    }
  }
  return true;
}

QMap<QString, PortData> PythonTransform::transform(
  const QMap<QString, PortData>& inputs)
{
  return m_backend.runTransform(this, inputs);
}

} // namespace pipeline
} // namespace tomviz
