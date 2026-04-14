/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ThreadedExecutor.h"

#include "Node.h"
#include "Pipeline.h"

#include <QThread>

namespace tomviz {
namespace pipeline {

class ExecutionWorker : public QObject
{
  Q_OBJECT

public:
  ExecutionWorker(std::atomic<bool>& cancelFlag,
                  std::atomic<Node*>& currentNode)
    : m_cancelFlag(cancelFlag), m_currentNode(currentNode)
  {
  }

public slots:
  void run(const QList<Node*>& nodes, Pipeline* pipeline)
  {
    for (auto* node : nodes) {
      if (m_cancelFlag.load()) {
        emit canceled();
        emit executionDone(false);
        return;
      }

      if (node->hasBreakpoint()) {
        emit breakpointHit(node);
        emit executionDone(false);
        return;
      }

      if (node->state() == NodeState::Current) {
        continue;
      }

      // Skip nodes whose inputs are stale due to upstream failure/cancellation
      if (node->anyInputStale()) {
        continue;
      }

      m_currentNode.store(node);
      emit nodeStarted(node);
      bool success = node->execute();
      m_currentNode.store(nullptr);
      emit nodeFinished(node, success);

      if (!success) {
        // Mark downstream nodes stale so they are skipped.
        node->markStale();
      }
    }

    // releaseTransientData must run on the main thread since it may
    // interact with Qt objects, but for simplicity we call it here —
    // the data is only read after the finished signal is delivered.
    pipeline->releaseTransientData();
    emit executionDone(true);
  }

signals:
  void nodeStarted(Node* node);
  void nodeFinished(Node* node, bool success);
  void executionDone(bool success);
  void breakpointHit(Node* node);
  void canceled();

private:
  std::atomic<bool>& m_cancelFlag;
  std::atomic<Node*>& m_currentNode;
};

ThreadedExecutor::ThreadedExecutor(QObject* parent)
  : PipelineExecutor(parent), m_thread(new QThread(this)),
    m_worker(new ExecutionWorker(m_cancelRequested, m_currentNode))
{
  m_worker->moveToThread(m_thread);

  // Forward worker signals to executor signals (queued connections across
  // thread boundary)
  connect(m_worker, &ExecutionWorker::nodeStarted, this,
          &PipelineExecutor::nodeExecutionStarted);
  connect(m_worker, &ExecutionWorker::nodeFinished, this,
          &PipelineExecutor::nodeExecutionFinished);
  connect(m_worker, &ExecutionWorker::canceled, this,
          &PipelineExecutor::canceled);

  connect(m_worker, &ExecutionWorker::executionDone, this,
          [this](bool success) {
            m_running = false;
            emit executionComplete(success);
          });

  m_thread->start();
}

ThreadedExecutor::~ThreadedExecutor()
{
  cancel();
  m_thread->quit();
  m_thread->wait();
  delete m_worker;
}

void ThreadedExecutor::execute(const QList<Node*>& nodes, Pipeline* pipeline)
{
  if (m_running) {
    cancel();
    // Wait for previous run to finish
    while (m_running) {
      QThread::msleep(1);
    }
  }

  // Disconnect previous breakpoint forwarding, if any
  if (m_breakpointConnection) {
    disconnect(m_breakpointConnection);
  }

  // Forward breakpointHit to the pipeline for this execution
  m_breakpointConnection = connect(
    m_worker, &ExecutionWorker::breakpointHit, pipeline,
    [pipeline](Node* node) { emit pipeline->breakpointReached(node); });

  m_cancelRequested = false;
  m_running = true;

  // Use invokeMethod to run the worker's slot on its thread
  QMetaObject::invokeMethod(
    m_worker, [this, nodes, pipeline]() { m_worker->run(nodes, pipeline); },
    Qt::QueuedConnection);
}

void ThreadedExecutor::cancel()
{
  m_cancelRequested = true;
  if (auto* node = m_currentNode.load()) {
    node->cancelExecution();
  }
}

bool ThreadedExecutor::isRunning() const
{
  return m_running;
}

} // namespace pipeline
} // namespace tomviz

#include "ThreadedExecutor.moc"
