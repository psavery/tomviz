/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "LinkPropertiesWidget.h"

#include "InputPort.h"
#include "Link.h"
#include "Node.h"
#include "OutputPort.h"
#include "PortType.h"

#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace tomviz {
namespace pipeline {

// "NodeLabel[portName]" for one endpoint, guarding against null pieces.
static QString endpointText(Node* node, const QString& portName)
{
  QString nodeLabel = node ? node->label() : QObject::tr("(none)");
  if (portName.isEmpty()) {
    return nodeLabel;
  }
  return QStringLiteral("%1[%2]").arg(nodeLabel, portName);
}

LinkPropertiesWidget::LinkPropertiesWidget(Link* link, QWidget* parent)
  : QWidget(parent), m_link(link)
{
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(10);

  auto* form = new QFormLayout;
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setHorizontalSpacing(8);
  form->setVerticalSpacing(6);
  layout->addLayout(form);

  OutputPort* from = m_link ? m_link->from() : nullptr;
  InputPort* to = m_link ? m_link->to() : nullptr;

  // Values are right-aligned so they line up on the right edge, opposite the
  // left-aligned labels.
  auto makeValue = [this](const QString& text) {
    auto* l = new QLabel(text, this);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return l;
  };

  form->addRow(tr("From:"),
               makeValue(endpointText(from ? from->node() : nullptr,
                                      from ? from->name() : QString())));
  form->addRow(tr("To:"),
               makeValue(endpointText(to ? to->node() : nullptr,
                                      to ? to->name() : QString())));

  if (from) {
    form->addRow(tr("Type:"), makeValue(portTypeToString(from->type())));
  }

  if (m_link && !m_link->isValid()) {
    auto* status = makeValue(tr("Invalid (incompatible types)"));
    QFont f = status->font();
    f.setBold(true);
    status->setFont(f);
    form->addRow(tr("Status:"), status);
  }

  auto* deleteButton = new QPushButton(tr("Delete Link"), this);
  connect(deleteButton, &QPushButton::clicked, this,
          [this]() { emit deleteRequested(m_link); });
  layout->addWidget(deleteButton);

  layout->addStretch();
}

} // namespace pipeline
} // namespace tomviz
