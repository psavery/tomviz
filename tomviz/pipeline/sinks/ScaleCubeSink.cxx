/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ScaleCubeSink.h"

#include "data/VolumeData.h"

#include <vtkActor.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkCubeSource.h>
#include <vtkPVRenderView.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <cmath>

namespace tomviz {
namespace pipeline {

ScaleCubeSink::ScaleCubeSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("volume", PortType::Volume);
  setLabel("Scale Cube");

  m_cubeSource->SetXLength(m_sideLength);
  m_cubeSource->SetYLength(m_sideLength);
  m_cubeSource->SetZLength(m_sideLength);

  m_mapper->SetInputConnection(m_cubeSource->GetOutputPort());
  m_actor->SetMapper(m_mapper);
  m_actor->SetProperty(m_property);
  m_property->SetRepresentationToWireframe();
  m_property->SetColor(1.0, 1.0, 1.0);
  m_property->SetLineWidth(2.0);

  // Configure text annotation
  auto* textProp = m_textActor->GetTextProperty();
  textProp->SetColor(1.0, 1.0, 1.0);
  textProp->SetFontSize(14);
  textProp->SetBold(1);
  textProp->SetJustificationToCentered();
  m_textActor->SetVisibility(0);
}

ScaleCubeSink::~ScaleCubeSink()
{
  finalize();
}

bool ScaleCubeSink::initialize(vtkSMViewProxy* view)
{
  if (!LegacyModuleSink::initialize(view)) {
    return false;
  }

  renderView()->AddPropToRenderer(m_actor);
  renderView()->AddPropToRenderer(m_textActor);
  renderView()->Update();
  return true;
}

bool ScaleCubeSink::finalize()
{
  if (renderView()) {
    renderView()->RemovePropFromRenderer(m_actor);
    renderView()->RemovePropFromRenderer(m_textActor);
  }
  return LegacyModuleSink::finalize();
}

bool ScaleCubeSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!validateInput(inputs, "volume")) {
    return false;
  }

  auto volume = inputs["volume"].value<VolumeDataPtr>();
  if (!volume || !volume->isValid()) {
    return false;
  }

  auto bounds = volume->bounds();

  // Adaptive scaling on first consume: set side length to ~10% of max extent
  if (m_firstConsume && m_adaptiveScaling) {
    double extentX = bounds[1] - bounds[0];
    double extentY = bounds[3] - bounds[2];
    double extentZ = bounds[5] - bounds[4];
    double maxExtent = std::max({ extentX, extentY, extentZ });
    m_sideLength = maxExtent * 0.1;
    m_cubeSource->SetXLength(m_sideLength);
    m_cubeSource->SetYLength(m_sideLength);
    m_cubeSource->SetZLength(m_sideLength);
    m_firstConsume = false;
  }

  // Position at the lower corner of the volume bounds
  m_cubeSource->SetCenter(bounds[0] + m_sideLength / 2.0,
                          bounds[2] + m_sideLength / 2.0,
                          bounds[4] + m_sideLength / 2.0);

  updateAnnotation();

  m_actor->SetVisibility(visibility() ? 1 : 0);
  m_textActor->SetVisibility(
    (visibility() && m_showAnnotation) ? 1 : 0);

  if (renderView()) {
    renderView()->Update();
  }

  emit renderNeeded();
  return true;
}

void ScaleCubeSink::updateAnnotation()
{
  double center[3];
  m_cubeSource->GetCenter(center);

  // Position text below the cube
  m_textActor->SetPosition(center[0],
                           center[1] - m_sideLength * 0.7,
                           center[2]);

  // Generate annotation text
  QString text = m_annotationText;
  if (text.isEmpty()) {
    // Format the side length nicely
    if (m_sideLength >= 1.0) {
      text = QString::number(m_sideLength, 'g', 3);
    } else {
      text = QString::number(m_sideLength, 'g', 2);
    }
  }
  m_textActor->SetInput(text.toUtf8().data());
}

double ScaleCubeSink::sideLength() const
{
  return m_sideLength;
}

void ScaleCubeSink::setSideLength(double length)
{
  m_sideLength = length;
  m_cubeSource->SetXLength(length);
  m_cubeSource->SetYLength(length);
  m_cubeSource->SetZLength(length);
  updateAnnotation();
  emit renderNeeded();
}

void ScaleCubeSink::setPosition(double x, double y, double z)
{
  m_cubeSource->SetCenter(x, y, z);
  updateAnnotation();
  emit renderNeeded();
}

bool ScaleCubeSink::adaptiveScaling() const
{
  return m_adaptiveScaling;
}

void ScaleCubeSink::setAdaptiveScaling(bool adaptive)
{
  m_adaptiveScaling = adaptive;
}

void ScaleCubeSink::setColor(double r, double g, double b)
{
  m_property->SetColor(r, g, b);
  emit renderNeeded();
}

bool ScaleCubeSink::showAnnotation() const
{
  return m_showAnnotation;
}

void ScaleCubeSink::setShowAnnotation(bool show)
{
  m_showAnnotation = show;
  m_textActor->SetVisibility(
    (visibility() && m_showAnnotation) ? 1 : 0);
  emit renderNeeded();
}

QString ScaleCubeSink::annotationText() const
{
  return m_annotationText;
}

void ScaleCubeSink::setAnnotationText(const QString& text)
{
  m_annotationText = text;
  updateAnnotation();
  emit renderNeeded();
}

QJsonObject ScaleCubeSink::serialize() const
{
  auto json = LegacyModuleSink::serialize();
  json["sideLength"] = m_sideLength;
  json["adaptiveScaling"] = m_adaptiveScaling;
  json["showAnnotation"] = m_showAnnotation;
  json["annotationText"] = m_annotationText;
  double center[3];
  m_cubeSource->GetCenter(center);
  json["centerX"] = center[0];
  json["centerY"] = center[1];
  json["centerZ"] = center[2];
  double rgb[3];
  m_property->GetColor(rgb);
  json["colorR"] = rgb[0];
  json["colorG"] = rgb[1];
  json["colorB"] = rgb[2];
  return json;
}

bool ScaleCubeSink::deserialize(const QJsonObject& json)
{
  if (!LegacyModuleSink::deserialize(json)) {
    return false;
  }
  if (json.contains("sideLength")) {
    setSideLength(json["sideLength"].toDouble());
  }
  if (json.contains("adaptiveScaling")) {
    setAdaptiveScaling(json["adaptiveScaling"].toBool());
  }
  if (json.contains("showAnnotation")) {
    setShowAnnotation(json["showAnnotation"].toBool());
  }
  if (json.contains("annotationText")) {
    setAnnotationText(json["annotationText"].toString());
  }
  if (json.contains("centerX")) {
    setPosition(json["centerX"].toDouble(), json["centerY"].toDouble(),
                json["centerZ"].toDouble());
  }
  if (json.contains("colorR")) {
    setColor(json["colorR"].toDouble(), json["colorG"].toDouble(),
             json["colorB"].toDouble());
  }
  return true;
}

} // namespace pipeline
} // namespace tomviz
