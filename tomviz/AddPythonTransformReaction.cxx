/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "AddPythonTransformReaction.h"

#include "ActiveObjects.h"
#include "MainWindow.h"

#include "pipeline/Pipeline.h"
#include "pipeline/InputPort.h"
#include "pipeline/Link.h"
#include "pipeline/Node.h"
#include "pipeline/OutputPort.h"
#include "pipeline/SinkNode.h"
#include "pipeline/SourceNode.h"
#include "pipeline/TransformNode.h"
#include "pipeline/transforms/LegacyPythonTransform.h"

#include <pqApplicationCore.h>
#include <pqSettings.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtDebug>

namespace tomviz {

/// Find the tip output port of the branch containing the given node.
/// Walks downstream from the node through TransformNodes to find the end
/// of that specific branch. If the node is a SinkNode, walks upstream first
/// to find the feeding source/transform, then walks downstream from there.
static pipeline::OutputPort* findBranchTip(pipeline::Node* node)
{
  if (!node) {
    return nullptr;
  }

  // If it's a sink, step upstream to the node feeding it
  pipeline::Node* start = node;
  while (dynamic_cast<pipeline::SinkNode*>(start)) {
    auto upstream = start->upstreamNodes();
    if (upstream.isEmpty()) {
      return nullptr;
    }
    start = upstream.first();
  }

  if (start->outputPorts().isEmpty()) {
    return nullptr;
  }

  pipeline::OutputPort* tip = start->outputPorts()[0];

  // Walk downstream through transforms to the end of this branch
  pipeline::Node* current = start;
  while (true) {
    pipeline::TransformNode* nextTransform = nullptr;
    for (auto* downstream : current->downstreamNodes()) {
      if (auto* xf = dynamic_cast<pipeline::TransformNode*>(downstream)) {
        nextTransform = xf;
        break;
      }
    }
    if (!nextTransform || nextTransform->outputPorts().isEmpty()) {
      break;
    }
    tip = nextTransform->outputPorts()[0];
    current = nextTransform;
  }

  return tip;
}

/// Find the tip output port using contextNode to select the right branch.
/// If contextNode is null, falls back to the first source in the pipeline.
static pipeline::OutputPort* findTipOutputPort(
  pipeline::Pipeline* pip, pipeline::Node* contextNode)
{
  if (!pip) {
    return nullptr;
  }

  // If we have context, find the tip of the branch containing that node
  if (contextNode && pip->nodes().contains(contextNode)) {
    auto* tip = findBranchTip(contextNode);
    if (tip) {
      return tip;
    }
  }

  // Fallback: first source's branch
  for (auto* node : pip->nodes()) {
    if (auto* src = dynamic_cast<pipeline::SourceNode*>(node)) {
      return findBranchTip(src);
    }
  }

  return nullptr;
}

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

/// Insert a transform before the given existing transform node.
static void insertTransformBefore(
  pipeline::Pipeline* pip,
  pipeline::LegacyPythonTransform* transform,
  pipeline::TransformNode* before)
{
  // Find the port currently feeding the "before" node
  auto* beforeInput = before->inputPorts()[0];
  auto* upstreamLink = beforeInput->link();
  pipeline::OutputPort* upstreamPort =
    upstreamLink ? upstreamLink->from() : nullptr;

  pip->addNode(transform);

  if (upstreamPort) {
    pip->removeLink(upstreamLink);
    pip->createLink(upstreamPort, transform->inputPorts()[0]);
  }
  pip->createLink(transform->outputPorts()[0], beforeInput);
}

/// Insert a LegacyPythonTransform into the pipeline using selection-aware
/// logic. Returns true on success.
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
  auto* activePort = ao.activePort();
  auto* activeNode = ao.activeNode();

  // Case 1: An output port is explicitly selected — connect there
  if (activePort && activePort->node() &&
      pip->nodes().contains(activePort->node())) {
    appendTransformAtPort(pip, transform, activePort);
    pip->execute();
    return true;
  }

  // Case 2: A transform node is selected — ask insert-before or append
  auto* selectedTransform =
    dynamic_cast<pipeline::TransformNode*>(activeNode);
  if (selectedTransform && pip->nodes().contains(selectedTransform)) {
    bool insertBefore = false;

    auto settings = pqApplicationCore::instance()->settings();
    bool skipConfirm =
      settings->value("TransformInsertConfirm/DontAsk", false).toBool();

    if (skipConfirm) {
      insertBefore = true;
    } else {
      QMessageBox msgBox;
      msgBox.setWindowTitle("Insert Transform?");
      msgBox.setText(
        QString("Insert this transform before \"%1\" in the pipeline?")
          .arg(selectedTransform->label()));
      QAbstractButton* insertBtn =
        msgBox.addButton("Insert Before", QMessageBox::AcceptRole);
      msgBox.addButton("Append to End", QMessageBox::RejectRole);
      msgBox.setDefaultButton(
        qobject_cast<QPushButton*>(insertBtn));
      QCheckBox dontAskAgain("Don't ask again (always insert before)");
      msgBox.setCheckBox(&dontAskAgain);

      msgBox.exec();

      if (dontAskAgain.isChecked()) {
        settings->setValue("TransformInsertConfirm/DontAsk", true);
      }
      insertBefore = (msgBox.clickedButton() == insertBtn);
    }

    if (insertBefore) {
      insertTransformBefore(pip, transform, selectedTransform);
    } else {
      // Append to the tip of the branch containing the selected transform
      auto* tipPort = findBranchTip(selectedTransform);
      if (!tipPort) {
        qCritical("No output port found in pipeline.");
        delete transform;
        return false;
      }
      appendTransformAtPort(pip, transform, tipPort);
    }

    pip->execute();
    return true;
  }

  // Case 3: No port/transform selected — append at chain tip
  // (activeNode provides multi-source context)
  auto* tipPort = findTipOutputPort(pip, activeNode);
  if (!tipPort) {
    qCritical("No source or transform output port found in pipeline.");
    delete transform;
    return false;
  }

  appendTransformAtPort(pip, transform, tipPort);
  pip->execute();
  return true;
}

AddPythonTransformReaction::AddPythonTransformReaction(
  QAction* parentObject, const QString& l, const QString& s, bool rts, bool rv,
  bool rf, const QString& json)
  : pqReaction(parentObject), jsonSource(json), scriptLabel(l), scriptSource(s),
    interactive(false), requiresTiltSeries(rts), requiresVolume(rv),
    requiresFib(rf)
{
  connect(&ActiveObjects::instance(),
          &ActiveObjects::activePipelineChanged,
          this, &AddPythonTransformReaction::updateEnableState);

  // If we have JSON, check whether the operator is compatible with being run
  // in an external pipeline.
  if (!json.isEmpty()) {
    auto document = QJsonDocument::fromJson(json.toLatin1());
    if (!document.isObject()) {
      qCritical() << "Failed to parse operator JSON";
      qCritical() << json;
      return;
    } else {
      QJsonObject root = document.object();
      QJsonValueRef externalNode = root["externalCompatible"];
      if (!externalNode.isUndefined() && !externalNode.isNull()) {
        this->externalCompatible = externalNode.toBool();
      }
    }
  }

  updateEnableState();
}

void AddPythonTransformReaction::updateEnableState()
{
  auto* pip = ActiveObjects::instance().activeNewPipeline();
  parentAction()->setEnabled(pip != nullptr);
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
