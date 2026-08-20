/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "AnimationHelperDialog.h"
#include "ui_AnimationHelperDialog.h"

#include "ActiveObjects.h"
#include "CameraAnimation.h"
#include "CameraViewpoints.h"
#include "ClipAnimation.h"
#include "ContourAnimation.h"
#include "ModuleAnimations.h"
#include "MovieExportDialog.h"
#include "OpacityAnimation.h"
#include "SliceAnimation.h"
#include "Utilities.h"

#include "pipeline/Pipeline.h"
#include "pipeline/SourceNode.h"
#include "pipeline/sinks/ClipSink.h"
#include "pipeline/sinks/ContourSink.h"
#include "pipeline/sinks/SliceSink.h"

#include <pqAnimationCue.h>
#include <pqImageUtil.h>
#include <pqAnimationManager.h>
#include <pqAnimationScene.h>
#include <pqApplicationCore.h>
#include <pqPVApplicationCore.h>
#include <pqPropertyLinks.h>
#include <pqRenderView.h>
#include <pqSMAdaptor.h>
#include <pqServerManagerModel.h>

#include <vtkCamera.h>
#include <vtkImageData.h>
#include <vtkRenderer.h>
#include <vtkSMProxy.h>
#include <vtkSMRenderViewProxy.h>
#include <vtkSmartPointer.h>
#include <vtkWeakPointer.h>

#include <QBuffer>
#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>

namespace tomviz {

namespace {

pipeline::SourceNode* findUpstreamSource(pipeline::Node* node)
{
  if (!node) {
    return nullptr;
  }
  if (auto* src = qobject_cast<pipeline::SourceNode*>(node)) {
    return src;
  }
  for (auto* up : node->upstreamNodes()) {
    if (auto* src = findUpstreamSource(up)) {
      return src;
    }
  }
  return nullptr;
}

// Big enough to tell two framings of the same volume apart, small
// enough that a dozen of them in a state file is not worth noticing.
const QSize thumbnailSize(128, 96);

// A PNG of what the view currently shows, or empty if it cannot be read.
QByteArray captureThumbnail(pqRenderView* renderView)
{
  auto* proxy = renderView ? renderView->getRenderViewProxy() : nullptr;
  if (!proxy) {
    return {};
  }

  // CaptureWindow hands back a reference that is ours to release.
  vtkSmartPointer<vtkImageData> image;
  image.TakeReference(proxy->CaptureWindow(1));
  if (!image) {
    return {};
  }

  QImage captured;
  if (!pqImageUtil::fromImageData(image, captured) || captured.isNull()) {
    return {};
  }

  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  captured.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
    .save(&buffer, "PNG");
  return png;
}

} // anonymous namespace

class AnimationHelperDialog::Internal : public QObject
{
public:
  Ui::AnimationHelperDialog ui;
  pqPropertyLinks pqLinks;
  QPointer<AnimationHelperDialog> parent;
  QPointer<CameraAnimation> cameraAnimation;
  vtkWeakPointer<vtkSMProxy> linkedScene;
  // Which unit the Clip tab is currently showing. The clip has slices
  // only while it is axis aligned, and the direction can change under us.
  bool clipTabIsOrtho = true;

  Internal(AnimationHelperDialog* p) : QObject(p), parent(p)
  {
    ui.setupUi(p);

    ui.modulesTabWidget->tabBar()->hide();

    ui.viewpointList->setViewMode(QListView::IconMode);
    ui.viewpointList->setIconSize(thumbnailSize);
    ui.viewpointList->setGridSize(thumbnailSize + QSize(12, 22));
    ui.viewpointList->setResizeMode(QListView::Adjust);
    ui.viewpointList->setMovement(QListView::Static);
    ui.viewpointList->setWordWrap(false);

    updateGui();
    setupConnections();
  }

