/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <gtest/gtest.h>

#include "DefaultExecutor.h"
#include "InputPort.h"
#include "Link.h"
#include "Node.h"
#include "OutputPort.h"
#include "Pipeline.h"
#include "PipelineExecutor.h"
#include "PortData.h"
#include "PortType.h"
#include "SinkNode.h"
#include "SourceNode.h"
#include "TransformNode.h"

using namespace tomviz::pipeline;

// Test helpers

class DoubleTransform : public TransformNode
{
public:
  DoubleTransform() : TransformNode()
  {
    addInput("in", PortType::Volume);
    addOutput("out", PortType::Volume);
  }

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override
  {
    int val = inputs["in"].value<int>();
    QMap<QString, PortData> result;
    result["out"] = PortData(std::any(val * 2), PortType::Volume);
    return result;
  }
};

class AddTransform : public TransformNode
{
public:
  AddTransform() : TransformNode()
  {
    addInput("a", PortType::Volume);
    addInput("b", PortType::Volume);
    addOutput("out", PortType::Volume);
  }

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override
  {
    int a = inputs["a"].value<int>();
    int b = inputs["b"].value<int>();
    QMap<QString, PortData> result;
    result["out"] = PortData(std::any(a + b), PortType::Volume);
    return result;
  }
};

class CollectorSink : public SinkNode
{
public:
  CollectorSink() : SinkNode() { addInput("in", PortType::Volume); }

  int lastValue = 0;
  bool consumed = false;

protected:
  bool consume(const QMap<QString, PortData>& inputs) override
  {
    lastValue = inputs["in"].value<int>();
    consumed = true;
    return true;
  }
};

// Tests

class PipelineLibTest : public ::testing::Test
{
protected:
  void SetUp() override { pipeline = new Pipeline(); }
  void TearDown() override { delete pipeline; }
  Pipeline* pipeline;
};

TEST_F(PipelineLibTest, PortDataBasics)
{
  PortData empty;
  EXPECT_FALSE(empty.isValid());
  EXPECT_EQ(empty.type(), PortType::None);

  PortData data(std::any(42), PortType::Volume);
  EXPECT_TRUE(data.isValid());
  EXPECT_EQ(data.type(), PortType::Volume);
  EXPECT_EQ(data.value<int>(), 42);

  data.clear();
  EXPECT_FALSE(data.isValid());
}

TEST_F(PipelineLibTest, PortTypeCompatibility)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  // Volume -> Volume should work
  auto* outPort = source->outputPort("out");
  auto* inPort = transform->inputPort("in");
  EXPECT_TRUE(inPort->canConnectTo(outPort));

  // Create a source with Table type
  auto* tableSource = new SourceNode();
  tableSource->addOutput("out", PortType::Table);
  pipeline->addNode(tableSource);

  // Table -> Volume input should fail
  auto* tablePort = tableSource->outputPort("out");
  EXPECT_FALSE(inPort->canConnectTo(tablePort));
}

TEST_F(PipelineLibTest, LinkCreation)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  auto* link =
    pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  EXPECT_NE(link, nullptr);
  EXPECT_TRUE(link->isValid());
  EXPECT_EQ(pipeline->links().size(), 1);
  EXPECT_EQ(transform->inputPort("in")->link(), link);
  EXPECT_EQ(source->outputPort("out")->links().size(), 1);
}

TEST_F(PipelineLibTest, LinkTypeRejection)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Table);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  auto* link =
    pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  EXPECT_EQ(link, nullptr);
  EXPECT_EQ(pipeline->links().size(), 0);
}

TEST_F(PipelineLibTest, CycleDetection)
{
  auto* t1 = new DoubleTransform();
  t1->setLabel("T1");
  auto* t2 = new DoubleTransform();
  t2->setLabel("T2");
  pipeline->addNode(t1);
  pipeline->addNode(t2);

  auto* link1 =
    pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));
  EXPECT_NE(link1, nullptr);

  // T2 -> T1 would create a cycle
  EXPECT_TRUE(
    pipeline->wouldCreateCycle(t2->outputPort("out"), t1->inputPort("in")));
  auto* link2 =
    pipeline->createLink(t2->outputPort("out"), t1->inputPort("in"));
  EXPECT_EQ(link2, nullptr);
}

TEST_F(PipelineLibTest, SelfLoop)
{
  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  EXPECT_TRUE(
    pipeline->wouldCreateCycle(t1->outputPort("out"), t1->inputPort("in")));
}

