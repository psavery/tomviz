/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "TransformPropertiesPanel.h"

#include "EditTransformWidget.h"
#include "InputPort.h"
#include "Pipeline.h"
#include "TransformNode.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace tomviz {
namespace pipeline {

TransformPropertiesPanel::TransformPropertiesPanel(TransformNode* transform,
                                                   Pipeline* pipeline,
                                                   QWidget* parent)
  : QWidget(parent), m_transform(transform), m_pipeline(pipeline)
{
  // Suppress the auto-execute wiring so this panel controls execution.
  QObject::disconnect(m_transform, &TransformNode::parametersApplied,
                      m_pipeline, nullptr);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Skip widget creation if the transform needs input data that isn't
  // available yet (e.g., nodeAdded fires before createLink).
  if (transform->propertiesWidgetNeedsInput()) {
    for (auto* input : transform->inputPorts()) {
      if (!input->link() || !input->hasData()) {
        return;
      }
    }
  }

  m_editWidget = transform->createPropertiesWidget(this);
  if (m_editWidget) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(m_editWidget);
    layout->addWidget(scrollArea, 1);

    auto* buttonBox = new QDialogButtonBox(
      QDialogButtonBox::Apply, Qt::Horizontal, this);
    auto* applyBtn = buttonBox->button(QDialogButtonBox::Apply);
    connect(applyBtn, &QPushButton::clicked,
            this, &TransformPropertiesPanel::apply);
    layout->addWidget(buttonBox);

    // Disable Apply while the pipeline is executing.
    connect(m_pipeline, &Pipeline::executionStarted, this,
            [applyBtn]() { applyBtn->setEnabled(false); });
    connect(m_pipeline, &Pipeline::executionFinished, this,
            [applyBtn]() { applyBtn->setEnabled(true); });
    if (m_pipeline->isExecuting()) {
      applyBtn->setEnabled(false);
    }
  }
}

TransformPropertiesPanel::~TransformPropertiesPanel()
{
  // Restore the auto-wiring if the transform is still in the pipeline.
  if (m_transform && m_pipeline &&
      m_pipeline->nodes().contains(m_transform)) {
    connect(m_transform, &TransformNode::parametersApplied, m_pipeline,
            [pip = m_pipeline]() { pip->execute(); });
  }
}

void TransformPropertiesPanel::apply()
{
  if (m_editWidget) {
    m_editWidget->applyChangesToOperator();
  }
  if (m_transform) {
    m_transform->markStale();
  }
  if (m_pipeline) {
    m_pipeline->execute();
  }
}

} // namespace pipeline
} // namespace tomviz
