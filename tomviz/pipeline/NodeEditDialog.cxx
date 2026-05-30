/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "NodeEditDialog.h"

#include "EditNodeWidget.h"
#include "InputPort.h"
#include "Link.h"
#include "Node.h"
#include "OutputPort.h"
#include "Pipeline.h"

#include "Utilities.h"

#include <pqApplicationCore.h>
#include <pqSettings.h>

#include <QDialogButtonBox>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

namespace tomviz {
namespace pipeline {

NodeEditDialog::NodeEditDialog(Node* node, Pipeline* pipeline, QWidget* parent)
  : QDialog(parent), m_node(node), m_pipeline(pipeline),
    m_isNewInsertion(false)
{
  init();
}

NodeEditDialog::NodeEditDialog(Node* node, Pipeline* pipeline,
                               const DeferredLinkInfo& deferred,
                               QWidget* parent)
  : QDialog(parent), m_node(node), m_pipeline(pipeline),
    m_deferred(deferred), m_isNewInsertion(true)
{
  init();
}

NodeEditDialog::~NodeEditDialog()
{
  saveGeometry();

  // Restore the parametersApplied → execute() auto-wiring that was
  // disconnected in init(), unless the node was removed (cancel in insertion
  // mode).
  if (m_node && m_pipeline && m_pipeline->nodes().contains(m_node)) {
    m_node->setEditing(false);
    connect(m_node, &Node::parametersApplied, m_pipeline,
            [pip = m_pipeline]() { pip->execute(); });
  }
}

Node* NodeEditDialog::node() const
{
  return m_node;
}

void NodeEditDialog::init()
{
  m_node->setEditing(true);

  // Suppress the auto-execute wiring so the dialog controls execution.
  QObject::disconnect(m_node, &Node::parametersApplied, m_pipeline, nullptr);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(5);

  m_buttonBox = new QDialogButtonBox(
    QDialogButtonBox::Apply | QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
    Qt::Horizontal, this);

  m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(false);

  connect(m_buttonBox, &QDialogButtonBox::accepted, this,
          &NodeEditDialog::onOkay);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this,
          &NodeEditDialog::reject);
  connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
          this, &NodeEditDialog::onApply);

  m_editWidget = m_node->createPropertiesWidget(m_pipeline, this);
  if (m_editWidget) {
    layout->addWidget(m_editWidget, 1);
    connect(m_editWidget, &EditNodeWidget::canApplyChanged,
            this, &NodeEditDialog::refreshButtonEnablement);
  }

  layout->addWidget(m_buttonBox);

  restoreGeometry();

  connect(m_pipeline, &Pipeline::executionStarted,
          this, &NodeEditDialog::refreshButtonEnablement);
  connect(m_pipeline, &Pipeline::executionFinished,
          this, &NodeEditDialog::refreshButtonEnablement);
  refreshButtonEnablement();
}

void NodeEditDialog::refreshButtonEnablement()
{
  bool canCommit = m_editWidget && m_editWidget->canApply() &&
                   !m_pipeline->isExecuting();
  m_buttonBox->button(QDialogButtonBox::Apply)->setEnabled(canCommit);
  m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(canCommit);
}

void NodeEditDialog::onApply()
{
  if (m_editWidget) {
    m_editWidget->applyChangesToOperator();
  }

  if (m_isNewInsertion && !m_insertionCompleted) {
    completeInsertion();
  }

  m_node->markStale();
  m_pipeline->execute();
}

void NodeEditDialog::onOkay()
{
  if (m_editWidget) {
    m_editWidget->applyChangesToOperator();
  }

  if (m_isNewInsertion && !m_insertionCompleted) {
    completeInsertion();
  }

  m_node->markStale();
  m_pipeline->execute();
  accept();
}

void NodeEditDialog::reject()
{
  if (m_isNewInsertion && !m_insertionCompleted) {
    // The insertion was applied eagerly when the dialog opened.  Undo it so
    // that cancel is a true no-op.  Removing the new node also drops its own
    // input/output links (for sources there are none).
    m_pipeline->removeNode(m_node);
    m_node = nullptr;

    // Recreate the original links that were broken to insert the node.
    for (const auto& ep : m_deferred.linksToRestore) {
      if (ep.from && ep.to) {
        m_pipeline->createLink(ep.from, ep.to);
      }
    }

    // createLink() above marks the affected downstream subtree stale.
    // Restore the states captured before the insertion so nothing is left
    // spuriously stale (which would otherwise force a needless re-run).
    for (const auto& ps : m_deferred.portStaleStates) {
      if (ps.port) {
        ps.port->setStale(ps.stale);
      }
    }
    for (const auto& ns : m_deferred.nodeStates) {
      if (ns.node) {
        ns.node->setStateNoCascade(ns.state);
      }
    }

    emit insertionCanceled();
  }

  QDialog::reject();
}

void NodeEditDialog::showEvent(QShowEvent* event)
{
  QDialog::showEvent(event);

  auto* mainWin = tomviz::mainWidget();
  if (!mainWin) {
    return;
  }

  auto* screen = mainWin->screen();
  auto screenGeom = screen ? screen->availableGeometry()
                           : QRect(0, 0, 1920, 1080);

  auto mainCenter = mainWin->frameGeometry().center();
  auto dlgSize = frameGeometry().size();

  int x = mainCenter.x() - dlgSize.width() / 2;
  int y = mainCenter.y() - dlgSize.height() / 2;

  x = qBound(screenGeom.left(), x,
              screenGeom.right() - dlgSize.width());
  y = qBound(screenGeom.top(), y,
              screenGeom.bottom() - dlgSize.height());

  move(x, y);
  raise();
  activateWindow();
}

void NodeEditDialog::saveGeometry()
{
  if (!m_node) {
    return;
  }
  QSettings* settings = pqApplicationCore::instance()->settings();
  QString key =
    QString("Edit%1NodeDialogGeometry").arg(m_node->label());
  settings->setValue(key, QVariant(geometry()));
}

void NodeEditDialog::restoreGeometry()
{
  if (!m_node) {
    return;
  }
  QSettings* settings = pqApplicationCore::instance()->settings();
  QString key =
    QString("Edit%1NodeDialogGeometry").arg(m_node->label());
  QVariant saved = settings->value(key);
  if (!saved.isNull()) {
    resize(saved.toRect().size());
  } else {
    resize(900, 700);
  }
}

void NodeEditDialog::completeInsertion()
{
  // The insertion (node + link rewiring) was performed eagerly when the
  // dialog opened, so the pipeline already has its final topology.  There is
  // nothing left to rewire here -- just mark the insertion committed so the
  // cancel path will not try to roll it back.
  m_insertionCompleted = true;
  m_isNewInsertion = false;
  emit insertionCompleted(m_node);
}

} // namespace pipeline
} // namespace tomviz
