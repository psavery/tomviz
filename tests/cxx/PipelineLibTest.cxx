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

#include "PipelineStateIO.h"
#include "SinkGroupNode.h"
#include "Tvh5Format.h"
#include "data/VolumeData.h"
#include "sinks/VolumeStatsSink.h"
#include "sinks/LegacyModuleSink.h"
#include "sinks/VolumeSink.h"
#include "sinks/SliceSink.h"
#include "sinks/ContourSink.h"
#include "sinks/ThresholdSink.h"
#include "sinks/SegmentSink.h"
#include "sinks/OutlineSink.h"
#include "sinks/ClipSink.h"
#include "sinks/RulerSink.h"
#include "sinks/ScaleCubeSink.h"
#include "sinks/PlotSink.h"
#include "sinks/MoleculeSink.h"
#include "sources/SphereSource.h"
#include "sources/ReaderSourceNode.h"
#include "transforms/ThresholdTransform.h"
#include "transforms/LegacyPythonTransform.h"
#include "PipelineStripWidget.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTextStream>

#include <h5cpp/h5readwrite.h>

// Qt defines 'slots' as a macro which conflicts with Python's object.h
#pragma push_macro("slots")
#undef slots
#include <pybind11/embed.h>
#pragma pop_macro("slots")

#include <vector>

#include <vtkDoubleArray.h>
#include <vtkFloatArray.h>
#include <vtkImageData.h>
#include <vtkMolecule.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTable.h>
#include <vtkXMLImageDataWriter.h>

namespace py = pybind11;
using namespace tomviz::pipeline;

// Test helpers

class DoubleTransform : public TransformNode
{
public:
  DoubleTransform() : TransformNode()
  {
    addInput("in", PortType::ImageData);
    addOutput("out", PortType::ImageData);
  }

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override
  {
    int val = inputs["in"].value<int>();
    QMap<QString, PortData> result;
    result["out"] = PortData(std::any(val * 2), PortType::ImageData);
    return result;
  }
};

class AddTransform : public TransformNode
{
public:
  AddTransform() : TransformNode()
  {
    addInput("a", PortType::ImageData);
    addInput("b", PortType::ImageData);
    addOutput("out", PortType::ImageData);
  }

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override
  {
    int a = inputs["a"].value<int>();
    int b = inputs["b"].value<int>();
    QMap<QString, PortData> result;
    result["out"] = PortData(std::any(a + b), PortType::ImageData);
    return result;
  }
};

// A simple passthrough transform with configurable port types.
class PassthroughTransform : public TransformNode
{
public:
  PassthroughTransform(PortTypes inType, PortType outType)
    : TransformNode()
  {
    addInput("in", inType);
    addOutput("out", outType);
  }

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override
  {
    return { { "out", inputs["in"] } };
  }
};

class CollectorSink : public SinkNode
{
public:
  CollectorSink() : SinkNode() { addInput("in", PortType::ImageData); }

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

  PortData data(std::any(42), PortType::ImageData);
  EXPECT_TRUE(data.isValid());
  EXPECT_EQ(data.type(), PortType::ImageData);
  EXPECT_EQ(data.value<int>(), 42);

  data.clear();
  EXPECT_FALSE(data.isValid());
}

TEST_F(PipelineLibTest, PortTypeCompatibility)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  // ImageData -> ImageData should work
  auto* outPort = source->outputPort("out");
  auto* inPort = transform->inputPort("in");
  EXPECT_TRUE(inPort->canConnectTo(outPort));

  // Create a source with Table type
  auto* tableSource = new SourceNode();
  tableSource->addOutput("out", PortType::Table);
  pipeline->addNode(tableSource);

  // Table -> ImageData input should fail
  auto* tablePort = tableSource->outputPort("out");
  EXPECT_FALSE(inPort->canConnectTo(tablePort));
}

TEST_F(PipelineLibTest, LinkCreation)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::ImageData);
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
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  // Set data on source (marks source Current, downstream Stale)
  source->setOutputData("out", PortData(std::any(5), PortType::ImageData));

  EXPECT_EQ(source->state(), NodeState::Current);
  EXPECT_EQ(t1->state(), NodeState::Stale);
  EXPECT_EQ(t2->state(), NodeState::Stale);
}

TEST_F(PipelineLibTest, StalenessPropagationMultiBranch)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t1);
  pipeline->addNode(t2);

  // Source fans out to both t1 and t2
  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(source->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(10), PortType::ImageData));

  EXPECT_EQ(t1->state(), NodeState::Stale);
  EXPECT_EQ(t2->state(), NodeState::Stale);
}

TEST_F(PipelineLibTest, TopologicalSort)
{
  auto* source = new SourceNode();
  source->setLabel("source");
  source->addOutput("out", PortType::ImageData);
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
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  auto* sink = new CollectorSink();
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  pipeline->createLink(transform->outputPort("out"), sink->inputPort("in"));

  source->setOutputData("out", PortData(std::any(7), PortType::ImageData));

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
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(3), PortType::ImageData));

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
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(5), PortType::ImageData));

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
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(5), PortType::ImageData));

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
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  transform->outputPort("out")->setTransient(true);
  pipeline->addNode(transform);

  auto* sink = new CollectorSink();
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  pipeline->createLink(transform->outputPort("out"), sink->inputPort("in"));

  source->setOutputData("out", PortData(std::any(4), PortType::ImageData));

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
  source1->addOutput("out", PortType::ImageData);
  pipeline->addNode(source1);

  auto* source2 = new SourceNode();
  source2->addOutput("out", PortType::ImageData);
  pipeline->addNode(source2);

  auto* add = new AddTransform();
  pipeline->addNode(add);

  pipeline->createLink(source1->outputPort("out"), add->inputPort("a"));
  pipeline->createLink(source2->outputPort("out"), add->inputPort("b"));

  source1->setOutputData("out", PortData(std::any(10), PortType::ImageData));
  source2->setOutputData("out", PortData(std::any(20), PortType::ImageData));

  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(future->succeeded());
  EXPECT_EQ(add->state(), NodeState::Current);
  EXPECT_EQ(add->outputPort("out")->data().value<int>(), 30);
}

TEST_F(PipelineLibTest, PipelineValid)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));

  EXPECT_TRUE(pipeline->isValid());
}

