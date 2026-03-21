/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ConvertToVolumeTransform.h"

#include "OutputPort.h"
#include "data/VolumeData.h"

#include <vtkImageData.h>
#include <vtkNew.h>

namespace tomviz {
namespace pipeline {

ConvertToVolumeTransform::ConvertToVolumeTransform(QObject* parent)
  : TransformNode(parent)
{
  addInput("volume", PortType::ImageData);
  addOutput("output", PortType::Volume);
  setLabel(m_outputLabel);
}

void ConvertToVolumeTransform::setOutputType(PortType type)
{
  m_outputType = type;
  outputPort("output")->setDeclaredType(type);
}

PortType ConvertToVolumeTransform::outputType() const
{
  return m_outputType;
}

void ConvertToVolumeTransform::setOutputLabel(const QString& label)
{
  m_outputLabel = label;
  setLabel(label);
}

QString ConvertToVolumeTransform::outputLabel() const
{
  return m_outputLabel;
}

QMap<QString, PortData> ConvertToVolumeTransform::transform(
  const QMap<QString, PortData>& inputs)
{
  QMap<QString, PortData> result;

  auto inputVolume = inputs["volume"].value<VolumeDataPtr>();
  if (!inputVolume || !inputVolume->isValid()) {
    return result;
  }

  // Deep-copy so the output is independent from the input
  vtkNew<vtkImageData> outputImage;
  outputImage->DeepCopy(inputVolume->imageData());

  auto output = std::make_shared<VolumeData>(outputImage.Get());
  output->setLabel(inputVolume->label());
  output->setUnits(inputVolume->units());

  result["output"] = PortData(std::any(output), m_outputType);
  return result;
}

} // namespace pipeline
} // namespace tomviz
