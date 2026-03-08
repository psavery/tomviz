/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <gtest/gtest.h>

#include "DefaultExecutor.h"
#include "ExecutionFuture.h"
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
#include "ThreadedExecutor.h"
#include "TransformNode.h"

#include "data/VolumeData.h"
#include "sinks/VolumeStatsSink.h"
#include "sources/SphereSource.h"
#include "sources/DataReader.h"
#include "sources/ReaderSourceNode.h"
#include "transforms/ThresholdTransform.h"
#include "transforms/LegacyPythonTransform.h"
#include "PipelineStripWidget.h"

#include <QApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTextStream>

// Qt defines 'slots' as a macro which conflicts with Python's object.h
#pragma push_macro("slots")
#undef slots
#include <pybind11/embed.h>
#pragma pop_macro("slots")

#include <vtkFloatArray.h>
#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkXMLImageDataWriter.h>

namespace py = pybind11;
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

  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(future->succeeded());
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
  auto* future = pipeline->execute(t1);

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(future->succeeded());
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

  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_FALSE(future->succeeded());
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

  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(future->succeeded());
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

  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(future->succeeded());
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

// --- VolumeData tests ---

TEST_F(PipelineLibTest, VolumeDataBasics)
{
  VolumeData vol;
  EXPECT_FALSE(vol.isValid());
  EXPECT_EQ(vol.numberOfComponents(), 0);

  auto dims = vol.dimensions();
  EXPECT_EQ(dims[0], 0);
}

TEST_F(PipelineLibTest, VolumeDataMetadata)
{
  VolumeData vol;
  vol.setLabel("Test Volume");
  vol.setUnits("nm");
  EXPECT_EQ(vol.label(), "Test Volume");
  EXPECT_EQ(vol.units(), "nm");
}

// --- SphereSource tests ---

TEST_F(PipelineLibTest, SphereSourceGeneratesVolume)
{
  auto* source = new SphereSource();
  pipeline->addNode(source);

  EXPECT_TRUE(source->execute());
  EXPECT_EQ(source->state(), NodeState::Current);

  auto portData = source->outputPort("volume")->data();
  EXPECT_TRUE(portData.isValid());
  EXPECT_EQ(portData.type(), PortType::Volume);

  auto volume = portData.value<VolumeDataPtr>();
  EXPECT_TRUE(volume->isValid());

  auto dims = volume->dimensions();
  EXPECT_EQ(dims[0], 32);
  EXPECT_EQ(dims[1], 32);
  EXPECT_EQ(dims[2], 32);
  EXPECT_EQ(volume->label(), "Sphere");
}

TEST_F(PipelineLibTest, SphereSourceCustomDimensions)
{
  auto* source = new SphereSource();
  source->setDimensions(16, 16, 16);
  pipeline->addNode(source);

  source->execute();

  auto volume = source->outputPort("volume")->data().value<VolumeDataPtr>();
  auto dims = volume->dimensions();
  EXPECT_EQ(dims[0], 16);
  EXPECT_EQ(dims[1], 16);
  EXPECT_EQ(dims[2], 16);
}

TEST_F(PipelineLibTest, SphereSourceScalarRange)
{
  auto* source = new SphereSource();
  source->setDimensions(16, 16, 16);
  pipeline->addNode(source);

  source->execute();

  auto volume = source->outputPort("volume")->data().value<VolumeDataPtr>();
  auto range = volume->scalarRange();
  // Signed distance: negative inside, positive outside
  EXPECT_LT(range[0], 0.0);
  EXPECT_GT(range[1], 0.0);
}

// --- ThresholdTransform tests ---

TEST_F(PipelineLibTest, ThresholdTransformBinaryMask)
{
  auto* source = new SphereSource();
  source->setDimensions(16, 16, 16);
  pipeline->addNode(source);

  auto* threshold = new ThresholdTransform();
  // Inside the sphere: signed distance <= 0
  threshold->setMinValue(-100.0);
  threshold->setMaxValue(0.0);
  pipeline->addNode(threshold);

  pipeline->createLink(source->outputPort("volume"),
                       threshold->inputPort("volume"));

  source->execute();
  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_EQ(threshold->state(), NodeState::Current);

  auto maskData = threshold->outputPort("mask")->data().value<VolumeDataPtr>();
  EXPECT_TRUE(maskData->isValid());

  auto range = maskData->scalarRange();
  EXPECT_EQ(range[0], 0.0);
  EXPECT_EQ(range[1], 1.0);
}