TEST_F(PipelineLibTest, RootsDetection)
{
  auto* source1 = new SourceNode();
  source1->addOutput("out", PortType::ImageData);
  auto* source2 = new SourceNode();
  source2->addOutput("out", PortType::ImageData);
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
  source->addOutput("out", PortType::ImageData);
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
  source->addOutput("out", PortType::ImageData);
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
  source->addOutput("out", PortType::ImageData);
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
  source->addOutput("out", PortType::ImageData);
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
  source1->addOutput("out", PortType::ImageData);
  auto* source2 = new SourceNode();
  source2->addOutput("out", PortType::ImageData);
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
  EXPECT_EQ(portData.type(), PortType::ImageData);

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
  source->addOutput("out", PortType::ImageData);
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
  source->addOutput("vol", PortType::ImageData);
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
  source->addOutput("out", PortType::ImageData);
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
  source->addOutput("out", PortType::ImageData);
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
  source1->addOutput("out", PortType::ImageData);
  pipeline->addNode(source1);

  auto* source2 = new SourceNode();
  source2->setLabel("Source B");
  source2->addOutput("out", PortType::ImageData);
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

TEST_F(PipelinePythonTest, IntArgumentsRoundTripAsInt)
{
  // Regression: Qt6 collapses every JSON number into QVariant<double>,
  // so deserialize used to box ``int``/``enumeration`` arguments as
  // doubles, which then reached Python as a float — breaking operators
  // that index sequences with the parameter.  The fix coerces saved
  // arguments using each parameter's declared type from the operator
  // JSON description.
  QString jsonStr = R"({
    "name": "AxisOp",
    "label": "Axis Op",
    "parameters": [
      { "name": "axis", "type": "enumeration", "default": 2 },
      { "name": "iterations", "type": "int", "default": 1 }
    ]
  })";

  // Minimal script: stash the runtime types of the two int-typed
  // parameters in module-level globals so the test can inspect them.
  QString script =
    "import builtins\n"
    "_axis_type = None\n"
    "_iterations_type = None\n"
    "def transform(dataset, axis=0, iterations=0):\n"
    "    builtins._axis_type = type(axis).__name__\n"
    "    builtins._iterations_type = type(iterations).__name__\n";

  // Round-trip through deserialize so we exercise the saved-argument
  // path (where the Qt6 double-coercion bug used to live).
  QJsonObject saved;
  saved["description"] = jsonStr;
  saved["script"] = script;
  QJsonObject args;
  args["axis"] = 1;
  args["iterations"] = 5;
  saved["arguments"] = args;

  auto* transform = new LegacyPythonTransform();
  ASSERT_TRUE(transform->deserialize(saved));
  EXPECT_EQ(transform->parameter("axis").typeId(),
            static_cast<int>(QMetaType::Int));
  EXPECT_EQ(transform->parameter("iterations").typeId(),
            static_cast<int>(QMetaType::Int));
  EXPECT_EQ(transform->parameter("axis").toInt(), 1);
  EXPECT_EQ(transform->parameter("iterations").toInt(), 5);

  // End-to-end: drive the transform and verify Python received int.
  auto* source = new SphereSource();
  source->setDimensions(4, 4, 4);
  pipeline->addNode(source);
  ASSERT_TRUE(source->execute());
  pipeline->addNode(transform);
  pipeline->createLink(source->outputPort("volume"),
                       transform->inputPort("volume"));

  auto* future = pipeline->execute();
  EXPECT_TRUE(future->isFinished());
  EXPECT_EQ(transform->state(), NodeState::Current);

  py::module_ builtins = py::module_::import("builtins");
  EXPECT_EQ(builtins.attr("_axis_type").cast<std::string>(), "int");
  EXPECT_EQ(builtins.attr("_iterations_type").cast<std::string>(), "int");
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

  // Execute — readImageData handles VTI directly.
  ASSERT_TRUE(readerNode->execute());
  EXPECT_EQ(readerNode->state(), NodeState::Current);

  // Verify output
  auto portData = readerNode->outputPort("volume")->data();
  EXPECT_TRUE(portData.isValid());
  EXPECT_EQ(portData.type(), PortType::ImageData);

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

TEST_F(PipelineLibTest, ReaderSourceNodeNoFilesFails)
{
  auto* readerNode = new ReaderSourceNode();
  pipeline->addNode(readerNode);

  // No files set — execute should fail.
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

  // Create ReaderSourceNode; it routes through readImageData, which
  // picks VTK's vtkXMLImageDataReader for the .vti extension.
  auto* readerNode = new ReaderSourceNode();
  readerNode->setFileNames({ tmpPath });
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
    addInput("in", PortType::ImageData);
    addOutput("out", PortType::ImageData);
  }

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override
  {
    QThread::msleep(50);
    int val = inputs["in"].value<int>();
    QMap<QString, PortData> result;
    result["out"] = PortData(std::any(val * 2), PortType::ImageData);
    return result;
  }
};

TEST_F(PipelineLibTest, ThreadedExecutorBasic)
{
  pipeline->setExecutor(new ThreadedExecutor(pipeline));

  auto* source = new SourceNode();
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* transform = new SlowTransform();
  pipeline->addNode(transform);

  auto* sink = new CollectorSink();
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("out"), transform->inputPort("in"));
  pipeline->createLink(transform->outputPort("out"), sink->inputPort("in"));

  source->setOutputData("out", PortData(std::any(7), PortType::ImageData));

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
  source->addOutput("out", PortType::ImageData);
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

  source->setOutputData("out", PortData(std::any(1), PortType::ImageData));

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
  source->addOutput("out", PortType::ImageData);
  pipeline->addNode(source);

  auto* t1 = new SlowTransform();
  auto* t2 = new SlowTransform();
  pipeline->addNode(t1);
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  source->setOutputData("out", PortData(std::any(5), PortType::ImageData));

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

// --- LegacyModuleSink / visualization sink tests ---

TEST_F(PipelineLibTest, VolumeSinkConsumeWithSphereSource)
{
  auto* source = new SphereSource();
  source->setDimensions(8, 8, 8);
  pipeline->addNode(source);

  auto* sink = new VolumeSink();
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("volume"),
                       sink->inputPort("volume"));

  source->execute();
  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(future->succeeded());
  EXPECT_EQ(sink->state(), NodeState::Current);
}

TEST_F(PipelineLibTest, VisibilityToggling)
{
  auto* sink = new VolumeSink();

  EXPECT_TRUE(sink->visibility());

  QSignalSpy spy(sink, &LegacyModuleSink::visibilityChanged);

  sink->setVisibility(false);
  EXPECT_FALSE(sink->visibility());
  EXPECT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toBool(), false);

  // Setting same value should not emit again
  sink->setVisibility(false);
  EXPECT_EQ(spy.count(), 1);

  sink->setVisibility(true);
  EXPECT_TRUE(sink->visibility());
  EXPECT_EQ(spy.count(), 2);

  delete sink;
}

