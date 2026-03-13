/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SliceSink.h"

#include "data/VolumeData.h"
#include "vtkNonOrthoImagePlaneWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

#include <vtkAlgorithmOutput.h>
#include <vtkImageData.h>
#include <vtkLookupTable.h>
#include <vtkPVRenderView.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkScalarsToColors.h>
#include <vtkSMProxy.h>
#include <vtkSMViewProxy.h>
#include <vtkTrivialProducer.h>

namespace tomviz {
namespace pipeline {

SliceSink::SliceSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("volume", PortType::Volume);
  setLabel("Slice");
}

SliceSink::~SliceSink()
{
  finalize();
}

QIcon SliceSink::icon() const
{
  return QIcon(QStringLiteral(":/icons/orthoslice.svg"));
}

void SliceSink::setVisibility(bool visible)
{
  if (m_widget) {
    m_widget->SetEnabled(visible ? 1 : 0);
    if (visible) {
      m_widget->SetInteraction(m_showArrow ? 1 : 0);
    }
  }
  LegacyModuleSink::setVisibility(visible);
}

bool SliceSink::isColorMapNeeded() const
{
  return true;
}

void SliceSink::setupWidget()
{
  m_widget = vtkSmartPointer<vtkNonOrthoImagePlaneWidget>::New();

  // Default: red border
  double color[3] = { 1, 0, 0 };
  m_widget->GetPlaneProperty()->SetColor(color);

  // Default: linear interpolation
  m_widget->TextureInterpolateOn();
  m_widget->SetResliceInterpolateToLinear();

  // Apply current state
  m_widget->SetThickSliceMode(static_cast<int>(m_thickSliceMode));
  m_widget->SetOpacity(m_opacity);
  m_widget->SetArrowVisibility(m_showArrow ? 1 : 0);
  m_widget->SetMapScalars(m_mapScalars);

  if (m_interpolate) {
    m_widget->SetTextureInterpolate(1);
    m_widget->SetResliceInterpolate(VTK_LINEAR_RESLICE);
  } else {
    m_widget->SetTextureInterpolate(0);
    m_widget->SetResliceInterpolate(VTK_NEAREST_RESLICE);
  }
}

bool SliceSink::initialize(vtkSMViewProxy* view)
{
  if (!LegacyModuleSink::initialize(view)) {
    return false;
  }

  vtkRenderWindowInteractor* rwi = view->GetRenderWindow()->GetInteractor();
  if (!rwi) {
    return false;
  }

  setupWidget();
  m_widget->SetInteractor(rwi);
  m_widget->On();
  m_widget->InteractionOn();

  return true;
}

bool SliceSink::finalize()
{
  if (m_widget) {
    m_widget->Off();
    m_widget = nullptr;
  }
  return LegacyModuleSink::finalize();
}

bool SliceSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!validateInput(inputs, "volume")) {
    return false;
  }

  auto volume = inputs["volume"].value<VolumeDataPtr>();
  if (!volume || !volume->isValid()) {
    return false;
  }

  auto dims = volume->dimensions();
  m_dims[0] = dims[0];
  m_dims[1] = dims[1];
  m_dims[2] = dims[2];
  volume->imageData()->GetBounds(m_bounds);

  if (m_widget) {
    // Feed data to the widget via a trivial producer
    vtkNew<vtkTrivialProducer> producer;
    producer->SetOutput(volume->imageData());
    m_widget->SetInputConnection(producer->GetOutputPort());

    // Apply direction and slice
    applyDirection();

    // Apply thick slicing
    m_widget->SetSliceThickness(m_sliceThickness);

    // Show/hide based on visibility
    m_widget->SetEnabled(visibility() ? 1 : 0);
  } else {
    // No widget yet (no view initialized) — just store dimensions for later
    if (m_slice < 0 && isOrtho()) {
      m_slice = m_dims[directionAxis()] / 2;
    }
  }

  emit renderNeeded();
  return true;
}

