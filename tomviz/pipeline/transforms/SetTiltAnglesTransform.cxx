/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SetTiltAnglesTransform.h"

#include "data/VolumeData.h"

namespace tomviz {
namespace pipeline {

SetTiltAnglesTransform::SetTiltAnglesTransform(QObject* parent)
  : TransformNode(parent)
{
  addInput("volume", PortType::Volume);
  addOutput("output", PortType::Volume);
}

void SetTiltAnglesTransform::setTiltAngles(
  const QMap<size_t, double>& angles)
{
  m_tiltAngles = angles;
}

QMap<size_t, double> SetTiltAnglesTransform::tiltAnglesMap() const
{
  return m_tiltAngles;
}

QMap<QString, PortData> SetTiltAnglesTransform::transform(
  const QMap<QString, PortData>& inputs)
{
  QMap<QString, PortData> outputs;

  auto it = inputs.find("volume");
  if (it == inputs.end()) {
    return outputs;
  }

  VolumeDataPtr vol;
  try {
    vol = it.value().value<VolumeDataPtr>();
  } catch (const std::bad_any_cast&) {
    return outputs;
  }

  if (!vol || !vol->imageData()) {
    return outputs;
  }

  // Build the full tilt angle vector from the sparse map
  int numSlices = vol->dimensions()[2];
  QVector<double> angles(numSlices, 0.0);
  for (auto jt = m_tiltAngles.constBegin(); jt != m_tiltAngles.constEnd();
       ++jt) {
    if (static_cast<int>(jt.key()) < numSlices) {
      angles[static_cast<int>(jt.key())] = jt.value();
    }
  }

  vol->setTiltAngles(angles);

  outputs["output"] = PortData(std::any(vol), PortType::Volume);
  return outputs;
}

} // namespace pipeline
} // namespace tomviz
