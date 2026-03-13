/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "VolumeSink.h"

#include "data/VolumeData.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QSlider>
#include <QWidget>

#include <vtkColorTransferFunction.h>
#include <vtkImageData.h>
#include <vtkSMProxy.h>
#include <vtkPVRenderView.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPlane.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolume.h>
#include <vtkVolumeMapper.h>
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

  m_volume->SetMapper(m_volumeMapper);
  m_volume->SetProperty(m_volumeProperty);
}

VolumeSink::~VolumeSink()
{
  finalize();
}

QIcon VolumeSink::icon() const
{
  return QIcon(QStringLiteral(":/icons/pqVolumeData.png"));
}

void VolumeSink::setVisibility(bool visible)
{
  m_volume->SetVisibility(visible ? 1 : 0);
  LegacyModuleSink::setVisibility(visible);
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
  m_volume->SetVisibility(visibility() ? 1 : 0);

  return true;
}

void VolumeSink::updateColorMap()
{
  auto* cmap = colorMap();
  if (!cmap) {
    return;
  }
  auto* ctf = vtkColorTransferFunction::SafeDownCast(
    cmap->GetClientSideObject());
  auto* omap = opacityMap();
  auto* opacity = omap ? vtkPiecewiseFunction::SafeDownCast(
                           omap->GetClientSideObject())
                       : nullptr;

  if (ctf) {
    m_volumeProperty->SetColor(ctf);
  }
  if (opacity) {
    m_volumeProperty->SetScalarOpacity(opacity);
  }

  auto* gradOp = gradientOpacity();
  if (gradOp) {
    m_volumeProperty->SetGradientOpacity(gradOp);
  }

  emit renderNeeded();
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

// --- Properties widget ---

QWidget* VolumeSink::createPropertiesWidget(QWidget* parent)
{
  auto* widget = new QWidget(parent);
  auto* layout = new QFormLayout(widget);

  // --- Custom color map toggle ---
  auto* customCmapCheck = new QCheckBox(widget);
  {
    QSignalBlocker blocker(customCmapCheck);
    customCmapCheck->setChecked(useDetachedColorMap());
  }
  layout->addRow("Custom Color Map", customCmapCheck);
  QObject::connect(customCmapCheck, &QCheckBox::toggled,
                   [this](bool on) { setUseDetachedColorMap(on); });

  // --- Lighting checkbox ---
  auto* lightCheck = new QCheckBox(widget);
  {
    QSignalBlocker blocker(lightCheck);
    lightCheck->setChecked(lighting());
  }
  layout->addRow("Lighting", lightCheck);

  // --- Lighting group ---
  auto* lightGroup = new QGroupBox("Lighting", widget);
  auto* lightLayout = new QFormLayout(lightGroup);
  lightGroup->setEnabled(lighting());

  auto addSlider = [&](const QString& label, double initial, int minVal,
                       int maxVal, auto setter,
                       bool directValue = false) -> QSlider* {
    auto* slider = new QSlider(Qt::Horizontal, widget);
    slider->setRange(minVal, maxVal);
    {
      QSignalBlocker blocker(slider);
      slider->setValue(
        static_cast<int>(directValue ? initial : initial * 100));
    }
    lightLayout->addRow(label, slider);
    if (directValue) {
      QObject::connect(slider, &QSlider::valueChanged,
                       [this, setter](int v) { (this->*setter)(v); });
    } else {
      QObject::connect(slider, &QSlider::valueChanged,
                       [this, setter](int v) { (this->*setter)(v / 100.0); });
    }
    return slider;
  };

  addSlider("Ambient", ambient(), 0, 100, &VolumeSink::setAmbient);
  addSlider("Diffuse", diffuse(), 0, 100, &VolumeSink::setDiffuse);
  addSlider("Specular", specular(), 0, 100, &VolumeSink::setSpecular);
  addSlider("Specular Power", specularPower(), 1, 150,
            &VolumeSink::setSpecularPower, true);
  layout->addRow(lightGroup);

  QObject::connect(lightCheck, &QCheckBox::toggled,
                   [this, lightGroup](bool on) {
                     setLighting(on);
                     lightGroup->setEnabled(on);
                   });

  // --- Jittering ---
  auto* jitterCheck = new QCheckBox(widget);
  {
    QSignalBlocker blocker(jitterCheck);
    jitterCheck->setChecked(jittering());
  }
  layout->addRow("Jittering", jitterCheck);
  QObject::connect(jitterCheck, &QCheckBox::toggled,
                   [this](bool on) { setJittering(on); });

  // --- Blending ---
  auto* blendCombo = new QComboBox(widget);
  blendCombo->addItem("Composite", vtkVolumeMapper::COMPOSITE_BLEND);
  blendCombo->addItem("Max", vtkVolumeMapper::MAXIMUM_INTENSITY_BLEND);
  blendCombo->addItem("Min", vtkVolumeMapper::MINIMUM_INTENSITY_BLEND);
  blendCombo->addItem("Average", vtkVolumeMapper::AVERAGE_INTENSITY_BLEND);
  {
    QSignalBlocker blocker(blendCombo);
    int idx = blendCombo->findData(blendingMode());
    blendCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  }
  layout->addRow("Blending", blendCombo);
  QObject::connect(
    blendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    [this, blendCombo](int idx) {
      setBlendingMode(blendCombo->itemData(idx).toInt());
    });

  // --- Interpolation ---
  auto* interpCombo = new QComboBox(widget);
  interpCombo->addItem("Linear", VTK_LINEAR_INTERPOLATION);
  interpCombo->addItem("Nearest", VTK_NEAREST_INTERPOLATION);
  {
    QSignalBlocker blocker(interpCombo);
    int idx = interpCombo->findData(interpolationType());
    interpCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  }
  layout->addRow("Interpolation", interpCombo);
  QObject::connect(
    interpCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    [this, interpCombo](int idx) {
      setInterpolationType(interpCombo->itemData(idx).toInt());
    });

  // --- Solidity ---
  auto* soliditySpin = new QDoubleSpinBox(widget);
  soliditySpin->setRange(0.001, 100.0);
  soliditySpin->setDecimals(3);
  soliditySpin->setSingleStep(0.1);
  {
    QSignalBlocker blocker(soliditySpin);
    soliditySpin->setValue(solidity());
  }
  layout->addRow("Solidity", soliditySpin);
  QObject::connect(
    soliditySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    [this](double v) { setSolidity(v); });

  return widget;
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
