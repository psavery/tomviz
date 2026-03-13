/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "RulerSink.h"

#include "data/VolumeData.h"

#include <vtkActor.h>
#include <vtkImageData.h>
#include <vtkLineSource.h>
#include <vtkPVRenderView.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

#include <cmath>

namespace tomviz {
namespace pipeline {

RulerSink::RulerSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("volume", PortType::Volume);
  setLabel("Ruler");

  m_mapper->SetInputConnection(m_lineSource->GetOutputPort());
  m_actor->SetMapper(m_mapper);
  m_actor->SetProperty(m_property);
  m_property->SetLineWidth(2.0);
  m_property->SetColor(1.0, 1.0, 0.0); // Yellow
}

RulerSink::~RulerSink()
{
  finalize();
}

QIcon RulerSink::icon() const
{
  return QIcon(QStringLiteral(":/pqWidgets/Icons/pqRuler.svg"));
}

void RulerSink::setVisibility(bool visible)
{
  m_actor->SetVisibility(visible ? 1 : 0);
  LegacyModuleSink::setVisibility(visible);
}

bool RulerSink::initialize(vtkSMViewProxy* view)
{
  if (!LegacyModuleSink::initialize(view)) {
    return false;
  }

  renderView()->AddPropToRenderer(m_actor);
  return true;
}

bool RulerSink::finalize()
{
  if (renderView()) {
    renderView()->RemovePropFromRenderer(m_actor);
  }
  return LegacyModuleSink::finalize();
}

bool RulerSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!validateInput(inputs, "volume")) {
    return false;
  }

  auto volume = inputs["volume"].value<VolumeDataPtr>();
  if (!volume || !volume->isValid()) {
    return false;
  }

  // Set default endpoints on first consume: diagonal corners of bounds
  // (like old ModuleRuler). Don't overwrite user-set endpoints.
  if (m_firstConsume) {
    auto bounds = volume->bounds();
    m_lineSource->SetPoint1(bounds[0], bounds[2], bounds[4]);
    m_lineSource->SetPoint2(bounds[1], bounds[3], bounds[5]);
    m_firstConsume = false;
  }

  m_actor->SetVisibility(visibility() ? 1 : 0);

  emit renderNeeded();
  return true;
}

void RulerSink::setPoint1(double x, double y, double z)
{
  m_lineSource->SetPoint1(x, y, z);
  emit renderNeeded();
}

void RulerSink::setPoint2(double x, double y, double z)
{
  m_lineSource->SetPoint2(x, y, z);
  emit renderNeeded();
}

double RulerSink::length() const
{
  double p1[3];
  double p2[3];
  m_lineSource->GetPoint1(p1);
  m_lineSource->GetPoint2(p2);
  double dx = p2[0] - p1[0];
  double dy = p2[1] - p1[1];
  double dz = p2[2] - p1[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

QJsonObject RulerSink::serialize() const
{
  auto json = LegacyModuleSink::serialize();
  double p1[3];
  double p2[3];
  m_lineSource->GetPoint1(p1);
  m_lineSource->GetPoint2(p2);
  json["point1X"] = p1[0];
  json["point1Y"] = p1[1];
  json["point1Z"] = p1[2];
  json["point2X"] = p2[0];
  json["point2Y"] = p2[1];
  json["point2Z"] = p2[2];
  return json;
}

bool RulerSink::deserialize(const QJsonObject& json)
{
  if (!LegacyModuleSink::deserialize(json)) {
    return false;
  }
  if (json.contains("point1X")) {
    setPoint1(json["point1X"].toDouble(), json["point1Y"].toDouble(),
              json["point1Z"].toDouble());
  }
  if (json.contains("point2X")) {
    setPoint2(json["point2X"].toDouble(), json["point2Y"].toDouble(),
              json["point2Z"].toDouble());
  }
  return true;
}

} // namespace pipeline
} // namespace tomviz