TEST_F(PipelineLibTest, StalenessPropagation)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  // Set data on source (marks source Current, downstream Stale)
  source->setOutputData("out", PortData(std::any(5), PortType::Volume));

  EXPECT_EQ(source->state(), NodeState::Current);
  EXPECT_EQ(t1->state(), NodeState::Stale);
  EXPECT_EQ(t2->state(), NodeState::Stale);
}

TEST_F(PipelineLibTest, StalenessPropagationMultiBranch)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t1);
  pipeline->addNode(t2);

  // Source fans out to both t1 and t2
  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(source->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(10), PortType::Volume));

  EXPECT_EQ(t1->state(), NodeState::Stale);
  EXPECT_EQ(t2->state(), NodeState::Stale);
}

TEST_F(PipelineLibTest, TopologicalSort)
{
  auto* source = new SourceNode();
  source->setLabel("source");
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  t1->setLabel("t1");
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  t2->setLabel("t2");
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  auto sorted = pipeline->topologicalSort();
  EXPECT_EQ(sorted.size(), 3);

  // Source must come before t1, t1 before t2
  int srcIdx = sorted.indexOf(source);
  int t1Idx = sorted.indexOf(t1);
  int t2Idx = sorted.indexOf(t2);
  EXPECT_LT(srcIdx, t1Idx);
  EXPECT_LT(t1Idx, t2Idx);
}

TEST_F(PipelineLibTest, SimpleExecution)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  auto* sink = new CollectorSink();
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  pipeline->createLink(transform->outputPort("out"), sink->inputPort("in"));

  source->setOutputData("out", PortData(std::any(7), PortType::Volume));

  pipeline->execute();

  EXPECT_TRUE(sink->consumed);
  EXPECT_EQ(sink->lastValue, 14);
  EXPECT_EQ(transform->state(), NodeState::Current);
  EXPECT_EQ(sink->state(), NodeState::Current);
}

TEST_F(PipelineLibTest, ExecutionFromTarget)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(3), PortType::Volume));

  // Execute only up to t1
  pipeline->execute(t1);

  EXPECT_EQ(t1->state(), NodeState::Current);
  // t2 should still be stale since we only executed up to t1
  EXPECT_EQ(t2->state(), NodeState::Stale);

  // Verify t1's output
  EXPECT_EQ(t1->outputPort("out")->data().value<int>(), 6);
}

TEST_F(PipelineLibTest, ExecutionOrderWithStaleUpstream)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(5), PortType::Volume));

  auto order = pipeline->executionOrder(t2);
  // Should include t1 and t2 (source is Current)
  EXPECT_EQ(order.size(), 2);
  EXPECT_TRUE(order.contains(t1));
  EXPECT_TRUE(order.contains(t2));
  EXPECT_LT(order.indexOf(t1), order.indexOf(t2));
}

TEST_F(PipelineLibTest, BreakpointStopsExecution)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(5), PortType::Volume));

  // Set breakpoint on t1
  t1->setBreakpoint(true);

  Node* reachedNode = nullptr;
  QObject::connect(pipeline, &Pipeline::breakpointReached,
                   [&reachedNode](Node* n) { reachedNode = n; });

  pipeline->execute();

  EXPECT_EQ(reachedNode, t1);
  // t1 should not have been executed
  EXPECT_NE(t1->state(), NodeState::Current);
  EXPECT_NE(t2->state(), NodeState::Current);
}

TEST_F(PipelineLibTest, TransientDataRelease)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  transform->outputPort("out")->setTransient(true);
  pipeline->addNode(transform);

  auto* sink = new CollectorSink();
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  pipeline->createLink(transform->outputPort("out"), sink->inputPort("in"));

  source->setOutputData("out", PortData(std::any(4), PortType::Volume));

  pipeline->execute();

  // After execution, transient data should be released since sink is Current
  EXPECT_FALSE(transform->outputPort("out")->hasData());
  EXPECT_TRUE(sink->consumed);
  EXPECT_EQ(sink->lastValue, 8);
}

TEST_F(PipelineLibTest, FanInMultipleInputs)
{
  auto* source1 = new SourceNode();
  source1->addOutput("out", PortType::Volume);
  pipeline->addNode(source1);

  auto* source2 = new SourceNode();
  source2->addOutput("out", PortType::Volume);
  pipeline->addNode(source2);

  auto* add = new AddTransform();
  pipeline->addNode(add);

  pipeline->createLink(source1->outputPort("out"), add->inputPort("a"));
  pipeline->createLink(source2->outputPort("out"), add->inputPort("b"));

  source1->setOutputData("out", PortData(std::any(10), PortType::Volume));
  source2->setOutputData("out", PortData(std::any(20), PortType::Volume));

  pipeline->execute();

  EXPECT_EQ(add->state(), NodeState::Current);
  EXPECT_EQ(add->outputPort("out")->data().value<int>(), 30);
}