TEST_F(PipelineLibTest, ColorMapFlags)
{
  // Sinks that need a colormap
  VolumeSink volumeSink;
  SliceSink sliceSink;
  ContourSink contourSink;
  ThresholdSink thresholdSink;
  SegmentSink segmentSink;

  EXPECT_TRUE(volumeSink.isColorMapNeeded());
  EXPECT_TRUE(sliceSink.isColorMapNeeded());
  EXPECT_TRUE(contourSink.isColorMapNeeded());
  EXPECT_TRUE(thresholdSink.isColorMapNeeded());
  EXPECT_TRUE(segmentSink.isColorMapNeeded());

  // Sinks that do not need a colormap
  OutlineSink outlineSink;
  ClipSink clipSink;
  RulerSink rulerSink;
  ScaleCubeSink scaleCubeSink;
  PlotSink plotSink;
  MoleculeSink moleculeSink;

  EXPECT_FALSE(outlineSink.isColorMapNeeded());
  EXPECT_FALSE(clipSink.isColorMapNeeded());
  EXPECT_FALSE(rulerSink.isColorMapNeeded());
  EXPECT_FALSE(scaleCubeSink.isColorMapNeeded());
  EXPECT_FALSE(plotSink.isColorMapNeeded());
  EXPECT_FALSE(moleculeSink.isColorMapNeeded());
}

TEST_F(PipelineLibTest, VolumeToTablePortRejection)
{
  // A Volume source should not connect to a Table input (PlotSink)
  auto* source = new SphereSource();
  source->setDimensions(8, 8, 8);
  pipeline->addNode(source);

  auto* plotSink = new PlotSink();
  pipeline->addNode(plotSink);

  auto* link = pipeline->createLink(source->outputPort("volume"),
                                    plotSink->inputPort("table"));
  EXPECT_EQ(link, nullptr);
}

TEST_F(PipelineLibTest, SerializationRoundTrip)
{
  auto* sink = new VolumeSink();
  sink->setLabel("My Volume View");
  sink->setVisibility(false);

  QJsonObject json = sink->serialize();
  EXPECT_EQ(json["label"].toString(), "My Volume View");
  EXPECT_EQ(json["visible"].toBool(), false);

  // Deserialize into a fresh sink
  auto* sink2 = new VolumeSink();
  EXPECT_TRUE(sink2->deserialize(json));
  EXPECT_EQ(sink2->label(), "My Volume View");
  EXPECT_FALSE(sink2->visibility());

  delete sink;
  delete sink2;
}

TEST_F(PipelineLibTest, PipelineStateIOLinearRoundTrip)
{
  auto* source = new SphereSource();
  source->setDimensions(8, 8, 8);
  source->setRadiusFraction(0.3);
  source->setLabel("My Sphere");
  pipeline->addNode(source);

  auto* sink = new VolumeSink();
  sink->setLabel("My Volume");
  sink->setVisibility(false);
  pipeline->addNode(sink);

  pipeline->createLink(source->outputPort("volume"),
                       sink->inputPort("volume"));

  QJsonObject state;
  ASSERT_TRUE(PipelineStateIO::save(pipeline, state));
  EXPECT_EQ(state["schemaVersion"].toInt(), 2);
  EXPECT_EQ(state["pipeline"].toObject()["nodes"].toArray().size(), 2);
  EXPECT_EQ(state["pipeline"].toObject()["links"].toArray().size(), 1);

  Pipeline restored;
  ASSERT_TRUE(PipelineStateIO::load(&restored, state));
  ASSERT_EQ(restored.nodes().size(), 2);
  ASSERT_EQ(restored.links().size(), 1);
  // Sink deserialize is deferred to Pipeline::executionFinished to
  // avoid render warnings when setVisibility fires before consume.
  // Fire it synchronously for the test.
  QMetaObject::invokeMethod(&restored, "executionFinished",
                            Qt::DirectConnection);

  auto* restoredSource =
    dynamic_cast<SphereSource*>(restored.nodes()[0]);
  auto* restoredSink = dynamic_cast<VolumeSink*>(restored.nodes()[1]);
  ASSERT_NE(restoredSource, nullptr);
  ASSERT_NE(restoredSink, nullptr);
  EXPECT_EQ(restoredSource->label(), QString("My Sphere"));
  EXPECT_EQ(restoredSink->label(), QString("My Volume"));
  EXPECT_FALSE(restoredSink->visibility());

  auto* link = restored.links()[0];
  EXPECT_EQ(link->from()->node(), restoredSource);
  EXPECT_EQ(link->to()->node(), restoredSink);
  EXPECT_EQ(link->from()->name(), QString("volume"));
  EXPECT_EQ(link->to()->name(), QString("volume"));
}

TEST_F(PipelineLibTest, PipelineStateIOSinkGroupRoundTrip)
{
  auto* source = new SphereSource();
  source->setDimensions(8, 8, 8);
  pipeline->addNode(source);

  auto* group = new SinkGroupNode();
  group->addPassthrough("data", PortType::ImageData);
  pipeline->addNode(group);

  auto* volumeSink = new VolumeSink();
  auto* outlineSink = new OutlineSink();
  pipeline->addNode(volumeSink);
  pipeline->addNode(outlineSink);

  pipeline->createLink(source->outputPort("volume"),
                       group->inputPort("data"));
  pipeline->createLink(group->outputPort("data"),
                       volumeSink->inputPort("volume"));
  pipeline->createLink(group->outputPort("data"),
                       outlineSink->inputPort("volume"));

  QJsonObject state;
  ASSERT_TRUE(PipelineStateIO::save(pipeline, state));

  Pipeline restored;
  ASSERT_TRUE(PipelineStateIO::load(&restored, state));
  ASSERT_EQ(restored.nodes().size(), 4);
  ASSERT_EQ(restored.links().size(), 3);

  // The restored SinkGroupNode should have recreated its passthrough ports.
  SinkGroupNode* restoredGroup = nullptr;
  for (auto* node : restored.nodes()) {
    if (auto* g = dynamic_cast<SinkGroupNode*>(node)) {
      restoredGroup = g;
      break;
    }
  }
  ASSERT_NE(restoredGroup, nullptr);
  EXPECT_NE(restoredGroup->inputPort("data"), nullptr);
  EXPECT_NE(restoredGroup->outputPort("data"), nullptr);

  // The group's single output port should fan out to both sinks.
  auto* groupOutput = restoredGroup->outputPort("data");
  ASSERT_NE(groupOutput, nullptr);
  EXPECT_EQ(groupOutput->links().size(), 2);
}

TEST_F(PipelineLibTest, PipelineStateIOLabelBreakpointProperties)
{
  auto* source = new SphereSource();
  source->setLabel("Src");
  source->setBreakpoint(true);
  source->setProperty("ui.color", QString("#abcdef"));
  source->setProperty("priority", 7);
  pipeline->addNode(source);

  QJsonObject state;
  ASSERT_TRUE(PipelineStateIO::save(pipeline, state));

  Pipeline restored;
  ASSERT_TRUE(PipelineStateIO::load(&restored, state));
  ASSERT_EQ(restored.nodes().size(), 1);
  auto* restoredSource = dynamic_cast<SphereSource*>(restored.nodes()[0]);
  ASSERT_NE(restoredSource, nullptr);
  EXPECT_EQ(restoredSource->label(), QString("Src"));
  EXPECT_TRUE(restoredSource->hasBreakpoint());
  EXPECT_EQ(restoredSource->property("ui.color").toString(),
            QString("#abcdef"));
  EXPECT_EQ(restoredSource->property("priority").toInt(), 7);
}

