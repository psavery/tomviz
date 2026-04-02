/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineTransformPropertiesWidget_h
#define tomvizPipelineTransformPropertiesWidget_h

#include "EditTransformWidget.h"

#include <QMap>
#include <QString>
#include <QVariant>

namespace tomviz {
namespace pipeline {

/// A JSON-driven parameter editing widget built by ParameterInterfaceBuilder.
/// Does not own any buttons — the wrapper (TransformPropertiesPanel or
/// TransformEditDialog) provides Apply/OK/Cancel.
class TransformPropertiesWidget
  : public EditTransformWidget
{
  Q_OBJECT

public:
  TransformPropertiesWidget(const QString& jsonDescription,
                            const QMap<QString, QVariant>& currentValues,
                            QWidget* parent = nullptr);

  QMap<QString, QVariant> values() const;

  void applyChangesToOperator() override;

signals:
  /// Emitted by applyChangesToOperator(), carrying the current values.
  /// The transform connects to this signal to update its parameters.
  void applyRequested(const QMap<QString, QVariant>& values);

private:
  QWidget* m_innerWidget = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
