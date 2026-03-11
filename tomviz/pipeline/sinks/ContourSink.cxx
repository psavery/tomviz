/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ContourSink.h"

#include "data/VolumeData.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QWidget>

#include <vtkActor.h>
#include <vtkColorTransferFunction.h>
#include <vtkDataSetMapper.h>
#include <vtkFlyingEdges3D.h>
#include <vtkImageData.h>
#include <vtkPVRenderView.h>
#include <vtkProperty.h>

namespace tomviz {
namespace pipeline {

ContourSink::ContourSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("volume", PortType::Volume);
  setLabel("Contour");

  m_property->SetAmbient(0.0);
  m_property->SetDiffuse(1.0);
  m_property->SetSpecular(1.0);
  m_property->SetSpecularPower(100.0);
  m_property->SetRepresentationToSurface();

  m_mapper->SetInputConnection(m_flyingEdges->GetOutputPort());
  m_actor->SetMapper(m_mapper);
  m_actor->SetProperty(m_property);
}

ContourSink::~ContourSink()
{
  finalize();
}

bool ContourSink::isColorMapNeeded() const
{
  return true;
}

bool ContourSink::initialize(vtkSMViewProxy* view)
{
  if (!LegacyModuleSink::initialize(view)) {
    return false;
  }

  renderView()->AddPropToRenderer(m_actor);
  return true;
}

bool ContourSink::finalize()
{
  if (renderView()) {
    renderView()->RemovePropFromRenderer(m_actor);
  }
  return LegacyModuleSink::finalize();
}

bool ContourSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!validateInput(inputs, "volume")) {
    return false;
  }

  auto volume = inputs["volume"].value<VolumeDataPtr>();
  if (!volume || !volume->isValid()) {
    return false;
  }

  m_flyingEdges->SetInputData(volume->imageData());

  // Cache scalar range
  auto range = volume->scalarRange();
  m_scalarRange[0] = range[0];
  m_scalarRange[1] = range[1];

  // Auto-set iso value to 2/3 of range (matching old ModuleContour default)
  if (!m_isoValueSet) {
    m_isoValue = range[0] + (range[1] - range[0]) * (2.0 / 3.0);
  }

  m_flyingEdges->SetValue(0, m_isoValue);
  m_actor->SetVisibility(visibility() ? 1 : 0);

  if (renderView()) {
    renderView()->Update();
  }

  emit renderNeeded();
  return true;
}

double ContourSink::isoValue() const
{
  return m_isoValue;
}

void ContourSink::setIsoValue(double value)
{
  m_isoValue = value;
  m_isoValueSet = true;
  m_flyingEdges->SetValue(0, value);
  emit renderNeeded();
}

double ContourSink::opacity() const
{
  return m_property->GetOpacity();
}

void ContourSink::setOpacity(double value)
{
  m_property->SetOpacity(value);
  emit renderNeeded();
}

// --- Lighting ---

double ContourSink::ambient() const
{
  return m_property->GetAmbient();
}

void ContourSink::setAmbient(double value)
{
  m_property->SetAmbient(value);
  emit renderNeeded();
}

double ContourSink::diffuse() const
{
  return m_property->GetDiffuse();
}

void ContourSink::setDiffuse(double value)
{
  m_property->SetDiffuse(value);
  emit renderNeeded();
}

double ContourSink::specular() const
{
  return m_property->GetSpecular();
}

void ContourSink::setSpecular(double value)
{
  m_property->SetSpecular(value);
  emit renderNeeded();
}

double ContourSink::specularPower() const
{
  return m_property->GetSpecularPower();
}

void ContourSink::setSpecularPower(double value)
{
  m_property->SetSpecularPower(value);
  emit renderNeeded();
}

// --- Representation ---

int ContourSink::representation() const
{
  return m_property->GetRepresentation();
}

void ContourSink::setRepresentation(int rep)
{
  m_property->SetRepresentation(rep);
  emit renderNeeded();
}

// --- Color ---

void ContourSink::color(double rgb[3]) const
{
  m_property->GetDiffuseColor(rgb);
}

void ContourSink::setColor(double r, double g, double b)
{
  m_property->SetDiffuseColor(r, g, b);
  emit renderNeeded();
}

void ContourSink::scalarRange(double range[2]) const
{
  range[0] = m_scalarRange[0];
  range[1] = m_scalarRange[1];
}