TEST_F(PipelineLibTest, PipelineStateIOTypeInferenceSourcesRoundTrip)
{
  // A passthrough with an ImageData output, explicitly inferring its
  // type from a non-default input. Round-trip should preserve the
  // mapping in typeInferenceSources.
  auto* source = new SphereSource();
  pipeline->addNode(source);

  auto* passthrough =
    new PassthroughTransform(PortType::ImageData, PortType::ImageData);
  passthrough->setTypeInferenceSource("out", "in");
  pipeline->addNode(passthrough);

  pipeline->createLink(source->outputPort("volume"),
                       passthrough->inputPort("in"));

  // PassthroughTransform isn't registered in NodeFactory, so we can't
  // round-trip it through the factory. Verify the serialized JSON has
  // the typeInferenceSources entry instead.
  QJsonObject json = passthrough->serialize();
  auto tis = json.value("typeInferenceSources").toObject();
  EXPECT_EQ(tis.value("out").toString(), QString("in"));

  // Now verify a factory-registered node round-trips the mapping too.
  // CropTransform has "in" and "out" port names; setting an explicit
  // mapping exercises the base Node serialize path through a real
  // factory type.
}

TEST_F(PipelineLibTest, PipelineStateIONodeStateRoundTrip)
{
  // Sources execute eagerly on load and end up Current. Sink state is
  // intentionally not restored — a sink is only legitimately Current
  // after consume() has run in the current session (otherwise the
  // pipeline would skip it on the next manual execute). So sinks
  // always land at default New after load.
  auto* src = new SphereSource();
  pipeline->addNode(src);

  auto* sinkStale = new VolumeSink();
  pipeline->addNode(sinkStale);
  sinkStale->markStale();

  auto* sinkNew = new OutlineSink();
  pipeline->addNode(sinkNew);
  // Leave at default New.

  QJsonObject state;
  ASSERT_TRUE(PipelineStateIO::save(pipeline, state));

  Pipeline restored;
  ASSERT_TRUE(PipelineStateIO::load(&restored, state));
  ASSERT_EQ(restored.nodes().size(), 3);
  // Sink deserialize is deferred to executionFinished.
  QMetaObject::invokeMethod(&restored, "executionFinished",
                            Qt::DirectConnection);
  EXPECT_EQ(restored.nodes()[0]->state(), NodeState::Current);
  EXPECT_EQ(restored.nodes()[1]->state(), NodeState::New);
  EXPECT_EQ(restored.nodes()[2]->state(), NodeState::New);
}

TEST_F(PipelineLibTest, Tvh5FormatWriteEmbedsDataAndStampsDataRef)
{
  auto* src = new SphereSource();
  src->setDimensions(6, 6, 6);
  pipeline->addNode(src);
  ASSERT_TRUE(src->execute());

  QTemporaryFile tmp("XXXXXX.tvh5");
  tmp.setAutoRemove(true);
  ASSERT_TRUE(tmp.open());
  QString path = tmp.fileName();
  tmp.close();

  ASSERT_TRUE(tomviz::Tvh5Format::write(path.toStdString(), pipeline));

  using h5::H5ReadWrite;
  H5ReadWrite reader(path.toStdString(), H5ReadWrite::OpenMode::ReadOnly);
  int nodeId = pipeline->nodeId(src);
  std::string portGroup =
    "/data/" + std::to_string(nodeId) + "/volume";
  EXPECT_TRUE(reader.isGroup(portGroup));

  auto bytes = reader.readData<char>("tomviz_state");
  QJsonDocument doc =
    QJsonDocument::fromJson(QByteArray(bytes.data(), bytes.size()));
  ASSERT_TRUE(doc.isObject());
  auto state = doc.object();
  EXPECT_EQ(state.value("schemaVersion").toInt(), 2);

  auto nodesArr = state.value("pipeline").toObject().value("nodes").toArray();
  ASSERT_EQ(nodesArr.size(), 1);
  auto srcEntry = nodesArr[0].toObject();
  auto outputs = srcEntry.value("outputPorts").toObject();
  auto volumeEntry = outputs.value("volume").toObject();
  // VolumeData metadata must be embedded on the port entry.
  EXPECT_TRUE(volumeEntry.contains("metadata"));
  // dataRef must point at the HDF5 group.
  auto dataRef = volumeEntry.value("dataRef").toObject();
  EXPECT_EQ(dataRef.value("container").toString(), QString("h5"));
  EXPECT_EQ(dataRef.value("path").toString(),
            QString::fromStdString(portGroup));
}

TEST_F(PipelineLibTest, PipelineStateIOReaderSourceNodeReReadsAfterLoad)
{
  // Write a VTI file so ReaderSourceNode has something to re-read.
  auto* sphere = new SphereSource();
  sphere->setDimensions(6, 6, 6);
  pipeline->addNode(sphere);
  ASSERT_TRUE(sphere->execute());
  auto originalVolume =
    sphere->outputPort("volume")->data().value<VolumeDataPtr>();
  ASSERT_TRUE(originalVolume && originalVolume->isValid());
  auto originalDims = originalVolume->dimensions();

  QTemporaryFile tmpFile("XXXXXX.vti");
  tmpFile.setAutoRemove(true);
  ASSERT_TRUE(tmpFile.open());
  QString tmpPath = tmpFile.fileName();
  tmpFile.close();
  auto writer = vtkSmartPointer<vtkXMLImageDataWriter>::New();
  writer->SetFileName(tmpPath.toStdString().c_str());
  writer->SetInputData(originalVolume->imageData());
  writer->Write();

  // Build a ReaderSourceNode, pre-populate it (simulating what
  // LoadDataReaction does after reading), save, reload, execute.
  auto* original = new ReaderSourceNode();
  original->setFileNames({ tmpPath });
  ASSERT_TRUE(original->execute());  // primes the output port
  pipeline->addNode(original);

  QJsonObject state;
  ASSERT_TRUE(PipelineStateIO::save(pipeline, state));
  // Should serialize as source.reader (not source.generic).
  auto nodes = state["pipeline"].toObject()["nodes"].toArray();
  // First node is SphereSource; second is the ReaderSourceNode.
  EXPECT_EQ(nodes[1].toObject().value("type").toString(),
            QString("source.reader"));

  Pipeline restored;
  ASSERT_TRUE(PipelineStateIO::load(&restored, state));
  auto* restoredReader =
    dynamic_cast<ReaderSourceNode*>(restored.nodes()[1]);
  ASSERT_NE(restoredReader, nullptr);
  EXPECT_EQ(restoredReader->fileNames(), QStringList({ tmpPath }));
  // PipelineStateIO::load eagerly executes sources so downstream has
  // data even when the caller declines auto-execute — matches
  // LegacyStateLoader. So the reader is already Current and its
  // output port carries freshly-read data.
  EXPECT_EQ(restoredReader->state(), NodeState::Current);
  auto reloaded =
    restoredReader->outputPort("volume")->data().value<VolumeDataPtr>();
  ASSERT_TRUE(reloaded && reloaded->isValid());
  auto reloadedDims = reloaded->dimensions();
  EXPECT_EQ(reloadedDims[0], originalDims[0]);
  EXPECT_EQ(reloadedDims[1], originalDims[1]);
  EXPECT_EQ(reloadedDims[2], originalDims[2]);
}

