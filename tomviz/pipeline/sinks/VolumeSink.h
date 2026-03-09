/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineVolumeSink_h
#define tomvizPipelineVolumeSink_h

#include "tomviz_pipeline_export.h"

#include "LegacyModuleSink.h"

#include <vtkNew.h>

class vtkColorTransferFunction;
class vtkPiecewiseFunction;
class vtkPlane;
class vtkSmartVolumeMapper;
class vtkVolume;
class vtkVolumeProperty;

namespace tomviz {
namespace pipeline {

/// Volume rendering visualization sink.
/// Matches the old ModuleVolume VTK pipeline: SmartVolumeMapper + Volume +
/// VolumeProperty with jittering, lighting, blending, interpolation, gradient
/// opacity, clipping planes, and external transfer function support.
class TOMVIZ_PIPELINE_EXPORT VolumeSink : public LegacyModuleSink
{
  Q_OBJECT

public:
  VolumeSink(QObject* parent = nullptr);
  ~VolumeSink() override;

  bool isColorMapNeeded() const override;

  bool initialize(vtkSMViewProxy* view) override;
  bool finalize() override;

  QJsonObject serialize() const override;
  bool deserialize(const QJsonObject& json) override;

  /// Lighting toggle (shade on/off).
  bool lighting() const;
  void setLighting(bool enabled);

  /// Phong lighting parameters.
  double ambient() const;
  void setAmbient(double value);
  double diffuse() const;
  void setDiffuse(double value);
  double specular() const;
  void setSpecular(double value);
  double specularPower() const;
  void setSpecularPower(double value);

  /// Blending mode (vtkVolumeMapper enum).
  int blendingMode() const;
  void setBlendingMode(int mode);

  /// Interpolation type (VTK_NEAREST/LINEAR_INTERPOLATION).
  int interpolationType() const;
  void setInterpolationType(int type);

  /// Ray jittering for noise reduction.
  bool jittering() const;
  void setJittering(bool enabled);

  /// Solidity (1 / ScalarOpacityUnitDistance).
  double solidity() const;
  void setSolidity(double value);

  /// Set external color transfer function (overrides built-in default).
  void setColorTransferFunction(vtkColorTransferFunction* ctf);
  /// Set external opacity transfer function (overrides built-in default).
  void setOpacityTransferFunction(vtkPiecewiseFunction* otf);
  /// Set gradient opacity function for gradient-based opacity mapping.
  void setGradientOpacityFunction(vtkPiecewiseFunction* gof);

  /// Clipping plane support.
  void addClippingPlane(vtkPlane* plane);
  void removeClippingPlane(vtkPlane* plane);
  void removeAllClippingPlanes();

protected:
  bool consume(const QMap<QString, PortData>& inputs) override;

private:
  vtkNew<vtkSmartVolumeMapper> m_volumeMapper;
  vtkNew<vtkVolume> m_volume;
  vtkNew<vtkVolumeProperty> m_volumeProperty;
  vtkNew<vtkPiecewiseFunction> m_defaultOpacity;
  vtkNew<vtkColorTransferFunction> m_defaultColor;
  vtkNew<vtkPiecewiseFunction> m_gradientOpacity;
  bool m_hasCustomOpacity = false;
  bool m_hasCustomColor = false;
};

} // namespace pipeline
} // namespace tomviz

#endif