QWidget* ContourSink::createPropertiesWidget(QWidget* parent)
{
  auto* widget = new QWidget(parent);
  auto* layout = new QFormLayout(widget);

  // --- Iso Value ---
  auto* isoSpin = new QDoubleSpinBox(widget);
  isoSpin->setRange(m_scalarRange[0], m_scalarRange[1]);
  isoSpin->setDecimals(4);
  isoSpin->setSingleStep((m_scalarRange[1] - m_scalarRange[0]) / 100.0);
  {
    QSignalBlocker blocker(isoSpin);
    isoSpin->setValue(isoValue());
  }
  layout->addRow("Iso Value", isoSpin);
  QObject::connect(isoSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                   [this](double v) { setIsoValue(v); });

  // --- Opacity ---
  auto* opacitySlider = new QSlider(Qt::Horizontal, widget);
  opacitySlider->setRange(0, 100);
  {
    QSignalBlocker blocker(opacitySlider);
    opacitySlider->setValue(static_cast<int>(opacity() * 100));
  }
  layout->addRow("Opacity", opacitySlider);
  QObject::connect(opacitySlider, &QSlider::valueChanged,
                   [this](int v) { setOpacity(v / 100.0); });

  // --- Representation ---
  auto* repCombo = new QComboBox(widget);
  repCombo->addItem("Points", 0);
  repCombo->addItem("Wireframe", 1);
  repCombo->addItem("Surface", 2);
  {
    QSignalBlocker blocker(repCombo);
    int idx = repCombo->findData(representation());
    repCombo->setCurrentIndex(idx >= 0 ? idx : 2);
  }
  layout->addRow("Representation", repCombo);
  QObject::connect(repCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   [this, repCombo](int idx) {
                     setRepresentation(repCombo->itemData(idx).toInt());
                   });

  // --- Map Scalars ---
  auto* mapCheck = new QCheckBox(widget);
  {
    QSignalBlocker blocker(mapCheck);
    mapCheck->setChecked(mapScalars());
  }
  layout->addRow("Map Scalars", mapCheck);

  // --- Color (visible when !mapScalars) ---
  auto* colorLayout = new QHBoxLayout();
  double rgb[3];
  color(rgb);
  auto* rEdit = new QLineEdit(QString::number(rgb[0], 'f', 3), widget);
  auto* gEdit = new QLineEdit(QString::number(rgb[1], 'f', 3), widget);
  auto* bEdit = new QLineEdit(QString::number(rgb[2], 'f', 3), widget);
  colorLayout->addWidget(rEdit);
  colorLayout->addWidget(gEdit);
  colorLayout->addWidget(bEdit);
  layout->addRow("Color", colorLayout);
  auto setColorVisible = [rEdit, gEdit, bEdit](bool visible) {
    rEdit->setVisible(visible);
    gEdit->setVisible(visible);
    bEdit->setVisible(visible);
  };
  setColorVisible(!mapScalars());

  auto updateColor = [this, rEdit, gEdit, bEdit]() {
    setColor(rEdit->text().toDouble(), gEdit->text().toDouble(),
             bEdit->text().toDouble());
  };
  QObject::connect(rEdit, &QLineEdit::editingFinished, updateColor);
  QObject::connect(gEdit, &QLineEdit::editingFinished, updateColor);
  QObject::connect(bEdit, &QLineEdit::editingFinished, updateColor);

  QObject::connect(mapCheck, &QCheckBox::toggled,
                   [this, setColorVisible](bool on) {
                     setMapScalars(on);
                     setColorVisible(!on);
                   });

  // --- Lighting ---
  auto* lightGroup = new QGroupBox("Lighting", widget);
  auto* lightLayout = new QFormLayout(lightGroup);

  auto addLightSlider = [&](const QString& label, double initial, int maxVal,
                            auto setter) {
    auto* slider = new QSlider(Qt::Horizontal, widget);
    slider->setRange(0, maxVal);
    {
      QSignalBlocker blocker(slider);
      slider->setValue(static_cast<int>(initial * (maxVal == 150 ? 1.0 : 100.0)));
    }
    lightLayout->addRow(label, slider);
    if (maxVal == 150) {
      QObject::connect(slider, &QSlider::valueChanged,
                       [this, setter](int v) { (this->*setter)(v); });
    } else {
      QObject::connect(slider, &QSlider::valueChanged,
                       [this, setter](int v) { (this->*setter)(v / 100.0); });
    }
    return slider;
  };

  addLightSlider("Ambient", ambient(), 100, &ContourSink::setAmbient);
  addLightSlider("Diffuse", diffuse(), 100, &ContourSink::setDiffuse);
  addLightSlider("Specular", specular(), 100, &ContourSink::setSpecular);
  addLightSlider("Specular Power", specularPower(), 150,
                 &ContourSink::setSpecularPower);
  layout->addRow(lightGroup);

  return widget;
}

bool ContourSink::mapScalars() const
{
  return m_mapScalars;
}

void ContourSink::setMapScalars(bool map)
{
  m_mapScalars = map;
  if (map) {
    m_mapper->SetColorModeToMapScalars();
  } else {
    m_mapper->SetColorModeToDirectScalars();
  }
  emit renderNeeded();
}

void ContourSink::setLookupTable(vtkColorTransferFunction* lut)
{
  if (lut) {
    m_mapper->SetLookupTable(lut);
  }
  emit renderNeeded();
}

// --- Serialization ---

QJsonObject ContourSink::serialize() const
{
  auto json = LegacyModuleSink::serialize();
  json["isoValue"] = m_isoValue;
  json["opacity"] = m_property->GetOpacity();
  json["ambient"] = m_property->GetAmbient();
  json["diffuse"] = m_property->GetDiffuse();
  json["specular"] = m_property->GetSpecular();
  json["specularPower"] = m_property->GetSpecularPower();
  json["representation"] = m_property->GetRepresentation();
  json["mapScalars"] = m_mapScalars;
  return json;
}

bool ContourSink::deserialize(const QJsonObject& json)
{
  if (!LegacyModuleSink::deserialize(json)) {
    return false;
  }
  if (json.contains("isoValue")) {
    setIsoValue(json["isoValue"].toDouble());
  }
  if (json.contains("opacity")) {
    m_property->SetOpacity(json["opacity"].toDouble());
  }
  if (json.contains("ambient")) {
    m_property->SetAmbient(json["ambient"].toDouble());
  }
  if (json.contains("diffuse")) {
    m_property->SetDiffuse(json["diffuse"].toDouble());
  }
  if (json.contains("specular")) {
    m_property->SetSpecular(json["specular"].toDouble());
  }
  if (json.contains("specularPower")) {
    m_property->SetSpecularPower(json["specularPower"].toDouble());
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
