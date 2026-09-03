/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SetDataTypeReaction.h"

#include "ActiveObjects.h"
#include "TransformUtils.h"

#include "pipeline/OutputPort.h"
#include "pipeline/PortType.h"
#include "pipeline/transforms/ConvertToVolumeTransform.h"
#include "pipeline/transforms/SetTiltAnglesTransform.h"

#include <cassert>

namespace tomviz {

SetDataTypeReaction::SetDataTypeReaction(QAction* action, QMainWindow* mw,
                                         pipeline::PortType t)
  : pqReaction(action), m_mainWindow(mw), m_type(t)
{
  connect(&ActiveObjects::instance(), &ActiveObjects::activeNodeChanged, this,
          &SetDataTypeReaction::updateEnableState);
  connect(&ActiveObjects::instance(),
          &ActiveObjects::activePipelineChanged,
          this, &SetDataTypeReaction::updateEnableState);
  setWidgetText(t);
  updateEnableState();
}

void SetDataTypeReaction::setDataType(QMainWindow* mw, DataSource*,
                                      pipeline::PortType t)
{
  Q_UNUSED(mw);
  if (t == pipeline::PortType::TiltSeries) {
    auto* transform = new pipeline::SetTiltAnglesTransform();
    insertTransformIntoPipeline(transform);
  } else {
    auto* transform = new pipeline::ConvertToVolumeTransform();
    transform->setOutputType(pipeline::PortType::Volume);
    transform->setOutputLabel("Mark as Volume");
    insertTransformIntoPipeline(transform);
  }
}

void SetDataTypeReaction::onTriggered()
{
  setDataType(m_mainWindow, nullptr, m_type);
}

void SetDataTypeReaction::updateEnableState()
{
  auto& ao = ActiveObjects::instance();
  parentAction()->setEnabled(ao.activeTipOutputPort() != nullptr);
}

void SetDataTypeReaction::setWidgetText(pipeline::PortType t)
{
  if (t == pipeline::PortType::Volume) {
    parentAction()->setText("Mark Data As Volume");
  } else if (t == pipeline::PortType::TiltSeries) {
    parentAction()->setText("Mark Data As Tilt Series");
  } else {
    assert("Unknown data source type" && false);
  }
}
} // namespace tomviz
