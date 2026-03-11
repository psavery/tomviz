/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "OutlineSink.h"

#include "data/VolumeData.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QWidget>

#include <vtkActor.h>
#include <vtkGridAxesActor3D.h>
#include <vtkImageData.h>
#include <vtkOutlineFilter.h>
#include <vtkPVRenderView.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkTextProperty.h>

namespace tomviz {
namespace pipeline {

OutlineSink::OutlineSink(QObject* parent) : LegacyModuleSink(parent)
{
  addInput("volume", PortType::Volume);
  setLabel("Outline");

  m_mapper->SetInputConnection(m_outlineFilter->GetOutputPort());
  m_actor->SetMapper(m_mapper);
  m_actor->SetProperty(m_property);
  // Off-white default color
  m_property->SetColor(0.9, 0.9, 0.9);

  // Configure grid axes actor
  m_gridAxes->SetVisibility(0);
  // Configure text properties to be readable
  for (int i = 0; i < 3; ++i) {
    auto* tp = m_gridAxes->GetTitleTextProperty(i);
    tp->SetColor(1.0, 1.0, 1.0);
    tp->SetFontSize(14);
    tp->SetBold(1);
    auto* lp = m_gridAxes->GetLabelTextProperty(i);
    lp->SetColor(0.9, 0.9, 0.9);
    lp->SetFontSize(10);
  }
  m_gridAxes->SetXTitle(m_xTitle.toUtf8().data());
  m_gridAxes->SetYTitle(m_yTitle.toUtf8().data());
  m_gridAxes->SetZTitle(m_zTitle.toUtf8().data());
  // Show all faces and labels
  m_gridAxes->SetFaceMask(0xFF);
  m_gridAxes->SetLabelMask(0xFF);
  m_gridAxes->SetProperty(m_property);
}

OutlineSink::~OutlineSink()
{
  finalize();
}

bool OutlineSink::initialize(vtkSMViewProxy* view)
{
  if (!LegacyModuleSink::initialize(view)) {
    return false;
  }

  renderView()->AddPropToRenderer(m_actor);
  renderView()->AddPropToRenderer(m_gridAxes);
  return true;
}

bool OutlineSink::finalize()
{
  if (renderView()) {
    renderView()->RemovePropFromRenderer(m_actor);
    renderView()->RemovePropFromRenderer(m_gridAxes);
  }
  return LegacyModuleSink::finalize();
}

bool OutlineSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!validateInput(inputs, "volume")) {
    return false;
  }

  auto volume = inputs["volume"].value<VolumeDataPtr>();
  if (!volume || !volume->isValid()) {
    return false;
  }

  m_outlineFilter->SetInputData(volume->imageData());

  // Update grid axes bounds to match the volume
  auto bounds = volume->bounds();
  m_gridAxes->SetGridBounds(bounds.data());

  m_actor->SetVisibility(visibility() ? 1 : 0);
  m_gridAxes->SetVisibility(
    (visibility() && m_showGridAxes) ? 1 : 0);

  if (renderView()) {
    renderView()->Update();
  }

  emit renderNeeded();
  return true;
}

void OutlineSink::color(double rgb[3]) const
{
  m_property->GetColor(rgb);
}

void OutlineSink::setColor(double r, double g, double b)
{
  m_property->SetColor(r, g, b);
  emit renderNeeded();
}

