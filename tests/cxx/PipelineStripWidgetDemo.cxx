/* Manual demo app for PipelineStripWidget. Build target: pipelineStripDemo */

#include "InputPort.h"
#include "Link.h"
#include "Node.h"
#include "OutputPort.h"
#include "Pipeline.h"
#include "PipelineStripWidget.h"
#include "PortData.h"
#include "PortType.h"
#include "SinkNode.h"
#include "SourceNode.h"
#include "TransformNode.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

using namespace tomviz::pipeline;

// Simple transform subclass for demo
class DemoTransform : public TransformNode
{
public:
  DemoTransform(const QString& label,
                const QList<QPair<QString, PortType>>& inputs,
                const QList<QPair<QString, PortType>>& outputs)
    : TransformNode()
  {
    setLabel(label);
    for (auto& [name, type] : inputs) {
      addInput(name, type);
    }
    for (auto& [name, type] : outputs) {
      addOutput(name, type);
    }
  }

  DemoTransform(const QString& label, int numInputs = 1, int numOutputs = 1)
    : TransformNode()
  {
    setLabel(label);
    for (int i = 0; i < numInputs; ++i) {
      QString name = numInputs == 1 ? "in" : QString("in%1").arg(i);
      addInput(name, PortType::Volume);
    }
    for (int i = 0; i < numOutputs; ++i) {
      QString name;
      PortType type;
      if (numOutputs == 1) {
        name = "out";
        type = PortType::Volume;
      } else {
        if (i == 0) {
          name = "volume";
          type = PortType::Volume;
        } else if (i == 1) {
          name = "table";
          type = PortType::Table;
        } else {
          name = QString("out%1").arg(i);
          type = PortType::Molecule;
        }
      }
      addOutput(name, type);
    }
  }

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>&) override
  {
    return {};
  }
};

class DemoSink : public SinkNode
{
public:
  DemoSink(const QString& label,
           const QList<QPair<QString, PortType>>& inputs = {{"in",
                                                             PortType::Volume}})
    : SinkNode()
  {
    setLabel(label);
    for (auto& [name, type] : inputs) {
      addInput(name, type);
    }
  }

protected:
  bool consume(const QMap<QString, PortData>&) override { return true; }
};

// ---------------------------------------------------------------------------
// Pipeline builders
// ---------------------------------------------------------------------------

// Source -> Gaussian -> Threshold -> Contour -> Volume Stats
static Pipeline* buildLinearPipeline()
{
  auto* p = new Pipeline();

  auto* src = new SourceNode();
  src->setLabel("sample.vti");
  src->addOutput("volume", PortType::Volume);
  src->setOutputData("volume", PortData(std::any(1), PortType::Volume));
  p->addNode(src);

  auto* gauss = new DemoTransform("Gaussian Filter");
  p->addNode(gauss);
  p->createLink(src->outputPort("volume"), gauss->inputPort("in"));

  auto* thresh = new DemoTransform("Threshold");
  p->addNode(thresh);
  p->createLink(gauss->outputPort("out"), thresh->inputPort("in"));

  auto* contour = new DemoTransform("Contour");
  p->addNode(contour);
  p->createLink(thresh->outputPort("out"), contour->inputPort("in"));

  auto* stats = new DemoSink("Volume Stats");
  p->addNode(stats);
  p->createLink(contour->outputPort("out"), stats->inputPort("in"));

  return p;
}

// Source -> Segment (vol, table, molecules) -> Stats, Plot, MolViewer
static Pipeline* buildMultiOutputPipeline()
{
  auto* p = new Pipeline();

  auto* src = new SourceNode();
  src->setLabel("Load Data");
  src->addOutput("volume", PortType::Volume);
  src->setOutputData("volume", PortData(std::any(1), PortType::Volume));
  p->addNode(src);

  auto* seg = new DemoTransform("Segment", 1, 3);
  p->addNode(seg);
  p->createLink(src->outputPort("volume"), seg->inputPort("in"));

  auto* stats = new DemoTransform("Stats");
  p->addNode(stats);
  p->createLink(seg->outputPort("volume"), stats->inputPort("in"));

  auto* plot = new DemoSink("Plot",
                            {{"table", PortType::Table}});
  p->addNode(plot);
  p->createLink(seg->outputPort("table"), plot->inputPort("table"));

  auto* molView = new DemoSink("Molecule Viewer",
                               {{"mol", PortType::Molecule}});
  p->addNode(molView);
  p->createLink(seg->outputPort("out2"), molView->inputPort("mol"));

  return p;
}

// Source A, Source B -> Merge -> Normalize -> Export
static Pipeline* buildFanInPipeline()
{
  auto* p = new Pipeline();

  auto* srcA = new SourceNode();
  srcA->setLabel("Source A");
  srcA->addOutput("volume", PortType::Volume);
  srcA->setOutputData("volume", PortData(std::any(1), PortType::Volume));
  p->addNode(srcA);

  auto* srcB = new SourceNode();
  srcB->setLabel("Source B");
  srcB->addOutput("volume", PortType::Volume);
  srcB->setOutputData("volume", PortData(std::any(2), PortType::Volume));
  p->addNode(srcB);

  auto* merge = new DemoTransform(
    "Merge",
    {{"volA", PortType::Volume}, {"volB", PortType::Volume}},
    {{"merged", PortType::Volume}});
  p->addNode(merge);
  p->createLink(srcA->outputPort("volume"), merge->inputPort("volA"));
  p->createLink(srcB->outputPort("volume"), merge->inputPort("volB"));

  auto* norm = new DemoTransform("Normalize");
  p->addNode(norm);
  p->createLink(merge->outputPort("merged"), norm->inputPort("in"));

  auto* sink = new DemoSink("Export");
  p->addNode(sink);
  p->createLink(norm->outputPort("out"), sink->inputPort("in"));

  return p;
}