  void setupConnections()
  {
    // Camera animations
    connect(ui.clearCameraAnimations, &QPushButton::clicked, this,
            &Internal::clearCameraAnimations);
    connect(ui.createCameraOrbit, &QPushButton::clicked, this,
            &Internal::createCameraOrbitInternal);

    // Camera viewpoints
    connect(ui.addViewpoint, &QPushButton::clicked, this,
            &Internal::addViewpoint);
    connect(ui.updateViewpoint, &QPushButton::clicked, this,
            &Internal::updateViewpoint);
    connect(ui.goToViewpoint, &QPushButton::clicked, this,
            &Internal::goToViewpoint);
    connect(ui.removeViewpoint, &QPushButton::clicked, this,
            &Internal::removeViewpoint);
    connect(ui.moveViewpointUp, &QPushButton::clicked, this,
            [this]() { moveViewpoint(-1); });
    connect(ui.moveViewpointDown, &QPushButton::clicked, this,
            [this]() { moveViewpoint(1); });
    connect(ui.viewpointList, &QListWidget::currentRowChanged, this,
            [this]() {
              updateSegmentControls();
              updateEnableStates();
            });
    connect(ui.viewpointList, &QListWidget::itemDoubleClicked, this,
            &Internal::goToViewpoint);
    connect(ui.segmentDuration,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &Internal::segmentChanged);
    connect(ui.segmentEased, &QCheckBox::toggled, this,
            &Internal::segmentChanged);
    connect(ui.animateViewpoints, &QPushButton::clicked, this,
            &Internal::animateViewpointsInternal);
    // The list is shared with the state file, so it can change while the
    // dialog is open.
    connect(&CameraViewpoints::instance(), &CameraViewpoints::changed, this,
            &Internal::refreshViewpoints);
    // Visualization animations are shared the same way, and a state file
    // can bring in a whole set of them at once.
    connect(&ModuleAnimations::instance(), &ModuleAnimations::changed, this,
            &Internal::updateEnableStates);

    // Time series
    connect(&activeObjects(),
            &ActiveObjects::timeSeriesAnimationsEnableStateChanged,
            ui.enableTimeSeriesAnimations, &QCheckBox::setChecked);
    connect(ui.enableTimeSeriesAnimations, &QCheckBox::toggled,
            &activeObjects(), &ActiveObjects::enableTimeSeriesAnimations);
    connect(ui.enableTimeSeriesAnimations, &QCheckBox::toggled, this,
            [this](bool b) {
              updateEnableStates();
              if (b) {
                play();
              }
            });

    // Pipeline node changes
    if (auto* pip = pipeline()) {
      connect(pip, &pipeline::Pipeline::nodeAdded, this,
              &Internal::onNodeAdded);
      connect(pip, &pipeline::Pipeline::nodeRemoved, this,
              &Internal::onNodeRemoved);
      // Sinks re-cache their scalar range / slice count on every
      // consume, but the tabs only re-read them on selection changes.
      // Track executions so the ranges follow the data.
      connect(pip, &pipeline::Pipeline::executionFinished, this,
              &Internal::refreshModuleRanges);
    }

    // Sink selection
    connect(ui.selectedDataSource,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &Internal::selectedDataSourceChanged);
    connect(ui.selectedModule,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &Internal::selectedModuleChanged);
    connect(ui.animateOpacity, &QCheckBox::toggled, this, [this](bool on) {
      ui.opacityStart->setEnabled(on);
      ui.opacityStop->setEnabled(on);
    });
    connect(ui.addModuleAnimation, &QPushButton::clicked, this,
            &Internal::addModuleAnimation);
    connect(ui.clearModuleAnimations, &QPushButton::clicked, this,
            &Internal::clearModuleAnimations);

    // All animations
    linkToScene();
    // React to user edits of the frame count only. Connecting to the spin
    // box's valueChanged directly would also fire when the property link
    // syncs the widget from the scene (e.g. after a relink, or when a time
    // series load sets the frame count), and numberOfFramesModified would
    // then clobber a freshly set "Snap To TimeSteps" play mode.
    // qtWidgetChanged is only emitted for Qt-originated changes.
    connect(&pqLinks, &pqPropertyLinks::qtWidgetChanged, this,
            &Internal::numberOfFramesModified);
    // Loading a state file resets the ParaView session and replaces the
    // animation scene; rebind the frame count link when that happens while
    // the dialog is open (showEvent covers the closed case).
    connect(pqPVApplicationCore::instance()->animationManager(),
            &pqAnimationManager::activeSceneChanged, this,
            &Internal::linkToScene);
    connect(ui.exportMovie, &QPushButton::clicked, this,
            &Internal::exportMovie);
    connect(ui.clearAllAnimations, &QPushButton::clicked, this,
            &Internal::clearAllAnimations);
  }

