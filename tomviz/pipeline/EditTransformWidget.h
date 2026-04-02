/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineEditTransformWidget_h
#define tomvizPipelineEditTransformWidget_h

#include <QWidget>

namespace tomviz {
namespace pipeline {

/// Base class for custom transform editing widgets.
///
/// Subclasses implement applyChangesToOperator() to commit the current UI
/// state to the transform's parameters.  The wrapper (TransformPropertiesPanel
/// or TransformEditDialog) owns the Apply/OK/Cancel buttons and calls this
/// slot when the user clicks them.
class EditTransformWidget : public QWidget
{
  Q_OBJECT

public:
  EditTransformWidget(QWidget* parent = nullptr);
  ~EditTransformWidget() override;

public slots:
  /// Apply the current widget state to the transform's parameters.
  virtual void applyChangesToOperator() = 0;
};

} // namespace pipeline
} // namespace tomviz

#endif
