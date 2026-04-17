/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineTransformEditDialog_h
#define tomvizPipelineTransformEditDialog_h

#include "DeferredLinkInfo.h"

#include <QDialog>

class QDialogButtonBox;
class QShowEvent;

namespace tomviz {
namespace pipeline {

class EditTransformWidget;
class Pipeline;
class TransformNode;

/// A dialog for configuring transform node parameters with Apply/OK/Cancel.
///
/// Two modes:
///   - **Insertion mode**: The transform is being added. Input links exist but
///     output links are deferred. Apply/OK completes the insertion and executes
///     the pipeline. Cancel removes the transform and its input links.
///   - **Edit mode**: The transform already exists. Apply/OK re-applies
///     parameters and executes. Cancel just closes the dialog.
class TransformEditDialog : public QDialog
{
  Q_OBJECT

public:
  /// Edit an existing transform (edit mode).
  TransformEditDialog(TransformNode* transform, Pipeline* pipeline,
                      QWidget* parent = nullptr);

  /// Insert a new transform (insertion mode).
  TransformEditDialog(TransformNode* transform, Pipeline* pipeline,
                      const DeferredLinkInfo& deferred,
                      QWidget* parent = nullptr);

  ~TransformEditDialog() override;

  TransformNode* transform() const;

signals:
  /// Emitted after Apply/OK completes an insertion.
  void insertionCompleted(TransformNode* node);

  /// Emitted after Cancel aborts an insertion.
  void insertionCanceled();

private slots:
  void onApply();
  void onOkay();
  void onCancel();

protected:
  void showEvent(QShowEvent* event) override;

private:
  void init();
  bool inputsReady() const;
  void setupContent();
  void saveGeometry();
  void restoreGeometry();

  void completeInsertion();

  TransformNode* m_transform;
  Pipeline* m_pipeline;
  EditTransformWidget* m_editWidget = nullptr;
  QDialogButtonBox* m_buttonBox = nullptr;
  DeferredLinkInfo m_deferred;
  bool m_isNewInsertion;
  bool m_insertionCompleted = false;
};

} // namespace pipeline
} // namespace tomviz

#endif