  // Bind the frame count spin box to the current animation scene. Loading
  // a state file resets the ParaView session, which destroys the scene and
  // creates a new one, so the link built when the dialog was first opened
  // can be pointing at a dead proxy. The dialog is created once and only
  // hidden on close, so re-check the scene every time it is shown.
  void linkToScene()
  {
    auto* sceneProxy = scene() ? scene()->getProxy() : nullptr;
    if (!sceneProxy || sceneProxy == linkedScene) {
      return;
    }

    pqLinks.removeAllPropertyLinks();
    pqLinks.addPropertyLink(ui.numberOfFrames, "value",
                            SIGNAL(valueChanged(int)), sceneProxy,
                            sceneProxy->GetProperty("NumberOfFrames"), 0);
    linkedScene = sceneProxy;
  }

  void refresh()
  {
    linkToScene();
    updateGui();
    refreshViewpoints();
    refreshModuleRanges();
  }

  // Re-read the selected module's data-dependent ranges after the
  // pipeline produces new data. Only rebuild a tab when its range
  // actually changed, so user-entered start/stop values survive
  // executions that don't affect this module.
  void refreshModuleRanges()
  {
    auto* node = selectedSink();
    if (auto* contour = qobject_cast<pipeline::ContourSink*>(node)) {
      double range[2];
      contour->scalarRange(range);
      if (range[0] != ui.contourStart->minimum() ||
          range[1] != ui.contourStart->maximum()) {
        setupContourTab(contour);
      }
    } else if (auto* slice = qobject_cast<pipeline::SliceSink*>(node)) {
      if (slice->maxSlice() != ui.sliceStop->maximum()) {
        setupSliceTab(slice);
      }
    } else if (auto* clip = qobject_cast<pipeline::ClipSink*>(node)) {
      // A clip that changed orientation is measured in a different unit
      // entirely, so the tab has to be rebuilt for that too.
      bool ortho = clip->isOrtho();
      if (ortho != clipTabIsOrtho ||
          (ortho && clip->maxSlice() != ui.clipStop->maximum())) {
        setupClipTab(clip);
      }
    }
  }

  void play() { scene()->getProxy()->InvokeCommand("Play"); }

  // The active view isn't necessarily a render view: a Plot module makes
  // an XYChartView active. Fall back to the first render view so the orbit
  // still ends up somewhere the user can see it.
  pqRenderView* renderViewForOrbit()
  {
    if (auto* renderView = activeObjects().activePqRenderView()) {
      return renderView;
    }

    auto* smModel = pqApplicationCore::instance()->getServerManagerModel();
    auto renderViews = smModel->findItems<pqRenderView*>();
    return renderViews.isEmpty() ? nullptr : renderViews.first();
  }

  void updateGui()
  {
    ui.enableTimeSeriesAnimations->setChecked(
      activeObjects().timeSeriesAnimationsEnabled());

    updateDataSourceOptions();
    updateEnableStates();
  }

