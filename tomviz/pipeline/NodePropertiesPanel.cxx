/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "NodePropertiesPanel.h"

#include "EditNodeWidget.h"
#include "InputPort.h"
#include "Node.h"
#include "Pipeline.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace tomviz {
namespace pipeline {

NodePropertiesPanel::NodePropertiesPanel(Node* node, Pipeline* pipeline,
                                         QWidget* parent)
  : QWidget(parent), m_node(node), m_pipeline(pipeline)
{
  // Suppress the auto-execute wiring so this panel controls execution.
  QObject::disconnect(m_node, &Node::parametersApplied, m_pipeline, nullptr);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Skip widget creation if the node needs input data that isn't
  // available yet (e.g., nodeAdded fires before createLink).
  if (node->propertiesWidgetNeedsInput()) {
    for (auto* input : node->inputPorts()) {
      if (!input->link() || !input->hasData()) {
        return;
      }
    }
  }

  m_editWidget = node->createPropertiesWidget(this);
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
            this, &NodePropertiesPanel::apply);
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

NodePropertiesPanel::~NodePropertiesPanel()
{
  // Restore the auto-wiring if the node is still in the pipeline.
  if (m_node && m_pipeline && m_pipeline->nodes().contains(m_node)) {
    connect(m_node, &Node::parametersApplied, m_pipeline,
            [pip = m_pipeline]() { pip->execute(); });
  }
}

void NodePropertiesPanel::apply()
{
  if (m_editWidget) {
    m_editWidget->applyChangesToOperator();
  }
  if (m_node) {
    m_node->markStale();
  }
  if (m_pipeline) {
    m_pipeline->execute();
  }
}

} // namespace pipeline
} // namespace tomviz