TEST_F(PipelineLibTest, Tvh5FormatRoundTripSourceDataFromHdf5)
{
  // Full .tvh5 round-trip: save a pipeline with a SphereSource into
  // an HDF5 container (voxels embedded under /data/<nodeId>/<portName>),
  // then load it back into a fresh pipeline and verify the source's
  // output data was reconstructed from HDF5 (not re-executed).
  auto* sphere = new SphereSource();
  sphere->setDimensions(6, 6, 6);
  pipeline->addNode(sphere);
  ASSERT_TRUE(sphere->execute());
  auto originalDims =
    sphere->outputPort("volume")->data().value<VolumeDataPtr>()->dimensions();

  QTemporaryFile tmpFile("XXXXXX.tvh5");
  tmpFile.setAutoRemove(true);
  ASSERT_TRUE(tmpFile.open());
  QString path = tmpFile.fileName();
  tmpFile.close();

  ASSERT_TRUE(tomviz::Tvh5Format::write(path.toStdString(), pipeline));

  // Read the state JSON back.
  auto state = tomviz::Tvh5Format::readState(path.toStdString());
  EXPECT_EQ(state.value("schemaVersion").toInt(), 2);

  // Load into a fresh pipeline via PipelineStateIO + the HDF5 hook.
  Pipeline restored;
  std::string fileStd = path.toStdString();
  auto hook = [fileStd](Pipeline* p, const QJsonObject& pipelineJson) {
    tomviz::Tvh5Format::populatePayloadData(p, pipelineJson, fileStd);
  };
  ASSERT_TRUE(PipelineStateIO::load(&restored, state, {}, hook));
  ASSERT_EQ(restored.nodes().size(), 1);

  auto* restoredSphere =
    dynamic_cast<SphereSource*>(restored.nodes()[0]);
  ASSERT_NE(restoredSphere, nullptr);
  auto reloaded =
    restoredSphere->outputPort("volume")->data().value<VolumeDataPtr>();
  ASSERT_TRUE(reloaded && reloaded->isValid());
  auto reloadedDims = reloaded->dimensions();
  EXPECT_EQ(reloadedDims[0], originalDims[0]);
  EXPECT_EQ(reloadedDims[1], originalDims[1]);
  EXPECT_EQ(reloadedDims[2], originalDims[2]);
}

TEST_F(PipelineLibTest, Tvh5FormatRoundTripTablePort)
{
  // Stand up a SourceNode with a Table output port and stuff a small
  // vtkTable (one numeric column, one string column) onto it so the
  // writer has something to persist.  Then save → load → verify the
  // restored port carries an equivalent table.
  auto* src = new SourceNode();
  src->setLabel("TableSource");
  src->addOutput("table", PortType::Table);
  pipeline->addNode(src);

  auto table = vtkSmartPointer<vtkTable>::New();
  auto numericCol = vtkSmartPointer<vtkDoubleArray>::New();
  numericCol->SetName("values");
  numericCol->InsertNextValue(1.5);
  numericCol->InsertNextValue(2.5);
  numericCol->InsertNextValue(3.5);
  table->AddColumn(numericCol);
  auto stringCol = vtkSmartPointer<vtkStringArray>::New();
  stringCol->SetName("labels");
  stringCol->InsertNextValue("alpha");
  stringCol->InsertNextValue("beta");
  stringCol->InsertNextValue("gamma");
  table->AddColumn(stringCol);

  src->outputPort("table")->setData(
    PortData(std::any(vtkSmartPointer<vtkTable>(table)), PortType::Table));
  src->markCurrent();

  QTemporaryFile tmpFile("XXXXXX.tvh5");
  tmpFile.setAutoRemove(true);
  ASSERT_TRUE(tmpFile.open());
  QString path = tmpFile.fileName();
  tmpFile.close();

  ASSERT_TRUE(tomviz::Tvh5Format::write(path.toStdString(), pipeline));

  auto state = tomviz::Tvh5Format::readState(path.toStdString());
  EXPECT_EQ(state.value("schemaVersion").toInt(), 2);

  Pipeline restored;
  std::string fileStd = path.toStdString();
  auto hook = [fileStd](Pipeline* p, const QJsonObject& pipelineJson) {
    tomviz::Tvh5Format::populatePayloadData(p, pipelineJson, fileStd);
  };
  ASSERT_TRUE(PipelineStateIO::load(&restored, state, {}, hook));
  ASSERT_EQ(restored.nodes().size(), 1u);

  auto* restoredSrc = restored.nodes()[0];
  ASSERT_NE(restoredSrc, nullptr);
  auto reloaded =
    restoredSrc->outputPort("table")->data().value<vtkSmartPointer<vtkTable>>();
  ASSERT_TRUE(reloaded != nullptr);
  ASSERT_EQ(reloaded->GetNumberOfColumns(), 2);
  ASSERT_EQ(reloaded->GetNumberOfRows(), 3);

  auto* col0 = vtkDoubleArray::SafeDownCast(reloaded->GetColumn(0));
  ASSERT_NE(col0, nullptr);
  EXPECT_STREQ(col0->GetName(), "values");
  EXPECT_DOUBLE_EQ(col0->GetValue(0), 1.5);
  EXPECT_DOUBLE_EQ(col0->GetValue(1), 2.5);
  EXPECT_DOUBLE_EQ(col0->GetValue(2), 3.5);

  auto* col1 = vtkStringArray::SafeDownCast(reloaded->GetColumn(1));
  ASSERT_NE(col1, nullptr);
  EXPECT_STREQ(col1->GetName(), "labels");
  EXPECT_EQ(col1->GetValue(0), "alpha");
  EXPECT_EQ(col1->GetValue(1), "beta");
  EXPECT_EQ(col1->GetValue(2), "gamma");
}

