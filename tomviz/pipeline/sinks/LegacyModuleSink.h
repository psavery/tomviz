/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineLegacyModuleSink_h
#define tomvizPipelineLegacyModuleSink_h

#include "tomviz_pipeline_export.h"

#include "SinkNode.h"

#include <QJsonObject>

#include <vtkWeakPointer.h>

class vtkPVRenderView;
class vtkSMViewProxy;

namespace tomviz {
namespace pipeline {

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

  virtual QJsonObject serialize() const;
  virtual bool deserialize(const QJsonObject& json);

signals:
  void visibilityChanged(bool visible);
  void renderNeeded();

protected:
  /// Get the ParaView render view (convenience).
  vtkPVRenderView* renderView() const;

  /// Helper for subclass consume() implementations.
  bool validateInput(const QMap<QString, PortData>& inputs,
                     const QString& portName) const;

private:
  bool m_visible = true;
  vtkWeakPointer<vtkSMViewProxy> m_viewProxy;
  vtkWeakPointer<vtkPVRenderView> m_renderView;
};

} // namespace pipeline
} // namespace tomviz

#endif
