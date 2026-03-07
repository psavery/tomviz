/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "VolumeData.h"

#include <vtkDataArray.h>
#include <vtkImageData.h>
#include <vtkPointData.h>

namespace tomviz {
namespace pipeline {

VolumeData::VolumeData() = default;

VolumeData::VolumeData(vtkSmartPointer<vtkImageData> imageData)
  : m_imageData(imageData)
{}

VolumeData::~VolumeData() = default;

vtkImageData* VolumeData::imageData() const
{
  return m_imageData;
}

void VolumeData::setImageData(vtkSmartPointer<vtkImageData> data)
{
  m_imageData = data;
}

bool VolumeData::isValid() const
{
  return m_imageData != nullptr;
}

std::array<int, 3> VolumeData::dimensions() const
{
  std::array<int, 3> dims = { 0, 0, 0 };
  if (m_imageData) {
    m_imageData->GetDimensions(dims.data());
  }
  return dims;
}

std::array<double, 3> VolumeData::spacing() const
{
  std::array<double, 3> s = { 1.0, 1.0, 1.0 };
  if (m_imageData) {
    m_imageData->GetSpacing(s.data());
  }
  return s;
}

void VolumeData::setSpacing(double x, double y, double z)
{
  if (m_imageData) {
    m_imageData->SetSpacing(x, y, z);
  }
}

std::array<double, 3> VolumeData::origin() const
{
  std::array<double, 3> o = { 0.0, 0.0, 0.0 };
  if (m_imageData) {
    m_imageData->GetOrigin(o.data());
  }
  return o;
}

void VolumeData::setOrigin(double x, double y, double z)
{
  if (m_imageData) {
    m_imageData->SetOrigin(x, y, z);
  }
}

std::array<int, 6> VolumeData::extent() const
{
  std::array<int, 6> e = { 0, 0, 0, 0, 0, 0 };
  if (m_imageData) {
    m_imageData->GetExtent(e.data());
  }
  return e;
}

std::array<double, 6> VolumeData::bounds() const
{
  std::array<double, 6> b = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
  if (m_imageData) {
    m_imageData->GetBounds(b.data());
  }
  return b;
}

vtkDataArray* VolumeData::scalars() const
{
  if (m_imageData) {
    return m_imageData->GetPointData()->GetScalars();
  }
  return nullptr;
}

int VolumeData::numberOfComponents() const
{
  auto* s = scalars();
  return s ? s->GetNumberOfComponents() : 0;
}

std::array<double, 2> VolumeData::scalarRange() const
{
  std::array<double, 2> range = { 0.0, 0.0 };
  auto* s = scalars();
  if (s) {
    s->GetRange(range.data());
  }
  return range;
}

QString VolumeData::label() const
{
  return m_label;
}

void VolumeData::setLabel(const QString& label)
{
  m_label = label;
}

QString VolumeData::units() const
{
  return m_units;
}

void VolumeData::setUnits(const QString& units)
{
  m_units = units;
}

} // namespace pipeline
} // namespace tomviz