QWidget* OutlineSink::createPropertiesWidget(QWidget* parent)
{
  auto* widget = new QWidget(parent);
  auto* layout = new QFormLayout(widget);

  // --- Color ---
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

  auto updateColor = [this, rEdit, gEdit, bEdit]() {
    setColor(rEdit->text().toDouble(), gEdit->text().toDouble(),
             bEdit->text().toDouble());
  };
  QObject::connect(rEdit, &QLineEdit::editingFinished, updateColor);
  QObject::connect(gEdit, &QLineEdit::editingFinished, updateColor);
  QObject::connect(bEdit, &QLineEdit::editingFinished, updateColor);

  // --- Show Grid Axes ---
  auto* gridCheck = new QCheckBox(widget);
  {
    QSignalBlocker blocker(gridCheck);
    gridCheck->setChecked(showGridAxes());
  }
  layout->addRow("Show Grid Axes", gridCheck);

  // --- Axis Titles ---
  auto* titleGroup = new QGroupBox("Axis Titles", widget);
  auto* titleLayout = new QFormLayout(titleGroup);

  auto* xTitleEdit = new QLineEdit(xTitle(), widget);
  auto* yTitleEdit = new QLineEdit(yTitle(), widget);
  auto* zTitleEdit = new QLineEdit(zTitle(), widget);
  titleLayout->addRow("X Title", xTitleEdit);
  titleLayout->addRow("Y Title", yTitleEdit);
  titleLayout->addRow("Z Title", zTitleEdit);
  titleGroup->setVisible(showGridAxes());
  layout->addRow(titleGroup);

  QObject::connect(gridCheck, &QCheckBox::toggled, [this, titleGroup](bool on) {
    setShowGridAxes(on);
    titleGroup->setVisible(on);
  });
  QObject::connect(xTitleEdit, &QLineEdit::editingFinished,
                   [this, xTitleEdit]() { setXTitle(xTitleEdit->text()); });
  QObject::connect(yTitleEdit, &QLineEdit::editingFinished,
                   [this, yTitleEdit]() { setYTitle(yTitleEdit->text()); });
  QObject::connect(zTitleEdit, &QLineEdit::editingFinished,
                   [this, zTitleEdit]() { setZTitle(zTitleEdit->text()); });

  return widget;
}

bool OutlineSink::showGridAxes() const
{
  return m_showGridAxes;
}

void OutlineSink::setShowGridAxes(bool show)
{
  m_showGridAxes = show;
  m_gridAxes->SetVisibility(
    (visibility() && m_showGridAxes) ? 1 : 0);
  emit renderNeeded();
}

QString OutlineSink::xTitle() const
{
  return m_xTitle;
}

void OutlineSink::setXTitle(const QString& title)
{
  m_xTitle = title;
  m_gridAxes->SetXTitle(title.toUtf8().data());
  emit renderNeeded();
}

QString OutlineSink::yTitle() const
{
  return m_yTitle;
}

void OutlineSink::setYTitle(const QString& title)
{
  m_yTitle = title;
  m_gridAxes->SetYTitle(title.toUtf8().data());
  emit renderNeeded();
}

QString OutlineSink::zTitle() const
{
  return m_zTitle;
}

void OutlineSink::setZTitle(const QString& title)
{
  m_zTitle = title;
  m_gridAxes->SetZTitle(title.toUtf8().data());
  emit renderNeeded();
}

QJsonObject OutlineSink::serialize() const
{
  auto json = LegacyModuleSink::serialize();
  double rgb[3];
  m_property->GetColor(rgb);
  json["colorR"] = rgb[0];
  json["colorG"] = rgb[1];
  json["colorB"] = rgb[2];
  json["showGridAxes"] = m_showGridAxes;
  json["xTitle"] = m_xTitle;
  json["yTitle"] = m_yTitle;
  json["zTitle"] = m_zTitle;
  return json;
}

bool OutlineSink::deserialize(const QJsonObject& json)
{
  if (!LegacyModuleSink::deserialize(json)) {
    return false;
  }
  if (json.contains("colorR")) {
    setColor(json["colorR"].toDouble(), json["colorG"].toDouble(),
             json["colorB"].toDouble());
  }
  if (json.contains("showGridAxes")) {
    setShowGridAxes(json["showGridAxes"].toBool());
  }
  if (json.contains("xTitle")) {
    setXTitle(json["xTitle"].toString());
  }
  if (json.contains("yTitle")) {
    setYTitle(json["yTitle"].toString());
  }
  if (json.contains("zTitle")) {
    setZTitle(json["zTitle"].toString());
  }
  return true;
}

} // namespace pipeline
} // namespace tomviz
