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
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtDebug>

namespace tomviz {

/// Find the "tip" output port in the pipeline — the last transform's output
/// or the source's output if no transforms exist.
static pipeline::OutputPort* findTipOutputPort(pipeline::Pipeline* pip)
{
  if (!pip) {
    return nullptr;
  }

  auto nodes = pip->nodes();
  pipeline::OutputPort* tipPort = nullptr;

  for (auto* node : nodes) {
    auto* source = dynamic_cast<pipeline::SourceNode*>(node);
    auto* transform = dynamic_cast<pipeline::TransformNode*>(node);

    if (source && !source->outputPorts().isEmpty()) {
      if (!tipPort) {
        tipPort = source->outputPorts()[0];
      }
    }
    if (transform && !transform->outputPorts().isEmpty()) {
      tipPort = transform->outputPorts()[0];
    }
  }

  return tipPort;
}

/// Insert a LegacyPythonTransform into the pipeline between the tip and sinks.
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

  auto* tipPort = findTipOutputPort(pip);
  if (!tipPort) {
    qCritical("No source or transform output port found in pipeline.");
    delete transform;
    return false;
  }

  // Add the transform to the pipeline
  pip->addNode(transform);

  // Link tip -> transform input
  pip->createLink(tipPort, transform->inputPorts()[0]);

  // Re-link: move all sink links from old tip to new transform's output
  auto* newTip = transform->outputPorts()[0];
  QList<pipeline::Link*> linksToMove;
  for (auto* link : tipPort->links()) {
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

  // Execute the pipeline
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
