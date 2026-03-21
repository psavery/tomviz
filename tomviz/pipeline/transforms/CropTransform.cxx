/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "CropTransform.h"

#include "EditTransformWidget.h"
#include "InputPort.h"
#include "data/VolumeData.h"
#include "SelectVolumeWidget.h"

#include <vtkExtractVOI.h>
#include <vtkImageData.h>
#include <vtkNew.h>

#include <QHBoxLayout>
#include <QPointer>

#include <limits>

namespace {

class CropWidget : public tomviz::pipeline::EditTransformWidget
{
  Q_OBJECT

public:
  CropWidget(tomviz::pipeline::CropTransform* op,
             tomviz::pipeline::VolumeDataPtr volumeData, QWidget* p)
    : tomviz::pipeline::EditTransformWidget(p), m_operator(op)
  {
    double displayPosition[3] = { 0, 0, 0 };
    double origin[3];
    double spacing[3];
    int extent[6];
    auto* imageData = volumeData->imageData();
    imageData->GetOrigin(origin);
    imageData->GetSpacing(spacing);
    imageData->GetExtent(extent);
    if (op->cropBounds()[0] == std::numeric_limits<int>::min()) {
      op->setCropBounds(extent);
    }
    m_widget = new tomviz::SelectVolumeWidget(
      origin, spacing, extent, op->cropBounds(), displayPosition, this);
    QHBoxLayout* hboxlayout = new QHBoxLayout;
    hboxlayout->addWidget(m_widget);
    setLayout(hboxlayout);
  }

  void applyChangesToOperator() override
  {
    int bounds[6];
    m_widget->getExtentOfSelection(bounds);
    if (m_operator) {
      m_operator->setCropBounds(bounds);
    }
  }

private:
  QPointer<tomviz::pipeline::CropTransform> m_operator;
  tomviz::SelectVolumeWidget* m_widget;
};

} // namespace

#include "CropTransform.moc"

namespace tomviz {
namespace pipeline {

CropTransform::CropTransform(QObject* parent) : TransformNode(parent)
{
  addInput("volume", PortType::ImageData);
  addOutput("output", PortType::ImageData);
  setLabel("Crop");

  for (int i = 0; i < 6; ++i) {
    m_bounds[i] = std::numeric_limits<int>::min();
  }
}

QIcon CropTransform::icon() const
{
  return QIcon(":/pqWidgets/Icons/pqExtractGrid.svg");
}

void CropTransform::setCropBounds(const int bounds[6])
{
  for (int i = 0; i < 6; ++i) {
    m_bounds[i] = bounds[i];
  }
}

bool CropTransform::hasPropertiesWidget() const
{
  return true;
}

bool CropTransform::propertiesWidgetNeedsInput() const
{
  return true;
}

EditTransformWidget* CropTransform::createPropertiesWidget(QWidget* parent)
{
  auto* inputPort = this->inputPorts()[0];
  if (!inputPort || !inputPort->hasData()) {
    return nullptr;
  }

  VolumeDataPtr vol;
  try {
    vol = inputPort->data().value<VolumeDataPtr>();
  } catch (const std::bad_any_cast&) {
    return nullptr;
  }

  if (!vol || !vol->isValid()) {
    return nullptr;
  }

  return new CropWidget(this, vol, parent);
}

QMap<QString, PortData> CropTransform::transform(
  const QMap<QString, PortData>& inputs)
{
  QMap<QString, PortData> result;

  auto inputVolume = inputs["volume"].value<VolumeDataPtr>();
  if (!inputVolume || !inputVolume->isValid()) {
    return result;
  }

  vtkNew<vtkExtractVOI> extractor;
  extractor->SetVOI(m_bounds);
  extractor->SetInputData(inputVolume->imageData());
  extractor->Update();
  extractor->UpdateWholeExtent();

  vtkNew<vtkImageData> outputImage;
  outputImage->ShallowCopy(extractor->GetOutputDataObject(0));

  auto volume = std::make_shared<VolumeData>(outputImage.Get());
  volume->setLabel(inputVolume->label());
  volume->setUnits(inputVolume->units());
  volume->setTiltAngles(inputVolume->tiltAngles());

  result["output"] = PortData(std::any(volume), inputs["volume"].type());
  return result;
}

} // namespace pipeline
} // namespace tomviz
