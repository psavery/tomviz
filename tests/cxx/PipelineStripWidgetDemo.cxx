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
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
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

// Source -> Reconstruction -> Pad -> Align -> Denoise (vol, table)
//   Denoise.vol -> Outline, Slice, Volume (fan-out to 3 sinks)
//   Denoise.table -> Plot
static Pipeline* buildTomographyPipeline()
{
  auto* p = new Pipeline();

  auto* src = new SourceNode();
  src->setLabel("experiment.tiff");
  src->addOutput("Tilt Series", PortType::Volume);
  src->setOutputData("Tilt Series",
                     PortData(std::any(1), PortType::Volume));
  p->addNode(src);

  auto* recon = new DemoTransform(
    "Reconstruction",
    {{"in", PortType::Volume}},
    {{"Volume", PortType::Volume}});
  p->addNode(recon);
  p->createLink(src->outputPort("Tilt Series"), recon->inputPort("in"));

  auto* pad = new DemoTransform(
    "Pad",
    {{"in", PortType::Volume}},
    {{"Volume", PortType::Volume}});
  p->addNode(pad);
  p->createLink(recon->outputPort("Volume"), pad->inputPort("in"));

  auto* align = new DemoTransform(
    "Align",
    {{"in", PortType::Volume}},
    {{"Volume", PortType::Volume}});
  p->addNode(align);
  p->createLink(pad->outputPort("Volume"), align->inputPort("in"));

  auto* denoise = new DemoTransform(
    "Denoise",
    {{"in", PortType::Volume}},
    {{"Volume", PortType::Volume}, {"Metrics", PortType::Table}});
  p->addNode(denoise);
  p->createLink(align->outputPort("Volume"), denoise->inputPort("in"));

  auto* plot = new DemoSink("Plot",
                            {{"table", PortType::Table}});
  p->addNode(plot);
  p->createLink(denoise->outputPort("Metrics"), plot->inputPort("table"));

  auto* outline = new DemoSink("Outline");
  p->addNode(outline);
  p->createLink(denoise->outputPort("Volume"), outline->inputPort("in"));

  auto* slice = new DemoSink("Slice");
  p->addNode(slice);
  p->createLink(denoise->outputPort("Volume"), slice->inputPort("in"));

  auto* volume = new DemoSink("Volume");
  p->addNode(volume);
  p->createLink(denoise->outputPort("Volume"), volume->inputPort("in"));

  return p;
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);

  QMainWindow window;
  window.setWindowTitle("PipelineStripWidget Demo");
  window.resize(800, 600);

  // --- Central widget: pipeline selector + node creation ---
  auto* central = new QWidget();
  auto* centralLayout = new QVBoxLayout(central);

  // Pipeline selector
  auto* combo = new QComboBox();
  combo->addItem("Linear");
  combo->addItem("Multi-Output");
  combo->addItem("Fan-In");
  combo->addItem("Fan-Out");
  combo->addItem("Complex");
  combo->addItem("Tomography");
  combo->addItem("Empty");
  centralLayout->addWidget(combo);

  // Sort order selector
  auto* sortCombo = new QComboBox();
  sortCombo->addItem("Default (Kahn's)");
  sortCombo->addItem("Stable (creation order)");
  sortCombo->addItem("Depth-First (chains)");
  centralLayout->addWidget(sortCombo);

  // --- Helpers ---
  auto makeSpin = [](int defaultVal) {
    auto* spin = new QSpinBox();
    spin->setRange(0, 8);
    spin->setValue(defaultVal);
    return spin;
  };

  // Build a port list from three type-count spinners
  auto collectPorts = [](QSpinBox* volSpin, QSpinBox* tblSpin,
                         QSpinBox* molSpin, const QString& prefix)
    -> QList<QPair<QString, PortType>> {
    QList<QPair<QString, PortType>> ports;
    int total = volSpin->value() + tblSpin->value() + molSpin->value();
    int idx = 0;
    auto add = [&](int count, PortType type, const QString& tag) {
      for (int i = 0; i < count; ++i) {
        QString name = (total == 1 && idx == 0)
                         ? prefix
                         : QString("%1_%2%3").arg(prefix, tag,
                                                  count > 1
                                                    ? QString::number(i)
                                                    : QString());
        ports.append({ name, type });
        ++idx;
      }
    };
    add(volSpin->value(), PortType::Volume, "vol");
    add(tblSpin->value(), PortType::Table, "tbl");
    add(molSpin->value(), PortType::Molecule, "mol");
    return ports;
  };

  // Build a port-count row: "         | Input | Output" header + per-type rows
  // Returns { inputVol, inputTbl, inputMol, outputVol, outputTbl, outputMol }
  // Pass showInput=false for Source, showOutput=false for Sink.
  auto makePortGrid = [&makeSpin](QVBoxLayout* parent,
                                  bool showInput, bool showOutput,
                                  int defaultInVol, int defaultOutVol)
    -> std::tuple<QSpinBox*, QSpinBox*, QSpinBox*,
                  QSpinBox*, QSpinBox*, QSpinBox*> {
    // Header row
    auto* header = new QHBoxLayout();
    auto* headerLabel = new QLabel();
    headerLabel->setFixedWidth(70);
    header->addWidget(headerLabel);
    if (showInput) {
      auto* inLabel = new QLabel("Input");
      inLabel->setAlignment(Qt::AlignCenter);
      header->addWidget(inLabel);
    }
    if (showOutput) {
      auto* outLabel = new QLabel("Output");
      outLabel->setAlignment(Qt::AlignCenter);
      header->addWidget(outLabel);
    }
    parent->addLayout(header);

    QSpinBox* inVol = nullptr;
    QSpinBox* inTbl = nullptr;
    QSpinBox* inMol = nullptr;
    QSpinBox* outVol = nullptr;
    QSpinBox* outTbl = nullptr;
    QSpinBox* outMol = nullptr;

    auto addRow = [&](const QString& label, int defIn, int defOut)
      -> QPair<QSpinBox*, QSpinBox*> {
      auto* row = new QHBoxLayout();
      auto* lbl = new QLabel(label);
      lbl->setFixedWidth(70);
      row->addWidget(lbl);
      QSpinBox* inSpin = nullptr;
      QSpinBox* outSpin = nullptr;
      if (showInput) {
        inSpin = makeSpin(defIn);
        row->addWidget(inSpin);
      }
      if (showOutput) {
        outSpin = makeSpin(defOut);
        row->addWidget(outSpin);
      }
      parent->addLayout(row);
      return { inSpin, outSpin };
    };

    auto [iv, ov] = addRow("Volume", defaultInVol, defaultOutVol);
    inVol = iv; outVol = ov;
    auto [it, ot] = addRow("Table", 0, 0);
    inTbl = it; outTbl = ot;
    auto [im, om] = addRow("Molecule", 0, 0);
    inMol = im; outMol = om;

    return { inVol, inTbl, inMol, outVol, outTbl, outMol };
  };

  // --- Add Source ---
  auto* srcGroup = new QGroupBox("Add Source");
  auto* srcLayout = new QVBoxLayout(srcGroup);
  auto* srcLabelEdit = new QLineEdit("New Source");
  srcLayout->addWidget(srcLabelEdit);
  auto [s_iv, s_it, s_im, s_ov, s_ot, s_om] =
    makePortGrid(srcLayout, false, true, 0, 1);
  auto* srcButton = new QPushButton("Add Source");
  srcLayout->addWidget(srcButton);
  centralLayout->addWidget(srcGroup);

  // --- Add Transform ---
  auto* xfGroup = new QGroupBox("Add Transform");
  auto* xfLayout = new QVBoxLayout(xfGroup);
  auto* xfLabelEdit = new QLineEdit("New Transform");
  xfLayout->addWidget(xfLabelEdit);
  auto [t_iv, t_it, t_im, t_ov, t_ot, t_om] =
    makePortGrid(xfLayout, true, true, 1, 1);
  auto* xfButton = new QPushButton("Add Transform");
  xfLayout->addWidget(xfButton);
  centralLayout->addWidget(xfGroup);

  // --- Add Sink ---
  auto* sinkGroup = new QGroupBox("Add Sink");
  auto* sinkLayout = new QVBoxLayout(sinkGroup);
  auto* sinkLabelEdit = new QLineEdit("New Sink");
  sinkLayout->addWidget(sinkLabelEdit);
  auto [k_iv, k_it, k_im, k_ov, k_ot, k_om] =
    makePortGrid(sinkLayout, true, false, 1, 0);
  auto* sinkButton = new QPushButton("Add Sink");
  sinkLayout->addWidget(sinkButton);
  centralLayout->addWidget(sinkGroup);

  centralLayout->addStretch();
  window.setCentralWidget(central);

  // --- Dock with strip widget ---
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
  Pipeline* pipelines[7] = {  buildLinearPipeline(), buildMultiOutputPipeline(),
                              buildFanInPipeline(), buildFanOutPipeline(),
                              buildComplexPipeline(), buildTomographyPipeline(),
                              new Pipeline(),
                            };

  strip->setPipeline(pipelines[0]);

  // --- Add node handlers ---
  // Dummy spinboxes for missing columns (always 0)
  auto* zeroSpin = makeSpin(0);
  zeroSpin->setVisible(false);

  QObject::connect(srcButton, &QPushButton::clicked, [&]() {
    auto* p = strip->pipeline();
    if (!p) return;
    QString label = srcLabelEdit->text();
    if (label.isEmpty()) label = "Source";
    auto outputs = collectPorts(s_ov, s_ot, s_om, "out");
    auto* src = new SourceNode();
    src->setLabel(label);
    for (auto& [name, type] : outputs) {
      src->addOutput(name, type);
    }
    p->addNode(src);
  });

  QObject::connect(xfButton, &QPushButton::clicked, [&]() {
    auto* p = strip->pipeline();
    if (!p) return;
    QString label = xfLabelEdit->text();
    if (label.isEmpty()) label = "Transform";
    auto inputs = collectPorts(t_iv, t_it, t_im, "in");
    auto outputs = collectPorts(t_ov, t_ot, t_om, "out");
    auto* xform = new DemoTransform(label, inputs, outputs);
    p->addNode(xform);
  });

  QObject::connect(sinkButton, &QPushButton::clicked, [&]() {
    auto* p = strip->pipeline();
    if (!p) return;
    QString label = sinkLabelEdit->text();
    if (label.isEmpty()) label = "Sink";
    auto inputs = collectPorts(k_iv, k_it, k_im, "in");
    auto* sink = new DemoSink(label, inputs);
    p->addNode(sink);
  });

  // Link validator: same type, different nodes, target not already connected
  auto linkValidator = [](OutputPort* from, InputPort* to) -> bool {
    if (from->node() == to->node()) {
      return false;
    }
    if (!to->acceptedTypes().testFlag(from->type())) {
      return false;
    }
    if (to->link()) {
      return false;
    }
    return true;
  };
  strip->setLinkValidator(linkValidator);

  // Create the link when requested
  QObject::connect(strip, &PipelineStripWidget::linkRequested,
                   [&](OutputPort* from, InputPort* to) {
                     if (auto* p = strip->pipeline()) {
                       p->createLink(from, to);
                       qDebug("Created link: %s.%s -> %s.%s",
                              qPrintable(from->node()->label()),
                              qPrintable(from->name()),
                              qPrintable(to->node()->label()),
                              qPrintable(to->name()));
                     }
                   });

  // Context menu on links: delete action
  strip->setLinkMenuProvider([](Link* link, QMenu& menu) {
    menu.addAction("Delete Link", [link]() {
      auto* p = qobject_cast<Pipeline*>(link->parent());
      if (p) {
        qDebug("Deleting link: %s.%s -> %s.%s",
               qPrintable(link->from()->node()->label()),
               qPrintable(link->from()->name()),
               qPrintable(link->to()->node()->label()),
               qPrintable(link->to()->name()));
        p->removeLink(link);
      }
    });
  });

  QObject::connect(combo, &QComboBox::currentIndexChanged,
                   [&](int idx) { strip->setPipeline(pipelines[idx]); });

  QObject::connect(sortCombo, &QComboBox::currentIndexChanged,
                   [&](int idx) {
                     strip->setSortOrder(static_cast<SortOrder>(idx));
                   });

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
