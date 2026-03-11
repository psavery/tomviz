/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineTransformPropertiesWidget_h
#define tomvizPipelineTransformPropertiesWidget_h

#include "tomviz_pipeline_export.h"

#include <QMap>
#include <QString>
#include <QVariant>
#include <QWidget>

namespace tomviz {
namespace pipeline {

/// A widget that wraps ParameterInterfaceBuilder and provides an Apply button.
/// Parameters are only committed when the user clicks Apply.
class TOMVIZ_PIPELINE_EXPORT TransformPropertiesWidget : public QWidget
{
  Q_OBJECT

public:
  TransformPropertiesWidget(const QString& jsonDescription,
                            const QMap<QString, QVariant>& currentValues,
                            QWidget* parent = nullptr);

  QMap<QString, QVariant> values() const;

signals:
  /// Emitted when the user clicks Apply, carrying the current values.
  void applyRequested(const QMap<QString, QVariant>& values);

private:
  QWidget* m_innerWidget = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
