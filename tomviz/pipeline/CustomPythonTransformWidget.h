/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineCustomPythonTransformWidget_h
#define tomvizPipelineCustomPythonTransformWidget_h

#include "tomviz_pipeline_export.h"

#include <QMap>
#include <QString>
#include <QVariant>
#include <QWidget>

namespace tomviz {
namespace pipeline {

/// Base class for custom parameter widgets that replace the auto-generated
/// parameter UI for specific Python transforms (e.g. RotationAlign,
/// ShiftRotationCenter).
class TOMVIZ_PIPELINE_EXPORT CustomPythonTransformWidget : public QWidget
{
  Q_OBJECT

public:
  CustomPythonTransformWidget(QWidget* parent = nullptr);
  ~CustomPythonTransformWidget() override;

  virtual void getValues(QMap<QString, QVariant>& map) = 0;
  virtual void setValues(const QMap<QString, QVariant>& map) = 0;

  /// Keep a copy of the current script (including edits) in case the
  /// custom widget needs to use it (e.g. for running test Python code).
  virtual void setScript(const QString& script) { m_script = script; }

  /// Called when the operator is applied (e.g. to persist settings).
  virtual void writeSettings() {}

protected:
  QString m_script;
};

} // namespace pipeline
} // namespace tomviz

#endif
