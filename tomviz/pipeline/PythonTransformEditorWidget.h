/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelinePythonTransformEditorWidget_h
#define tomvizPipelinePythonTransformEditorWidget_h

#include "tomviz_pipeline_export.h"

#include "EditTransformWidget.h"

#include <QMap>
#include <QString>
#include <QVariant>

class QLineEdit;
class QTabWidget;
class QTextEdit;

namespace tomviz {
namespace pipeline {

class TransformPropertiesWidget;

/// Tabbed editor widget for LegacyPythonTransform operators.
/// Tab 1: Python script editor with syntax highlighting.
/// Tab 2: Operator description + JSON-driven parameter controls.
///
/// applyChangesToOperator() commits the label, script text, and parameter
/// values back to the transform.
class TOMVIZ_PIPELINE_EXPORT PythonTransformEditorWidget
  : public EditTransformWidget
{
  Q_OBJECT

public:
  PythonTransformEditorWidget(const QString& label, const QString& script,
                              const QString& jsonDescription,
                              const QMap<QString, QVariant>& currentValues,
                              QWidget* parent = nullptr);

  void applyChangesToOperator() override;

  /// Switch to the "Script" tab (used for "View Code" actions).
  void showScriptTab();

signals:
  /// Emitted by applyChangesToOperator() carrying the updated label, script,
  /// and parameter values.
  void applied(const QString& label, const QString& script,
               const QMap<QString, QVariant>& values);

private:
  QLineEdit* m_nameEdit = nullptr;
  QTabWidget* m_tabWidget = nullptr;
  QTextEdit* m_scriptEdit = nullptr;
  TransformPropertiesWidget* m_paramsWidget = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
