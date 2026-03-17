/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PythonTransformEditorWidget.h"

#include "CustomPythonTransformWidget.h"
#include "TransformPropertiesWidget.h"

#include <pqPythonSyntaxHighlighter.h>

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace tomviz {
namespace pipeline {

PythonTransformEditorWidget::PythonTransformEditorWidget(
  const QString& label, const QString& script, const QString& jsonDescription,
  const QMap<QString, QVariant>& currentValues,
  CustomPythonTransformWidget* customParamsWidget, QWidget* parent)
  : EditTransformWidget(parent), m_customParamsWidget(customParamsWidget)
{
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Name row
  auto* nameRow = new QWidget(this);
  auto* nameLayout = new QHBoxLayout(nameRow);
  nameLayout->setContentsMargins(5, 5, 5, 5);
  nameLayout->setSpacing(5);
  nameLayout->addWidget(new QLabel(tr("Name"), nameRow));
  m_nameEdit = new QLineEdit(label, nameRow);
  nameLayout->addWidget(m_nameEdit);
  mainLayout->addWidget(nameRow);

  // Tab widget
  m_tabWidget = new QTabWidget(this);
  mainLayout->addWidget(m_tabWidget, 1);

  // --- Tab 1: Script ---
  auto* scriptTab = new QWidget(m_tabWidget);
  auto* scriptLayout = new QVBoxLayout(scriptTab);

  m_scriptEdit = new QTextEdit(scriptTab);
  m_scriptEdit->setFont(QFont("Monospace"));
  m_scriptEdit->setLineWrapMode(QTextEdit::NoWrap);

  // Attach the syntax highlighter. ConnectHighligter() must be called —
  // the constructor alone does not wire the highlighter to the QTextEdit.
  auto* highlighter =
    new pqPythonSyntaxHighlighter(m_scriptEdit, *m_scriptEdit);
  highlighter->ConnectHighligter();

  if (!script.isEmpty()) {
    m_scriptEdit->setPlainText(script);
  }

  scriptLayout->addWidget(m_scriptEdit, 1);
  m_tabWidget->addTab(scriptTab, tr("Script"));

  // --- Tab 2: Parameters ---
  auto* paramsTab = new QWidget(m_tabWidget);
  auto* paramsLayout = new QVBoxLayout(paramsTab);

  if (m_customParamsWidget) {
    // Use the custom widget instead of auto-generated parameters
    m_customParamsWidget->setParent(paramsTab);
    paramsLayout->addWidget(m_customParamsWidget, 1);
  } else {
    // Show operator description from JSON if available
    if (!jsonDescription.isEmpty()) {
      QJsonDocument doc = QJsonDocument::fromJson(jsonDescription.toUtf8());
      if (doc.isObject()) {
        QString desc = doc.object().value("description").toString();
        if (!desc.isEmpty()) {
          auto* descLabel = new QLabel(desc, paramsTab);
          descLabel->setWordWrap(true);
          descLabel->setStyleSheet(
            "QLabel { color: palette(text); padding: 4px; }");
          paramsLayout->addWidget(descLabel);
        }
      }

      m_paramsWidget = new TransformPropertiesWidget(jsonDescription,
                                                     currentValues, paramsTab);
      paramsLayout->addWidget(m_paramsWidget, 1);
    }

    paramsLayout->addStretch();
  }

  m_tabWidget->addTab(paramsTab, tr("Parameters"));

  // Start on the Parameters tab
  m_tabWidget->setCurrentIndex(1);
}

void PythonTransformEditorWidget::applyChangesToOperator()
{
  QMap<QString, QVariant> values;
  if (m_paramsWidget) {
    values = m_paramsWidget->values();
  }
  emit applied(m_nameEdit->text(), m_scriptEdit->toPlainText(), values);
}

void PythonTransformEditorWidget::showScriptTab()
{
  m_tabWidget->setCurrentIndex(0);
}

} // namespace pipeline
} // namespace tomviz
