/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "TransformEditDialog.h"

#include "EditTransformWidget.h"
#include "InputPort.h"
#include "Link.h"
#include "OutputPort.h"
#include "Pipeline.h"
#include "TransformNode.h"

#include "Utilities.h"

#include <pqApplicationCore.h>
#include <pqSettings.h>

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

namespace tomviz {
namespace pipeline {

TransformEditDialog::TransformEditDialog(TransformNode* transform,
                                         Pipeline* pipeline, QWidget* parent)
  : QDialog(parent), m_transform(transform), m_pipeline(pipeline),
    m_isNewInsertion(false)
{
  init();
}

TransformEditDialog::TransformEditDialog(TransformNode* transform,
                                         Pipeline* pipeline,
                                         const DeferredLinkInfo& deferred,
                                         QWidget* parent)
  : QDialog(parent), m_transform(transform), m_pipeline(pipeline),
    m_deferred(deferred), m_isNewInsertion(true)
{
  init();
}

TransformEditDialog::~TransformEditDialog()
{
  saveGeometry();

  // Restore the parametersApplied → execute() auto-wiring that was
  // disconnected in init(), unless the node was removed (cancel in insertion
  // mode).
  if (m_transform && m_pipeline &&
      m_pipeline->nodes().contains(m_transform)) {
    m_transform->setEditing(false);
    connect(m_transform, &TransformNode::parametersApplied, m_pipeline,
            [pip = m_pipeline]() { pip->execute(); });
  }
}

TransformNode* TransformEditDialog::transform() const
{
  return m_transform;
}

void TransformEditDialog::init()
{
  m_transform->setEditing(true);

  // Suppress the auto-execute wiring so the dialog controls execution.
  QObject::disconnect(m_transform, &TransformNode::parametersApplied,
                      m_pipeline, nullptr);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(5);

  // Dialog buttons (created before setupContent so it can disable them)
  m_buttonBox = new QDialogButtonBox(
    QDialogButtonBox::Apply | QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
    Qt::Horizontal, this);

  m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(false);

  connect(m_buttonBox, &QDialogButtonBox::accepted, this,
          &TransformEditDialog::onOkay);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this,
          &TransformEditDialog::onCancel);
  connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
          this, &TransformEditDialog::onApply);

  // Content area (properties widget or warning)
  setupContent();

  // Add button box at the bottom
  layout->addWidget(m_buttonBox);

  restoreGeometry();

  // Disable Apply/OK while the pipeline is executing.
  auto updateButtons = [this](bool executing) {
    m_buttonBox->button(QDialogButtonBox::Apply)->setEnabled(!executing);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!executing);
  };
  connect(m_pipeline, &Pipeline::executionStarted, this,
          [updateButtons]() { updateButtons(true); });
  connect(m_pipeline, &Pipeline::executionFinished, this,
          [updateButtons]() { updateButtons(false); });
  if (m_pipeline->isExecuting()) {
    updateButtons(true);
  }
}

bool TransformEditDialog::inputsReady() const
{
  if (!m_transform->propertiesWidgetNeedsInput()) {
    return true;
  }

  // Widget needs input data — all inputs must be connected with current data
  for (auto* input : m_transform->inputPorts()) {
    if (!input->link() || input->isStale() || !input->hasData()) {
      return false;
    }
  }

  return true;
}

void TransformEditDialog::setupContent()
{
  auto* layout = qobject_cast<QVBoxLayout*>(this->layout());

  if (!inputsReady()) {
    // propertiesWidgetNeedsInput() is true but inputs aren't available
    auto* warning = new QLabel(this);
    warning->setWordWrap(true);
    warning->setAlignment(Qt::AlignCenter);
    warning->setText(
      tr("This transform's properties widget requires connected, current "
         "input data.\n\nPlease ensure all input ports are connected and "
         "upstream transforms have been executed."));
    warning->setStyleSheet(
      "QLabel { color: #b45309; background: #fef3c7; border: 1px solid "
      "#fcd34d; border-radius: 4px; padding: 12px; }");
    layout->addWidget(warning, 1);
    return;
  }

  m_editWidget = m_transform->createPropertiesWidget(this);
  if (m_editWidget) {
    layout->addWidget(m_editWidget, 1);
  }
}

void TransformEditDialog::onApply()
{
  if (m_editWidget) {
    m_editWidget->applyChangesToOperator();
  }

  if (m_isNewInsertion && !m_insertionCompleted) {
    completeInsertion();
  }

  // Mark the transform stale so the pipeline re-executes it with the
  // updated parameters (and cascades staleness downstream).
  m_transform->markStale();
  m_pipeline->execute();
}

void TransformEditDialog::onOkay()
{
  if (m_editWidget) {
    m_editWidget->applyChangesToOperator();
  }

  if (m_isNewInsertion && !m_insertionCompleted) {
    completeInsertion();
  }

  m_transform->markStale();
  m_pipeline->execute();
  accept();
}

void TransformEditDialog::onCancel()
{
  if (m_isNewInsertion && !m_insertionCompleted) {
    // Remove the transform and its input links.
    // Output links were never created, so the pipeline remains valid.
    m_pipeline->removeNode(m_transform);
    m_transform = nullptr;
    emit insertionCanceled();
  }

  reject();
}

void TransformEditDialog::showEvent(QShowEvent* event)
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

void TransformEditDialog::saveGeometry()
{
  if (!m_transform) {
    return;
  }
  QSettings* settings = pqApplicationCore::instance()->settings();
  QString key =
    QString("Edit%1TransformDialogGeometry").arg(m_transform->label());
  settings->setValue(key, QVariant(geometry()));
}

void TransformEditDialog::restoreGeometry()
{
  if (!m_transform) {
    return;
  }
  QSettings* settings = pqApplicationCore::instance()->settings();
  QString key =
    QString("Edit%1TransformDialogGeometry").arg(m_transform->label());
  QVariant saved = settings->value(key);
  if (!saved.isNull()) {
    resize(saved.toRect().size());
  }
}

void TransformEditDialog::completeInsertion()
{
  // Break old links
  for (const auto& ep : m_deferred.linksToBreak) {
    // Find the actual Link* by matching from/to ports
    for (auto* link : m_pipeline->links()) {
      if (link->from() == ep.from && link->to() == ep.to) {
        m_pipeline->removeLink(link);
        break;
      }
    }
  }

  // Create new output links
  for (const auto& ep : m_deferred.linksToCreate) {
    m_pipeline->createLink(ep.from, ep.to);
  }

  m_insertionCompleted = true;
  m_isNewInsertion = false;
  emit insertionCompleted(m_transform);
}

} // namespace pipeline
} // namespace tomviz