void SliceSink::applyDirection()
{
  if (!m_widget) {
    return;
  }

  int axis = directionAxis();

  if (axis >= 0) {
    // Orthogonal direction
    m_widget->SetPlaneOrientation(axis);

    // Default to mid-slice if not explicitly set
    if (m_slice < 0) {
      m_slice = m_dims[axis] / 2;
    }
    m_slice = qBound(0, m_slice, m_dims[axis] - 1);
    m_widget->SetSliceIndex(m_slice);
  } else {
    // Custom direction — set normal and center
    m_widget->SetNormal(m_planeNormal);
    if (m_planeCenterSet) {
      m_widget->SetCenter(m_planeCenter);
    }
    m_widget->UpdatePlacement();
  }
}

int SliceSink::directionAxis() const
{
  switch (m_direction) {
    case XY:
      return 2;
    case YZ:
      return 0;
    case XZ:
      return 1;
    default:
      return -1;
  }
}

SliceSink::Direction SliceSink::direction() const
{
  return m_direction;
}

void SliceSink::setDirection(Direction dir)
{
  if (m_direction == dir) {
    return;
  }
  m_direction = dir;

  if (isOrtho() && m_dims[directionAxis()] > 0) {
    double normal[3] = { 0, 0, 0 };
    int axis = directionAxis();
    normal[axis] = 1;
    m_planeNormal[0] = normal[0];
    m_planeNormal[1] = normal[1];
    m_planeNormal[2] = normal[2];
    m_slice = m_dims[axis] / 2;
  }

  applyDirection();
  emit directionChanged(dir);
  emit renderNeeded();
}

bool SliceSink::isOrtho() const
{
  return directionAxis() >= 0;
}

int SliceSink::slice() const
{
  return m_slice;
}

void SliceSink::setSlice(int index)
{
  m_slice = index;
  if (m_widget && isOrtho()) {
    int axis = directionAxis();
    m_slice = qBound(0, m_slice, m_dims[axis] - 1);
    m_widget->SetSliceIndex(m_slice);
  }
  emit sliceChanged(m_slice);
  emit renderNeeded();
}

int SliceSink::maxSlice() const
{
  if (!isOrtho()) {
    return -1;
  }
  int axis = directionAxis();
  return m_dims[axis] - 1;
}

double SliceSink::opacity() const
{
  return m_opacity;
}

void SliceSink::setOpacity(double value)
{
  m_opacity = value;
  if (m_widget) {
    m_widget->SetOpacity(value);
  }
  emit renderNeeded();
}

int SliceSink::sliceThickness() const
{
  return m_sliceThickness;
}

void SliceSink::setSliceThickness(int slices)
{
  m_sliceThickness = slices;
  if (m_widget) {
    m_widget->SetSliceThickness(slices);
  }
  emit renderNeeded();
}

SliceSink::ThickSliceMode SliceSink::thickSliceMode() const
{
  return m_thickSliceMode;
}

void SliceSink::setThickSliceMode(ThickSliceMode mode)
{
  m_thickSliceMode = mode;
  if (m_widget) {
    m_widget->SetThickSliceMode(static_cast<int>(mode));
  }
  emit renderNeeded();
}

bool SliceSink::textureInterpolate() const
{
  return m_interpolate;
}

void SliceSink::setTextureInterpolate(bool interpolate)
{
  m_interpolate = interpolate;
  if (m_widget) {
    int val = interpolate ? 1 : 0;
    m_widget->SetTextureInterpolate(val);
    m_widget->SetResliceInterpolate(val);
  }
  emit renderNeeded();
}

bool SliceSink::showArrow() const
{
  return m_showArrow;
}

void SliceSink::setShowArrow(bool show)
{
  m_showArrow = show;
  if (m_widget) {
    m_widget->SetArrowVisibility(show ? 1 : 0);
    // Interaction follows arrow visibility
    if (visibility()) {
      m_widget->SetInteraction(show ? 1 : 0);
    }
  }
  emit renderNeeded();
}

bool SliceSink::mapScalars() const
{
  return m_mapScalars;
}

void SliceSink::setMapScalars(bool map)
{
  m_mapScalars = map;
  if (m_widget) {
    m_widget->SetMapScalars(map);
  }
  emit renderNeeded();
}

void SliceSink::updateColorMap()
{
  if (!m_widget) {
    return;
  }
  auto* cmap = colorMap();
  if (cmap) {
    auto* ctf = vtkScalarsToColors::SafeDownCast(
      cmap->GetClientSideObject());
    if (ctf) {
      m_widget->SetLookupTable(ctf);
    }
  }
  emit renderNeeded();
}