// --- VolumeStatsSink tests ---

TEST_F(PipelineLibTest, VolumeStatsSinkComputesStats)
{
  auto* source = new SphereSource();
  source->setDimensions(16, 16, 16);
  pipeline->addNode(source);

  auto* stats = new VolumeStatsSink();
  pipeline->addNode(stats);

  pipeline->createLink(source->outputPort("volume"),
                       stats->inputPort("volume"));

  source->execute();
  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(stats->hasResults());
  EXPECT_EQ(stats->voxelCount(), 16 * 16 * 16);
  EXPECT_LT(stats->min(), 0.0);
  EXPECT_GT(stats->max(), 0.0);
  EXPECT_EQ(stats->state(), NodeState::Current);
}

// --- End-to-end: SphereSource → ThresholdTransform → VolumeStatsSink ---

TEST_F(PipelineLibTest, EndToEndSphereThresholdStats)
{
  auto* source = new SphereSource();
  source->setDimensions(16, 16, 16);
  pipeline->addNode(source);

  auto* threshold = new ThresholdTransform();
  threshold->setMinValue(-100.0);
  threshold->setMaxValue(0.0);
  pipeline->addNode(threshold);

  auto* stats = new VolumeStatsSink();
  pipeline->addNode(stats);

  pipeline->createLink(source->outputPort("volume"),
                       threshold->inputPort("volume"));
  pipeline->createLink(threshold->outputPort("mask"),
                       stats->inputPort("volume"));

  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(future->succeeded());
  EXPECT_EQ(source->state(), NodeState::Current);
  EXPECT_EQ(threshold->state(), NodeState::Current);
  EXPECT_EQ(stats->state(), NodeState::Current);

  EXPECT_TRUE(stats->hasResults());
  // Mask is binary: min=0, max=1
  EXPECT_EQ(stats->min(), 0.0);
  EXPECT_EQ(stats->max(), 1.0);
  // Mean should be between 0 and 1 (fraction of voxels inside sphere)
  EXPECT_GT(stats->mean(), 0.0);
  EXPECT_LT(stats->mean(), 1.0);
}

// --- PipelineStripWidget tests ---

TEST_F(PipelineLibTest, WidgetBasicInstantiation)
{
  PipelineStripWidget widget;
  EXPECT_EQ(widget.pipeline(), nullptr);
  EXPECT_EQ(widget.selectedNode(), nullptr);
  EXPECT_EQ(widget.selectedPort(), nullptr);

  widget.setPipeline(pipeline);
  EXPECT_EQ(widget.pipeline(), pipeline);
}

TEST_F(PipelineLibTest, WidgetLayoutLinear)
{
  auto* source = new SourceNode();
  source->setLabel("Source");
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  transform->setLabel("Double");
  pipeline->addNode(transform);

  auto* sink = new CollectorSink();
  sink->setLabel("Collector");
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  pipeline->createLink(transform->outputPort("out"), sink->inputPort("in"));

  PipelineStripWidget widget;
  widget.setPipeline(pipeline);
  widget.resize(250, 400);
  widget.rebuildLayout();

  // Should have 3 node cards (single-output nodes are collapsed by default)
  EXPECT_EQ(widget.selectedNode(), nullptr);

  // Verify the widget has a reasonable size hint
  auto hint = widget.sizeHint();
  EXPECT_GT(hint.height(), 0);
  EXPECT_GT(hint.width(), 0);
}