  void updateEnableStates()
  {
    // The viewpoint list can change while a state file is being loaded,
    // which is exactly when the session has no scene to ask.
    bool hasCameraCues = false;
    if (auto* animationScene = scene()) {
      for (auto* cue : animationScene->getCues()) {
        if (cue->getSMName().startsWith("CameraAnimationCue")) {
          hasCameraCues = true;
          break;
        }
      }
    }

    bool hasCameraAnimations = hasCameraCues || !cameraAnimation.isNull();

    bool hasTimeSeries = false;
    auto* tk = activeObjects().activeTimeKeeper();
    if (tk && !tk->getTimeSteps().empty()) {
      hasTimeSeries = true;
    }

    bool timeSeriesEnabled =
      ui.enableTimeSeriesAnimations->isChecked() && hasTimeSeries;

    bool hasDataSourceOptions = ui.selectedDataSource->count() != 0;
    bool hasModuleOptions = ui.selectedModule->count() != 0;
    bool moduleSelected = selectedSink() != nullptr;
    bool hasModuleAnimations = !ModuleAnimations::instance().isEmpty();

    bool hasAnyAnimations =
      hasCameraAnimations || timeSeriesEnabled || hasModuleAnimations;

    int viewpointCount = CameraViewpoints::instance().size();
    int selectedViewpoint = ui.viewpointList->currentRow();
    bool viewpointSelected = selectedViewpoint >= 0;

    ui.clearCameraAnimations->setEnabled(hasCameraAnimations);
    ui.addViewpoint->setEnabled(renderViewForOrbit() != nullptr);
    ui.updateViewpoint->setEnabled(viewpointSelected);
    ui.goToViewpoint->setEnabled(viewpointSelected);
    ui.removeViewpoint->setEnabled(viewpointSelected);
    ui.moveViewpointUp->setEnabled(viewpointSelected && selectedViewpoint > 0);
    ui.moveViewpointDown->setEnabled(viewpointSelected &&
                                     selectedViewpoint < viewpointCount - 1);
    // One viewpoint is a camera position, not a path.
    ui.animateViewpoints->setEnabled(viewpointCount >= 2);
    ui.enableTimeSeriesAnimations->setEnabled(hasTimeSeries);
    ui.addModuleAnimation->setEnabled(moduleSelected);
    ui.selectedDataSource->setEnabled(hasDataSourceOptions);
    ui.selectedModule->setEnabled(hasModuleOptions);
    ui.clearModuleAnimations->setEnabled(hasModuleAnimations);
    ui.exportMovie->setEnabled(hasAnyAnimations);
    ui.clearAllAnimations->setEnabled(hasAnyAnimations);
  }

  // Camera
  void clearCameraAnimations()
  {
    clearCameraCues();
    deleteCameraAnimation();
    // The saved viewpoints are authoring input rather than an animation,
    // so they survive this: clearing the path should not throw away the
    // camera positions the user framed to build it.
    updateEnableStates();
  }

  void deleteCameraAnimation()
  {
    if (cameraAnimation) {
      cameraAnimation->deleteLater();
      cameraAnimation = nullptr;
    }
  }

  void createCameraOrbitInternal()
  {
    auto* renderView = renderViewForOrbit();
    if (!renderView) {
      return;
    }

    clearCameraCues(renderView->getRenderViewProxy());
    // The orbit and the viewpoint path both drive the camera every tick.
    deleteCameraAnimation();
    createCameraOrbit(renderView->getRenderViewProxy());

    updateEnableStates();
    play();
  }

  // Camera viewpoints
  void refreshViewpoints()
  {
    auto& viewpoints = CameraViewpoints::instance();

    QSignalBlocker blocked(ui.viewpointList);
    int previousRow = ui.viewpointList->currentRow();
    ui.viewpointList->clear();
    for (int i = 0; i < viewpoints.size(); ++i) {
      auto* item = new QListWidgetItem(QString("Viewpoint %1").arg(i + 1));
      QPixmap thumbnail;
      if (thumbnail.loadFromData(viewpoints.at(i).thumbnail, "PNG")) {
        item->setIcon(QIcon(thumbnail));
      }
      ui.viewpointList->addItem(item);
    }
    if (previousRow >= 0 && previousRow < viewpoints.size()) {
      ui.viewpointList->setCurrentRow(previousRow);
    }

    updateSegmentControls();
    updateEnableStates();
  }

  // The duration and easing belong to the leg leaving the selected
  // viewpoint, so the last one in the list has nothing to edit.
  void updateSegmentControls()
  {
    auto& viewpoints = CameraViewpoints::instance();
    int row = ui.viewpointList->currentRow();
    bool hasSegment = row >= 0 && row < viewpoints.size() - 1;

    QSignalBlocker blockedDuration(ui.segmentDuration);
    QSignalBlocker blockedEased(ui.segmentEased);
    if (hasSegment) {
      ui.segmentDuration->setValue(viewpoints.at(row).duration);
      ui.segmentEased->setChecked(viewpoints.at(row).eased);
    }

    ui.segmentDurationLabel->setEnabled(hasSegment);
    ui.segmentDuration->setEnabled(hasSegment);
    ui.segmentEased->setEnabled(hasSegment);
  }

