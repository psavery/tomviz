/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SinkGroupPropertiesWidget.h"

#include "Node.h"
#include "Pipeline.h"
#include "SinkGroupNode.h"
#include "SinkNode.h"
#include "sinks/LegacyModuleSink.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace tomviz {
namespace pipeline {

SinkGroupPropertiesWidget::SinkGroupPropertiesWidget(SinkGroupNode* group,
                                                     Pipeline* pipeline,
                                                     QWidget* parent)
  : QWidget(parent), m_group(group), m_pipeline(pipeline)
{
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);

  auto* scroll = new QScrollArea(this);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  outer->addWidget(scroll);

  auto* listContainer = new QWidget;
  m_listLayout = new QVBoxLayout(listContainer);
  m_listLayout->setContentsMargins(4, 4, 4, 4);
  m_listLayout->setSpacing(2);
  scroll->setWidget(listContainer);

  // Membership changes (a sink joining/leaving, a node being removed) alter
  // the group's sink list — rebuild the rows when they happen.
  if (m_pipeline) {
    connect(m_pipeline, &Pipeline::nodeRemoved, this,
            [this]() { rebuild(); });
    connect(m_pipeline, &Pipeline::nodeAdded, this,
            [this]() { rebuild(); });
    connect(m_pipeline, &Pipeline::linkCreated, this,
            [this]() { rebuild(); });
    connect(m_pipeline, &Pipeline::linkRemoved, this,
            [this]() { rebuild(); });
  }

  rebuild();
}

void SinkGroupPropertiesWidget::rebuild()
{
  // Clear existing rows.
  QLayoutItem* item;
  while ((item = m_listLayout->takeAt(0)) != nullptr) {
    if (auto* w = item->widget()) {
      w->deleteLater();
    }
    delete item;
  }

  if (!m_group) {
    return;
  }

  static const QIcon leaveIcon(QStringLiteral(":/pipeline/icon_leave.svg"));
  static const QIcon deleteIcon(QStringLiteral(":/pipeline/canceled.png"));

  const auto members = m_group->sinks();
  for (auto* member : members) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(2, 1, 2, 1);
    h->setSpacing(4);

    auto* iconLabel = new QLabel(row);
    iconLabel->setPixmap(member->icon().pixmap(16, 16));
    h->addWidget(iconLabel);

    auto* nameLabel = new QLabel(member->label(), row);
    h->addWidget(nameLabel, 1);
    connect(member, &Node::labelChanged, nameLabel,
            [member, nameLabel]() { nameLabel->setText(member->label()); });

    // Toggle visibility — same action as the sink's card button.
    auto* visBtn = new QToolButton(row);
    visBtn->setAutoRaise(true);
    visBtn->setIcon(member->actionIcon());
    visBtn->setToolTip(tr("Toggle visibility"));
    connect(visBtn, &QToolButton::clicked, member,
            [member]() { member->triggerAction(); });
    if (auto* sink = qobject_cast<LegacyModuleSink*>(member)) {
      connect(sink, &LegacyModuleSink::visibilityChanged, visBtn,
              [member, visBtn]() { visBtn->setIcon(member->actionIcon()); });
    }
    h->addWidget(visBtn);

    // Leave the group.
    auto* leaveBtn = new QToolButton(row);
    leaveBtn->setAutoRaise(true);
    leaveBtn->setIcon(leaveIcon);
    leaveBtn->setToolTip(tr("Leave group"));
    connect(leaveBtn, &QToolButton::clicked, this,
            [this, member]() { emit leaveGroupRequested(member); });
    h->addWidget(leaveBtn);

    // Delete the node.
    auto* deleteBtn = new QToolButton(row);
    deleteBtn->setAutoRaise(true);
    deleteBtn->setIcon(deleteIcon);
    deleteBtn->setToolTip(tr("Delete"));
    connect(deleteBtn, &QToolButton::clicked, this,
            [this, member]() { emit deleteRequested(member); });
    h->addWidget(deleteBtn);

    m_listLayout->addWidget(row);
  }

  m_listLayout->addStretch();
}

} // namespace pipeline
} // namespace tomviz
