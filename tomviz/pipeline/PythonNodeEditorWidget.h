/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelinePythonNodeEditorWidget_h
#define tomvizPipelinePythonNodeEditorWidget_h

#include "EditNodeWidget.h"
#include "PortData.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <memory>

class QComboBox;
class QLineEdit;
class QLabel;
class QTabWidget;
class QTextEdit;
class QVBoxLayout;
class QWidget;

namespace tomviz {
namespace pipeline {

class CustomPythonNodeWidget;
class InputsNotReadyWidget;
class Node;
class NodeDefinitionWidget;
class NodePropertiesWidget;
class Pipeline;

/// Everything one Apply/OK commits back to a Python node, in the order
/// the node must apply it: the description first (it decides which
/// parameters exist at all), then the rest.
struct PythonNodeEdits
{
  QString label;
  QString script;
  QString jsonDescription;
  QMap<QString, QVariant> values;
  /// Empty for the default in-process executor, or the type string
  /// (e.g. "external") for an alternative.
  QString executorType;
  /// Type-specific executor configuration (currently the env path).
  QString executorEnvPath;
};

/// Tabbed editor widget for Python source / transform nodes.
/// Tab 1: The node's raw JSON description — what the other tabs are
///        derived from, hence first. Editing it re-renders the
///        Parameters tab live, so the form on screen always matches the
///        description being edited rather than the one the node is
///        running. canApply() goes false while the description doesn't
///        validate, which is what disables the host's Apply/OK.
/// Tab 2: Python script editor with syntax highlighting.
/// Tab 3: Operator description + JSON-driven parameter controls, or a
///        custom widget if one is registered. When the custom widget
///        needs input data and the data isn't yet available, this tab
///        renders an InputsNotReadyWidget and swaps in the real custom
///        widget once the inputs materialize. Script and Execution tabs
///        remain fully usable in the meantime.
/// Tab 4: Execution strategy — Internal (default) or External (run via
///        the `tomviz-pipeline` CLI in a foreign Python env).
///
/// applyChangesToOperator() commits the label, script text, description,
/// parameter values, and execution-strategy choice back to the node.
/// Parameter values are pulled from whichever Parameters-tab widget is
/// currently installed; if the tab is the not-ready warning, no
/// parameter values are emitted (the existing values stay).
class PythonNodeEditorWidget : public EditNodeWidget
{
  Q_OBJECT

public:
  using CustomWidgetFactory =
    std::function<CustomPythonNodeWidget*(QWidget* parent)>;

  PythonNodeEditorWidget(
    Node* node, Pipeline* pipeline,
    const QString& label, const QString& script,
    const QString& jsonDescription,
    const QMap<QString, QVariant>& currentValues,
    const QString& executorType, const QString& executorEnvPath,
    CustomWidgetFactory customWidgetFactory,
    bool customWidgetNeedsData,
    QWidget* parent = nullptr);

  void applyChangesToOperator() override;

  /// Reads "help": {"url": ...} from the JSON description.
  QString helpUrl() const override;

  /// False while the Definition tab holds a description the node can't
  /// adopt, so the host's Apply/OK stays disabled until it is fixed or
  /// reverted.
  bool canApply() const override;

  /// Switch to the "Script" tab (used for "View Code" actions).
  void showScriptTab();

signals:
  /// Emitted by applyChangesToOperator() carrying everything the node
  /// should adopt.
  void applied(const PythonNodeEdits& edits);

private:
  void onRunRequested();
  void onExecutionFinished();
  void installCustomWidget();
  void installJsonFormWidget();
  void installNotReadyWidget();
  void rebuildParametersTab(const QString& json);
  bool inputsInMemory() const;

  Node* m_node;
  Pipeline* m_pipeline;
  CustomWidgetFactory m_customFactory;
  bool m_customWidgetNeedsData;
  bool m_jsonFormNeedsData = false;
  // Description declared "externalOnly": Internal executor is disabled.
  bool m_externalOnly = false;
  // JSON "name" field; keys the remembered external-env path.
  QString m_operatorName;
  QString m_jsonDescription;
  QMap<QString, QVariant> m_currentValues;

  QLineEdit* m_nameEdit = nullptr;
  QTabWidget* m_tabWidget = nullptr;
  QTextEdit* m_scriptEdit = nullptr;
  NodeDefinitionWidget* m_definitionWidget = nullptr;
  QWidget* m_paramsTab = nullptr;
  int m_scriptTabIndex = -1;
  int m_paramsTabIndex = -1;
  QVBoxLayout* m_paramsLayout = nullptr;
  NodePropertiesWidget* m_paramsWidget = nullptr;
  CustomPythonNodeWidget* m_customParamsWidget = nullptr;
  InputsNotReadyWidget* m_notReadyWidget = nullptr;
  QComboBox* m_executorCombo = nullptr;
  QLabel* m_envPathLabel = nullptr;
  QWidget* m_envPathRow = nullptr;
  QLineEdit* m_envPathEdit = nullptr;

  /// Holds OnDisk-evicted upstream payloads in memory while the editor
  /// is shown, so the custom widget can read from the input ports.
  QList<std::shared_ptr<PortData>> m_inputPins;
};

} // namespace pipeline
} // namespace tomviz

#endif
