/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "AddPythonTransformReaction.h"

#include "ActiveObjects.h"
#include "MainWindow.h"
#include "TransformUtils.h"

#include "pipeline/InputPort.h"
#include "pipeline/OutputPort.h"
#include "pipeline/Pipeline.h"
#include "pipeline/TransformNode.h"
#include "pipeline/transforms/LegacyPythonTransform.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtDebug>

namespace tomviz {

static pipeline::PortType portTypeFromString(const QString& str)
{
  if (str == "TiltSeries")
    return pipeline::PortType::TiltSeries;
  if (str == "Volume")
    return pipeline::PortType::Volume;
  if (str == "ImageData")
    return pipeline::PortType::ImageData;
  return pipeline::PortType::None;
}

AddPythonTransformReaction::AddPythonTransformReaction(
  QAction* parentObject, const QString& l, const QString& s,
  const QString& json)
  : pqReaction(parentObject), jsonSource(json), scriptLabel(l), scriptSource(s),
    interactive(false)
{
  connect(&ActiveObjects::instance(),
          &ActiveObjects::activePipelineChanged,
          this, &AddPythonTransformReaction::updateEnableState);
  connect(&ActiveObjects::instance(),
          &ActiveObjects::activeTipOutputPortChanged,
          this, &AddPythonTransformReaction::updateEnableState);

  // Parse JSON to extract inputType and externalCompatible.
  if (!json.isEmpty()) {
    auto document = QJsonDocument::fromJson(json.toLatin1());
    if (!document.isObject()) {
      qCritical() << "Failed to parse operator JSON";
      qCritical() << json;
      return;
    }
    QJsonObject root = document.object();
    QJsonValueRef externalNode = root["externalCompatible"];
    if (!externalNode.isUndefined() && !externalNode.isNull()) {
      this->externalCompatible = externalNode.toBool();
    }
    if (root.contains("inputType")) {
      auto pt = portTypeFromString(root.value("inputType").toString());
      if (pt != pipeline::PortType::None) {
        m_acceptedInputTypes = pt;
      }
    }
  }

  // Extract the description from JSON and set it on the action for the search
  // dialog to display.
  if (!json.isEmpty()) {
    auto descDoc = QJsonDocument::fromJson(json.toLatin1());
    if (descDoc.isObject()) {
      QString desc = descDoc.object().value("description").toString();
      if (!desc.isEmpty()) {
        parentObject->setStatusTip(desc);
      }
    }
  }

  qApp->installEventFilter(this);
  updateEnableState();
}

bool AddPythonTransformReaction::eventFilter(QObject* obj, QEvent* event)
{
  if (event->type() == QEvent::KeyPress ||
      event->type() == QEvent::KeyRelease) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Control) {
      m_ctrlHeld = (event->type() == QEvent::KeyPress);
      updateEnableState();
    }
  }
  return pqReaction::eventFilter(obj, event);
}

void AddPythonTransformReaction::updateEnableState()
{
  // Ctrl held: user wants to add an unconnected node, so always enable.
  if (m_ctrlHeld) {
    parentAction()->setEnabled(true);
    return;
  }

  auto& ao = ActiveObjects::instance();
  auto* tipPort = ao.activeTipOutputPort();
  if (!tipPort) {
    parentAction()->setEnabled(false);
    return;
  }
  parentAction()->setEnabled(
    pipeline::isPortTypeCompatible(tipPort->type(), m_acceptedInputTypes));
}

OperatorPython* AddPythonTransformReaction::addExpression(DataSource*)
{
  auto* mainWindow = qobject_cast<MainWindow*>(QApplication::activeWindow());
  auto* pip = mainWindow ? mainWindow->pipeline() : nullptr;
  if (!pip) {
    return nullptr;
  }

  bool hasJson = this->jsonSource.size() > 0;

  // Create the transform node
  auto* transform = new pipeline::LegacyPythonTransform();
  transform->setLabel(scriptLabel);
  transform->setScript(scriptSource);

  if (hasJson) {
    transform->setJSONDescription(jsonSource);

    // If the transform has parameters (from JSON), show properties after
    // insertion. For now, just insert it directly.
    insertTransformIntoPipeline(transform);
  } else {
    // Simple script with no JSON, no custom UI — insert directly
    insertTransformIntoPipeline(transform);
  }

  return nullptr;
}

void AddPythonTransformReaction::addExpressionFromNonModalDialog()
{
  // Non-modal dialogs for Clear Volume and Background Subtraction are not
  // yet supported in the new pipeline. Log and return.
  qWarning("Non-modal transform dialogs not yet supported in new pipeline.");
}

void AddPythonTransformReaction::addPythonOperator(
  DataSource*, const QString& scriptLabel,
  const QString& scriptBaseString, const QMap<QString, QVariant> arguments,
  const QString& jsonString)
{
  auto* transform = new pipeline::LegacyPythonTransform();
  transform->setLabel(scriptLabel);
  transform->setScript(scriptBaseString);
  if (!jsonString.isEmpty()) {
    transform->setJSONDescription(jsonString);
  }
  for (auto it = arguments.begin(); it != arguments.end(); ++it) {
    transform->setParameter(it.key(), it.value());
  }
  insertTransformIntoPipeline(transform);
}

void AddPythonTransformReaction::addPythonOperator(
  DataSource*, const QString& scriptLabel,
  const QString& scriptBaseString, const QMap<QString, QVariant> arguments,
  const QMap<QString, QString>)
{
  auto* transform = new pipeline::LegacyPythonTransform();
  transform->setLabel(scriptLabel);
  transform->setScript(scriptBaseString);
  for (auto it = arguments.begin(); it != arguments.end(); ++it) {
    transform->setParameter(it.key(), it.value());
  }
  insertTransformIntoPipeline(transform);
}
} // namespace tomviz