TEST_F(PipelineLibTest, WidgetLayoutMultiOutput)
{
  // Create a node with multiple outputs
  auto* source = new SourceNode();
  source->setLabel("Source");
  source->addOutput("vol", PortType::Volume);
  source->addOutput("table", PortType::Table);
  pipeline->addNode(source);

  PipelineStripWidget widget;
  widget.setPipeline(pipeline);
  widget.resize(250, 400);
  widget.rebuildLayout();

  // Multi-output node should automatically show port sub-cards
  // 1 node card + 2 port cards = 3 layout items
  auto hint = widget.sizeHint();
  EXPECT_GT(hint.height(), 0);
}

TEST_F(PipelineLibTest, WidgetExpandCollapse)
{
  auto* source = new SourceNode();
  source->setLabel("Source");
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  PipelineStripWidget widget;
  widget.setPipeline(pipeline);
  widget.resize(250, 400);

  // Single-output: collapsed by default
  EXPECT_FALSE(widget.isExpanded(source));

  // Expand
  widget.setExpanded(source, true);
  EXPECT_TRUE(widget.isExpanded(source));

  // Collapse
  widget.setExpanded(source, false);
  EXPECT_FALSE(widget.isExpanded(source));
}

TEST_F(PipelineLibTest, WidgetSignalWiring)
{
  PipelineStripWidget widget;
  widget.setPipeline(pipeline);
  widget.resize(250, 400);

  // Adding nodes should trigger layout rebuild
  auto* source = new SourceNode();
  source->setLabel("Source");
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto hint1 = widget.sizeHint();

  auto* transform = new DoubleTransform();
  transform->setLabel("Double");
  pipeline->addNode(transform);

  auto hint2 = widget.sizeHint();

  // After adding a second node, size hint should increase
  EXPECT_GT(hint2.height(), hint1.height());
}

TEST_F(PipelineLibTest, WidgetFanIn)
{
  auto* source1 = new SourceNode();
  source1->setLabel("Source A");
  source1->addOutput("out", PortType::Volume);
  pipeline->addNode(source1);

  auto* source2 = new SourceNode();
  source2->setLabel("Source B");
  source2->addOutput("out", PortType::Volume);
  pipeline->addNode(source2);

  auto* add = new AddTransform();
  add->setLabel("Merge");
  pipeline->addNode(add);

  pipeline->createLink(source1->outputPort("out"), add->inputPort("a"));
  pipeline->createLink(source2->outputPort("out"), add->inputPort("b"));

  PipelineStripWidget widget;
  widget.setPipeline(pipeline);
  widget.resize(250, 400);
  widget.rebuildLayout();

  auto hint = widget.sizeHint();
  EXPECT_GT(hint.height(), 0);
}

// --- LegacyPythonTransform tests ---

class PipelinePythonTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (!Py_IsInitialized()) {
      py::initialize_interpreter();
      s_ownInterpreter = true;
    }
  }

  static void TearDownTestSuite()
  {
    if (s_ownInterpreter) {
      py::finalize_interpreter();
      s_ownInterpreter = false;
    }
  }

  void SetUp() override { pipeline = new Pipeline(); }
  void TearDown() override { delete pipeline; }

  static QString readFile(const QString& path)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return {};
    }
    QTextStream stream(&file);
    return stream.readAll();
  }

  Pipeline* pipeline;
  static bool s_ownInterpreter;
};

bool PipelinePythonTest::s_ownInterpreter = false;

