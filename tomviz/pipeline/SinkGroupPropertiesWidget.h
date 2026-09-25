/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineSinkGroupPropertiesWidget_h
#define tomvizPipelineSinkGroupPropertiesWidget_h

#include <QPointer>
#include <QWidget>

class QVBoxLayout;

namespace tomviz {
namespace pipeline {

class Pipeline;
class SinkGroupNode;
class SinkNode;

/// Properties panel for a SinkGroupNode: lists the member sink
/// (visualization) nodes, each with actions to toggle visibility, leave the
/// group, and delete the node. The leave/delete actions are performed by the
/// owner (MainWindow) via the emitted signals.
class SinkGroupPropertiesWidget : public QWidget
{
  Q_OBJECT

public:
  SinkGroupPropertiesWidget(SinkGroupNode* group, Pipeline* pipeline,
                            QWidget* parent = nullptr);
  ~SinkGroupPropertiesWidget() override = default;

signals:
  void leaveGroupRequested(SinkNode* member);
  void deleteRequested(SinkNode* member);

private:
  void rebuild();

  QPointer<SinkGroupNode> m_group;
  Pipeline* m_pipeline = nullptr;
  QVBoxLayout* m_listLayout = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
