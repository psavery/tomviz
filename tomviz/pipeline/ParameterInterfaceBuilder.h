/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineParameterInterfaceBuilder_h
#define tomvizPipelineParameterInterfaceBuilder_h

#include "tomviz_pipeline_export.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QObject>
#include <QVariant>

class QGridLayout;

namespace tomviz {
namespace pipeline {

/// Builds a Qt widget from a JSON parameter description.
/// This is a standalone version of tomviz::InterfaceBuilder that does not
/// depend on DataSource, ActiveObjects, or ModuleManager.
class TOMVIZ_PIPELINE_EXPORT ParameterInterfaceBuilder : public QObject
{
  Q_OBJECT

public:
  ParameterInterfaceBuilder(QObject* parent = nullptr);

  /// Set the JSON description (operator JSON with "parameters" array).
  void setJSONDescription(const QString& json);
  void setJSONDescription(const QJsonDocument& doc);

  /// Set parameter values to override defaults.
  void setParameterValues(const QMap<QString, QVariant>& values);

  /// Build the widget tree. Caller owns the returned widget.
  QWidget* buildWidget(QWidget* parent) const;

  /// Extract current parameter values from a widget tree built by this class.
  static QMap<QString, QVariant> parameterValues(const QWidget* widget);

private:
  Q_DISABLE_COPY(ParameterInterfaceBuilder)

  void buildParameterInterface(QGridLayout* layout,
                               QJsonArray& parameters) const;
  void setupEnableAndVisibleStates(const QObject* parent,
                                   QJsonArray& parameters) const;
  void setupEnableStates(const QObject* parent, QJsonArray& parameters,
                         bool visible) const;

  QJsonDocument m_json;
  QMap<QString, QVariant> m_parameterValues;
};

} // namespace pipeline
} // namespace tomviz

#endif