TEST_F(PipelinePythonTest, AddConstantOperator)
{
  // Load AddConstant JSON and Python script
  QString pythonDir = TOMVIZ_PYTHON_DIR;
  QString jsonStr = readFile(pythonDir + "/AddConstant.json");
  QString scriptStr = readFile(pythonDir + "/AddConstant.py");
  ASSERT_FALSE(jsonStr.isEmpty());
  ASSERT_FALSE(scriptStr.isEmpty());

  // Create a 4x4x4 volume with all voxels set to 5.0
  auto* source = new SphereSource();
  source->setDimensions(4, 4, 4);
  pipeline->addNode(source);
  source->execute();

  // Get the source volume and record its scalar range
  auto inputVolume =
    source->outputPort("volume")->data().value<VolumeDataPtr>();
  auto inputRange = inputVolume->scalarRange();

  // Create the legacy Python transform
  auto* transform = new LegacyPythonTransform();
  transform->setJSONDescription(jsonStr);
  transform->setScript(scriptStr);
  transform->setParameter("constant", 10.0);
  pipeline->addNode(transform);

  EXPECT_EQ(transform->label(), "Add Constant");
  EXPECT_EQ(transform->operatorName(), "AddConstant");
  EXPECT_EQ(transform->parameter("constant").toDouble(), 10.0);

  pipeline->createLink(source->outputPort("volume"),
                       transform->inputPort("volume"));

  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_EQ(transform->state(), NodeState::Current);

  auto outputData =
    transform->outputPort("volume")->data().value<VolumeDataPtr>();
  ASSERT_TRUE(outputData && outputData->isValid());

  auto outputRange = outputData->scalarRange();

  // The output range should be shifted by +10.0
  EXPECT_NEAR(outputRange[0], inputRange[0] + 10.0, 0.01);
  EXPECT_NEAR(outputRange[1], inputRange[1] + 10.0, 0.01);
}

TEST_F(PipelinePythonTest, JSONParameterDefaults)
{
  QString jsonStr = R"({
    "name": "TestOp",
    "label": "Test Operator",
    "parameters": [
      { "name": "threshold", "type": "double", "default": 0.5 },
      { "name": "iterations", "type": "int", "default": 10 },
      { "name": "verbose", "type": "bool", "default": true }
    ]
  })";

  auto* transform = new LegacyPythonTransform();
  transform->setJSONDescription(jsonStr);

  EXPECT_EQ(transform->label(), "Test Operator");
  EXPECT_EQ(transform->operatorName(), "TestOp");
  EXPECT_EQ(transform->parameter("threshold").toDouble(), 0.5);
  EXPECT_EQ(transform->parameter("iterations").toInt(), 10);
  EXPECT_EQ(transform->parameter("verbose").toBool(), true);

  // Override a parameter
  transform->setParameter("threshold", 1.5);
  EXPECT_EQ(transform->parameter("threshold").toDouble(), 1.5);

  delete transform;
}

TEST_F(PipelinePythonTest, JSONResultsPorts)
{
  QString jsonStr = R"({
    "name": "SegOp",
    "label": "Segment",
    "results": [
      { "name": "segmentation_table", "type": "table" },
      { "name": "molecule_result", "type": "molecule" }
    ]
  })";

  auto* transform = new LegacyPythonTransform();
  transform->setJSONDescription(jsonStr);

  // Should have volume output plus two result ports
  EXPECT_NE(transform->outputPort("volume"), nullptr);
  EXPECT_NE(transform->outputPort("segmentation_table"), nullptr);
  EXPECT_NE(transform->outputPort("molecule_result"), nullptr);

  delete transform;
}

// --- ReaderSourceNode tests ---

TEST_F(PipelineLibTest, ReaderSourceNodeVTIRoundTrip)
{
  // Create a sphere source and execute to get volume data
  auto* sphereSource = new SphereSource();
  sphereSource->setDimensions(8, 8, 8);
  pipeline->addNode(sphereSource);
  ASSERT_TRUE(sphereSource->execute());

  auto originalVolume =
    sphereSource->outputPort("volume")->data().value<VolumeDataPtr>();
  ASSERT_TRUE(originalVolume && originalVolume->isValid());

  auto originalDims = originalVolume->dimensions();
  auto originalRange = originalVolume->scalarRange();

  // Write to a temp VTI file
  QTemporaryFile tmpFile("XXXXXX.vti");
  tmpFile.setAutoRemove(true);
  ASSERT_TRUE(tmpFile.open());
  QString tmpPath = tmpFile.fileName();
  tmpFile.close();

  auto writer = vtkSmartPointer<vtkXMLImageDataWriter>::New();
  writer->SetFileName(tmpPath.toStdString().c_str());
  writer->SetInputData(originalVolume->imageData());
  writer->Write();

  // Create ReaderSourceNode and read the VTI file
  auto* readerNode = new ReaderSourceNode();
  pipeline->addNode(readerNode);
  readerNode->setFileNames({ tmpPath });

  // Verify reader was auto-selected
  ASSERT_NE(readerNode->reader(), nullptr);

  // Execute
  ASSERT_TRUE(readerNode->execute());
  EXPECT_EQ(readerNode->state(), NodeState::Current);

  // Verify output
  auto portData = readerNode->outputPort("volume")->data();
  EXPECT_TRUE(portData.isValid());
  EXPECT_EQ(portData.type(), PortType::Volume);

  auto readVolume = portData.value<VolumeDataPtr>();
  ASSERT_TRUE(readVolume && readVolume->isValid());

  auto readDims = readVolume->dimensions();
  EXPECT_EQ(readDims[0], originalDims[0]);
  EXPECT_EQ(readDims[1], originalDims[1]);
  EXPECT_EQ(readDims[2], originalDims[2]);

  auto readRange = readVolume->scalarRange();
  EXPECT_NEAR(readRange[0], originalRange[0], 0.01);
  EXPECT_NEAR(readRange[1], originalRange[1], 0.01);
}

