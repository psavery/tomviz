/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineTransformPropertiesPanel_h
#define tomvizPipelineTransformPropertiesPanel_h

#include "tomviz_pipeline_export.h"

#include <QWidget>

namespace tomviz {
namespace pipeline {

class EditTransformWidget;
class Pipeline;
class TransformNode;

/// Panel wrapper for transform properties shown in the properties dock.
/// Creates the transform's EditTransformWidget, adds an Apply button,
/// and handles applying parameters + pipeline re-execution.
///
/// Suppresses the parametersApplied → execute() auto-wiring while alive
/// to avoid double execution / deadlock with ThreadedExecutor.
class TOMVIZ_PIPELINE_EXPORT TransformPropertiesPanel : public QWidget
{
  Q_OBJECT

public:
  TransformPropertiesPanel(TransformNode* transform, Pipeline* pipeline,
                           QWidget* parent = nullptr);
  ~TransformPropertiesPanel() override;

private slots:
  void apply();

private:
  TransformNode* m_transform;
  Pipeline* m_pipeline;
  EditTransformWidget* m_editWidget = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
