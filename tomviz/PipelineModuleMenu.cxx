/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PipelineModuleMenu.h"

#include "ActiveObjects.h"
#include "MainWindow.h"

#include "pipeline/Pipeline.h"
#include "pipeline/OutputPort.h"
#include "pipeline/InputPort.h"
#include "pipeline/sinks/LegacyModuleSink.h"
#include "pipeline/sinks/VolumeSink.h"
#include "pipeline/sinks/SliceSink.h"
#include "pipeline/sinks/ContourSink.h"
#include "pipeline/sinks/OutlineSink.h"
#include "pipeline/sinks/ThresholdSink.h"
#include "pipeline/sinks/ClipSink.h"
#include "pipeline/sinks/RulerSink.h"
#include "pipeline/sinks/ScaleCubeSink.h"
#include "pipeline/sinks/SegmentSink.h"
#include "pipeline/sinks/MoleculeSink.h"
#include "pipeline/sinks/PlotSink.h"

#include <pqApplicationCore.h>
#include <pqObjectBuilder.h>
#include <pqServer.h>
#include <pqServerManagerModel.h>
#include <pqView.h>
#include <vtkSMRenderViewProxy.h>
#include <vtkSMContextViewProxy.h>
#include <vtkSMViewLayoutProxy.h>
#include <vtkSMViewProxy.h>

#include <QApplication>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QToolBar>