void SliceSink::setPlaneCenter(double x, double y, double z)
{
  m_planeCenter[0] = x;
  m_planeCenter[1] = y;
  m_planeCenter[2] = z;
  m_planeCenterSet = true;
  if (m_widget) {
    m_widget->SetCenter(m_planeCenter);
    m_widget->UpdatePlacement();
  }
  emit renderNeeded();
}

void SliceSink::planeCenter(double xyz[3]) const
{
  if (m_widget) {
    m_widget->GetCenter(xyz);
  } else {
    xyz[0] = m_planeCenter[0];
    xyz[1] = m_planeCenter[1];
    xyz[2] = m_planeCenter[2];
  }
}

void SliceSink::setPlaneNormal(double x, double y, double z)
{
  m_planeNormal[0] = x;
  m_planeNormal[1] = y;
  m_planeNormal[2] = z;
  if (m_widget) {
    m_widget->SetNormal(m_planeNormal);
    m_widget->UpdatePlacement();
  }
  emit renderNeeded();
}

void SliceSink::planeNormal(double xyz[3]) const
{
  if (m_widget) {
    m_widget->GetNormal(xyz);
  } else {
    xyz[0] = m_planeNormal[0];
    xyz[1] = m_planeNormal[1];
    xyz[2] = m_planeNormal[2];
  }
}

