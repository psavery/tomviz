/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizVolumeSinkWidget_h
#define tomvizVolumeSinkWidget_h

#include <QScopedPointer>
#include <QWidget>

class QFormLayout;

/**
 * \brief UI layer of VolumeSink.
 *
 * Signals are forwarded to the actuators on the mapper in VolumeSink.
 * This class is intended to contain only logic related to UI actions.
 */

namespace Ui {
class VolumeSinkWidget;
class VolumeLightingForm;
} // namespace Ui

namespace tomviz {

class VolumeSinkWidget : public QWidget
{
  Q_OBJECT

public:
  VolumeSinkWidget(QWidget* parent_ = nullptr);
  ~VolumeSinkWidget() override;

  //@{
  /**
   * UI update methods. The actual model state is stored in VolumeSink (either
   * in the mapper or serialized), so the UI needs to be updated if the state
   * changes or when constructing the UI.
   */
  void setActiveScalars(const QString& scalars);
  void setJittering(const bool enable);
  void setBlendingMode(const int mode);
  void setInterpolationType(const int type);
  void setLighting(const bool enable);
  void setAmbient(const double value);
  void setDiffuse(const double value);
  void setSpecular(const double value);
  void setSpecularPower(const double value);
  void setVolumetricScattering(const double value);
  void setShadowReach(const double value);
  void setAnisotropy(const double value);
  void setSmoothNormals(const bool enable);
  /// Highlight the preset button matching the sink's current lighting
  /// state (VolumeSink::LightingPreset); -1 (Custom) unchecks them all.
  void setActiveLightingPreset(const int preset);
  /// Enable or disable the presets and controls that cast volumetric
  /// shadows. When disabling, @a reason is shown as their tool tip.
  void setScatteringAvailable(const bool available, const QString& reason);
  void setTransferMode(const int transferMode);
  void setSolidity(const double value);
  void setRgbaMappingAllowed(const bool allowed);
  void setUseRgbaMapping(const bool b);
  void setRgbaMappingMin(const double value);
  void setRgbaMappingMax(const double value);
  void setRgbaMappingSliderRange(const double range[2]);
  void setRgbaMappingCombineComponents(const bool b);
  void setRgbaMappingComponentOptions(const QStringList& list);
  void setRgbaMappingComponent(const QString& component);
  void setAllowMultiVolume(const bool allow);
  void setEnableAllowMultiVolume(const bool enable);
  QFormLayout* formLayout();
  //@}

signals:
  //@{
  /**
   * Forwarded signals.
   */
  void jitteringToggled(const bool state);
  void blendingChanged(const int state);
  void interpolationChanged(const int state);
  void lightingToggled(const bool state);
  void ambientChanged(const double value);
  void diffuseChanged(const double value);
  void specularChanged(const double value);
  void specularPowerChanged(const double value);
  void volumetricScatteringChanged(const double value);
  void shadowReachChanged(const double value);
  void anisotropyChanged(const double value);
  void smoothNormalsToggled(const bool state);
  void lightingPresetClicked(const int preset);
  void transferModeChanged(const int mode);
  void solidityChanged(const double value);
  void useRgbaMappingToggled(const bool b);
  void rgbaMappingCombineComponentsToggled(const bool b);
  void rgbaMappingMinChanged(const double value);
  void rgbaMappingMaxChanged(const double value);
  void rgbaMappingComponentChanged(const QString& component);
  void allowMultiVolumeToggled(const bool state);
  //@}

private:
  VolumeSinkWidget(const VolumeSinkWidget&) = delete;
  void operator=(const VolumeSinkWidget&) = delete;

  bool usesLighting(const int mode) const;
  /// Controls that only do anything when volumetric scattering is available.
  QList<QWidget*> scatteringWidgets() const;

  QScopedPointer<Ui::VolumeSinkWidget> m_ui;
  QScopedPointer<Ui::VolumeLightingForm> m_uiLighting;

private slots:
  void onBlendingChanged(const int mode);
  void onRgbaMappingMinChanged(double value);
  void onRgbaMappingMaxChanged(double value);
};
} // namespace tomviz
#endif
