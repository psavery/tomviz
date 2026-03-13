/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineLegacyPythonTransform_h
#define tomvizPipelineLegacyPythonTransform_h

#include "tomviz_pipeline_export.h"

#include "TransformNode.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace tomviz {
namespace pipeline {

/// A TransformNode that loads and executes an existing tomviz Python operator
/// described by a JSON description and Python script file pair.
///
/// This enables the ~59 existing Python operators (e.g. AddConstant.py/.json)
/// to run unchanged within the new pipeline framework. Python execution uses
/// direct pybind11/CPython — no dependency on the old tomvizlib.
class TOMVIZ_PIPELINE_EXPORT LegacyPythonTransform : public TransformNode
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

  bool hasPropertiesWidget() const override;
  bool propertiesWidgetNeedsInput() const override;
  QWidget* createPropertiesWidget(QWidget* parent) override;

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override;

private:
  void parseJSON();

  QString m_jsonDescription;
  QString m_script;
  QString m_operatorName;
  QMap<QString, QVariant> m_parameters;
  QStringList m_resultNames;
  QStringList m_resultTypes;
  QStringList m_datasetInputNames;
};

} // namespace pipeline
} // namespace tomviz

#endif
