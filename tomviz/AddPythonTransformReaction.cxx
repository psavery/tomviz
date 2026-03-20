/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "AddPythonTransformReaction.h"

#include "ActiveObjects.h"
#include "MainWindow.h"

#include "pipeline/DeferredLinkInfo.h"
#include "pipeline/Pipeline.h"
#include "pipeline/InputPort.h"
#include "pipeline/Link.h"
#include "pipeline/Node.h"
#include "pipeline/OutputPort.h"
#include "pipeline/SinkNode.h"
#include "pipeline/TransformEditDialog.h"
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

/// Append a transform at the given targetPort, moving sink links to the
/// new transform's output.
static void appendTransformAtPort(
  pipeline::Pipeline* pip,
  pipeline::LegacyPythonTransform* transform,
  pipeline::OutputPort* targetPort)
{
  pip->addNode(transform);
  pip->createLink(targetPort, transform->inputPorts()[0]);

  // Re-link: move all sink links from old tip to new transform's output
  auto* newTip = transform->outputPorts()[0];
  QList<pipeline::Link*> linksToMove;
  for (auto* link : targetPort->links()) {
    auto* targetNode = link->to()->node();
    if (dynamic_cast<pipeline::SinkNode*>(targetNode)) {
      linksToMove.append(link);
    }
  }
  for (auto* link : linksToMove) {
    auto* sinkInput = link->to();
    pip->removeLink(link);
    pip->createLink(newTip, sinkInput);
  }
}

/// Deferred variant: adds node and input link only, returns deferred info
/// for the output links to be completed later.
static pipeline::DeferredLinkInfo appendTransformAtPortDeferred(
  pipeline::Pipeline* pip,
  pipeline::LegacyPythonTransform* transform,
  pipeline::OutputPort* targetPort)
{
  pip->addNode(transform);
  pip->createLink(targetPort, transform->inputPorts()[0]);

  pipeline::DeferredLinkInfo deferred;
  auto* newTip = transform->outputPorts()[0];
  for (auto* link : targetPort->links()) {
    auto* targetNode = link->to()->node();
    if (dynamic_cast<pipeline::SinkNode*>(targetNode)) {
      deferred.linksToBreak.append({ targetPort, link->to() });
      deferred.linksToCreate.append({ newTip, link->to() });
    }
  }
  return deferred;
}

/// Show a TransformEditDialog for a newly inserted transform with deferred
/// link info.
static void showInsertionDialog(
  pipeline::LegacyPythonTransform* transform,
  pipeline::Pipeline* pip,
  const pipeline::DeferredLinkInfo& deferred,
  QWidget* parent)
{
  auto* dialog = new pipeline::TransformEditDialog(
    transform, pip, deferred, parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(
    QString("Configure - %1").arg(transform->label()));
  dialog->show();
}

/// Insert a transform at a link: break the existing link, connect the
/// link's "from" port to the new transform's input, and connect the new
/// transform's output to the link's "to" port.
static void insertTransformAtLink(
  pipeline::Pipeline* pip,
  pipeline::LegacyPythonTransform* transform,
  pipeline::Link* link)
{
  auto* fromPort = link->from();
  auto* toPort = link->to();
  pip->removeLink(link);
  pip->addNode(transform);
  pip->createLink(fromPort, transform->inputPorts()[0]);
  pip->createLink(transform->outputPorts()[0], toPort);
}

/// Deferred variant for insert-at-link.
static pipeline::DeferredLinkInfo insertTransformAtLinkDeferred(
  pipeline::Pipeline* pip,
  pipeline::LegacyPythonTransform* transform,
  pipeline::Link* link)
{
  auto* fromPort = link->from();
  auto* toPort = link->to();

  pip->addNode(transform);
  pip->createLink(fromPort, transform->inputPorts()[0]);

  pipeline::DeferredLinkInfo deferred;
  deferred.linksToBreak.append({ fromPort, toPort });
  deferred.linksToCreate.append({ transform->outputPorts()[0], toPort });
  return deferred;
}

/// Insert a LegacyPythonTransform at the active tip output port.
/// Returns true on success.
static bool insertTransformIntoPipeline(
  pipeline::LegacyPythonTransform* transform)
{
  auto* mainWindow = qobject_cast<MainWindow*>(QApplication::activeWindow());
  auto* pip = mainWindow ? mainWindow->pipeline() : nullptr;
  if (!pip) {
    qCritical("No active pipeline. Load data first.");
    delete transform;
    return false;
  }

  // Ctrl held: add the node unconnected (user will link manually)
  if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
    pip->addNode(transform);
    return true;
  }

  auto& ao = ActiveObjects::instance();
  auto* input = transform->inputPorts()[0];

  // If a link is selected and its "to" is a transform, insert between them
  auto* activeLink = ao.activeLink();
  if (activeLink &&
      dynamic_cast<pipeline::TransformNode*>(activeLink->to()->node())) {
    auto* fromPort = activeLink->from();
    if (!pipeline::isPortTypeCompatible(fromPort->type(),
                                        input->acceptedTypes())) {
      qCritical("Incompatible port types: transform input does not accept "
                "the link's output port type.");
      delete transform;
      return false;
    }
    if (transform->hasPropertiesWidget()) {
      auto deferred =
        insertTransformAtLinkDeferred(pip, transform, activeLink);
      showInsertionDialog(transform, pip, deferred, mainWindow);
    } else {
      insertTransformAtLink(pip, transform, activeLink);
      pip->execute();
    }
    return true;
  }

  // Otherwise append at the tip output port
  auto* tipPort = ao.activeTipOutputPort();
  if (!tipPort) {
    qCritical("No output port available. Load data first.");
    delete transform;
    return false;
  }

  if (!pipeline::isPortTypeCompatible(tipPort->type(),
                                      input->acceptedTypes())) {
    qCritical("Incompatible port types: transform input does not accept "
              "the tip output port type.");
    delete transform;
    return false;
  }

  if (transform->hasPropertiesWidget()) {
    auto deferred = appendTransformAtPortDeferred(pip, transform, tipPort);
    showInsertionDialog(transform, pip, deferred, mainWindow);
  } else {
    appendTransformAtPort(pip, transform, tipPort);
    pip->execute();
  }
  return true;
}

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