TEST_F(PipelineLibTest, CreateReaderUnknownExtension)
{
  auto reader = createReader({ "foo.xyz" });
  EXPECT_EQ(reader, nullptr);
}

TEST_F(PipelineLibTest, CreateReaderVTKFormats)
{
  // VTK-supported extensions should return a reader
  EXPECT_NE(createReader({ "test.vti" }), nullptr);
  EXPECT_NE(createReader({ "test.tif" }), nullptr);
  EXPECT_NE(createReader({ "test.tiff" }), nullptr);
  EXPECT_NE(createReader({ "test.png" }), nullptr);
  EXPECT_NE(createReader({ "test.jpg" }), nullptr);
  EXPECT_NE(createReader({ "test.jpeg" }), nullptr);
  EXPECT_NE(createReader({ "test.mrc" }), nullptr);
}

TEST_F(PipelineLibTest, ReaderSourceNodeNoReaderFails)
{
  auto* readerNode = new ReaderSourceNode();
  pipeline->addNode(readerNode);

  // No files set, no reader — execute should fail
  EXPECT_FALSE(readerNode->execute());
}

TEST_F(PipelineLibTest, ReaderSourceNodeLabelFromFileName)
{
  auto* readerNode = new ReaderSourceNode();
  pipeline->addNode(readerNode);
  readerNode->setFileNames({ "/path/to/my_data.vti" });

  EXPECT_EQ(readerNode->label(), "my_data.vti");
}

TEST_F(PipelinePythonTest, ReaderSourceNodePythonReader)
{
  // Write a VTI file via VTK (since NumpyReader depends on tomviz internals
  // that aren't available in standalone tests, we create a custom Python
  // reader module that wraps vtkXMLImageDataReader).

  // First create a VTI file to read
  vtkNew<vtkImageData> imageData;
  imageData->SetDimensions(4, 5, 6);
  imageData->AllocateScalars(VTK_FLOAT, 1);

  QTemporaryFile tmpFile("XXXXXX.vti");
  tmpFile.setAutoRemove(true);
  ASSERT_TRUE(tmpFile.open());
  QString tmpPath = tmpFile.fileName();
  tmpFile.close();

  auto writer = vtkSmartPointer<vtkXMLImageDataWriter>::New();
  writer->SetFileName(tmpPath.toStdString().c_str());
  writer->SetInputData(imageData.Get());
  writer->Write();

  // Register a Python reader module that reads VTI files
  {
    py::gil_scoped_acquire gil;
    py::exec(R"(
import types, sys
mod = types.ModuleType("test_vti_reader")
mod.__dict__['__file__'] = '<test>'

exec('''
from vtk import vtkXMLImageDataReader

class TestVTIReader:
    def read(self, path):
        reader = vtkXMLImageDataReader()
        reader.SetFileName(path)
        reader.Update()
        return reader.GetOutput()
''', mod.__dict__)

sys.modules["test_vti_reader"] = mod
)");
  }

  // Create ReaderSourceNode with our custom Python reader
  auto* readerNode = new ReaderSourceNode();
  readerNode->setFileNames({ tmpPath });
  readerNode->setReader(std::make_unique<PythonDataReader>(
    "test_vti_reader.TestVTIReader"));
  pipeline->addNode(readerNode);

  ASSERT_TRUE(readerNode->execute());

  auto portData = readerNode->outputPort("volume")->data();
  EXPECT_TRUE(portData.isValid());

  auto volume = portData.value<VolumeDataPtr>();
  ASSERT_TRUE(volume && volume->isValid());

  auto dims = volume->dimensions();
  EXPECT_EQ(dims[0], 4);
  EXPECT_EQ(dims[1], 5);
  EXPECT_EQ(dims[2], 6);
}

