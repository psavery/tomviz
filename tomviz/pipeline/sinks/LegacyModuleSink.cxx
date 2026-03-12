/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "LegacyModuleSink.h"

#include "InputPort.h"
#include "Link.h"
#include "OutputPort.h"
#include "Pipeline.h"
#include "data/VolumeData.h"

#include <vtkPVRenderView.h>
#include <vtkPiecewiseFunction.h>
#include <vtkSMPropertyHelper.h>
#include <vtkSMProxy.h>
#include <vtkSMProxyManager.h>
#include <vtkSMRenderViewProxy.h>
#include <vtkSMSessionProxyManager.h>
#include <vtkSMTransferFunctionManager.h>
#include <vtkSMTransferFunctionProxy.h>
#include <vtkSMViewProxy.h>

namespace tomviz {
namespace pipeline {

LegacyModuleSink::LegacyModuleSink(QObject* parent) : SinkNode(parent) {}

LegacyModuleSink::~LegacyModuleSink()
{
  finalize();
}

bool LegacyModuleSink::initialize(vtkSMViewProxy* view)
{
  if (!view) {
    return false;
  }
  m_viewProxy = view;
  m_renderView =
    vtkPVRenderView::SafeDownCast(view->GetClientSideView());

  // Rendering is wired externally (MainWindow connects renderNeeded() →
  // pqView::render(), which coalesces multiple requests via an internal timer).

  return m_renderView != nullptr;
}

bool LegacyModuleSink::finalize()
{
  m_renderView = nullptr;
  m_viewProxy = nullptr;
  return true;
}

vtkSMViewProxy* LegacyModuleSink::view() const
{
  return m_viewProxy;
}

vtkPVRenderView* LegacyModuleSink::renderView() const
{
  return m_renderView;
}

bool LegacyModuleSink::visibility() const
{
  return m_visible;
}

void LegacyModuleSink::setVisibility(bool visible)
{
  if (m_visible != visible) {
    m_visible = visible;
    emit visibilityChanged(visible);
  }
}

bool LegacyModuleSink::isColorMapNeeded() const
{
  return false;
}

bool LegacyModuleSink::useDetachedColorMap() const
{
  return m_useDetachedColorMap;
}

void LegacyModuleSink::setUseDetachedColorMap(bool detached)
{
  if (m_useDetachedColorMap == detached) {
    return;
  }
  m_useDetachedColorMap = detached;

  if (detached && !m_detachedColorMap) {
    // Lazily create the detached color map
    auto* mgr = vtkSMProxyManager::GetProxyManager();
    auto* pxm = mgr ? mgr->GetActiveSessionProxyManager() : nullptr;
    if (!pxm) {
      m_useDetachedColorMap = false;
      return;
    }

    static unsigned int sinkCmapCounter = 0;
    ++sinkCmapCounter;

    vtkNew<vtkSMTransferFunctionManager> tfmgr;
    m_detachedColorMap = tfmgr->GetColorTransferFunction(
      QString("SinkColorMap%1").arg(sinkCmapCounter).toLatin1().data(), pxm);

    // Copy range from VolumeData's color map
    auto vol = m_volumeData.lock();
    if (vol) {
      auto range = vol->scalarRange();
      double r[2] = { range[0], range[1] };
      vtkSMTransferFunctionProxy::RescaleTransferFunction(
        m_detachedColorMap, r);
      auto* detachedOmap =
        vtkSMPropertyHelper(m_detachedColorMap, "ScalarOpacityFunction")
          .GetAsProxy();
      if (detachedOmap) {
        vtkSMTransferFunctionProxy::RescaleTransferFunction(detachedOmap, r);
      }
    }
  }

  updateColorMap();
  emit colorMapChanged();
}

vtkSMProxy* LegacyModuleSink::colorMap()
{
  if (m_useDetachedColorMap && m_detachedColorMap) {
    return m_detachedColorMap;
  }
  auto vol = m_volumeData.lock();
  if (vol && vol->hasColorMap()) {
    return vol->colorMap();
  }
  return nullptr;
}

vtkSMProxy* LegacyModuleSink::opacityMap()
{
  auto* cmap = colorMap();
  if (!cmap) {
    return nullptr;
  }
  return vtkSMPropertyHelper(cmap, "ScalarOpacityFunction").GetAsProxy();
}

vtkPiecewiseFunction* LegacyModuleSink::gradientOpacity() const
{
  if (m_useDetachedColorMap) {
    return m_detachedGradientOpacity;
  }
  auto vol = m_volumeData.lock();
  if (vol) {
    return vol->gradientOpacity();
  }
  return nullptr;
}

VolumeDataPtr LegacyModuleSink::volumeData() const
{
  return m_volumeData.lock();
}

void LegacyModuleSink::updateColorMap()
{
  // Base implementation does nothing. Subclasses override to push
  // color/opacity into their VTK pipeline.
}

QJsonObject LegacyModuleSink::serialize() const
{
  QJsonObject json;
  json["label"] = label();
  json["visible"] = m_visible;
  return json;
}

bool LegacyModuleSink::deserialize(const QJsonObject& json)
{
  if (json.contains("label")) {
    setLabel(json["label"].toString());
  }
  if (json.contains("visible")) {
    setVisibility(json["visible"].toBool());
  }
  return true;
}

bool LegacyModuleSink::execute()
{
  // Before consuming, cache VolumeData from the first Volume-type input port
  for (auto* port : inputPorts()) {
    if (port->acceptedTypes().testFlag(PortType::Volume)) {
      auto* lnk = port->link();
      if (lnk) {
        auto* outPort = lnk->from();
        if (outPort && outPort->hasData() &&
            outPort->type() == PortType::Volume) {
          auto vol = outPort->data().value<VolumeDataPtr>();
          if (vol) {
            m_volumeData = vol;
            break;
          }
        }
      }
    }
  }

  bool success = SinkNode::execute();

  if (success && m_firstConsume) {
    m_firstConsume = false;
    resetCameraIfFirstSink();
  }

  // Push color map into VTK pipeline after consume.
  // updateColorMap() already emits renderNeeded(), so only emit separately
  // when it was not called.
  if (success && isColorMapNeeded()) {
    updateColorMap();
  } else if (success) {
    emit renderNeeded();
  }

  return success;
}

void LegacyModuleSink::resetCameraIfFirstSink()
{
  if (!m_viewProxy) {
    return;
  }

  // Check if any other sink sharing our view has already consumed data
  auto* pip = qobject_cast<Pipeline*>(parent());
  if (pip) {
    for (auto* node : pip->nodes()) {
      auto* other = dynamic_cast<LegacyModuleSink*>(node);
      if (other && other != this && other->view() == m_viewProxy &&
          !other->m_firstConsume) {
        // Another sink already rendered to this view — skip reset
        return;
      }
    }
  }

  // Schedule camera reset on the main thread
  auto* renderViewProxy = vtkSMRenderViewProxy::SafeDownCast(m_viewProxy);
  if (renderViewProxy) {
    QMetaObject::invokeMethod(this, [renderViewProxy]() {
      renderViewProxy->ResetCamera();
    }, Qt::QueuedConnection);
  }
}

QWidget* LegacyModuleSink::createPropertiesWidget(QWidget* /*parent*/)
{
  return nullptr;
}

bool LegacyModuleSink::validateInput(const QMap<QString, PortData>& inputs,
                                     const QString& portName) const
{
  return inputs.contains(portName) && inputs[portName].isValid();
}

} // namespace pipeline
} // namespace tomviz
