/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "VolumeSink.h"

#include "data/VolumeData.h"

#include <vtkColorTransferFunction.h>
#include <vtkImageData.h>
#include <vtkPVRenderView.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPlane.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolume.h>
#include <vtkVolumeProperty.h>

namespace tomviz {
namespace pipeline {

VolumeSink::VolumeSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("volume", PortType::Volume);
  setLabel("Volume");

  m_volumeMapper->SetRequestedRenderModeToGPU();
  m_volumeMapper->SetBlendMode(vtkVolumeMapper::COMPOSITE_BLEND);
  m_volumeMapper->UseJitteringOn();

  m_volumeProperty->SetInterpolationType(VTK_LINEAR_INTERPOLATION);
  m_volumeProperty->SetAmbient(0.0);
  m_volumeProperty->SetDiffuse(1.0);
  m_volumeProperty->SetSpecular(1.0);
  m_volumeProperty->SetSpecularPower(100.0);

  // Default grayscale color transfer function
  m_defaultColor->AddRGBPoint(0.0, 0.0, 0.0, 0.0);
  m_defaultColor->AddRGBPoint(1.0, 1.0, 1.0, 1.0);
  m_volumeProperty->SetColor(m_defaultColor);

  // Default opacity: ramp from 0 to 1
  m_defaultOpacity->AddPoint(0.0, 0.0);
  m_defaultOpacity->AddPoint(1.0, 1.0);
  m_volumeProperty->SetScalarOpacity(m_defaultOpacity);

  // Gradient opacity (needed for multi-volume shader workaround)
  m_gradientOpacity->AddPoint(0.0, 1.0);

  m_volume->SetMapper(m_volumeMapper);
  m_volume->SetProperty(m_volumeProperty);
}

VolumeSink::~VolumeSink()
{
  finalize();
}

bool VolumeSink::isColorMapNeeded() const
{
  return true;
}

bool VolumeSink::initialize(vtkSMViewProxy* view)
{
  if (!LegacyModuleSink::initialize(view)) {
    return false;
  }

  renderView()->AddPropToRenderer(m_volume);
  renderView()->Update();
  return true;
}

bool VolumeSink::finalize()
{
  if (renderView()) {
    renderView()->RemovePropFromRenderer(m_volume);
  }
  return LegacyModuleSink::finalize();
}

bool VolumeSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!validateInput(inputs, "volume")) {
    return false;
  }

  auto volume = inputs["volume"].value<VolumeDataPtr>();
  if (!volume || !volume->isValid()) {
    return false;
  }

  m_volumeMapper->SetInputData(volume->imageData());

  // Update the default transfer function ranges to match data
  auto range = volume->scalarRange();
  m_defaultColor->RemoveAllPoints();
  m_defaultColor->AddRGBPoint(range[0], 0.0, 0.0, 0.0);
  m_defaultColor->AddRGBPoint(range[1], 1.0, 1.0, 1.0);

  m_defaultOpacity->RemoveAllPoints();
  m_defaultOpacity->AddPoint(range[0], 0.0);
  m_defaultOpacity->AddPoint(range[1], 1.0);

  m_volume->SetVisibility(visibility() ? 1 : 0);

  if (renderView()) {
    renderView()->Update();
  }

  emit renderNeeded();
  return true;
}

// --- Lighting ---

bool VolumeSink::lighting() const
{
  return m_volumeProperty->GetShade() != 0;
}

void VolumeSink::setLighting(bool enabled)
{
  m_volumeProperty->SetShade(enabled ? 1 : 0);
  emit renderNeeded();
}

double VolumeSink::ambient() const
{
  return m_volumeProperty->GetAmbient();
}

void VolumeSink::setAmbient(double value)
{
  m_volumeProperty->SetAmbient(value);
  emit renderNeeded();
}

double VolumeSink::diffuse() const
{
  return m_volumeProperty->GetDiffuse();
}

void VolumeSink::setDiffuse(double value)
{
  m_volumeProperty->SetDiffuse(value);
  emit renderNeeded();
}

double VolumeSink::specular() const
{
  return m_volumeProperty->GetSpecular();
}

void VolumeSink::setSpecular(double value)
{
  m_volumeProperty->SetSpecular(value);
  emit renderNeeded();
}