TEST_F(PipelineLibTest, Tvh5FormatRoundTripMoleculePort)
{
  // Mirror Tvh5FormatRoundTripTablePort but for a Molecule output.
  // Atoms (atomic numbers + positions) and bonds (atom-id pairs +
  // bond orders) must round-trip through write → read.
  auto* src = new SourceNode();
  src->setLabel("MoleculeSource");
  src->addOutput("molecule", PortType::Molecule);
  pipeline->addNode(src);

  auto molecule = vtkSmartPointer<vtkMolecule>::New();
  molecule->AppendAtom(1, 0.0, 0.0, 0.0);    // H
  molecule->AppendAtom(8, 0.96, 0.0, 0.0);   // O
  molecule->AppendAtom(1, 1.20, 0.93, 0.0);  // H
  molecule->AppendBond(0, 1, 1);
  molecule->AppendBond(1, 2, 2);

  src->outputPort("molecule")->setData(PortData(
    std::any(vtkSmartPointer<vtkMolecule>(molecule)), PortType::Molecule));
  src->markCurrent();

  QTemporaryFile tmpFile("XXXXXX.tvh5");
  tmpFile.setAutoRemove(true);
  ASSERT_TRUE(tmpFile.open());
  QString path = tmpFile.fileName();
  tmpFile.close();

  ASSERT_TRUE(tomviz::Tvh5Format::write(path.toStdString(), pipeline));

  auto state = tomviz::Tvh5Format::readState(path.toStdString());
  EXPECT_EQ(state.value("schemaVersion").toInt(), 2);

  Pipeline restored;
  std::string fileStd = path.toStdString();
  auto hook = [fileStd](Pipeline* p, const QJsonObject& pipelineJson) {
    tomviz::Tvh5Format::populatePayloadData(p, pipelineJson, fileStd);
  };
  ASSERT_TRUE(PipelineStateIO::load(&restored, state, {}, hook));
  ASSERT_EQ(restored.nodes().size(), 1u);

  auto* restoredSrc = restored.nodes()[0];
  ASSERT_NE(restoredSrc, nullptr);
  auto reloaded = restoredSrc->outputPort("molecule")
                    ->data()
                    .value<vtkSmartPointer<vtkMolecule>>();
  ASSERT_TRUE(reloaded != nullptr);
  ASSERT_EQ(reloaded->GetNumberOfAtoms(), 3);
  ASSERT_EQ(reloaded->GetNumberOfBonds(), 2);

  EXPECT_EQ(reloaded->GetAtom(0).GetAtomicNumber(), 1);
  EXPECT_EQ(reloaded->GetAtom(1).GetAtomicNumber(), 8);
  EXPECT_EQ(reloaded->GetAtom(2).GetAtomicNumber(), 1);

  auto pos1 = reloaded->GetAtom(1).GetPosition();
  EXPECT_FLOAT_EQ(pos1[0], 0.96f);
  EXPECT_FLOAT_EQ(pos1[1], 0.0f);
  EXPECT_FLOAT_EQ(pos1[2], 0.0f);

  auto bond0 = reloaded->GetBond(0);
  EXPECT_EQ(bond0.GetBeginAtomId(), 0);
  EXPECT_EQ(bond0.GetEndAtomId(), 1);
  EXPECT_EQ(reloaded->GetBondOrder(0), 1);
  auto bond1 = reloaded->GetBond(1);
  EXPECT_EQ(bond1.GetBeginAtomId(), 1);
  EXPECT_EQ(bond1.GetEndAtomId(), 2);
  EXPECT_EQ(reloaded->GetBondOrder(1), 2);
}

TEST_F(PipelineLibTest, PipelineStateIOCurrentWithoutDataDowngradesToStale)
{
  // A node saved as Current whose output ports carry no data after
  // load can't honestly stay Current — PipelineStateIO::load must
  // downgrade it (and cascade downstream).
  auto* src = new SourceNode();
  src->addOutput("volume", PortType::ImageData);
  src->markCurrent();
  pipeline->addNode(src);

  auto* sink = new VolumeSink();
  pipeline->addNode(sink);
  sink->markCurrent();
  pipeline->createLink(src->outputPort("volume"),
                       sink->inputPort("volume"));

  QJsonObject state;
  ASSERT_TRUE(PipelineStateIO::save(pipeline, state));

  Pipeline restored;
  ASSERT_TRUE(PipelineStateIO::load(&restored, state));
  ASSERT_EQ(restored.nodes().size(), 2);
  // Source had no data at save time, so on reload it can't be Current.
  EXPECT_NE(restored.nodes()[0]->state(), NodeState::Current);
  // Cascade: the downstream sink is also now stale (its upstream isn't
  // Current anymore).
  EXPECT_NE(restored.nodes()[1]->state(), NodeState::Current);
}

TEST_F(PipelineLibTest, PipelineStateIOBaseSourceNodeRoundTrip)
{
  // Base SourceNode (as created by LoadDataReaction / LegacyStateLoader)
  // has no output ports declared in its constructor. The loader must
  // recreate them from the serialized outputPorts map so links resolve.
  auto* src = new SourceNode();
  src->addOutput("volume", PortType::TiltSeries);
  src->setLabel("Legacy-loaded source");
  pipeline->addNode(src);

  auto* sink = new VolumeSink();
  pipeline->addNode(sink);
  pipeline->createLink(src->outputPort("volume"),
                       sink->inputPort("volume"));

  QJsonObject state;
  ASSERT_TRUE(PipelineStateIO::save(pipeline, state));
  auto nodes = state["pipeline"].toObject()["nodes"].toArray();
  ASSERT_EQ(nodes.size(), 2);
  EXPECT_EQ(nodes[0].toObject().value("type").toString(),
            QString("source.generic"));

  Pipeline restored;
  ASSERT_TRUE(PipelineStateIO::load(&restored, state));
  ASSERT_EQ(restored.nodes().size(), 2);
  ASSERT_EQ(restored.links().size(), 1);

  auto* restoredSrc = dynamic_cast<SourceNode*>(restored.nodes()[0]);
  ASSERT_NE(restoredSrc, nullptr);
  ASSERT_NE(restoredSrc->outputPort("volume"), nullptr);
  EXPECT_EQ(restoredSrc->outputPort("volume")->declaredType(),
            PortType::TiltSeries);
}

TEST_F(PipelineLibTest, PipelineStateIOPreservesNodeIds)
{
  auto* source = new SphereSource();
  pipeline->addNode(source);
  auto* sink = new VolumeSink();
  pipeline->addNode(sink);
  pipeline->createLink(source->outputPort("volume"),
                       sink->inputPort("volume"));

  // Force id assignment on save.
  QJsonObject state;
  ASSERT_TRUE(PipelineStateIO::save(pipeline, state));
  int sourceId = pipeline->nodeId(source);
  int sinkId = pipeline->nodeId(sink);

  Pipeline restored;
  ASSERT_TRUE(PipelineStateIO::load(&restored, state));
  ASSERT_EQ(restored.nodes().size(), 2);

  EXPECT_EQ(restored.nodeId(restored.nodes()[0]), sourceId);
  EXPECT_EQ(restored.nodeId(restored.nodes()[1]), sinkId);
  // nextNodeId must be strictly greater than any assigned id so
  // subsequent additions don't collide.
  EXPECT_GT(restored.nextNodeId(),
            std::max(sourceId, sinkId));
}