namespace tomviz {

static vtkSMViewProxy* resolveViewForSink(const QString& sinkType)
{
  bool needsChart = (sinkType == "Plot");
  QString viewTypeName = needsChart ? "XYChartView" : "RenderView";

  auto* activeView = ActiveObjects::instance().activeView();
  if (activeView) {
    bool activeIsChart = vtkSMContextViewProxy::SafeDownCast(activeView);
    bool activeIsRender = vtkSMRenderViewProxy::SafeDownCast(activeView);
    if ((needsChart && activeIsChart) || (!needsChart && activeIsRender)) {
      return activeView;
    }
  }

  auto* smModel = pqApplicationCore::instance()->getServerManagerModel();
  QList<pqView*> allViews = smModel->findItems<pqView*>();

  QList<pqView*> matching;
  for (auto* v : allViews) {
    auto* proxy = v->getViewProxy();
    bool isChart = vtkSMContextViewProxy::SafeDownCast(proxy);
    bool isRender = vtkSMRenderViewProxy::SafeDownCast(proxy);
    if ((needsChart && isChart) || (!needsChart && isRender)) {
      matching.append(v);
    }
  }

  if (matching.isEmpty()) {
    ActiveObjects::instance().createRenderViewIfNeeded();
    return ActiveObjects::instance().activeView();
  }

  if (matching.size() == 1) {
    auto* proxy = matching.first()->getViewProxy();
    ActiveObjects::instance().setActiveView(proxy);
    return proxy;
  }

  QStringList names;
  for (auto* v : matching) {
    names << v->getSMName();
  }
  bool ok = false;
  QString chosen = QInputDialog::getItem(
    nullptr, "Select View",
    QString("Multiple views available. Select one:"),
    names, 0, false, &ok);
  if (!ok) {
    return nullptr;
  }
  int idx = names.indexOf(chosen);
  auto* proxy = matching[idx]->getViewProxy();
  ActiveObjects::instance().setActiveView(proxy);
  return proxy;
}

PipelineModuleMenu::PipelineModuleMenu(QToolBar* toolBar, QMenu* menu,
                                       QObject* parentObject)
  : QObject(parentObject), m_menu(menu), m_toolBar(toolBar)
{
  Q_ASSERT(menu);
  Q_ASSERT(toolBar);
  connect(menu, &QMenu::triggered, this, &PipelineModuleMenu::triggered);
  updateActions();
}

PipelineModuleMenu::~PipelineModuleMenu() = default;

QList<QString> PipelineModuleMenu::sinkTypes()
{
  return { "Volume", "Outline", "Slice", "Contour", "Threshold", "Clip",
           "Segment", "Ruler", "Scale Cube", "Molecule", "Plot" };
}

QIcon PipelineModuleMenu::sinkIcon(const QString& type)
{
  // Icon paths must match the legacy Module::icon() implementations
  static QMap<QString, QString> iconMap = {
    { "Volume", ":/icons/pqVolumeData.png" },
    { "Outline", ":/pqWidgets/Icons/pqProbeLocation.svg" },
    { "Slice", ":/icons/orthoslice.svg" },
    { "Contour", ":pqWidgets/Icons/pqIsosurface.svg" },
    { "Threshold", ":/pqWidgets/Icons/pqThreshold.svg" },
    { "Clip", ":/pqWidgets/Icons/pqClip.svg" },
    { "Segment", ":/pqWidgets/Icons/pqCalculator.svg" },
    { "Ruler", ":/pqWidgets/Icons/pqRuler.svg" },
    { "Scale Cube", ":/icons/pqMeasurementCube.png" },
    { "Molecule", ":/pqWidgets/Icons/pqGroup.svg" },
    { "Plot", ":/pqWidgets/Icons/pqLineChart16.png" },
  };
  auto it = iconMap.find(type);
  return (it != iconMap.end()) ? QIcon(it.value()) : QIcon();
}

pipeline::LegacyModuleSink* PipelineModuleMenu::createSink(const QString& type)
{
  if (type == "Volume") {
    return new pipeline::VolumeSink();
  } else if (type == "Outline") {
    return new pipeline::OutlineSink();
  } else if (type == "Slice") {
    return new pipeline::SliceSink();
  } else if (type == "Contour") {
    return new pipeline::ContourSink();
  } else if (type == "Threshold") {
    return new pipeline::ThresholdSink();
  } else if (type == "Clip") {
    return new pipeline::ClipSink();
  } else if (type == "Segment") {
    return new pipeline::SegmentSink();
  } else if (type == "Ruler") {
    return new pipeline::RulerSink();
  } else if (type == "Scale Cube") {
    return new pipeline::ScaleCubeSink();
  } else if (type == "Molecule") {
    return new pipeline::MoleculeSink();
  } else if (type == "Plot") {
    return new pipeline::PlotSink();
  }
  return nullptr;
}

void PipelineModuleMenu::updateActions()
{
  QMenu* menu = m_menu;
  QToolBar* toolBar = m_toolBar;
  Q_ASSERT(menu);
  Q_ASSERT(toolBar);

  menu->clear();
  toolBar->clear();

  for (const QString& type : sinkTypes()) {
    auto actn = menu->addAction(sinkIcon(type), type);
    actn->setData(type);
    toolBar->addAction(actn);
  }
}

void PipelineModuleMenu::triggered(QAction* maction)
{
  auto type = maction->data().toString();
  if (type.isEmpty()) {
    return;
  }

  auto* mainWindow = qobject_cast<MainWindow*>(QApplication::activeWindow());
  auto* pip = mainWindow ? mainWindow->pipeline() : nullptr;
  if (!pip) {
    qCritical("No active pipeline. Load data first.");
    return;
  }

  auto* view = resolveViewForSink(type);
  if (!view) {
    return;
  }

  auto* targetPort = ActiveObjects::instance().activeTipOutputPort();
  if (!targetPort) {
    qCritical("No output port available. Load data first.");
    return;
  }

  auto* sink = createSink(type);
  if (!sink) {
    qCritical("Failed to create sink for type: %s", qPrintable(type));
    return;
  }

  sink->setLabel(type);
  sink->initialize(view);
  pip->addNode(sink);

  // Ctrl held: add the node unconnected (user will link manually)
  if (!(QApplication::keyboardModifiers() & Qt::ControlModifier)) {
    auto* input = sink->inputPorts()[0];
    if (!pipeline::isPortTypeCompatible(targetPort->type(),
                                        input->acceptedTypes())) {
      qCritical("Incompatible port types: sink input does not accept "
                "the tip output port type.");
      return;
    }
    pip->createLink(targetPort, input);
  }
  pip->execute();
}

} // namespace tomviz