  void addViewpoint()
  {
    auto* renderView = renderViewForOrbit();
    auto* proxy = renderView ? renderView->getRenderViewProxy() : nullptr;
    auto* camera = proxy ? proxy->GetActiveCamera() : nullptr;
    if (!camera) {
      return;
    }

    Viewpoint viewpoint;
    viewpoint.readFrom(camera);
    viewpoint.thumbnail = captureThumbnail(renderView);

    auto& viewpoints = CameraViewpoints::instance();
    viewpoints.append(viewpoint);
    ui.viewpointList->setCurrentRow(viewpoints.size() - 1);
  }

  void updateViewpoint()
  {
    auto& viewpoints = CameraViewpoints::instance();
    int row = ui.viewpointList->currentRow();
    auto* renderView = renderViewForOrbit();
    auto* proxy = renderView ? renderView->getRenderViewProxy() : nullptr;
    auto* camera = proxy ? proxy->GetActiveCamera() : nullptr;
    if (row < 0 || row >= viewpoints.size() || !camera) {
      return;
    }

    // Re-frame the viewpoint but leave its place in the path alone.
    auto viewpoint = viewpoints.at(row);
    viewpoint.readFrom(camera);
    viewpoint.thumbnail = captureThumbnail(renderView);
    viewpoints.replace(row, viewpoint);
  }

  void goToViewpoint()
  {
    auto& viewpoints = CameraViewpoints::instance();
    int row = ui.viewpointList->currentRow();
    auto* renderView = renderViewForOrbit();
    auto* proxy = renderView ? renderView->getRenderViewProxy() : nullptr;
    auto* camera = proxy ? proxy->GetActiveCamera() : nullptr;
    if (row < 0 || row >= viewpoints.size() || !camera) {
      return;
    }

    viewpoints.at(row).applyTo(camera);
    if (auto* renderer = proxy->GetRenderer()) {
      renderer->ResetCameraClippingRange();
    }
    renderView->render();
  }

  void removeViewpoint()
  {
    CameraViewpoints::instance().removeAt(ui.viewpointList->currentRow());
  }

  void moveViewpoint(int offset)
  {
    auto& viewpoints = CameraViewpoints::instance();
    int row = ui.viewpointList->currentRow();
    int target = row + offset;
    if (row < 0 || target < 0 || target >= viewpoints.size()) {
      return;
    }

    viewpoints.move(row, target);
    ui.viewpointList->setCurrentRow(target);
  }

  void segmentChanged()
  {
    auto& viewpoints = CameraViewpoints::instance();
    int row = ui.viewpointList->currentRow();
    if (row < 0 || row >= viewpoints.size()) {
      return;
    }

    auto viewpoint = viewpoints.at(row);
    viewpoint.duration = ui.segmentDuration->value();
    viewpoint.eased = ui.segmentEased->isChecked();
    viewpoints.replace(row, viewpoint);
  }

  void animateViewpointsInternal()
  {
    auto* renderView = renderViewForOrbit();
    if (!renderView || CameraViewpoints::instance().size() < 2) {
      return;
    }

    // Only one animation can own the camera.
    clearCameraCues(renderView->getRenderViewProxy());
    deleteCameraAnimation();
    cameraAnimation = new CameraAnimation(renderView);

    updateEnableStates();
    play();
  }

  QStringList moduleTabTexts()
  {
    QStringList types;
    for (int i = 0; i < ui.modulesTabWidget->count(); ++i) {
      types.append(ui.modulesTabWidget->tabText(i));
    }
    return types;
  }