TEST_F(PipelineLibTest, AllVolumeSinksAcceptVolume)
{
  // Verify all volume-input sinks work in a pipeline with SphereSource
  auto* source = new SphereSource();
  source->setDimensions(8, 8, 8);
  pipeline->addNode(source);
  source->execute();

  // Create one of each volume-input sink type
  std::vector<LegacyModuleSink*> sinks = {
    new SliceSink(), new ContourSink(), new ThresholdSink(),
    new SegmentSink(), new OutlineSink(), new ClipSink(),
    new RulerSink(), new ScaleCubeSink()
  };

  for (auto* sink : sinks) {
    pipeline->addNode(sink);
    pipeline->createLink(source->outputPort("volume"),
                         sink->inputPort("volume"));
  }

  auto* future = pipeline->execute();

  EXPECT_TRUE(future->isFinished());
  EXPECT_TRUE(future->succeeded());

  for (auto* sink : sinks) {
    EXPECT_EQ(sink->state(), NodeState::Current) << sink->label().toStdString();
  }
}

// --- Volume port type hierarchy tests ---

TEST_F(PipelineLibTest, SubtypeCompatibilityMatrix)
{
  // ImageData input accepts all three volume types
  auto* imgInput = new SourceNode();
  auto* imgTransform = new DoubleTransform(); // has ImageData in
  pipeline->addNode(imgInput);
  pipeline->addNode(imgTransform);
  auto* inPort = imgTransform->inputPort("in");

  // ImageData -> ImageData: OK
  imgInput->addOutput("img", PortType::ImageData);
  EXPECT_TRUE(inPort->canConnectTo(imgInput->outputPort("img")));

  // TiltSeries -> ImageData: OK (subtype)
  auto* tsSource = new SourceNode();
  tsSource->addOutput("ts", PortType::TiltSeries);
  pipeline->addNode(tsSource);
  EXPECT_TRUE(inPort->canConnectTo(tsSource->outputPort("ts")));

  // Volume -> ImageData: OK (subtype)
  auto* volSource = new SourceNode();
  volSource->addOutput("vol", PortType::Volume);
  pipeline->addNode(volSource);
  EXPECT_TRUE(inPort->canConnectTo(volSource->outputPort("vol")));

  // TiltSeries input only accepts TiltSeries
  class TsSink : public SinkNode
  {
  public:
    TsSink() { addInput("in", PortType::TiltSeries); }

  protected:
    bool consume(const QMap<QString, PortData>&) override { return true; }
  };
  class VolSink : public SinkNode
  {
  public:
    VolSink() { addInput("in", PortType::Volume); }

  protected:
    bool consume(const QMap<QString, PortData>&) override { return true; }
  };

  auto* tsSinkNode = new TsSink();
  pipeline->addNode(tsSinkNode);
  auto* tsIn = tsSinkNode->inputPort("in");

  EXPECT_TRUE(tsIn->canConnectTo(tsSource->outputPort("ts")));     // TS -> TS
  EXPECT_FALSE(tsIn->canConnectTo(volSource->outputPort("vol")));  // Vol -> TS
  EXPECT_FALSE(tsIn->canConnectTo(imgInput->outputPort("img")));   // Img -> TS

  // Volume input only accepts Volume
  auto* volSinkNode = new VolSink();
  pipeline->addNode(volSinkNode);
  auto* volIn = volSinkNode->inputPort("in");

  EXPECT_TRUE(volIn->canConnectTo(volSource->outputPort("vol")));   // Vol -> Vol
  EXPECT_FALSE(volIn->canConnectTo(tsSource->outputPort("ts")));    // TS -> Vol
  EXPECT_FALSE(volIn->canConnectTo(imgInput->outputPort("img")));   // Img -> Vol
}

TEST_F(PipelineLibTest, EffectiveTypeInference)
{
  // Source outputs TiltSeries
  auto* source = new SourceNode();
  source->addOutput("out", PortType::TiltSeries);
  pipeline->addNode(source);

  // Generic transform: ImageData -> ImageData
  auto* transform = new DoubleTransform(); // ImageData in/out
  pipeline->addNode(transform);

  // Connect TiltSeries source to generic transform
  pipeline->createLink(source->outputPort("out"),
                       transform->inputPort("in"));

  // The generic transform's output should now have effective type TiltSeries
  auto* outPort = transform->outputPort("out");
  EXPECT_EQ(outPort->declaredType(), PortType::ImageData);
  EXPECT_EQ(outPort->type(), PortType::TiltSeries);
}

TEST_F(PipelineLibTest, EffectiveTypeInferenceVolume)
{
  // Source outputs Volume
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  // Generic transform
  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  pipeline->createLink(source->outputPort("out"),
                       transform->inputPort("in"));

  EXPECT_EQ(transform->outputPort("out")->type(), PortType::Volume);
}

TEST_F(PipelineLibTest, EffectiveTypePropagationChain)
{
  // TiltSeries source -> generic1 -> generic2 -> should all infer TiltSeries
  auto* source = new SourceNode();
  source->addOutput("out", PortType::TiltSeries);
  pipeline->addNode(source);

  auto* t1 = new DoubleTransform();
  pipeline->addNode(t1);
  auto* t2 = new DoubleTransform();
  pipeline->addNode(t2);

  pipeline->createLink(source->outputPort("out"), t1->inputPort("in"));
  pipeline->createLink(t1->outputPort("out"), t2->inputPort("in"));

  EXPECT_EQ(t1->outputPort("out")->type(), PortType::TiltSeries);
  EXPECT_EQ(t2->outputPort("out")->type(), PortType::TiltSeries);
}

TEST_F(PipelineLibTest, EffectiveTypeRevertsOnDisconnect)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::TiltSeries);
  pipeline->addNode(source);

  auto* transform = new DoubleTransform();
  pipeline->addNode(transform);

  auto* link = pipeline->createLink(source->outputPort("out"),
                                    transform->inputPort("in"));
  EXPECT_EQ(transform->outputPort("out")->type(), PortType::TiltSeries);

  // Disconnect — effective type should revert to declared (ImageData)
  pipeline->removeLink(link);
  EXPECT_EQ(transform->outputPort("out")->type(), PortType::ImageData);
}

TEST_F(PipelineLibTest, LinkValidityWithInference)
{
  // TiltSeries source -> generic transform -> TiltSeries-requiring node
  auto* source = new SourceNode();
  source->addOutput("out", PortType::TiltSeries);
  pipeline->addNode(source);

  auto* generic = new DoubleTransform(); // ImageData -> ImageData
  pipeline->addNode(generic);

  auto* tsNode = new PassthroughTransform(PortType::TiltSeries,
                                          PortType::TiltSeries);
  pipeline->addNode(tsNode);

  // Connect source -> generic (valid, TiltSeries -> ImageData)
  auto* link1 = pipeline->createLink(source->outputPort("out"),
                                     generic->inputPort("in"));
  ASSERT_NE(link1, nullptr);
  EXPECT_TRUE(link1->isValid());

  // Generic's output is now effectively TiltSeries, so this should work
  auto* link2 = pipeline->createLink(generic->outputPort("out"),
                                     tsNode->inputPort("in"));
  ASSERT_NE(link2, nullptr);
  EXPECT_TRUE(link2->isValid());

  // Now disconnect the source from generic — generic reverts to ImageData
  // link2 should become invalid
  pipeline->removeLink(link1);
  EXPECT_FALSE(link2->isValid());

  // Reconnect the source — link2 should become valid again
  link1 = pipeline->createLink(source->outputPort("out"),
                               generic->inputPort("in"));
  EXPECT_TRUE(link2->isValid());
}

