/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "TransformNode.h"

#include "EditTransformWidget.h"
#include "InputPort.h"
#include "OutputPort.h"
#include "data/VolumeData.h"

#include <vtkImageData.h>

namespace tomviz {
namespace pipeline {

TransformNode::TransformNode(QObject* parent) : Node(parent) {}

QIcon TransformNode::icon() const
{
  return QIcon(
    QStringLiteral(":/pqWidgets/Icons/pqProgrammableFilter.svg"));
}

InputPort* TransformNode::addInput(const QString& name,
                                   PortTypes acceptedTypes)
{
  return addInputPort(name, acceptedTypes);
}

OutputPort* TransformNode::addOutput(const QString& name, PortType type)
{
  return addOutputPort(name, type);
}

bool TransformNode::hasPropertiesWidget() const
{
  return false;
}

bool TransformNode::propertiesWidgetNeedsInput() const
{
  return false;
}

EditTransformWidget* TransformNode::createPropertiesWidget(
  QWidget* /*parent*/)
{
  return nullptr;
}

bool TransformNode::execute()
{
  setExecState(NodeExecState::Running);

  // Check that all input ports are connected and have data.
  // If any required input is missing, skip execution rather than crash.
  for (auto* port : inputPorts()) {
    if (!port->link() || !port->hasData()) {
      setExecState(NodeExecState::Failed);
      return false;
    }
  }

  QMap<QString, PortData> inputs;
  for (auto* port : inputPorts()) {
    inputs[port->name()] = port->data();
  }

  auto outputs = transform(inputs);

  // An empty output map when the node declares output ports means the
  // transform failed (e.g. a Python exception was caught).
  if (outputs.isEmpty() && !outputPorts().isEmpty()) {
    setExecState(NodeExecState::Failed);
    return false;
  }

  for (auto it = outputs.constBegin(); it != outputs.constEnd(); ++it) {
    auto* port = outputPort(it.key());
    if (!port) {
      continue;
    }

    // Reuse existing VolumeData on re-execution: update its imageData
    // rather than replacing it, so the color map and other state persist.
    if (port->hasData() && isVolumeType(port->data().type()) &&
        isVolumeType(it.value().type())) {
      try {
        auto existing = port->data().value<VolumeDataPtr>();
        auto fresh = it.value().value<VolumeDataPtr>();
        if (existing && fresh && existing != fresh) {
          existing->setImageData(
            vtkSmartPointer<vtkImageData>(fresh->imageData()));
          existing->setLabel(fresh->label());
          existing->setUnits(fresh->units());
          port->setData(PortData(std::any(existing), it.value().type()));
          continue;
        }
      } catch (const std::bad_any_cast&) {
      }
    }

    port->setData(it.value());
  }

  markCurrent();
  setExecState(NodeExecState::Idle);
  return true;
}

} // namespace pipeline
} // namespace tomviz
