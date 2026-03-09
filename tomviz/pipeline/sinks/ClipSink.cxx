/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ClipSink.h"

#include "data/VolumeData.h"
#include "vtkNonOrthoImagePlaneWidget.h"

#include <vtkColorTransferFunction.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPVRenderView.h>
#include <vtkPlane.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSMViewProxy.h>
#include <vtkTrivialProducer.h>

namespace tomviz {
namespace pipeline {

ClipSink::ClipSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("volume", PortType::Volume);
  setLabel("Clip");
}

ClipSink::~ClipSink()
{
  finalize();
}

void ClipSink::setupWidget()
{
  m_widget = vtkSmartPointer<vtkNonOrthoImagePlaneWidget>::New();
  m_widget->TextureInterpolateOn();
  m_widget->SetResliceInterpolateToLinear();

  // Grayscale LUT (saturation=0, value=1) like old ModuleClip
  vtkNew<vtkColorTransferFunction> lut;
  lut->SetColorSpaceToHSV();
  lut->HSVWrapOff();
  lut->AddHSVPoint(0.0, 0.0, 0.0, 1.0);
  lut->AddHSVPoint(1.0, 0.0, 0.0, 1.0);
  m_widget->SetLookupTable(lut);

  // Set plane color to off-white
  auto* prop = m_widget->GetPlaneProperty();
  prop->SetColor(204.0 / 255, 204.0 / 255, 204.0 / 255);

  m_widget->SetArrowVisibility(m_showArrow ? 1 : 0);
  m_widget->SetOpacity(m_opacity);
}

bool ClipSink::initialize(vtkSMViewProxy* view)
{
  if (!LegacyModuleSink::initialize(view)) {
    return false;
  }

  setupWidget();

  auto* renderWindow = view->GetRenderWindow();
  if (renderWindow) {
    auto* rwi = renderWindow->GetInteractor();
    if (rwi) {
      m_widget->SetInteractor(rwi);
      m_widget->On();
      m_widget->InteractionOn();
    }
  }

  return true;
}

bool ClipSink::finalize()
{
  if (m_widget) {
    m_widget->InteractionOff();
    m_widget->Off();
    m_widget->SetInteractor(nullptr);
    m_widget = nullptr;
  }
  return LegacyModuleSink::finalize();
}

bool ClipSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!validateInput(inputs, "volume")) {
    return false;
  }

  auto volume = inputs["volume"].value<VolumeDataPtr>();
  if (!volume || !volume->isValid()) {
    return false;
  }

  auto* imageData = volume->imageData();
  imageData->GetDimensions(m_dims);
  imageData->GetBounds(m_bounds);

  if (m_widget) {
    vtkNew<vtkTrivialProducer> producer;
    producer->SetOutput(imageData);
    m_widget->SetInputConnection(producer->GetOutputPort());

    // Set default slice to center if not explicitly set
    if (m_slice < 0) {
      m_slice = m_dims[2] / 2;
    }

    applyDirection();

    // Update the clipping plane from the widget's plane geometry
    double origin[3], pt1[3], pt2[3], normal[3];
    m_widget->GetOrigin(origin);
    m_widget->GetPoint1(pt1);
    m_widget->GetPoint2(pt2);
    m_widget->GetNormal(normal);
    if (m_invertPlane) {
      normal[0] = -normal[0];
      normal[1] = -normal[1];
      normal[2] = -normal[2];
    }
    m_clippingPlane->SetOrigin(origin);
    m_clippingPlane->SetNormal(normal);

    m_widget->SetEnabled(visibility() ? 1 : 0);
  } else {
    // Without widget: just update clipping plane from stored direction
    double cx = (m_bounds[0] + m_bounds[1]) / 2.0;
    double cy = (m_bounds[2] + m_bounds[3]) / 2.0;
    double cz = (m_bounds[4] + m_bounds[5]) / 2.0;
    m_clippingPlane->SetOrigin(cx, cy, cz);
    double n[3] = { 0, 0, 1 };
    if (m_direction == YZ) {
      n[0] = 1; n[1] = 0; n[2] = 0;
    } else if (m_direction == XZ) {
      n[0] = 0; n[1] = 1; n[2] = 0;
    }
    if (m_invertPlane) {
      n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
    }
    m_clippingPlane->SetNormal(n);
  }

  emit clipPlaneUpdated();
  emit renderNeeded();
  return true;
}