QWidget* SliceSink::createPropertiesWidget(QWidget* parent)
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

  // --- Direction ---
  auto* dirCombo = new QComboBox(widget);
  dirCombo->addItem("XY", static_cast<int>(XY));
  dirCombo->addItem("YZ", static_cast<int>(YZ));
  dirCombo->addItem("XZ", static_cast<int>(XZ));
  dirCombo->addItem("Custom", static_cast<int>(Custom));
  {
    QSignalBlocker blocker(dirCombo);
    dirCombo->setCurrentIndex(static_cast<int>(direction()));
  }
  layout->addRow("Direction", dirCombo);

  // --- Slice slider ---
  auto* sliceSlider = new QSlider(Qt::Horizontal, widget);
  {
    int ms = maxSlice();
    sliceSlider->setRange(0, ms >= 0 ? ms : 0);
    QSignalBlocker blocker(sliceSlider);
    sliceSlider->setValue(slice() >= 0 ? slice() : 0);
  }
  sliceSlider->setVisible(isOrtho());
  layout->addRow("Slice", sliceSlider);
  QObject::connect(sliceSlider, &QSlider::valueChanged,
                   [this](int v) { setSlice(v); });

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

  // --- Interpolate ---
  auto* interpCheck = new QCheckBox(widget);
  {
    QSignalBlocker blocker(interpCheck);
    interpCheck->setChecked(textureInterpolate());
  }
  layout->addRow("Interpolate", interpCheck);
  QObject::connect(interpCheck, &QCheckBox::toggled,
                   [this](bool on) { setTextureInterpolate(on); });

  // --- Show Arrow ---
  auto* arrowCheck = new QCheckBox(widget);
  {
    QSignalBlocker blocker(arrowCheck);
    arrowCheck->setChecked(showArrow());
  }
  layout->addRow("Show Arrow", arrowCheck);
  QObject::connect(arrowCheck, &QCheckBox::toggled,
                   [this](bool on) { setShowArrow(on); });

  // --- Thickness ---
  auto* thickSpin = new QSpinBox(widget);
  thickSpin->setMinimum(1);
  thickSpin->setMaximum(999);
  {
    QSignalBlocker blocker(thickSpin);
    thickSpin->setValue(sliceThickness());
  }
  layout->addRow("Thickness", thickSpin);

  // --- Aggregation ---
  auto* aggCombo = new QComboBox(widget);
  aggCombo->addItem("Min", static_cast<int>(Min));
  aggCombo->addItem("Max", static_cast<int>(Max));
  aggCombo->addItem("Mean", static_cast<int>(Mean));
  aggCombo->addItem("Sum", static_cast<int>(Sum));
  {
    QSignalBlocker blocker(aggCombo);
    aggCombo->setCurrentIndex(static_cast<int>(thickSliceMode()));
  }
  aggCombo->setVisible(sliceThickness() > 1);
  layout->addRow("Aggregation", aggCombo);

  QObject::connect(thickSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                   [this, aggCombo](int v) {
                     setSliceThickness(v);
                     aggCombo->setVisible(v > 1);
                   });
  QObject::connect(
    aggCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    [this, aggCombo](int idx) {
      setThickSliceMode(
        static_cast<ThickSliceMode>(aggCombo->itemData(idx).toInt()));
    });

  // --- Map Scalars ---
  auto* mapCheck = new QCheckBox(widget);
  {
    QSignalBlocker blocker(mapCheck);
    mapCheck->setChecked(mapScalars());
  }
  layout->addRow("Map Scalars", mapCheck);
  QObject::connect(mapCheck, &QCheckBox::toggled,
                   [this](bool on) { setMapScalars(on); });

  // --- Custom Plane ---
  auto* customGroup = new QGroupBox("Custom Plane", widget);
  auto* customLayout = new QFormLayout(customGroup);

  double center[3], normal[3];
  planeCenter(center);
  planeNormal(normal);

  auto* pxEdit = new QLineEdit(QString::number(center[0]), widget);
  auto* pyEdit = new QLineEdit(QString::number(center[1]), widget);
  auto* pzEdit = new QLineEdit(QString::number(center[2]), widget);
  auto* nxEdit = new QLineEdit(QString::number(normal[0]), widget);
  auto* nyEdit = new QLineEdit(QString::number(normal[1]), widget);
  auto* nzEdit = new QLineEdit(QString::number(normal[2]), widget);

  auto* pointLayout = new QHBoxLayout();
  pointLayout->addWidget(pxEdit);
  pointLayout->addWidget(pyEdit);
  pointLayout->addWidget(pzEdit);
  customLayout->addRow("Point", pointLayout);

  auto* normalLayout = new QHBoxLayout();
  normalLayout->addWidget(nxEdit);
  normalLayout->addWidget(nyEdit);
  normalLayout->addWidget(nzEdit);
  customLayout->addRow("Normal", normalLayout);

  customGroup->setVisible(direction() == Custom);
  layout->addRow(customGroup);

  auto updateCenter = [this, pxEdit, pyEdit, pzEdit]() {
    setPlaneCenter(pxEdit->text().toDouble(), pyEdit->text().toDouble(),
                   pzEdit->text().toDouble());
  };
  auto updateNormal = [this, nxEdit, nyEdit, nzEdit]() {
    setPlaneNormal(nxEdit->text().toDouble(), nyEdit->text().toDouble(),
                   nzEdit->text().toDouble());
  };
  QObject::connect(pxEdit, &QLineEdit::editingFinished, updateCenter);
  QObject::connect(pyEdit, &QLineEdit::editingFinished, updateCenter);
  QObject::connect(pzEdit, &QLineEdit::editingFinished, updateCenter);
  QObject::connect(nxEdit, &QLineEdit::editingFinished, updateNormal);
  QObject::connect(nyEdit, &QLineEdit::editingFinished, updateNormal);
  QObject::connect(nzEdit, &QLineEdit::editingFinished, updateNormal);

  // Direction combo connections
  QObject::connect(
    dirCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    [this, sliceSlider, customGroup, dirCombo](int idx) {
      auto dir = static_cast<Direction>(dirCombo->itemData(idx).toInt());
      setDirection(dir);
      bool ortho = (dir != Custom);
      sliceSlider->setVisible(ortho);
      customGroup->setVisible(!ortho);
      if (ortho) {
        int ms = maxSlice();
        sliceSlider->setRange(0, ms >= 0 ? ms : 0);
        QSignalBlocker blocker(sliceSlider);
        sliceSlider->setValue(slice() >= 0 ? slice() : 0);
      }
    });

  // Update slice slider when direction/slice changes from the sink itself
  QObject::connect(this, &SliceSink::directionChanged,
                   widget,
                   [sliceSlider, customGroup, this](Direction dir) {
                     bool ortho = (dir != Custom);
                     sliceSlider->setVisible(ortho);
                     customGroup->setVisible(!ortho);
                     if (ortho) {
                       int ms = maxSlice();
                       sliceSlider->setRange(0, ms >= 0 ? ms : 0);
                       QSignalBlocker blocker(sliceSlider);
                       sliceSlider->setValue(slice() >= 0 ? slice() : 0);
                     }
                   });
  QObject::connect(this, &SliceSink::sliceChanged,
                   widget,
                   [sliceSlider](int s) {
                     QSignalBlocker blocker(sliceSlider);
                     sliceSlider->setValue(s);
                   });

  return widget;
}