  // Data sources (pipeline source nodes)
  void updateDataSourceOptions()
  {
    QSignalBlocker blocked(ui.selectedDataSource);
    auto* previouslySelected = selectedSource();
    int previouslySelectedIndex = -1;

    ui.selectedDataSource->clear();

    auto* pip = pipeline();
    if (!pip) {
      updateEnableStates();
      return;
    }

    QStringList usedLabels;
    int idx = 0;
    for (auto* node : pip->nodes()) {
      auto* source = qobject_cast<pipeline::SourceNode*>(node);
      if (!source) {
        continue;
      }

      auto label = source->label();
      if (label.isEmpty()) {
        label = "Source";
      }

      auto uniqueLabel = label;
      int n = 1;
      while (usedLabels.contains(uniqueLabel)) {
        uniqueLabel = label + " " + QString::number(++n);
      }
      usedLabels.append(uniqueLabel);

      ui.selectedDataSource->addItem(
        uniqueLabel, QVariant::fromValue(static_cast<QObject*>(source)));

      if (source == previouslySelected) {
        previouslySelectedIndex = idx;
      }
      ++idx;
    }

    if (previouslySelectedIndex != -1) {
      ui.selectedDataSource->setCurrentIndex(previouslySelectedIndex);
    } else {
      selectedDataSourceChanged();
    }

    updateEnableStates();
  }

  pipeline::SourceNode* selectedSource()
  {
    if (ui.selectedDataSource->count() == 0) {
      return nullptr;
    }

    return qobject_cast<pipeline::SourceNode*>(
      ui.selectedDataSource->currentData().value<QObject*>());
  }

  // Sinks (animatable modules)
  void updateModuleOptions()
  {
    QSignalBlocker blocked(ui.selectedModule);
    auto* previouslySelected = selectedSink();
    int previouslySelectedIndex = -1;

    ui.selectedModule->clear();

    auto* source = selectedSource();
    auto* pip = pipeline();
    if (!source || !pip) {
      updateEnableStates();
      return;
    }

    QList<pipeline::Node*> sinks;
    for (auto* node : pip->nodes()) {
      if (qobject_cast<pipeline::ContourSink*>(node) ||
          qobject_cast<pipeline::SliceSink*>(node) ||
          qobject_cast<pipeline::ClipSink*>(node)) {
        if (findUpstreamSource(node) == source) {
          sinks.append(node);
        }
      }
    }

    QStringList labels;
    for (auto* sink : sinks) {
      auto label = sink->label();
      if (label.isEmpty()) {
        if (qobject_cast<pipeline::ContourSink*>(sink)) {
          label = "Contour";
        } else if (qobject_cast<pipeline::SliceSink*>(sink)) {
          label = "Slice";
        } else if (qobject_cast<pipeline::ClipSink*>(sink)) {
          label = "Clip";
        }
      }

      auto uniqueLabel = label;
      int n = 1;
      while (labels.contains(uniqueLabel)) {
        uniqueLabel = label + " " + QString::number(++n);
      }
      labels.append(uniqueLabel);
    }

    for (int i = 0; i < sinks.size(); ++i) {
      ui.selectedModule->addItem(
        labels[i], QVariant::fromValue(static_cast<QObject*>(sinks[i])));

      if (sinks[i] == previouslySelected) {
        previouslySelectedIndex = i;
      }
    }

    if (previouslySelectedIndex != -1) {
      ui.selectedModule->setCurrentIndex(previouslySelectedIndex);
    } else {
      selectedModuleChanged();
    }

    updateEnableStates();
  }

  pipeline::Node* selectedSink()
  {
    if (ui.selectedModule->count() == 0) {
      return nullptr;
    }

    return qobject_cast<pipeline::Node*>(
      ui.selectedModule->currentData().value<QObject*>());
  }

  void selectedDataSourceChanged() { updateModuleOptions(); }

