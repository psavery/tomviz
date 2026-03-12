/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "VolumeData.h"

#include <vtkColorTransferFunction.h>
#include <vtkDataArray.h>
#include <vtkImageData.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPointData.h>
#include <vtkSMPropertyHelper.h>
#include <vtkSMProxy.h>
#include <vtkSMProxyManager.h>
#include <vtkSMSessionProxyManager.h>
#include <vtkSMTransferFunctionManager.h>
#include <vtkSMTransferFunctionProxy.h>

#include <array>
#include <vector>

namespace {

double rescaleValue(double val, double oldMin, double oldMax, double newMin,
                    double newMax)
{
  if (oldMax == oldMin) {
    return newMin;
  }
  return (val - oldMin) * (newMax - newMin) / (oldMax - oldMin) + newMin;
}

void rescaleCTFNodes(vtkColorTransferFunction* lut, double newMin,
                     double newMax)
{
  auto numNodes = lut->GetSize();
  if (numNodes == 0) {
    return;
  }

  auto* dataArray = lut->GetDataPointer();
  int nodeStride = 4; // X, R, G, B
  double* firstNode = dataArray;
  double* backNode = firstNode + (numNodes - 1) * nodeStride;

  double oldMin = firstNode[0];
  double oldMax = backNode[0];

  std::vector<std::array<double, 4>> points;
  for (int i = 0; i < numNodes; ++i) {
    double* n = firstNode + i * nodeStride;
    double newX = rescaleValue(n[0], oldMin, oldMax, newMin, newMax);
    points.push_back({ newX, n[1], n[2], n[3] });
  }

  lut->RemoveAllPoints();
  for (const auto& p : points) {
    lut->AddRGBPoint(p[0], p[1], p[2], p[3]);
  }
}

void rescalePWFNodes(vtkPiecewiseFunction* pwf, double newMin, double newMax)
{
  int numNodes = pwf->GetSize();
  if (numNodes == 0) {
    return;
  }

  // Use GetNodeValue() which returns [X, Y, Midpoint, Sharpness]
  // (GetDataPointer() only returns (X,Y) pairs without midpoint/sharpness)
  double first[4], last[4];
  pwf->GetNodeValue(0, first);
  pwf->GetNodeValue(numNodes - 1, last);

  double oldMin = first[0];
  double oldMax = last[0];

  std::vector<std::array<double, 4>> points;
  for (int i = 0; i < numNodes; ++i) {
    double val[4];
    pwf->GetNodeValue(i, val);
    double newX = rescaleValue(val[0], oldMin, oldMax, newMin, newMax);
    points.push_back({ newX, val[1], val[2], val[3] });
  }

  pwf->RemoveAllPoints();
  for (const auto& p : points) {
    pwf->AddPoint(p[0], p[1], p[2], p[3]);
  }
}

} // anonymous namespace

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

bool VolumeData::hasColorMap() const
{
  return m_ctf != nullptr;
}

void VolumeData::initColorMap()
{
  if (m_ctf) {
    return; // already initialized
  }

  auto* mgr = vtkSMProxyManager::GetProxyManager();
  if (!mgr) {
    return;
  }
  auto* pxm = mgr->GetActiveSessionProxyManager();
  if (!pxm) {
    return;
  }

  static unsigned int counter = 0;
  ++counter;

  vtkNew<vtkSMTransferFunctionManager> tfmgr;
  m_colorMap = tfmgr->GetColorTransferFunction(
    QString("VolumeDataColorMap%1").arg(counter).toLatin1().data(), pxm);

  // Cache client-side VTK objects for direct manipulation
  if (m_colorMap) {
    m_ctf = vtkColorTransferFunction::SafeDownCast(
      m_colorMap->GetClientSideObject());

    auto* omap =
      vtkSMPropertyHelper(m_colorMap, "ScalarOpacityFunction").GetAsProxy();
    if (omap) {
      m_opacity =
        vtkPiecewiseFunction::SafeDownCast(omap->GetClientSideObject());
    }
  }
}

vtkSMProxy* VolumeData::colorMap()
{
  if (!m_colorMap) {
    initColorMap();
  }
  return m_colorMap;
}

vtkSMProxy* VolumeData::opacityMap()
{
  auto* cmap = colorMap();
  if (!cmap) {
    return nullptr;
  }
  return vtkSMPropertyHelper(cmap, "ScalarOpacityFunction").GetAsProxy();
}

vtkColorTransferFunction* VolumeData::colorTransferFunction() const
{
  return m_ctf;
}

vtkPiecewiseFunction* VolumeData::scalarOpacity() const
{
  return m_opacity;
}

vtkPiecewiseFunction* VolumeData::gradientOpacity() const
{
  return m_gradientOpacity;
}

void VolumeData::rescaleColorMap()
{
  if (!m_ctf || !m_opacity) {
    return;
  }
  auto range = scalarRange();
  rescaleCTFNodes(m_ctf, range[0], range[1]);
  rescalePWFNodes(m_opacity, range[0], range[1]);
}

void VolumeData::copyColorMapFrom(const VolumeData& source)
{
  if (!m_ctf || !source.m_ctf) {
    return;
  }

  // Copy color transfer function control points
  auto* srcCTF = source.m_ctf;
  if (srcCTF->GetSize() > 0) {
    m_ctf->RemoveAllPoints();
    int n = srcCTF->GetSize();
    double* data = srcCTF->GetDataPointer();
    for (int i = 0; i < n; ++i) {
      double* p = data + i * 4; // X, R, G, B
      m_ctf->AddRGBPoint(p[0], p[1], p[2], p[3]);
    }
    m_ctf->Modified();
  }

  // Copy opacity function control points
  auto* srcPWF = source.m_opacity;
  if (m_opacity && srcPWF && srcPWF->GetSize() > 0) {
    m_opacity->RemoveAllPoints();
    int n = srcPWF->GetSize();
    for (int i = 0; i < n; ++i) {
      double val[4]; // X, Y, Midpoint, Sharpness
      srcPWF->GetNodeValue(i, val);
      m_opacity->AddPoint(val[0], val[1], val[2], val[3]);
    }
    m_opacity->Modified();
  }

  // Copy gradient opacity
  auto* srcGrad = source.gradientOpacity();
  if (srcGrad && srcGrad->GetSize() > 0) {
    m_gradientOpacity->RemoveAllPoints();
    int n = srcGrad->GetSize();
    for (int i = 0; i < n; ++i) {
      double val[4]; // X, Y, Midpoint, Sharpness
      srcGrad->GetNodeValue(i, val);
      m_gradientOpacity->AddPoint(val[0], val[1], val[2], val[3]);
    }
    m_gradientOpacity->Modified();
  }
}

void VolumeData::copyAndRescaleColorMapFrom(const VolumeData& source)
{
  copyColorMapFrom(source);
  rescaleColorMap();
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
