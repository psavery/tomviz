/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SphereSource.h"

#include "OutputPort.h"
#include "PortData.h"
#include "data/VolumeData.h"

#include <vtkFloatArray.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPointData.h>

#include <cmath>

namespace tomviz {
namespace pipeline {

SphereSource::SphereSource(QObject* parent) : SourceNode(parent)
{
  addOutput("volume", PortType::ImageData);
  setLabel("Sphere Source");
}

void SphereSource::setDimensions(int x, int y, int z)
{
  m_dimensions[0] = x;
  m_dimensions[1] = y;
  m_dimensions[2] = z;
}

void SphereSource::setRadiusFraction(double fraction)
{
  m_radiusFraction = fraction;
}

bool SphereSource::execute()
{
  setExecState(NodeExecState::Running);

  int nx = m_dimensions[0];
  int ny = m_dimensions[1];
  int nz = m_dimensions[2];

  vtkNew<vtkImageData> imageData;
  imageData->SetDimensions(nx, ny, nz);
  imageData->SetSpacing(1.0, 1.0, 1.0);
  imageData->SetOrigin(0.0, 0.0, 0.0);

  vtkNew<vtkFloatArray> scalars;
  scalars->SetName("ImageScalars");
  scalars->SetNumberOfTuples(static_cast<vtkIdType>(nx) * ny * nz);

  double center[3] = { (nx - 1) / 2.0, (ny - 1) / 2.0, (nz - 1) / 2.0 };
  int minDim = std::min({ nx, ny, nz });
  double radius = m_radiusFraction * minDim;

  for (int k = 0; k < nz; ++k) {
    for (int j = 0; j < ny; ++j) {
      for (int i = 0; i < nx; ++i) {
        double dx = i - center[0];
        double dy = j - center[1];
        double dz = k - center[2];
        double dist = std::sqrt(dx * dx + dy * dy + dz * dz) - radius;
        vtkIdType idx = static_cast<vtkIdType>(k) * ny * nx + j * nx + i;
        scalars->SetValue(idx, static_cast<float>(dist));
      }
    }
  }

  imageData->GetPointData()->SetScalars(scalars);

  auto volume = std::make_shared<VolumeData>(imageData.Get());
  volume->setLabel("Sphere");

  setOutputData("volume",
                PortData(std::any(volume), PortType::ImageData));

  setExecState(NodeExecState::Idle);
  return true;
}

} // namespace pipeline
} // namespace tomviz
