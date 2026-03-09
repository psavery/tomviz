/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "LegacyModuleSink.h"

#include <vtkPVRenderView.h>
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
