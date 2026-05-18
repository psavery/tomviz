/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineEditNodeWidget_h
#define tomvizPipelineEditNodeWidget_h

#include <QWidget>

namespace tomviz {
namespace pipeline {

/// Base class for custom node editing widgets.
///
/// Subclasses implement applyChangesToOperator() to commit the current UI
/// state to the node's parameters.  The wrapper (NodePropertiesPanel
/// or NodeEditDialog) owns the Apply/OK/Cancel buttons and calls this
/// slot when the user clicks them.
class EditNodeWidget : public QWidget
{
  Q_OBJECT

public:
  EditNodeWidget(QWidget* parent = nullptr);
  ~EditNodeWidget() override;

public slots:
  /// Apply the current widget state to the node's parameters.
  virtual void applyChangesToOperator() = 0;
};

} // namespace pipeline
} // namespace tomviz

#endif