TEST_F(PipelineLibTest, PipelineValid)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));

  EXPECT_TRUE(pipeline->isValid());
}

TEST_F(PipelineLibTest, RootsDetection)
{
  auto* source1 = new SourceNode();
  source1->addOutput("out", PortType::Volume);
  auto* source2 = new SourceNode();
  source2->addOutput("out", PortType::Volume);
  auto* transform = new DoubleTransform();

  pipeline->addNode(source1);
  pipeline->addNode(source2);
  pipeline->addNode(transform);

  pipeline->createLink(source1->outputPort("out"), transform->inputPort("in"));

  auto r = pipeline->roots();
  EXPECT_EQ(r.size(), 2);
  EXPECT_TRUE(r.contains(source1));
  EXPECT_TRUE(r.contains(source2));
  EXPECT_FALSE(r.contains(transform));
}

TEST_F(PipelineLibTest, RemoveNode)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  auto* transform = new DoubleTransform();

  pipeline->addNode(source);
  pipeline->addNode(transform);
  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));

  EXPECT_EQ(pipeline->nodes().size(), 2);
  EXPECT_EQ(pipeline->links().size(), 1);

  pipeline->removeNode(transform);

  EXPECT_EQ(pipeline->nodes().size(), 1);
  EXPECT_EQ(pipeline->links().size(), 0);
}

TEST_F(PipelineLibTest, RemoveLink)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  auto* transform = new DoubleTransform();

  pipeline->addNode(source);
  pipeline->addNode(transform);
  auto* link =
    pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));

  EXPECT_EQ(pipeline->links().size(), 1);
  EXPECT_NE(transform->inputPort("in")->link(), nullptr);

  pipeline->removeLink(link);

  EXPECT_EQ(pipeline->links().size(), 0);
  EXPECT_EQ(transform->inputPort("in")->link(), nullptr);
}

TEST_F(PipelineLibTest, UpstreamDownstreamNodes)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  auto* transform = new DoubleTransform();
  auto* sink = new CollectorSink();

  pipeline->addNode(source);
  pipeline->addNode(transform);
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  pipeline->createLink(transform->outputPort("out"), sink->inputPort("in"));

  EXPECT_EQ(source->downstreamNodes().size(), 1);
  EXPECT_EQ(source->downstreamNodes().first(), transform);
  EXPECT_EQ(source->upstreamNodes().size(), 0);

  EXPECT_EQ(transform->upstreamNodes().size(), 1);
  EXPECT_EQ(transform->upstreamNodes().first(), source);
  EXPECT_EQ(transform->downstreamNodes().size(), 1);
  EXPECT_EQ(transform->downstreamNodes().first(), sink);

  EXPECT_EQ(sink->upstreamNodes().size(), 1);
  EXPECT_EQ(sink->upstreamNodes().first(), transform);
  EXPECT_EQ(sink->downstreamNodes().size(), 0);
}

TEST_F(PipelineLibTest, Signals)
{
  int nodeAddedCount = 0;
  int linkCreatedCount = 0;

  QObject::connect(pipeline, &Pipeline::nodeAdded,
                   [&nodeAddedCount](Node*) { nodeAddedCount++; });
  QObject::connect(pipeline, &Pipeline::linkCreated,
                   [&linkCreatedCount](Link*) { linkCreatedCount++; });

  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  auto* transform = new DoubleTransform();

  pipeline->addNode(source);
  pipeline->addNode(transform);
  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));

  EXPECT_EQ(nodeAddedCount, 2);
  EXPECT_EQ(linkCreatedCount, 1);
}

TEST_F(PipelineLibTest, ReplaceLinkOnInput)
{
  auto* source1 = new SourceNode();
  source1->addOutput("out", PortType::Volume);
  auto* source2 = new SourceNode();
  source2->addOutput("out", PortType::Volume);
  auto* transform = new DoubleTransform();

  pipeline->addNode(source1);
  pipeline->addNode(source2);
  pipeline->addNode(transform);

  auto* link1 = pipeline->createLink(source1->outputPort("out"),
                                     transform->inputPort("in"));
  EXPECT_NE(link1, nullptr);
  EXPECT_EQ(pipeline->links().size(), 1);

  // Creating a new link to the same input should replace the old one
  auto* link2 = pipeline->createLink(source2->outputPort("out"),
                                     transform->inputPort("in"));
  EXPECT_NE(link2, nullptr);
  EXPECT_EQ(pipeline->links().size(), 1);
  EXPECT_EQ(transform->inputPort("in")->link(), link2);
}
