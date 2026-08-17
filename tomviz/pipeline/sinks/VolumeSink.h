/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineVolumeSink_h
#define tomvizPipelineVolumeSink_h

#include "LegacyModuleSink.h"

#include <QPointer>
#include <QTimer>
#include <vtkNew.h>
#include <vtkSmartPointer.h>

class QComboBox;

class vtkCallbackCommand;
class vtkColorTransferFunction;
class vtkImageData;
class vtkMultiBlockDataSet;
class vtkMultiBlockVolumeMapper;
class vtkPiecewiseFunction;
class vtkPlane;
class vtkVolume;
class vtkVolumeProperty;

namespace tomviz {
namespace pipeline {

class SmartVolumeMapper;

/// Volume rendering visualization sink.
/// Matches the old ModuleVolume VTK pipeline: SmartVolumeMapper + Volume +
/// VolumeProperty with jittering, lighting, blending, interpolation, gradient
/// opacity, clipping planes, and external transfer function support.
class VolumeSink : public LegacyModuleSink
{
  Q_OBJECT

public:
  VolumeSink(QObject* parent = nullptr);
  ~VolumeSink() override;

  QIcon icon() const override;

  void setVisibility(bool visible) override;
  bool isColorMapNeeded() const override;

  bool initialize(vtkSMViewProxy* view) override;
  bool finalize() override;

  void clearVisualization() override;

  QWidget* createSinkPropertiesWidget(QWidget* parent) override;

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

  /// Volumetric scattering blending (0 = surface shading only,
  /// 2 = fully volumetric with shadows). GPU ray-cast mapper parameter.
  double volumetricScattering() const;
  void setVolumetricScattering(double value);

  /// Shadow reach (GlobalIlluminationReach): 0 = local shadows only,
  /// 1 = shadows across the whole volume.
  double shadowReach() const;
  void setShadowReach(double value);

  /// Scattering anisotropy: 0 = isotropic, +1 = forward, -1 = backward.
  double scatteringAnisotropy() const;
  void setScatteringAnisotropy(double value);

  /// Compute shading normals from the opacity transfer function instead of
  /// the raw scalar gradient (reduces noise-induced shading artifacts).
  bool smoothNormals() const;
  void setSmoothNormals(bool enabled);

  /// Named bundles of the lighting parameters above. The int values match
  /// the preset button indices in VolumeSinkWidget.
  enum class LightingPreset
  {
    Custom = -1,
    Flat = 0,
    Simple,
    Soft,
    Full
  };
  void applyLightingPreset(LightingPreset preset);
  /// The preset matching the current parameter values, or Custom.
  LightingPreset currentLightingPreset() const;

  /// Whether volumetric scattering can be bounded well enough to be offered
  /// here. False for bricked volumes (their per-brick mappers are out of
  /// reach) and when the Volume.AllowVolumetricScattering setting is off.
  bool scatteringSupported() const;

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

  /// Active scalar array index (-1 = use default active scalars).
  int activeScalars() const;
  void setActiveScalars(int index);

  /// Clipping plane support.
  void addClippingPlane(vtkPlane* plane) override;
  void removeClippingPlane(vtkPlane* plane) override;
  void removeAllClippingPlanes();

  void onMetadataChanged() override;

signals:
  void interpolationTypeChanged(int type);
  void lightingChanged(bool enabled);
  /// Emitted whenever any lighting parameter changes; the properties widget
  /// uses this to refresh its sliders and the active preset highlight.
  void lightingStateChanged();

protected:
  bool consume(const QMap<QString, PortData>& inputs) override;
  void updateColorMap() override;

private:
  void applyActiveScalars();
  void populateScalarsCombo();

  // Choose between the single-texture SmartVolumeMapper and the bricked
  // vtkMultiBlockVolumeMapper based on whether image exceeds the GPU's
  // GL_MAX_3D_TEXTURE_SIZE, and point m_volume at the right one.
  void updateMapperForInput(vtkImageData* image);
  // GPU's max 3-D texture size (queried from the render window when one is
  // available; a conservative fallback otherwise).
  int maxTextureSize() const;
  // Emit a warning that clipping has no effect on a bricked (over-cap) volume.
  void warnClippingUnsupported() const;
  // Log why scatteringSupported() is false.
  void warnScatteringUnsupported() const;
  // User-facing version of the above, for the properties widget. Empty when
  // scattering is supported.
  QString scatteringUnavailableReason() const;
  // Ask before switching to a preset that casts volumetric shadows, unless
  // the user has opted out. Returns false if they declined.
  bool confirmScatteringPreset(LightingPreset preset, QWidget* parent) const;
  // Called at the end of every render of this sink's view. Arms the settle
  // timer after unlit interactive frames (so the scattering look is always
  // restored, even when no end-of-interaction still render arrives) and
  // re-renders when the guard's measurement says a sharper frame now fits
  // the time budget.
  void onRenderFinished();

  vtkNew<SmartVolumeMapper> m_volumeMapper;
  // Used only when a volume exceeds the texture-size cap; renders the volume
  // as resident per-brick textures (see VolumeBricking.h).
  vtkNew<vtkMultiBlockVolumeMapper> m_multiBlockMapper;
  vtkSmartPointer<vtkMultiBlockDataSet> m_brickedVolume;
  bool m_usingMultiBlock = false;
  vtkNew<vtkVolume> m_volume;
  vtkNew<vtkVolumeProperty> m_volumeProperty;
  vtkNew<vtkPiecewiseFunction> m_gradientOpacity;

  // Watches render completion for onRenderFinished().
  vtkNew<vtkCallbackCommand> m_refinementObserver;
  unsigned long m_refinementObserverId = 0;
  // Requests the still render that restores the scattering look once
  // interactive frames stop arriving; armed in onRenderFinished().
  QTimer m_settleTimer;

  QPointer<QComboBox> m_scalarsCombo;
  int m_activeScalars = -1;
  // True once nearest-interpolation + lighting have been auto-applied
  // for a LabelMap input on this instance. Latches so user overrides
  // stick across subsequent executions.
  bool m_labelMapDefaultsApplied = false;
};

} // namespace pipeline
} // namespace tomviz

#endif
