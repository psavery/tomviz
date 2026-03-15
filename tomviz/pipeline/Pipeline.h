/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelinePipeline_h
#define tomvizPipelinePipeline_h

#include "tomviz_pipeline_export.h"

#include <QList>
#include <QObject>

#include <functional>

namespace tomviz {
namespace pipeline {

class ExecutionFuture;
class InputPort;
class Link;
class Node;
class OutputPort;
class PipelineExecutor;

enum class SortOrder
{
  Default,    // Kahn's algorithm, arbitrary tiebreaking (by pointer address)
  Stable,     // Kahn's algorithm, creation-order tiebreaking
  DepthFirst  // DFS reverse post-order (keeps chains together)
};

class TOMVIZ_PIPELINE_EXPORT Pipeline : public QObject
{
  Q_OBJECT

public:
  Pipeline(QObject* parent = nullptr);
  ~Pipeline() override;

  // Node management
  void addNode(Node* node);
  void removeNode(Node* node);
  QList<Node*> nodes() const;
  QList<Node*> roots() const;

  /// Return the index at which @a node was added (creation order).
  /// Returns -1 if the node is not in this pipeline.
  int creationIndex(Node* node) const;

  // Link management
  Link* createLink(OutputPort* from, InputPort* to);
  void removeLink(Link* link);
  QList<Link*> links() const;

  // Validation
  bool wouldCreateCycle(OutputPort* from, InputPort* to) const;
  bool isValid() const;

  // Traversal
  void depthFirstTraversal(std::function<void(Node*)> visitor,
                           const QList<Node*>& startNodes = {});
  QList<Node*> topologicalSort(
    const QList<Node*>& startNodes = {},
    SortOrder order = SortOrder::Default);

  // Execution
  void setExecutor(PipelineExecutor* executor);
  PipelineExecutor* executor() const;
  ExecutionFuture* execute();
  ExecutionFuture* execute(Node* target);
  QList<Node*> executionOrder(Node* target);

  // Transient cleanup
  void releaseTransientData();

signals:
  void nodeAdded(Node* node);
  void nodeRemoved(Node* node);
  void linkCreated(Link* link);
  void linkRemoved(Link* link);
  void executionStarted();
  void executionFinished();
  void breakpointReached(Node* node);

private:
  QList<Node*> m_nodes;
  QList<Link*> m_links;
  PipelineExecutor* m_executor = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
