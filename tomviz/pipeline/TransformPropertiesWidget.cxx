/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "TransformPropertiesWidget.h"

#include "ParameterInterfaceBuilder.h"

#include <QVBoxLayout>

namespace tomviz {
namespace pipeline {

TransformPropertiesWidget::TransformPropertiesWidget(
  const QString& jsonDescription,
  const QMap<QString, QVariant>& currentValues, QWidget* parent)
  : EditTransformWidget(parent)
{
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  ParameterInterfaceBuilder builder;
  builder.setJSONDescription(jsonDescription);
  builder.setParameterValues(currentValues);

  m_innerWidget = builder.buildWidget(this);
  layout->addWidget(m_innerWidget);
}

QMap<QString, QVariant> TransformPropertiesWidget::values() const
{
  if (!m_innerWidget) {
    return {};
  }
  return ParameterInterfaceBuilder::parameterValues(m_innerWidget);
}

void TransformPropertiesWidget::applyChangesToOperator()
{
  emit applyRequested(values());
}

} // namespace pipeline
} // namespace tomviz
