/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "TransformUtils.h"

#include "ActiveObjects.h"
#include "MainWindow.h"

#include "pipeline/DeferredLinkInfo.h"
#include "pipeline/InputPort.h"
#include "pipeline/Link.h"
#include "pipeline/Node.h"
#include "pipeline/OutputPort.h"
#include "pipeline/Pipeline.h"
#include "pipeline/SinkGroupNode.h"
#include "pipeline/SinkNode.h"
#include "pipeline/TransformEditDialog.h"
#include "pipeline/TransformNode.h"

#include <QApplication>
#include <QtDebug>

namespace tomviz {

/// True for SinkNode and SinkGroupNode (terminal pipeline nodes).
static bool isTerminalNode(pipeline::Node* node)
{
  return dynamic_cast<pipeline::SinkNode*>(node) ||
         dynamic_cast<pipeline::SinkGroupNode*>(node);
}

/// Append a transform at the given targetPort, moving sink/group links to the
/// new transform's output.
static void appendTransformAtPort(
  pipeline::Pipeline* pip,
  pipeline::TransformNode* transform,
  pipeline::OutputPort* targetPort)
{
  pip->addNode(transform);
  pip->createLink(targetPort, transform->inputPorts()[0]);

  // Re-link: move all terminal node links from old tip to new transform's output
  auto* newTip = transform->outputPorts()[0];
  QList<pipeline::Link*> linksToMove;
  for (auto* link : targetPort->links()) {
    if (link->to()->node() == transform) {
      continue; // skip the link we just created
    }
    if (isTerminalNode(link->to()->node())) {
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
  pipeline::TransformNode* transform,
  pipeline::OutputPort* targetPort)
{
  pip->addNode(transform);
  pip->createLink(targetPort, transform->inputPorts()[0]);

  pipeline::DeferredLinkInfo deferred;
  auto* newTip = transform->outputPorts()[0];
  for (auto* link : targetPort->links()) {
    if (link->to()->node() == transform) {
      continue; // skip the link we just created
    }
    if (isTerminalNode(link->to()->node())) {
      deferred.linksToBreak.append({ targetPort, link->to() });
      deferred.linksToCreate.append({ newTip, link->to() });
    }
  }
  return deferred;
}

/// Show a TransformEditDialog for a newly inserted transform with deferred
/// link info.
static void showInsertionDialog(
  pipeline::TransformNode* transform,
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
  pipeline::TransformNode* transform,
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
  pipeline::TransformNode* transform,
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

bool insertTransformIntoPipeline(pipeline::TransformNode* transform)
{
  auto* mainWindow = MainWindow::instance();
  auto* pip = mainWindow ? mainWindow->pipeline() : nullptr;
  if (!pip) {
    qCritical("insertTransformIntoPipeline: No active pipeline. "
              "Load data first. (activeWindow=%p, mainWindow=%p)",
              static_cast<void*>(activeWin),
              static_cast<void*>(mainWindow));
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
    // Multi-input: only the first input gets connected here.  Commit
    // immediately and wait for the user to connect remaining inputs via
    // manual linking (which triggers the MainWindow linkRequested handler).
    if (transform->inputPorts().size() > 1) {
      insertTransformAtLink(pip, transform, activeLink);
    } else if (transform->hasPropertiesWidget()) {
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
    qCritical("insertTransformIntoPipeline: No output port available. "
              "Load data first.");
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

  // Multi-input: same as above — commit and wait for remaining connections.
  if (transform->inputPorts().size() > 1) {
    appendTransformAtPort(pip, transform, tipPort);
  } else if (transform->hasPropertiesWidget()) {
    qDebug("insertTransformIntoPipeline: showing insertion dialog for '%s'",
           qPrintable(transform->label()));
    auto deferred = appendTransformAtPortDeferred(pip, transform, tipPort);
    showInsertionDialog(transform, pip, deferred, mainWindow);
  } else {
    qDebug("insertTransformIntoPipeline: appending '%s' at tip and executing",
           qPrintable(transform->label()));
    appendTransformAtPort(pip, transform, tipPort);
    pip->execute();
  }
  return true;
}

} // namespace tomviz