double VolumeSink::specularPower() const
{
  return m_volumeProperty->GetSpecularPower();
}

void VolumeSink::setSpecularPower(double value)
{
  m_volumeProperty->SetSpecularPower(value);
  emit renderNeeded();
}

// --- Blending ---

int VolumeSink::blendingMode() const
{
  return m_volumeMapper->GetBlendMode();
}

void VolumeSink::setBlendingMode(int mode)
{
  m_volumeMapper->SetBlendMode(mode);
  emit renderNeeded();
}

// --- Interpolation ---

int VolumeSink::interpolationType() const
{
  return m_volumeProperty->GetInterpolationType();
}

void VolumeSink::setInterpolationType(int type)
{
  m_volumeProperty->SetInterpolationType(type);
  emit renderNeeded();
}

// --- Jittering ---

bool VolumeSink::jittering() const
{
  return m_volumeMapper->GetUseJittering() != 0;
}

void VolumeSink::setJittering(bool enabled)
{
  m_volumeMapper->SetUseJittering(enabled ? 1 : 0);
  emit renderNeeded();
}

// --- Solidity ---

double VolumeSink::solidity() const
{
  return 1.0 / m_volumeProperty->GetScalarOpacityUnitDistance();
}

void VolumeSink::setSolidity(double value)
{
  if (value > 0.0) {
    m_volumeProperty->SetScalarOpacityUnitDistance(1.0 / value);
    emit renderNeeded();
  }
}

// --- Transfer functions ---

void VolumeSink::setColorTransferFunction(vtkColorTransferFunction* ctf)
{
  if (ctf) {
    m_volumeProperty->SetColor(ctf);
  } else {
    m_volumeProperty->SetColor(m_defaultColor);
  }
  emit renderNeeded();
}

void VolumeSink::setOpacityTransferFunction(vtkPiecewiseFunction* otf)
{
  if (otf) {
    m_volumeProperty->SetScalarOpacity(otf);
  } else {
    m_volumeProperty->SetScalarOpacity(m_defaultOpacity);
  }
  emit renderNeeded();
}

void VolumeSink::setGradientOpacityFunction(vtkPiecewiseFunction* gof)
{
  m_volumeProperty->SetGradientOpacity(gof);
  emit renderNeeded();
}

// --- Clipping ---

void VolumeSink::addClippingPlane(vtkPlane* plane)
{
  if (plane) {
    m_volumeMapper->AddClippingPlane(plane);
    emit renderNeeded();
  }
}

void VolumeSink::removeClippingPlane(vtkPlane* plane)
{
  if (plane) {
    m_volumeMapper->RemoveClippingPlane(plane);
    emit renderNeeded();
  }
}

void VolumeSink::removeAllClippingPlanes()
{
  m_volumeMapper->RemoveAllClippingPlanes();
  emit renderNeeded();
}

// --- Serialization ---

QJsonObject VolumeSink::serialize() const
{
  auto json = LegacyModuleSink::serialize();
  json["interpolation"] = interpolationType();
  json["blendingMode"] = blendingMode();
  json["rayJittering"] = jittering();
  json["solidity"] = solidity();

  QJsonObject light;
  light["enabled"] = lighting();
  light["ambient"] = ambient();
  light["diffuse"] = diffuse();
  light["specular"] = specular();
  light["specularPower"] = specularPower();
  json["lighting"] = light;

  return json;
}

bool VolumeSink::deserialize(const QJsonObject& json)
{
  if (!LegacyModuleSink::deserialize(json)) {
    return false;
  }
  if (json.contains("interpolation")) {
    setInterpolationType(json["interpolation"].toInt());
  }
  if (json.contains("blendingMode")) {
    setBlendingMode(json["blendingMode"].toInt());
  }
  if (json.contains("rayJittering")) {
    setJittering(json["rayJittering"].toBool());
  }
  if (json.contains("solidity")) {
    setSolidity(json["solidity"].toDouble());
  }
  if (json.contains("lighting")) {
    auto light = json["lighting"].toObject();
    setLighting(light["enabled"].toBool());
    setAmbient(light["ambient"].toDouble());
    setDiffuse(light["diffuse"].toDouble());
    setSpecular(light["specular"].toDouble());
    setSpecularPower(light["specularPower"].toDouble());
  }
  return true;
}

} // namespace pipeline
} // namespace tomviz
