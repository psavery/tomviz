/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "VolumeSink.h"
#include "VolumeSinkWidget.h"

#include "data/VolumeData.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>

#include <vtkColorTransferFunction.h>
#include <vtkDataArray.h>
#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkSMProxy.h>
#include <vtkPVRenderView.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPlane.h>
#include <vtkGPUVolumeRayCastMapper.h>
#include <vtkObjectFactory.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolume.h>
#include <vtkVolumeMapper.h>
#include <vtkVolumeProperty.h>

namespace tomviz {
namespace pipeline {

// Subclass vtkSmartVolumeMapper so we can forward jittering to the GPU mapper,
// matching the legacy ModuleVolume behavior.
class SmartVolumeMapper : public vtkSmartVolumeMapper
{
public:
  SmartVolumeMapper() { SetRequestedRenderModeToGPU(); }

  static SmartVolumeMapper* New();

  void UseJitteringOn() { GetGPUMapper()->UseJitteringOn(); }
  void UseJitteringOff() { GetGPUMapper()->UseJitteringOff(); }
  vtkTypeBool GetUseJittering() { return GetGPUMapper()->GetUseJittering(); }
  void SetUseJittering(vtkTypeBool b) { GetGPUMapper()->SetUseJittering(b); }
};

vtkStandardNewMacro(SmartVolumeMapper)

VolumeSink::VolumeSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("volume", PortType::ImageData);
  setLabel("Volume");

  // NOTE: Due to a bug in vtkMultiVolume, a gradient opacity function must be
  // set or the shader will fail to compile.
  m_gradientOpacity->AddPoint(0.0, 1.0);

  m_volumeMapper->SetScalarModeToUsePointFieldData();
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
  applyActiveScalars();
  m_volume->SetVisibility(visibility() ? 1 : 0);

  onMetadataChanged();
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

  // Gradient opacity: only set it if the function actually has control points.
  // An empty vtkPiecewiseFunction evaluates to 0 everywhere, which would make
  // the entire volume transparent. The legacy ModuleVolume set gradient opacity
  // to nullptr in SCALAR transfer mode (the default), disabling it.
  auto* gradOp = gradientOpacity();
  if (gradOp && gradOp->GetSize() > 0) {
    m_volumeProperty->SetGradientOpacity(gradOp);
  } else if (m_gradientOpacity->GetSize() > 0) {
    m_volumeProperty->SetGradientOpacity(m_gradientOpacity);
  } else {
    m_volumeProperty->SetGradientOpacity(nullptr);
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

// --- Active scalars ---

int VolumeSink::activeScalars() const
{
  return m_activeScalars;
}

void VolumeSink::setActiveScalars(int index)
{
  m_activeScalars = index;
  applyActiveScalars();
  emit renderNeeded();
}

void VolumeSink::applyActiveScalars()
{
  auto vol = volumeData();
  if (!vol || !vol->isValid()) {
    return;
  }

  auto* pointData = vol->imageData()->GetPointData();
  int idx = m_activeScalars;
  if (idx < 0) {
    // Default: use whatever vtkPointData considers active
    auto* active = pointData->GetScalars();
    if (active && active->GetName()) {
      m_volumeMapper->SelectScalarArray(active->GetName());
    }
  } else if (idx < pointData->GetNumberOfArrays()) {
    auto* array = pointData->GetArray(idx);
    if (array && array->GetName()) {
      m_volumeMapper->SelectScalarArray(array->GetName());
    }
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
  auto* widget = new VolumeSinkWidget(parent);
  int insertRow = 0;

  // --- Active Scalars combo (row 0) ---
  auto* scalarsCombo = new QComboBox(widget);
  {
    QSignalBlocker blocker(scalarsCombo);
    scalarsCombo->addItem("Default", -1);

    auto vol = volumeData();
    if (vol && vol->isValid()) {
      auto* pointData = vol->imageData()->GetPointData();
      for (int i = 0; i < pointData->GetNumberOfArrays(); ++i) {
        auto* array = pointData->GetArray(i);
        if (array && array->GetName()) {
          scalarsCombo->addItem(QString(array->GetName()), i);
        }
      }
    }

    if (m_activeScalars < 0) {
      scalarsCombo->setCurrentIndex(0);
    } else {
      int idx = scalarsCombo->findData(m_activeScalars);
      scalarsCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
  }
  widget->formLayout()->insertRow(insertRow++, "Active Scalars", scalarsCombo);
  connect(scalarsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this, scalarsCombo](int idx) {
            setActiveScalars(scalarsCombo->itemData(idx).toInt());
          });

  // --- Separate Color Map checkbox ---
  auto* separateCmapCheck = new QCheckBox(widget);
  {
    QSignalBlocker blocker(separateCmapCheck);
    separateCmapCheck->setChecked(useDetachedColorMap());
  }
  widget->formLayout()->insertRow(insertRow++, "Separate Color Map",
                                  separateCmapCheck);
  connect(separateCmapCheck, &QCheckBox::toggled,
          [this](bool on) { setUseDetachedColorMap(on); });

  // Push current state into the widget
  {
    QSignalBlocker blocker(widget);
    widget->setJittering(jittering());
    widget->setLighting(lighting());
    widget->setBlendingMode(blendingMode());
    widget->setInterpolationType(interpolationType());
    widget->setAmbient(ambient());
    widget->setDiffuse(diffuse());
    widget->setSpecular(specular());
    widget->setSpecularPower(specularPower());
    widget->setSolidity(solidity());
  }

  // Connect widget signals to VolumeSink setters
  connect(widget, &VolumeSinkWidget::jitteringToggled, this,
          &VolumeSink::setJittering);
  connect(widget, &VolumeSinkWidget::lightingToggled, this,
          &VolumeSink::setLighting);
  connect(widget, &VolumeSinkWidget::blendingChanged, this,
          &VolumeSink::setBlendingMode);
  connect(widget, &VolumeSinkWidget::interpolationChanged, this,
          &VolumeSink::setInterpolationType);
  connect(widget, &VolumeSinkWidget::ambientChanged, this,
          &VolumeSink::setAmbient);
  connect(widget, &VolumeSinkWidget::diffuseChanged, this,
          &VolumeSink::setDiffuse);
  connect(widget, &VolumeSinkWidget::specularChanged, this,
          &VolumeSink::setSpecular);
  connect(widget, &VolumeSinkWidget::specularPowerChanged, this,
          &VolumeSink::setSpecularPower);
  connect(widget, &VolumeSinkWidget::solidityChanged, this,
          &VolumeSink::setSolidity);

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
  json["activeScalars"] = m_activeScalars;

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
  if (json.contains("activeScalars")) {
    m_activeScalars = json["activeScalars"].toInt(-1);
  }
  return true;
}

void VolumeSink::onMetadataChanged()
{
  auto vol = volumeData();
  if (!vol) return;
  auto pos = vol->displayPosition();
  auto orient = vol->displayOrientation();
  m_volume->SetPosition(pos.data());
  m_volume->SetOrientation(orient.data());
  applyActiveScalars();
  emit renderNeeded();
}

} // namespace pipeline
} // namespace tomviz