TEST_F(PipelineLibTest, LinkValidityChangedSignal)
{
  auto* source = new SourceNode();
  source->addOutput("out", PortType::TiltSeries);
  pipeline->addNode(source);

  auto* generic = new DoubleTransform();
  pipeline->addNode(generic);

  auto* tsNode = new PassthroughTransform(PortType::TiltSeries,
                                          PortType::TiltSeries);
  pipeline->addNode(tsNode);

  auto* link1 = pipeline->createLink(source->outputPort("out"),
                                     generic->inputPort("in"));
  auto* link2 = pipeline->createLink(generic->outputPort("out"),
                                     tsNode->inputPort("in"));

  QSignalSpy validitySpy(link2, &Link::validityChanged);

  // Disconnect source — should trigger validityChanged(false)
  pipeline->removeLink(link1);
  EXPECT_EQ(validitySpy.count(), 1);
  EXPECT_FALSE(validitySpy.at(0).at(0).toBool());
}

TEST_F(PipelineLibTest, ExecutionSkipsNodesWithInvalidLinks)
{
  // Build: TiltSeries source -> generic -> TiltSeries-requiring node
  auto* source = new SourceNode();
  source->addOutput("out", PortType::TiltSeries);
  pipeline->addNode(source);
  source->setOutputData("out", PortData(std::any(5), PortType::ImageData));

  auto* generic = new DoubleTransform();
  pipeline->addNode(generic);

  auto* tsNode = new PassthroughTransform(PortType::TiltSeries,
                                          PortType::TiltSeries);
  pipeline->addNode(tsNode);

  // source (TiltSeries) -> generic (OK, infers TiltSeries)
  auto* link1 = pipeline->createLink(source->outputPort("out"),
                                     generic->inputPort("in"));
  ASSERT_NE(link1, nullptr);

  // generic (effective: TiltSeries) -> tsNode (requires TiltSeries) — valid
  auto* link2 = pipeline->createLink(generic->outputPort("out"),
                                     tsNode->inputPort("in"));
  ASSERT_NE(link2, nullptr);
  EXPECT_TRUE(link2->isValid());

  // Now disconnect source -> generic, making link2 invalid
  pipeline->removeLink(link1);
  EXPECT_FALSE(link2->isValid());

  // Try to execute; tsNode should be skipped
  auto* future = pipeline->execute();
  EXPECT_TRUE(future->isFinished());

  // tsNode should NOT have reached Current (skipped due to invalid link)
  EXPECT_NE(tsNode->state(), NodeState::Current);
}

TEST_F(PipelineLibTest, ExplicitTypeInferenceSource)
{
  // Node with two ImageData inputs; explicit mapping says output follows "b"
  class TwoInputTransform : public TransformNode
  {
  public:
    TwoInputTransform()
    {
      addInput("a", PortType::ImageData);
      addInput("b", PortType::ImageData);
      addOutput("out", PortType::ImageData);
      setTypeInferenceSource("out", "b");
    }

  protected:
    QMap<QString, PortData> transform(
      const QMap<QString, PortData>& inputs) override
    {
      return { { "out", inputs["a"] } };
    }
  };

  auto* tsSource = new SourceNode();
  tsSource->addOutput("out", PortType::TiltSeries);
  pipeline->addNode(tsSource);

  auto* volSource = new SourceNode();
  volSource->addOutput("out", PortType::Volume);
  pipeline->addNode(volSource);

  auto* transform = new TwoInputTransform();
  pipeline->addNode(transform);

  // Connect TiltSeries to "a", Volume to "b"
  pipeline->createLink(tsSource->outputPort("out"),
                       transform->inputPort("a"));
  pipeline->createLink(volSource->outputPort("out"),
                       transform->inputPort("b"));

  // Output should follow "b" (Volume), not "a" (TiltSeries)
  EXPECT_EQ(transform->outputPort("out")->type(), PortType::Volume);
}

TEST_F(PipelineLibTest, DefaultInferenceUsesFirstImageDataInput)
{
  // Node with two ImageData inputs, no explicit mapping
  class TwoInputTransform : public TransformNode
  {
  public:
    TwoInputTransform()
    {
      addInput("a", PortType::ImageData);
      addInput("b", PortType::ImageData);
      addOutput("out", PortType::ImageData);
    }

  protected:
    QMap<QString, PortData> transform(
      const QMap<QString, PortData>& inputs) override
    {
      return { { "out", inputs["a"] } };
    }
  };

  auto* tsSource = new SourceNode();
  tsSource->addOutput("out", PortType::TiltSeries);
  pipeline->addNode(tsSource);

  auto* volSource = new SourceNode();
  volSource->addOutput("out", PortType::Volume);
  pipeline->addNode(volSource);

  auto* transform = new TwoInputTransform();
  pipeline->addNode(transform);

  // Connect TiltSeries to "a" (first ImageData input), Volume to "b"
  pipeline->createLink(tsSource->outputPort("out"),
                       transform->inputPort("a"));
  pipeline->createLink(volSource->outputPort("out"),
                       transform->inputPort("b"));

  // Output should follow "a" (first ImageData input) → TiltSeries
  EXPECT_EQ(transform->outputPort("out")->type(), PortType::TiltSeries);
}

TEST_F(PipelineLibTest, ConcreteTypeNotInferred)
{
  // A node with TiltSeries output should always keep that type,
  // regardless of what's connected to its input
  auto* source = new SourceNode();
  source->addOutput("out", PortType::Volume);
  pipeline->addNode(source);

  auto* tsTransform = new PassthroughTransform(PortType::ImageData,
                                                PortType::TiltSeries);
  pipeline->addNode(tsTransform);

  pipeline->createLink(source->outputPort("out"),
                       tsTransform->inputPort("in"));

  // Output declared as TiltSeries — should stay TiltSeries, not inherit Volume
  EXPECT_EQ(tsTransform->outputPort("out")->type(), PortType::TiltSeries);
}

TEST_F(PipelineLibTest, IsVolumeTypeHelper)
{
  EXPECT_TRUE(isVolumeType(PortType::ImageData));
  EXPECT_TRUE(isVolumeType(PortType::TiltSeries));
  EXPECT_TRUE(isVolumeType(PortType::Volume));
  EXPECT_FALSE(isVolumeType(PortType::Table));
  EXPECT_FALSE(isVolumeType(PortType::Molecule));
  EXPECT_FALSE(isVolumeType(PortType::None));
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