// Source -> Gaussian + Median (fan-out), both -> Compare (fan-in)
static Pipeline* buildFanOutPipeline()
{
  auto* p = new Pipeline();

  auto* src = new SourceNode();
  src->setLabel("sample.vti");
  src->addOutput("volume", PortType::Volume);
  src->setOutputData("volume", PortData(std::any(1), PortType::Volume));
  p->addNode(src);

  auto* gauss = new DemoTransform("Gaussian Filter");
  p->addNode(gauss);
  p->createLink(src->outputPort("volume"), gauss->inputPort("in"));

  auto* median = new DemoTransform("Median Filter");
  p->addNode(median);
  p->createLink(src->outputPort("volume"), median->inputPort("in"));

  auto* compare = new DemoTransform(
    "Compare",
    {{"volA", PortType::Volume}, {"volB", PortType::Volume}},
    {{"diff", PortType::Volume}, {"stats", PortType::Table}});
  p->addNode(compare);
  p->createLink(gauss->outputPort("out"), compare->inputPort("volA"));
  p->createLink(median->outputPort("out"), compare->inputPort("volB"));

  auto* view = new DemoSink("Diff Viewer");
  p->addNode(view);
  p->createLink(compare->outputPort("diff"), view->inputPort("in"));

  auto* plot = new DemoSink("Error Plot",
                            {{"table", PortType::Table}});
  p->addNode(plot);
  p->createLink(compare->outputPort("stats"), plot->inputPort("table"));

  return p;
}

// A more complex diamond + multi-output topology:
// Source -> Denoise -> Segment (vol, table)
//                  \-> Align -> Reconstruct -> Export
//           Segment.table -> Plot
static Pipeline* buildComplexPipeline()
{
  auto* p = new Pipeline();

  auto* src = new SourceNode();
  src->setLabel("tilt_series.mrc");
  src->addOutput("volume", PortType::Volume);
  src->setOutputData("volume", PortData(std::any(1), PortType::Volume));
  p->addNode(src);

  auto* denoise = new DemoTransform("Denoise (BM3D)");
  p->addNode(denoise);
  p->createLink(src->outputPort("volume"), denoise->inputPort("in"));

  auto* segment = new DemoTransform(
    "Segment",
    {{"in", PortType::Volume}},
    {{"labels", PortType::Volume}, {"stats", PortType::Table}});
  p->addNode(segment);
  p->createLink(denoise->outputPort("out"), segment->inputPort("in"));

  auto* align = new DemoTransform("Align Tilt");
  p->addNode(align);
  p->createLink(denoise->outputPort("out"), align->inputPort("in"));

  auto* recon = new DemoTransform("Reconstruct (SIRT)");
  p->addNode(recon);
  p->createLink(align->outputPort("out"), recon->inputPort("in"));

  auto* exportSink = new DemoSink("Export Volume");
  p->addNode(exportSink);
  p->createLink(recon->outputPort("out"), exportSink->inputPort("in"));

  auto* plot = new DemoSink("Segmentation Plot",
                            {{"table", PortType::Table}});
  p->addNode(plot);
  p->createLink(segment->outputPort("stats"), plot->inputPort("table"));

  return p;
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);

  QMainWindow window;
  window.setWindowTitle("PipelineStripWidget Demo");
  window.resize(800, 600);

  // Central placeholder
  auto* central = new QWidget();
  auto* centralLayout = new QVBoxLayout(central);
  auto* combo = new QComboBox();
  combo->addItem("Linear");
  combo->addItem("Multi-Output");
  combo->addItem("Fan-In");
  combo->addItem("Fan-Out");
  combo->addItem("Complex");
  centralLayout->addWidget(combo);
  centralLayout->addStretch();
  window.setCentralWidget(central);

  // Dock with scroll area containing the strip widget
  auto* dock = new QDockWidget("Pipeline", &window);
  dock->setMinimumWidth(200);

  auto* scroll = new QScrollArea();
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto* strip = new PipelineStripWidget();
  scroll->setWidget(strip);
  dock->setWidget(scroll);
  window.addDockWidget(Qt::LeftDockWidgetArea, dock);

  // Pipeline instances
  Pipeline* pipelines[5] = { buildLinearPipeline(), buildMultiOutputPipeline(),
                              buildFanInPipeline(), buildFanOutPipeline(),
                              buildComplexPipeline() };

  strip->setPipeline(pipelines[0]);

  QObject::connect(combo, &QComboBox::currentIndexChanged,
                   [&](int index) { strip->setPipeline(pipelines[index]); });

  QObject::connect(strip, &PipelineStripWidget::nodeSelected,
                   [](Node* node) {
                     if (node)
                       qDebug("Selected node: %s",
                              qPrintable(node->label()));
                   });

  QObject::connect(strip, &PipelineStripWidget::nodeDoubleClicked,
                   [](Node* node) {
                     qDebug("Double-clicked: %s", qPrintable(node->label()));
                   });

  window.show();
  return app.exec();
}
