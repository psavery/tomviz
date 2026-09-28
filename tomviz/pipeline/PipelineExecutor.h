/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelinePipelineExecutor_h
#define tomvizPipelinePipelineExecutor_h

#include <QList>
#include <QObject>

namespace tomviz {
namespace pipeline {

class Node;
class Pipeline;

class PipelineExecutor : public QObject
{
  Q_OBJECT

public:
  PipelineExecutor(QObject* parent = nullptr);
  ~PipelineExecutor() override = default;

  virtual void execute(const QList<Node*>& nodes, Pipeline* pipeline) = 0;
  virtual void cancel() = 0;

  /// Cancel and block until execution has actually stopped. Callers that
  /// are about to delete nodes (e.g. Pipeline::clear()) must use this, not
  /// cancel(), so a worker thread can't still be executing a node we free.
  /// The base implementation just cancels — enough for synchronous
  /// executors, which can't be running when this is called from outside
  /// execute(); ThreadedExecutor overrides it to join its worker.
  virtual void cancelAndWait() { cancel(); }

  virtual bool isRunning() const = 0;

signals:
  void nodeExecutionStarted(Node* node);
  void nodeExecutionFinished(Node* node, bool success);
  void executionComplete(bool success);
  void canceled();
};

} // namespace pipeline
} // namespace tomviz

#endif
