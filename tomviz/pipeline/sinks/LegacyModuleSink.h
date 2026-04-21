/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineLegacyModuleSink_h
#define tomvizPipelineLegacyModuleSink_h

#include "SinkNode.h"

#include <QJsonObject>

#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtkWeakPointer.h>

#include <memory>

class QWidget;
class vtkPiecewiseFunction;
class vtkPlane;
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
class LegacyModuleSink : public SinkNode
{
  Q_OBJECT

public:
  LegacyModuleSink(QObject* parent = nullptr);
  ~LegacyModuleSink() override;

  QIcon icon() const override;
  QIcon actionIcon() const override;
  void triggerAction() override;

  /// Set up the visualization pipeline for the given view.
  /// Call this before the pipeline is executed.
  virtual bool initialize(vtkSMViewProxy* view);

  /// Tear down the visualization pipeline (remove props from renderer).
  virtual bool finalize();

  /// Access the view proxy and render view.
  vtkSMViewProxy* view() const;

  bool visibility() const;
  virtual void setVisibility(bool visible);

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

  QJsonObject serialize() const override;
  bool deserialize(const QJsonObject& json) override;

  /// Push active color/opacity maps into the VTK pipeline. Subclasses
  /// that need a color map should override this.
  virtual void updateColorMap();

  /// Called when lightweight metadata (units, label, spacing, origin)
  /// changes on an upstream port without a full pipeline re-execution.
  /// Subclasses can override to update annotations/UI.  Default is a no-op.
  virtual void onMetadataChanged();

  /// Clipping plane support. Subclasses that clip geometry/volumes
  /// should override these. Default implementations are no-ops.
  virtual void addClippingPlane(vtkPlane* plane);
  virtual void removeClippingPlane(vtkPlane* plane);

  /// Return sibling LegacyModuleSinks connected to the same upstream
  /// OutputPort as this sink's given input port (excluding this node).
  QList<LegacyModuleSink*> siblingSinks(
    const QString& inputPortName) const;

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

  /// Serialize @a activeScalarsIdx as a scalar-array name (legacy
  /// shape). Writes "tomviz::DefaultScalars" for -1; otherwise looks
  /// up the Nth array on the sink's current VolumeData. Falls back to
  /// the sentinel if the VolumeData isn't available.
  QString activeScalarsToName(int activeScalarsIdx) const;

  /// Read "activeScalars" from @a json (legacy saved a scalar-array
  /// name string, newer saves may use an int index) and set the
  /// sink's int index accordingly. Names that can't be resolved yet
  /// (because the sink hasn't consumed data) are stashed in
  /// m_pendingActiveScalarsName for the subclass's applyActiveScalars
  /// to pick up on the next consume; call resolvePendingActiveScalar()
  /// at apply time.
  void readActiveScalars(const QJsonObject& json, int& activeScalarsIdx);

  /// Resolve any pending scalar-array name against the sink's current
  /// VolumeData. If the name matches an array, update @a activeScalarsIdx
  /// and clear the pending name. No-op when no pending name is set.
  void resolvePendingActiveScalar(int& activeScalarsIdx);

private:
  /// Reset camera on first consume if no other sink has rendered to this view.
  void resetCameraIfFirstSink();

  bool m_visible = true;
  bool m_firstConsume = true;
  bool m_metadataConnected = false;
  vtkWeakPointer<vtkSMViewProxy> m_viewProxy;
  vtkWeakPointer<vtkPVRenderView> m_renderView;

  // Color map state
  bool m_useDetachedColorMap = false;
  vtkSmartPointer<vtkSMProxy> m_detachedColorMap;
  vtkNew<vtkPiecewiseFunction> m_detachedGradientOpacity;
  std::weak_ptr<VolumeData> m_volumeData;

protected:
  /// Pending scalar-array name waiting to be resolved against the
  /// sink's VolumeData (populated by readActiveScalars when the saved
  /// state is a name the VolumeData doesn't carry yet).
  QString m_pendingActiveScalarsName;
};

} // namespace pipeline
} // namespace tomviz

#endif
