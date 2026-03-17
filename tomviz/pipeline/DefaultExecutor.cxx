/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "DefaultExecutor.h"

#include "Node.h"
#include "Pipeline.h"

namespace tomviz {
namespace pipeline {

DefaultExecutor::DefaultExecutor(QObject* parent)
  : PipelineExecutor(parent)
{}

void DefaultExecutor::execute(const QList<Node*>& nodes, Pipeline* pipeline)
{
  m_running = true;
  m_cancelRequested = false;

  for (auto* node : nodes) {
    if (m_cancelRequested) {
      m_running = false;
      emit canceled();
      emit executionComplete(false);
      return;
    }

    if (node->hasBreakpoint()) {
      emit pipeline->breakpointReached(node);
      m_running = false;
      emit executionComplete(false);
      return;
    }

    if (node->state() == NodeState::Current) {
      continue;
    }

    // Skip nodes with invalid (type-incompatible) input links
    if (node->hasInvalidInputLinks()) {
      emit nodeExecutionStarted(node);
      emit nodeExecutionFinished(node, false);
      continue;
    }

    emit nodeExecutionStarted(node);
    bool success = node->execute();
    emit nodeExecutionFinished(node, success);

    if (!success) {
      m_running = false;
      emit executionComplete(false);
      return;
    }
  }

  pipeline->releaseTransientData();
  m_running = false;
  emit executionComplete(true);
}

void DefaultExecutor::cancel()
{
  m_cancelRequested = true;
}

bool DefaultExecutor::isRunning() const
{
  return m_running;
}

} // namespace pipeline
} // namespace tomviz
