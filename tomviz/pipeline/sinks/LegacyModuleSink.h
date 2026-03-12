/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineLegacyModuleSink_h
#define tomvizPipelineLegacyModuleSink_h

#include "tomviz_pipeline_export.h"

#include "SinkNode.h"

#include <QJsonObject>

#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtkWeakPointer.h>

#include <memory>

class QWidget;
class vtkPiecewiseFunction;
class vtkPVRenderView;
class vtkSMProxy;
class vtkSMRenderViewProxy;
class vtkSMViewProxy;

namespace tomviz {
namespace pipeline {

class VolumeData;
using VolumeDataPtr = std::shared_ptr<VolumeData>;

/// Base class for visualization sink nodes that replace old Module classes.
/// Provides common view management, visibility, colormap, and serialization.
class TOMVIZ_PIPELINE_EXPORT LegacyModuleSink : public SinkNode
{
  Q_OBJECT

public:
  LegacyModuleSink(QObject* parent = nullptr);
  ~LegacyModuleSink() override;

  /// Set up the visualization pipeline for the given view.
  /// Call this before the pipeline is executed.
  virtual bool initialize(vtkSMViewProxy* view);

  /// Tear down the visualization pipeline (remove props from renderer).
  virtual bool finalize();

  /// Access the view proxy and render view.
  vtkSMViewProxy* view() const;

  bool visibility() const;
  void setVisibility(bool visible);

  virtual bool isColorMapNeeded() const;

  /// Whether this sink uses a detached (private) color map.
  bool useDetachedColorMap() const;
  void setUseDetachedColorMap(bool detached);

  /// Return active color/opacity maps (detached if toggled, else from VolumeData).
  vtkSMProxy* colorMap();
  vtkSMProxy* opacityMap();
  vtkPiecewiseFunction* gradientOpacity() const;

  /// The VolumeData this sink last consumed.
  VolumeDataPtr volumeData() const;

  /// Create a widget for editing this sink's properties. Returns nullptr by
  /// default. Caller owns the returned widget.
  virtual QWidget* createPropertiesWidget(QWidget* parent);

  bool execute() override;

  virtual QJsonObject serialize() const;
  virtual bool deserialize(const QJsonObject& json);

  /// Push active color/opacity maps into the VTK pipeline. Subclasses
  /// that need a color map should override this.
  virtual void updateColorMap();

signals:
  void visibilityChanged(bool visible);
  void renderNeeded();
  void colorMapChanged();

protected:
  /// Get the ParaView render view (convenience).
  vtkPVRenderView* renderView() const;

  /// Helper for subclass consume() implementations.
  bool validateInput(const QMap<QString, PortData>& inputs,
                     const QString& portName) const;

private:
  /// Reset camera on first consume if no other sink has rendered to this view.
  void resetCameraIfFirstSink();

  bool m_visible = true;
  bool m_firstConsume = true;
  vtkWeakPointer<vtkSMViewProxy> m_viewProxy;
  vtkWeakPointer<vtkPVRenderView> m_renderView;

  // Color map state
  bool m_useDetachedColorMap = false;
  vtkSmartPointer<vtkSMProxy> m_detachedColorMap;
  vtkNew<vtkPiecewiseFunction> m_detachedGradientOpacity;
  std::weak_ptr<VolumeData> m_volumeData;
};

} // namespace pipeline
} // namespace tomviz

#endif