  void selectedModuleChanged()
  {
    auto* node = selectedSink();
    int tabIndex = 0;

    if (auto* contour = qobject_cast<pipeline::ContourSink*>(node)) {
      tabIndex = moduleTabTexts().indexOf("Contour");
      if (tabIndex < 0) {
        tabIndex = 0;
      }
      ui.modulesTabWidget->setCurrentIndex(tabIndex);
      setupContourTab(contour);
    } else if (auto* slice = qobject_cast<pipeline::SliceSink*>(node)) {
      tabIndex = moduleTabTexts().indexOf("Slice");
      if (tabIndex < 0) {
        tabIndex = 0;
      }
      ui.modulesTabWidget->setCurrentIndex(tabIndex);
      setupSliceTab(slice);
    } else if (auto* clip = qobject_cast<pipeline::ClipSink*>(node)) {
      tabIndex = moduleTabTexts().indexOf("Clip");
      if (tabIndex < 0) {
        tabIndex = 0;
      }
      ui.modulesTabWidget->setCurrentIndex(tabIndex);
      setupClipTab(clip);
    } else {
      ui.modulesTabWidget->setCurrentIndex(0);
    }

    setupOpacityRow(node);
    updateEnableStates();
  }

  void onNodeAdded(pipeline::Node*)
  {
    QTimer::singleShot(0, this, [this]() {
      updateDataSourceOptions();
      updateModuleOptions();
    });
  }

  void onNodeRemoved(pipeline::Node*)
  {
    updateDataSourceOptions();
    updateModuleOptions();
    updateEnableStates();
  }

  void setupContourTab(pipeline::ContourSink* sink)
  {
    double range[2];
    sink->scalarRange(range);

    ui.contourStart->setMinimum(range[0]);
    ui.contourStart->setMaximum(range[1]);
    ui.contourStop->setMinimum(range[0]);
    ui.contourStop->setMaximum(range[1]);

    ui.contourStart->setValue((range[1] - range[0]) / 3 + range[0]);
    ui.contourStop->setValue((range[1] - range[0]) * 2 / 3 + range[0]);
  }

  void setupSliceTab(pipeline::SliceSink* sink)
  {
    double max = sink->maxSlice();

    ui.sliceStart->setMinimum(0);
    ui.sliceStart->setMaximum(max);
    ui.sliceStop->setMinimum(0);
    ui.sliceStop->setMaximum(max);

    ui.sliceStart->setValue(0);
    ui.sliceStop->setValue(max);

    // This runs on every reselection (and now on direction changes and
    // data updates), so drop the previous connection first or they
    // accumulate, each firing another setupSliceTab.
    disconnect(sink, &pipeline::SliceSink::directionChanged, this, nullptr);
    connect(sink, &pipeline::SliceSink::directionChanged, this,
            [this, sink]() {
              if (sink != this->selectedSink()) {
                disconnect(sink, nullptr, this, nullptr);
                return;
              }
              this->setupSliceTab(sink);
            });
  }

  void setupClipTab(pipeline::ClipSink* sink)
  {
    clipTabIsOrtho = sink->isOrtho();

    if (clipTabIsOrtho) {
      double max = sink->maxSlice();

      ui.clipRangeLabel->setText("Slice:");
      ui.clipStart->setDecimals(0);
      ui.clipStop->setDecimals(0);
      ui.clipStart->setRange(0, max);
      ui.clipStop->setRange(0, max);
      ui.clipStart->setValue(0);
      ui.clipStop->setValue(max);
      return;
    }

    // A plane at an arbitrary angle has no slices to count, so it is
    // positioned by how far it sits from the centre of the data along
    // its own normal.
    double minDistance = 0;
    double maxDistance = 0;
    sink->planeDistanceRange(minDistance, maxDistance);

    ui.clipRangeLabel->setText("Position:");
    ui.clipStart->setDecimals(2);
    ui.clipStop->setDecimals(2);
    ui.clipStart->setRange(minDistance, maxDistance);
    ui.clipStop->setRange(minDistance, maxDistance);
    ui.clipStart->setValue(minDistance);
    ui.clipStop->setValue(maxDistance);
  }

