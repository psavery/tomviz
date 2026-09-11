/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineNodeDefinitionFormWidget_h
#define tomvizPipelineNodeDefinitionFormWidget_h

#include "NodeDefinitionValidator.h"

#include <QJsonObject>
#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace tomviz {
namespace pipeline {

/// Generated form over a node's JSON description — the alternative view
/// to the raw editor in NodeDefinitionWidget.
///
/// Edits mutate the parsed document in place rather than rebuilding it
/// from the rendered fields, so the many keys the form doesn't show
/// (``filter``, ``bindToSink``, ``apply_to_each_array``,
/// ``externalCompatible``, ``input``, …) survive untouched.
///
/// The port sections are hidden while ports can't be edited — see
/// kPortsEditable. Their rows and commit path are written, so enabling
/// them is a focused change rather than a rewrite.
class NodeDefinitionFormWidget : public QWidget
{
  Q_OBJECT

public:
  NodeDefinitionFormWidget(NodeShape shape, DefinitionSchema schema,
                           QWidget* parent = nullptr);

  /// Repopulate every control from @a json. Does not emit changed().
  /// Returns false when @a json isn't a JSON object, in which case the
  /// form is left untouched — the caller keeps the user in raw mode.
  bool setJson(const QString& json);

  /// Serialize the current document.
  QString json() const;

signals:
  /// Emitted after any edit that changed the document.
  void changed();

private:
  /// One port's controls: name, type, and (outputs only) persistency.
  struct PortRow
  {
    QWidget* container = nullptr;
    QLineEdit* name = nullptr;
    QComboBox* type = nullptr;
    QComboBox* persistence = nullptr;
    QPushButton* remove = nullptr;
  };

  /// One enumeration option: its label and the value the script gets.
  struct OptionRow
  {
    QWidget* container = nullptr;
    QLineEdit* label = nullptr;
    QLineEdit* value = nullptr;
    QPushButton* remove = nullptr;
  };

  /// One parameter's inline controls. Everything type-specific lives in
  /// the detail pane below, which follows the selected row.
  struct ParameterRow
  {
    QWidget* container = nullptr;
    QLineEdit* name = nullptr;
    QLineEdit* label = nullptr;
    QComboBox* type = nullptr;
    QPushButton* edit = nullptr;
    QPushButton* remove = nullptr;
  };

  void buildGeneralSection(QVBoxLayout* layout);
  void buildPortsSections(QVBoxLayout* layout);
  void buildParametersSection(QVBoxLayout* layout);
  void buildBehaviorSection(QVBoxLayout* layout);

  void refreshPortRows();
  void refreshParameterRows();
  void refreshOptionRows(const QJsonArray& options);
  void rebuildPortRows(bool input);
  PortRow makePortRow(bool input, const QJsonObject& port);
  ParameterRow makeParameterRow(const QJsonObject& param);
  OptionRow makeOptionRow(const QString& label, const QJsonValue& value);

  void commitRoot();
  void commitPorts(bool input);
  void commitParameterRow(int index);
  void commitParameterDetail();

  void selectParameter(int index);
  void loadParameterDetail();
  void updateTypeDependentRows();
  QJsonArray collectOptions() const;

  void addPort(bool input);
  void addParameter();
  void removeParameter(int index);
  void addOption();

  int indexOfParameterRow(const QWidget* container) const;

  QJsonArray parameters() const;
  void setParameters(const QJsonArray& parameters);

  NodeShape m_shape;
  DefinitionSchema m_schema;
  QJsonObject m_root;
  /// Set while setJson() / loadParameterDetail() drive the controls, so
  /// their change signals don't loop back as user edits.
  bool m_populating = false;
  int m_selectedParameter = -1;

  QLineEdit* m_nameEdit = nullptr;
  QLineEdit* m_labelEdit = nullptr;
  QLineEdit* m_descriptionEdit = nullptr;
  QLabel* m_fixedInfoLabel = nullptr;

  QWidget* m_inputBox = nullptr;
  QWidget* m_outputBox = nullptr;
  QLabel* m_legacyPortsNote = nullptr;
  QVBoxLayout* m_inputRowsLayout = nullptr;
  QVBoxLayout* m_outputRowsLayout = nullptr;
  QList<PortRow> m_inputRows;
  QList<PortRow> m_outputRows;

  QVBoxLayout* m_paramRowsLayout = nullptr;
  QList<ParameterRow> m_paramRows;
  QWidget* m_detail = nullptr;
  QLabel* m_detailHeader = nullptr;
  QLineEdit* m_pDescription = nullptr;
  QLineEdit* m_pDefault = nullptr;
  QLineEdit* m_pMinimum = nullptr;
  QLineEdit* m_pMaximum = nullptr;
  QLineEdit* m_pStep = nullptr;
  QSpinBox* m_pPrecision = nullptr;
  QWidget* m_optionsRow = nullptr;
  QVBoxLayout* m_optionRowsLayout = nullptr;
  QList<OptionRow> m_optionRows;
  QList<QWidget*> m_numericRows;

  QCheckBox* m_cancelCheck = nullptr;
  QCheckBox* m_completeCheck = nullptr;
  QCheckBox* m_externalOnlyCheck = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
