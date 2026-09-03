/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineThreadedExecutor_h
#define tomvizPipelineThreadedExecutor_h

#include "PipelineExecutor.h"

#include <QSemaphore>

#include <atomic>

class QThread;

namespace tomviz {
namespace pipeline {

class ExecutionWorker;

class ThreadedExecutor : public PipelineExecutor
{
  Q_OBJECT

public:
  ThreadedExecutor(QObject* parent = nullptr);
  ~ThreadedExecutor() override;

  void execute(const QList<Node*>& nodes, Pipeline* pipeline) override;
  void cancel() override;
  void cancelAndWait() override;
  bool isRunning() const override;

private:
  void executePending();

  QThread* m_thread = nullptr;
  // Per-node barrier: the worker releases nothing itself; the main
  // thread releases one permit after each node's nodeExecutionFinished
  // handlers (the color-map rescale) have run, letting the worker start
  // the next node. Constructed before m_worker so the reference handed
  // to the worker is valid. See the barrier in ExecutionWorker::run.
  QSemaphore m_syncSem;
  ExecutionWorker* m_worker = nullptr;
  std::atomic<bool> m_cancelRequested{ false };
  std::atomic<bool> m_running{ false };
  std::atomic<Node*> m_currentNode{ nullptr };
  QMetaObject::Connection m_breakpointConnection;

  // Pending execution request queued while a run was in progress
  QList<Node*> m_pendingNodes;
  Pipeline* m_pendingPipeline = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
