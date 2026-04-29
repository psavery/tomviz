/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineLegacyPythonTransform_h
#define tomvizPipelineLegacyPythonTransform_h

#include "TransformNode.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>

#include <vtkSmartPointer.h>

class vtkImageData;
class vtkSMProxy;

namespace tomviz {
namespace pipeline {

class CustomPythonTransformWidget;

/// Registration info for a custom widget that replaces the auto-generated
/// parameter UI for specific Python operators (e.g. RotationAlign).
struct CustomWidgetInfo
{
  /// Whether the widget needs input data (vtkImageData) to display.
  bool needsData = false;

  /// Factory: creates the widget given a parent, input image data, and the
  /// source volume's color map proxy (may be null).
  std::function<CustomPythonTransformWidget*(
    QWidget* parent, vtkSmartPointer<vtkImageData>, vtkSMProxy* colorMap)>
    create;
};

/// A TransformNode that loads and executes an existing tomviz Python operator
/// described by a JSON description and Python script file pair.
///
/// This enables the ~59 existing Python operators (e.g. AddConstant.py/.json)
/// to run unchanged within the new pipeline framework. Python execution uses
/// direct pybind11/CPython — no dependency on the old tomvizlib.
class LegacyPythonTransform : public TransformNode
{
  Q_OBJECT

public:
  LegacyPythonTransform(QObject* parent = nullptr);
  ~LegacyPythonTransform() override = default;

  /// Load from JSON description string
  void setJSONDescription(const QString& json);
  QString jsonDescription() const;

  /// Set the Python script source code
  void setScript(const QString& script);
  QString scriptSource() const;

  /// Parameter access (populated from JSON defaults, overridable)
  void setParameter(const QString& name, const QVariant& value);
  QVariant parameter(const QString& name) const;
  QMap<QString, QVariant> parameters() const;

  /// The operator name from the JSON description
  QString operatorName() const;

  /// The custom widget ID from the JSON "widget" field (empty if none).
  QString customWidgetID() const;

  /// Register a custom widget factory for a given widget ID.
  static void registerCustomWidget(const QString& id,
                                   const CustomWidgetInfo& info);

  bool hasPropertiesWidget() const override;
  bool propertiesWidgetNeedsInput() const override;
  EditTransformWidget* createPropertiesWidget(QWidget* parent) override;

  QJsonObject serialize() const override;
  bool deserialize(const QJsonObject& json) override;

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override;

private:
  void parseJSON();

  QString m_jsonDescription;
  QString m_script;
  QString m_operatorName;
  QMap<QString, QVariant> m_parameters;
  // Declared parameter type (per the operator JSON description's
  // `parameters[*].type`) — needed at deserialize time because Qt6
  // collapses every JSON number into QVariant<double>, so we'd
  // otherwise pass `axis: 2` to Python as 2.0 and break operators that
  // index with it.
  QMap<QString, QString> m_parameterTypes;
  QString m_customWidgetID;
  QStringList m_resultNames;
  QStringList m_resultTypes;
  QStringList m_datasetInputNames;
  QString m_primaryOutputName = QStringLiteral("volume");
  QString m_childName;  // Non-empty when JSON declares a "children" entry

  static QMap<QString, CustomWidgetInfo> s_customWidgetMap;
};

} // namespace pipeline
} // namespace tomviz

#endif