  void setupOpacityRow(pipeline::Node* node)
  {
    bool supported = OpacityAnimation::supports(node);

    if (!supported) {
      QSignalBlocker blocked(ui.animateOpacity);
      ui.animateOpacity->setChecked(false);
    } else {
      // Fading out from wherever the module sits now is the useful
      // default; the user can invert it by swapping the two values.
      QSignalBlocker blockedStart(ui.opacityStart);
      QSignalBlocker blockedStop(ui.opacityStop);
      ui.opacityStart->setValue(OpacityAnimation::opacityOf(node));
      ui.opacityStop->setValue(0.0);
    }

    bool checked = supported && ui.animateOpacity->isChecked();
    ui.animateOpacity->setEnabled(supported);
    ui.opacityStart->setEnabled(checked);
    ui.opacityStop->setEnabled(checked);
  }

  void addModuleAnimation()
  {
    auto* node = selectedSink();
    if (!node) {
      return;
    }

    // A module carries at most one animation of each kind, so replace
    // whatever it already had.
    ModuleAnimations::instance().removeForNode(node);

    if (qobject_cast<pipeline::ContourSink*>(node)) {
      addContourAnimation();
    } else if (qobject_cast<pipeline::SliceSink*>(node)) {
      addSliceAnimation();
    } else if (qobject_cast<pipeline::ClipSink*>(node)) {
      addClipAnimation();
    }

    // Opacity rides along with whatever the module's own animation is,
    // so a module can move and fade at the same time.
    if (ui.animateOpacity->isChecked() && OpacityAnimation::supports(node)) {
      ModuleAnimations::instance().add(new OpacityAnimation(
        node, ui.opacityStart->value(), ui.opacityStop->value()));
    }

    updateEnableStates();
    play();
  }

  void addContourAnimation()
  {
    auto start = ui.contourStart->value();
    auto stop = ui.contourStop->value();
    auto* sink = qobject_cast<pipeline::ContourSink*>(selectedSink());
    ModuleAnimations::instance().add(new ContourAnimation(sink, start, stop));
  }

  void addSliceAnimation()
  {
    auto start = ui.sliceStart->value();
    auto stop = ui.sliceStop->value();
    auto* sink = qobject_cast<pipeline::SliceSink*>(selectedSink());
    ModuleAnimations::instance().add(new SliceAnimation(sink, start, stop));
  }

  void addClipAnimation()
  {
    auto start = ui.clipStart->value();
    auto stop = ui.clipStop->value();
    auto* sink = qobject_cast<pipeline::ClipSink*>(selectedSink());
    auto unit =
      sink->isOrtho() ? ClipAnimation::Slice : ClipAnimation::Distance;
    ModuleAnimations::instance().add(new ClipAnimation(sink, start, stop, unit));
  }

  void clearModuleAnimations()
  {
    ModuleAnimations::instance().clear();
    updateEnableStates();
  }

  // All animations
  void numberOfFramesModified()
  {
    pqSMAdaptor::setEnumerationProperty(
      scene()->getProxy()->GetProperty("PlayMode"), "Sequence");
    // qtWidgetChanged is emitted after the property link has copied and
    // flushed the frame count, so the play mode needs its own push to
    // reach the animation player.
    scene()->getProxy()->UpdateVTKObjects();
  }

  void exportMovie()
  {
    MovieExportDialog dialog(parent);
    dialog.exec();
  }

  void clearAllAnimations()
  {
    clearCameraAnimations();
    if (ui.enableTimeSeriesAnimations->isEnabled()) {
      ui.enableTimeSeriesAnimations->setChecked(false);
    }
    clearModuleAnimations();

    updateEnableStates();
  }

  ActiveObjects& activeObjects() { return ActiveObjects::instance(); }

  pipeline::Pipeline* pipeline()
  {
    return activeObjects().pipeline();
  }

  pqAnimationScene* scene()
  {
    return pqPVApplicationCore::instance()
      ->animationManager()
      ->getActiveScene();
  }
}; // end class Internal

AnimationHelperDialog::AnimationHelperDialog(QWidget* parent)
  : QDialog(parent), m_internal(new Internal(this))
{
  // Float above the main window so the dialog does not slip behind it on
  // macOS.
  floatAboveMainWindow(this);
}

AnimationHelperDialog::~AnimationHelperDialog() = default;

void AnimationHelperDialog::showEvent(QShowEvent* e)
{
  QDialog::showEvent(e);
  m_internal->refresh();
}

} // namespace tomviz
