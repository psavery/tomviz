/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "Pipeline.h"

#include "DefaultExecutor.h"
#include "ExecutionFuture.h"
#include "InputPort.h"
#include "Link.h"
#include "Node.h"
#include "OutputPort.h"
#include "PipelineExecutor.h"
#include "TransformNode.h"

#include <QMap>
#include <QQueue>
#include <QSet>
#include <QStack>

namespace tomviz {
namespace pipeline {

Pipeline::Pipeline(QObject* parent) : QObject(parent) {}

Pipeline::~Pipeline()
{
  // Delete links before nodes to avoid dangling pointer access in Link::~Link
  qDeleteAll(m_links);
  m_links.clear();
}

void Pipeline::addNode(Node* node)
{
  if (!node || m_nodes.contains(node)) {
    return;
  }
  node->setParent(this);
  m_nodes.append(node);

  // Auto re-execute when transform parameters change
  if (auto* transform = dynamic_cast<TransformNode*>(node)) {
    connect(transform, &TransformNode::parametersApplied,
            this, [this]() { execute(); });
  }

  emit nodeAdded(node);
}

void Pipeline::removeNode(Node* node)
{
  if (!node || !m_nodes.contains(node)) {
    return;
  }

  // Remove all links connected to this node
  QList<Link*> linksToRemove;
  for (auto* link : m_links) {
    if (link->from()->node() == node || link->to()->node() == node) {
      linksToRemove.append(link);
    }
  }
  for (auto* link : linksToRemove) {
    removeLink(link);
  }

  m_nodes.removeOne(node);
  emit nodeRemoved(node);
  delete node;
}

QList<Node*> Pipeline::nodes() const
{
  return m_nodes;
}

QList<Node*> Pipeline::roots() const
{
  QList<Node*> result;
  for (auto* node : m_nodes) {
    if (node->inputPorts().isEmpty()) {
      result.append(node);
    } else {
      // Check if all inputs are unconnected
      bool hasConnection = false;
      for (auto* input : node->inputPorts()) {
        if (input->link()) {
          hasConnection = true;
          break;
        }
      }
      if (!hasConnection) {
        result.append(node);
      }
    }
  }
  return result;
}

Link* Pipeline::createLink(OutputPort* from, InputPort* to)
{
  if (!from || !to) {
    return nullptr;
  }

  if (!to->canConnectTo(from)) {
    return nullptr;
  }

  if (wouldCreateCycle(from, to)) {
    return nullptr;
  }

  // Remove existing link on this input port
  if (to->link()) {
    removeLink(to->link());
  }

  auto* link = new Link(from, to, this);
  m_links.append(link);
  emit linkCreated(link);

  // Mark downstream nodes stale
  Node* downstream = to->node();
  if (downstream) {
    downstream->markStale();
  }

  return link;
}

void Pipeline::removeLink(Link* link)
{
  if (!link || !m_links.contains(link)) {
    return;
  }

  m_links.removeOne(link);
  emit linkRemoved(link);
  delete link;
}

QList<Link*> Pipeline::links() const
{
  return m_links;
}

bool Pipeline::wouldCreateCycle(OutputPort* from, InputPort* to) const
{
  if (!from || !to) {
    return false;
  }

  Node* sourceNode = from->node();
  Node* targetNode = to->node();

  if (!sourceNode || !targetNode) {
    return false;
  }

  if (sourceNode == targetNode) {
    return true;
  }

  // DFS from targetNode downstream to see if we can reach sourceNode.
  // If so, adding sourceNode -> targetNode would create a cycle.
  QSet<Node*> visited;
  QStack<Node*> stack;
  stack.push(targetNode);

  while (!stack.isEmpty()) {
    Node* current = stack.pop();
    if (current == sourceNode) {
      return true;
    }
    if (visited.contains(current)) {
      continue;
    }
    visited.insert(current);

    for (auto* downstream : current->downstreamNodes()) {
      if (!visited.contains(downstream)) {
        stack.push(downstream);
      }
    }
  }

  return false;
}

bool Pipeline::isValid() const
{
  // Check all links are valid
  for (auto* link : m_links) {
    if (!link->isValid()) {
      return false;
    }
  }

  // Check no cycles (try topological sort)
  // Use Kahn's algorithm - if we can sort all nodes, no cycles
  QMap<Node*, int> inDegree;
  for (auto* node : m_nodes) {
    inDegree[node] = 0;
  }
  for (auto* link : m_links) {
    Node* downstream = link->to()->node();
    if (downstream) {
      inDegree[downstream]++;
    }
  }

  QQueue<Node*> queue;
  for (auto it = inDegree.constBegin(); it != inDegree.constEnd(); ++it) {
    if (it.value() == 0) {
      queue.enqueue(it.key());
    }
  }

  int count = 0;
  while (!queue.isEmpty()) {
    Node* node = queue.dequeue();
    count++;
    for (auto* downstream : node->downstreamNodes()) {
      inDegree[downstream]--;
      if (inDegree[downstream] == 0) {
        queue.enqueue(downstream);
      }
    }
  }

  return count == m_nodes.size();
}

void Pipeline::depthFirstTraversal(std::function<void(Node*)> visitor,
                                   const QList<Node*>& startNodes)
{
  QList<Node*> starts = startNodes.isEmpty() ? roots() : startNodes;
  QSet<Node*> visited;
  QStack<Node*> stack;

  for (auto* node : starts) {
    stack.push(node);
  }

  while (!stack.isEmpty()) {
    Node* current = stack.pop();
    if (visited.contains(current)) {
      continue;
    }
    visited.insert(current);
    visitor(current);

    for (auto* downstream : current->downstreamNodes()) {
      if (!visited.contains(downstream)) {
        stack.push(downstream);
      }
    }
  }
}

QList<Node*> Pipeline::topologicalSort(const QList<Node*>& startNodes)
{
  // Determine the subset of nodes to sort
  QSet<Node*> subset;
  if (startNodes.isEmpty()) {
    for (auto* node : m_nodes) {
      subset.insert(node);
    }
  } else {
    // Collect all nodes reachable from startNodes
    QStack<Node*> stack;
    for (auto* node : startNodes) {
      stack.push(node);
    }
    while (!stack.isEmpty()) {
      Node* current = stack.pop();
      if (subset.contains(current)) {
        continue;
      }
      subset.insert(current);
      for (auto* downstream : current->downstreamNodes()) {
        if (!subset.contains(downstream)) {
          stack.push(downstream);
        }
      }
    }
  }

  // Kahn's algorithm on the subset
  QMap<Node*, int> inDegree;
  for (auto* node : subset) {
    inDegree[node] = 0;
  }
  for (auto* link : m_links) {
    Node* from = link->from()->node();
    Node* to = link->to()->node();
    if (subset.contains(from) && subset.contains(to)) {
      inDegree[to]++;
    }
  }

  QQueue<Node*> queue;
  for (auto it = inDegree.constBegin(); it != inDegree.constEnd(); ++it) {
    if (it.value() == 0) {
      queue.enqueue(it.key());
    }
  }

  QList<Node*> result;
  while (!queue.isEmpty()) {
    Node* node = queue.dequeue();
    result.append(node);
    for (auto* downstream : node->downstreamNodes()) {
      if (!subset.contains(downstream)) {
        continue;
      }
      inDegree[downstream]--;
      if (inDegree[downstream] == 0) {
        queue.enqueue(downstream);
      }
    }
  }

  return result;
}

void Pipeline::setExecutor(PipelineExecutor* executor)
{
  m_executor = executor;
  if (m_executor) {
    m_executor->setParent(this);
  }
}

PipelineExecutor* Pipeline::executor() const
{
  return m_executor;
}

ExecutionFuture* Pipeline::execute()
{
  if (!m_executor) {
    auto* defaultExec = new DefaultExecutor(this);
    setExecutor(defaultExec);
  }

  if (m_executor->isRunning()) {
    m_executor->cancel();
  }

  auto* future = new ExecutionFuture(this);
  auto order = topologicalSort();

  connect(m_executor, &PipelineExecutor::executionComplete, future,
          [this, future](bool success) {
            future->setFinished(success);
            emit executionFinished();
          },
          static_cast<Qt::ConnectionType>(Qt::AutoConnection |
                                          Qt::SingleShotConnection));

  emit executionStarted();
  m_executor->execute(order, this);

  return future;
}

ExecutionFuture* Pipeline::execute(Node* target)
{
  if (!m_executor) {
    auto* defaultExec = new DefaultExecutor(this);
    setExecutor(defaultExec);
  }

  auto order = executionOrder(target);
  if (order.isEmpty()) {
    auto* future = new ExecutionFuture(this);
    future->setFinished(true);
    return future;
  }

  if (m_executor->isRunning()) {
    m_executor->cancel();
  }

  auto* future = new ExecutionFuture(this);

  connect(m_executor, &PipelineExecutor::executionComplete, future,
          [this, future](bool success) {
            future->setFinished(success);
            emit executionFinished();
          },
          static_cast<Qt::ConnectionType>(Qt::AutoConnection |
                                          Qt::SingleShotConnection));

  emit executionStarted();
  m_executor->execute(order, this);

  return future;
}

QList<Node*> Pipeline::executionOrder(Node* target)
{
  if (!target) {
    return {};
  }

  // DFS upstream collecting Stale/New nodes, stopping at Current nodes
  QSet<Node*> needed;
  QStack<Node*> stack;
  stack.push(target);

  while (!stack.isEmpty()) {
    Node* current = stack.pop();
    if (needed.contains(current)) {
      continue;
    }
    if (current->state() == NodeState::Current) {
      continue;
    }
    needed.insert(current);
    for (auto* upstream : current->upstreamNodes()) {
      if (!needed.contains(upstream)) {
        stack.push(upstream);
      }
    }
  }

  if (needed.isEmpty()) {
    return {};
  }

  // Topological sort of just the needed nodes
  QMap<Node*, int> inDegree;
  for (auto* node : needed) {
    inDegree[node] = 0;
  }
  for (auto* link : m_links) {
    Node* from = link->from()->node();
    Node* to = link->to()->node();
    if (needed.contains(from) && needed.contains(to)) {
      inDegree[to]++;
    }
  }

  QQueue<Node*> queue;
  for (auto it = inDegree.constBegin(); it != inDegree.constEnd(); ++it) {
    if (it.value() == 0) {
      queue.enqueue(it.key());
    }
  }

  QList<Node*> result;
  while (!queue.isEmpty()) {
    Node* node = queue.dequeue();
    result.append(node);
    for (auto* downstream : node->downstreamNodes()) {
      if (!needed.contains(downstream)) {
        continue;
      }
      inDegree[downstream]--;
      if (inDegree[downstream] == 0) {
        queue.enqueue(downstream);
      }
    }
  }

  return result;
}

void Pipeline::releaseTransientData()
{
  for (auto* node : m_nodes) {
    for (auto* output : node->outputPorts()) {
      if (!output->isTransient() || !output->hasData()) {
        continue;
      }

      // Check if all downstream consumers are Current
      bool allCurrent = true;
      for (auto* link : output->links()) {
        Node* downstream = link->to()->node();
        if (downstream && downstream->state() != NodeState::Current) {
          allCurrent = false;
          break;
        }
      }

      if (allCurrent) {
        output->clearData();
      }
    }
  }
}

} // namespace pipeline
} // namespace tomviz