QJsonObject SliceSink::serialize() const
{
  auto json = LegacyModuleSink::serialize();
  json["direction"] = static_cast<int>(m_direction);
  json["slice"] = m_slice;
  json["opacity"] = m_opacity;
  json["sliceThickness"] = m_sliceThickness;
  json["thickSliceMode"] = static_cast<int>(m_thickSliceMode);
  json["interpolate"] = m_interpolate;
  json["showArrow"] = m_showArrow;
  json["mapScalars"] = m_mapScalars;

  // Serialize plane geometry from widget if available
  double point[3];
  if (m_widget) {
    QJsonArray center, normal;
    m_widget->GetCenter(point);
    center = { point[0], point[1], point[2] };
    m_widget->GetNormal(point);
    normal = { point[0], point[1], point[2] };
    json["planeCenter"] = center;
    json["planeNormal"] = normal;

    // Also save origin/point1/point2 for exact reconstruction
    QJsonArray origin, point1, point2;
    m_widget->GetOrigin(point);
    origin = { point[0], point[1], point[2] };
    m_widget->GetPoint1(point);
    point1 = { point[0], point[1], point[2] };
    m_widget->GetPoint2(point);
    point2 = { point[0], point[1], point[2] };
    json["origin"] = origin;
    json["point1"] = point1;
    json["point2"] = point2;
  }

  return json;
}

bool SliceSink::deserialize(const QJsonObject& json)
{
  if (!LegacyModuleSink::deserialize(json)) {
    return false;
  }
  if (json.contains("direction")) {
    m_direction = static_cast<Direction>(json["direction"].toInt());
  }
  if (json.contains("slice")) {
    m_slice = json["slice"].toInt();
  }
  if (json.contains("opacity")) {
    setOpacity(json["opacity"].toDouble());
  }
  if (json.contains("sliceThickness")) {
    setSliceThickness(json["sliceThickness"].toInt());
  }
  if (json.contains("thickSliceMode")) {
    setThickSliceMode(
      static_cast<ThickSliceMode>(json["thickSliceMode"].toInt()));
  }
  if (json.contains("interpolate")) {
    setTextureInterpolate(json["interpolate"].toBool());
  }
  if (json.contains("showArrow")) {
    setShowArrow(json["showArrow"].toBool());
  }
  if (json.contains("mapScalars")) {
    setMapScalars(json["mapScalars"].toBool());
  }
  // Restore plane geometry
  if (m_widget && json.contains("origin") && json.contains("point1") &&
      json.contains("point2")) {
    auto o = json["origin"].toArray();
    auto p1 = json["point1"].toArray();
    auto p2 = json["point2"].toArray();
    double origin[3] = { o[0].toDouble(), o[1].toDouble(), o[2].toDouble() };
    double pt1[3] = { p1[0].toDouble(), p1[1].toDouble(), p1[2].toDouble() };
    double pt2[3] = { p2[0].toDouble(), p2[1].toDouble(), p2[2].toDouble() };
    m_widget->SetOrigin(origin);
    m_widget->SetPoint1(pt1);
    m_widget->SetPoint2(pt2);
    m_widget->UpdatePlacement();
  }
  if (json.contains("planeCenter")) {
    auto c = json["planeCenter"].toArray();
    m_planeCenter[0] = c[0].toDouble();
    m_planeCenter[1] = c[1].toDouble();
    m_planeCenter[2] = c[2].toDouble();
    m_planeCenterSet = true;
  }
  if (json.contains("planeNormal")) {
    auto n = json["planeNormal"].toArray();
    m_planeNormal[0] = n[0].toDouble();
    m_planeNormal[1] = n[1].toDouble();
    m_planeNormal[2] = n[2].toDouble();
  }

  return true;
}

} // namespace pipeline
} // namespace tomviz
