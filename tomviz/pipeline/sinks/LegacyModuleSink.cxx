/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "LegacyModuleSink.h"

#include "Pipeline.h"

#include <vtkPVRenderView.h>
#include <vtkSMRenderViewProxy.h>
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

  // Auto-render the view when sink properties change or after execution.
  // Use QueuedConnection so this is safe when emitted from a worker thread.
  connect(this, &LegacyModuleSink::renderNeeded, this, [this]() {
    if (m_viewProxy) {
      m_viewProxy->StillRender();
    }
  }, Qt::QueuedConnection);

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
  bool success = SinkNode::execute();

  if (success && m_firstConsume) {
    m_firstConsume = false;
    resetCameraIfFirstSink();
  }

  // Signal that the view needs a render (handled on the main thread via
  // the QueuedConnection established in initialize()).
  if (success) {
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