void ClipSink::applyDirection()
{
  if (!m_widget) {
    return;
  }

  if (m_direction != Custom) {
    m_widget->SetPlaneOrientation(static_cast<int>(m_direction));
    int maxSlice = m_dims[m_direction] - 1;
    int s = qBound(0, m_slice, maxSlice);
    m_widget->SetSliceIndex(s);
  }
}

ClipSink::Direction ClipSink::direction() const
{
  return m_direction;
}

void ClipSink::setDirection(Direction dir)
{
  m_direction = dir;
  applyDirection();
  emit renderNeeded();
}

int ClipSink::slice() const
{
  return m_slice;
}

void ClipSink::setSlice(int s)
{
  m_slice = s;
  if (m_widget && m_direction != Custom) {
    int maxSlice = m_dims[m_direction] - 1;
    m_widget->SetSliceIndex(qBound(0, s, maxSlice));
  }
  emit renderNeeded();
}

double ClipSink::opacity() const
{
  return m_opacity;
}

void ClipSink::setOpacity(double value)
{
  m_opacity = value;
  if (m_widget) {
    m_widget->SetOpacity(value);
  }
  emit renderNeeded();
}

bool ClipSink::showArrow() const
{
  return m_showArrow;
}

void ClipSink::setShowArrow(bool show)
{
  m_showArrow = show;
  if (m_widget) {
    m_widget->SetArrowVisibility(show ? 1 : 0);
  }
  emit renderNeeded();
}

bool ClipSink::invertPlane() const
{
  return m_invertPlane;
}

void ClipSink::setInvertPlane(bool invert)
{
  m_invertPlane = invert;
  emit clipPlaneUpdated();
  emit renderNeeded();
}

void ClipSink::setPlaneOrigin(double x, double y, double z)
{
  if (m_widget) {
    double c[3] = { x, y, z };
    m_widget->SetCenter(c);
  }
  m_clippingPlane->SetOrigin(x, y, z);
  emit clipPlaneUpdated();
  emit renderNeeded();
}

void ClipSink::setPlaneNormal(double nx, double ny, double nz)
{
  if (m_widget) {
    double n[3] = { nx, ny, nz };
    m_widget->SetNormal(n);
  }
  m_clippingPlane->SetNormal(nx, ny, nz);
  emit clipPlaneUpdated();
  emit renderNeeded();
}

vtkPlane* ClipSink::clippingPlane() const
{
  return m_clippingPlane;
}

QJsonObject ClipSink::serialize() const
{
  auto json = LegacyModuleSink::serialize();
  json["direction"] = static_cast<int>(m_direction);
  json["slice"] = m_slice;
  json["opacity"] = m_opacity;
  json["showArrow"] = m_showArrow;
  json["invertPlane"] = m_invertPlane;

  if (m_widget) {
    double origin[3], point1[3], point2[3];
    m_widget->GetOrigin(origin);
    m_widget->GetPoint1(point1);
    m_widget->GetPoint2(point2);
    json["originX"] = origin[0];
    json["originY"] = origin[1];
    json["originZ"] = origin[2];
    json["point1X"] = point1[0];
    json["point1Y"] = point1[1];
    json["point1Z"] = point1[2];
    json["point2X"] = point2[0];
    json["point2Y"] = point2[1];
    json["point2Z"] = point2[2];
  }

  return json;
}

bool ClipSink::deserialize(const QJsonObject& json)
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
  if (json.contains("showArrow")) {
    setShowArrow(json["showArrow"].toBool());
  }
  if (json.contains("invertPlane")) {
    setInvertPlane(json["invertPlane"].toBool());
  }
  if (json.contains("originX") && m_widget) {
    m_widget->SetOrigin(json["originX"].toDouble(),
                        json["originY"].toDouble(),
                        json["originZ"].toDouble());
    m_widget->SetPoint1(json["point1X"].toDouble(),
                        json["point1Y"].toDouble(),
                        json["point1Z"].toDouble());
    m_widget->SetPoint2(json["point2X"].toDouble(),
                        json["point2Y"].toDouble(),
                        json["point2Z"].toDouble());
    m_widget->UpdatePlacement();
  }
  return true;
}

} // namespace pipeline
} // namespace tomviz