// --- ThreadedExecutor tests ---

class SlowTransform : public TransformNode
{
public:
  SlowTransform() : TransformNode()
  {
    addInput("in", PortType::Volume);
    addOutput("out", PortType::Volume);
  }

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override
  {
    QThread::msleep(50);
    int val = inputs["in"].value<int>();
    QMap<QString, PortData> result;
    result["out"] = PortData(std::any(val * 2), PortType::Volume);
    return result;
  }
};

TEST_F(PipelineLibTest, ThreadedExecutorBasic)
{
  pipeline->setExecutor(new ThreadedExecutor(pipeline));

  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* transform = new SlowTransform();
  pipeline->addNode(transform);

  auto* sink = new CollectorSink();
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  pipeline->createLink(transform->outputPort("out"), sink->inputPort("in"));

  source->setOutputData("out", PortData(std::any(7), PortType::Volume));

  auto* future = pipeline->execute();

  // With threaded executor, future should not be immediately finished
  QSignalSpy spy(future, &ExecutionFuture::finished);
  ASSERT_TRUE(spy.wait(5000));

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(future->succeeded());
  EXPECT_TRUE(sink->consumed);
  EXPECT_EQ(sink->lastValue, 14);
  EXPECT_EQ(transform->state(), NodeState::Current);
}

TEST_F(PipelineLibTest, ThreadedExecutorCancellation)
{
  auto* executor = new ThreadedExecutor(pipeline);
  pipeline->setExecutor(executor);

  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  // Chain of slow transforms — cancel should stop before all complete
  auto* t1 = new SlowTransform();
  auto* t2 = new SlowTransform();
  auto* t3 = new SlowTransform();
  pipeline->addNode(t1);
  pipeline->addNode(t2);
  pipeline->addNode(t3);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));
  pipeline->createLink(t2->outputPort("out"), t3->inputPort("in"));

  source->setOutputData("out", PortData(std::any(1), PortType::Volume));

  auto* future = pipeline->execute();
  executor->cancel();

  QSignalSpy spy(future, &ExecutionFuture::finished);
  ASSERT_TRUE(spy.wait(5000));

  EXPECT_TRUE(future->isFinished());
  EXPECT_FALSE(future->succeeded());
  // t3 should not have finished
  EXPECT_NE(t3->state(), NodeState::Current);
}

TEST_F(PipelineLibTest, ThreadedExecutorBreakpoint)
{
  pipeline->setExecutor(new ThreadedExecutor(pipeline));

  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* t1 = new SlowTransform();
  auto* t2 = new SlowTransform();
  pipeline->addNode(t1);
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(5), PortType::Volume));

  t1->setBreakpoint(true);

  Node* reachedNode = nullptr;
  QObject::connect(pipeline, &Pipeline::breakpointReached,
                   [&reachedNode](Node* n) { reachedNode = n; });

  auto* future = pipeline->execute();

  QSignalSpy spy(future, &ExecutionFuture::finished);
  ASSERT_TRUE(spy.wait(5000));

  EXPECT_TRUE(future->isFinished());
  EXPECT_FALSE(future->succeeded());
  EXPECT_EQ(reachedNode, t1);
  EXPECT_NE(t1->state(), NodeState::Current);
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
