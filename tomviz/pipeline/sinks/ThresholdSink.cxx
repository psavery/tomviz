/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ThresholdSink.h"

#include "data/VolumeData.h"

#include <vtkActor.h>
#include <vtkDataSetMapper.h>
#include <vtkImageData.h>
#include <vtkPVRenderView.h>
#include <vtkProperty.h>
#include <vtkThreshold.h>

namespace tomviz {
namespace pipeline {

ThresholdSink::ThresholdSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("volume", PortType::Volume);
  setLabel("Threshold");

  m_property->SetRepresentationToSurface();
  m_property->SetSpecular(1.0);
  m_property->SetSpecularPower(100.0);

  m_mapper->SetInputConnection(m_threshold->GetOutputPort());
  m_actor->SetMapper(m_mapper);
  m_actor->SetProperty(m_property);
}

ThresholdSink::~ThresholdSink()
{
  finalize();
}

bool ThresholdSink::isColorMapNeeded() const
{
  return true;
}

bool ThresholdSink::initialize(vtkSMViewProxy* view)
{
  if (!LegacyModuleSink::initialize(view)) {
    return false;
  }

  renderView()->AddPropToRenderer(m_actor);
  renderView()->Update();
  return true;
}

bool ThresholdSink::finalize()
{
  if (renderView()) {
    renderView()->RemovePropFromRenderer(m_actor);
  }
  return LegacyModuleSink::finalize();
}

bool ThresholdSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!validateInput(inputs, "volume")) {
    return false;
  }

  auto volume = inputs["volume"].value<VolumeDataPtr>();
  if (!volume || !volume->isValid()) {
    return false;
  }

  m_threshold->SetInputData(volume->imageData());

  // Auto-set to the middle 10% of range if not explicitly set
  if (!m_rangeSet) {
    auto range = volume->scalarRange();
    double mid = (range[0] + range[1]) / 2.0;
    double delta = (range[1] - range[0]) * 0.1;
    m_lower = mid - delta;
    m_upper = mid + delta;
  }

  m_threshold->SetLowerThreshold(m_lower);
  m_threshold->SetUpperThreshold(m_upper);
  m_threshold->SetThresholdFunction(vtkThreshold::THRESHOLD_BETWEEN);
  m_actor->SetVisibility(visibility() ? 1 : 0);

  if (renderView()) {
    renderView()->Update();
  }

  emit renderNeeded();
  return true;
}

double ThresholdSink::lowerThreshold() const
{
  return m_lower;
}

double ThresholdSink::upperThreshold() const
{
  return m_upper;
}

void ThresholdSink::setThresholdRange(double lower, double upper)
{
  m_lower = lower;
  m_upper = upper;
  m_rangeSet = true;
  m_threshold->SetLowerThreshold(m_lower);
  m_threshold->SetUpperThreshold(m_upper);
  emit renderNeeded();
}

double ThresholdSink::opacity() const
{
  return m_property->GetOpacity();
}

void ThresholdSink::setOpacity(double value)
{
  m_property->SetOpacity(value);
  emit renderNeeded();
}

double ThresholdSink::specular() const
{
  return m_property->GetSpecular();
}

void ThresholdSink::setSpecular(double value)
{
  m_property->SetSpecular(value);
  emit renderNeeded();
}

int ThresholdSink::representation() const
{
  return m_property->GetRepresentation();
}

void ThresholdSink::setRepresentation(int rep)
{
  m_property->SetRepresentation(rep);
  emit renderNeeded();
}

bool ThresholdSink::mapScalars() const
{
  return m_mapScalars;
}

void ThresholdSink::setMapScalars(bool map)
{
  m_mapScalars = map;
  if (map) {
    m_mapper->SetColorModeToMapScalars();
  } else {
    m_mapper->SetColorModeToDirectScalars();
  }
  emit renderNeeded();
}

QJsonObject ThresholdSink::serialize() const
{
  auto json = LegacyModuleSink::serialize();
  json["lowerThreshold"] = m_lower;
  json["upperThreshold"] = m_upper;
  json["opacity"] = m_property->GetOpacity();
  json["specular"] = m_property->GetSpecular();
  json["representation"] = m_property->GetRepresentation();
  json["mapScalars"] = m_mapScalars;
  return json;
}

bool ThresholdSink::deserialize(const QJsonObject& json)
{
  if (!LegacyModuleSink::deserialize(json)) {
    return false;
  }
  if (json.contains("lowerThreshold")) {
    setThresholdRange(json["lowerThreshold"].toDouble(),
                      json["upperThreshold"].toDouble());
  }
  if (json.contains("opacity")) {
    setOpacity(json["opacity"].toDouble());
  }
  if (json.contains("specular")) {
    setSpecular(json["specular"].toDouble());
  }
  if (json.contains("representation")) {
    setRepresentation(json["representation"].toInt());
  }
  if (json.contains("mapScalars")) {
    setMapScalars(json["mapScalars"].toBool());
  }
  return true;
}

} // namespace pipeline
} // namespace tomviz
