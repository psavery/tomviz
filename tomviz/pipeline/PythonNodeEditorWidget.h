/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelinePythonNodeEditorWidget_h
#define tomvizPipelinePythonNodeEditorWidget_h

#include "EditNodeWidget.h"

#include <QMap>
#include <QString>
#include <QVariant>

class QComboBox;
class QLineEdit;
class QLabel;
class QTabWidget;
class QTextEdit;
class QWidget;

namespace tomviz {
namespace pipeline {

class NodePropertiesWidget;
class CustomPythonNodeWidget;

/// Tabbed editor widget for Python source / transform nodes.
/// Tab 1: Python script editor with syntax highlighting.
/// Tab 2: Operator description + JSON-driven parameter controls.
/// Tab 3: Execution strategy — Internal (default) or External (run via
///        the `tomviz-pipeline` CLI in a foreign Python env).
///
/// applyChangesToOperator() commits the label, script text, parameter
/// values, and execution-strategy choice back to the node.
class PythonNodeEditorWidget
  : public EditNodeWidget
{
  Q_OBJECT

public:
  PythonNodeEditorWidget(
    const QString& label, const QString& script,
    const QString& jsonDescription,
    const QMap<QString, QVariant>& currentValues,
    const QString& executorType, const QString& executorEnvPath,
    CustomPythonNodeWidget* customParamsWidget = nullptr,
    QWidget* parent = nullptr);

  void applyChangesToOperator() override;

  /// Switch to the "Script" tab (used for "View Code" actions).
  void showScriptTab();

signals:
  /// Emitted by applyChangesToOperator() carrying the updated label,
  /// script, parameter values, and execution strategy. `executorType`
  /// is empty for the default in-process executor or the type string
  /// (e.g. "external") for an alternative; `executorEnvPath` carries
  /// any type-specific configuration (currently the env path).
  void applied(const QString& label, const QString& script,
               const QMap<QString, QVariant>& values,
               const QString& executorType,
               const QString& executorEnvPath);

private:
  QLineEdit* m_nameEdit = nullptr;
  QTabWidget* m_tabWidget = nullptr;
  QTextEdit* m_scriptEdit = nullptr;
  NodePropertiesWidget* m_paramsWidget = nullptr;
  CustomPythonNodeWidget* m_customParamsWidget = nullptr;
  QComboBox* m_executorCombo = nullptr;
  QLabel* m_envPathLabel = nullptr;
  QWidget* m_envPathRow = nullptr;
  QLineEdit* m_envPathEdit = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
