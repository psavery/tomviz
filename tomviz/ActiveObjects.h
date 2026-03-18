/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizActiveObjects_h
#define tomvizActiveObjects_h

#include <QObject>

class pqRenderView;
class pqTimeKeeper;
class pqView;
class vtkSMSessionProxyManager;
class vtkSMViewProxy;

namespace tomviz {

namespace pipeline {
class Pipeline;
class Node;
class OutputPort;
} // namespace pipeline

/// ActiveObjects keeps track of active objects in tomviz.
/// This is similar to pqActiveObjects in ParaView, however it tracks objects
/// relevant to tomviz.
class ActiveObjects : public QObject
{
  Q_OBJECT

public:
  /// Returns reference to the singleton instance.
  static ActiveObjects& instance();

  /// Returns the active view.
  vtkSMViewProxy* activeView() const;

  /// Returns the active view as a pqView object.
  pqView* activePqView() const;

  /// Returns the active pqRenderView object.
  pqRenderView* activePqRenderView() const;

  /// Returns the vtkSMSessionProxyManager from the active server/session.
  /// Provided here for convenience, since we need to access the proxy manager
  /// often.
  vtkSMSessionProxyManager* proxyManager() const;

  /// Returns the active time keeper
  pqTimeKeeper* activeTimeKeeper() const;

  /// Pipeline tracking
  void setActivePipeline(pipeline::Pipeline* p);
  pipeline::Pipeline* activePipeline() const;
  void setActiveNode(pipeline::Node* node);
  pipeline::Node* activeNode() const;
  void setActivePort(pipeline::OutputPort* port);
  pipeline::OutputPort* activePort() const;

public slots:
  /// Set the active view;
  void setActiveView(vtkSMViewProxy*);

  /// Create a render view if needed.
  void createRenderViewIfNeeded();

  /// Set first existing render view to be active.
  void setActiveViewToFirstRenderView();

  /// Renders all views.
  void renderAllViews();

  /// Edit interaction modes for all data sources
  void enableTranslation(bool b);
  void enableRotation(bool b);
  void enableScaling(bool b);

  /// Set whether to enable time series animations.
  void enableTimeSeriesAnimations(bool b);
  void setShowTimeSeriesLabel(bool b);

  bool translationEnabled() const { return m_translationEnabled; }
  bool rotationEnabled() const { return m_rotationEnabled; }
  bool scalingEnabled() const { return m_scalingEnabled; }

  bool timeSeriesAnimationsEnabled() const
  {
    return m_timeSeriesAnimationsEnabled;
  }
  bool showTimeSeriesLabel() const { return m_showTimeSeriesLabel; }

signals:
  /// Fired whenever the active view changes.
  void viewChanged(vtkSMViewProxy*);

  /// Fired when interaction modes change
  void translationStateChanged(bool b);
  void rotationStateChanged(bool b);
  void scalingStateChanged(bool b);

  /// Fired when time series animations enable state is changed.
  void timeSeriesAnimationsEnableStateChanged(bool b);

  /// Fired when time series label visibility changes
  void showTimeSeriesLabelChanged(bool b);

  /// Fired to set image viewer mode
  void setImageViewerMode(bool b);

  /// Fired whenever the active pipeline changes.
  void activePipelineChanged(pipeline::Pipeline*);

  /// Fired whenever the active node changes.
  void activeNodeChanged(pipeline::Node*);

  /// Fired whenever the active output port changes.
  void activePortChanged(pipeline::OutputPort*);

private slots:
  void viewChanged(pqView*);

protected:
  ActiveObjects();
  ~ActiveObjects() override;

  /// Pipeline tracking
  pipeline::Pipeline* m_activePipeline = nullptr;
  pipeline::Node* m_activeNode = nullptr;
  pipeline::OutputPort* m_activePort = nullptr;

  /// interaction states
  bool m_translationEnabled = false;
  bool m_rotationEnabled = false;
  bool m_scalingEnabled = false;

  /// Time series
  bool m_timeSeriesAnimationsEnabled = true;
  bool m_showTimeSeriesLabel = true;

private:
  Q_DISABLE_COPY(ActiveObjects)
};

inline void ActiveObjects::enableTranslation(bool b)
{
  if (m_translationEnabled == b) {
    return;
  }

  m_translationEnabled = b;
  emit translationStateChanged(b);
}

inline void ActiveObjects::enableRotation(bool b)
{
  if (m_rotationEnabled == b) {
    return;
  }

  m_rotationEnabled = b;
  emit rotationStateChanged(b);
}

inline void ActiveObjects::enableScaling(bool b)
{
  if (m_scalingEnabled == b) {
    return;
  }

  m_scalingEnabled = b;
  emit scalingStateChanged(b);
}

inline void ActiveObjects::enableTimeSeriesAnimations(bool b)
{
  if (m_timeSeriesAnimationsEnabled == b) {
    return;
  }

  m_timeSeriesAnimationsEnabled = b;
  emit timeSeriesAnimationsEnableStateChanged(b);
}

inline void ActiveObjects::setShowTimeSeriesLabel(bool b)
{
  if (m_showTimeSeriesLabel == b) {
    return;
  }

  m_showTimeSeriesLabel = b;
  emit showTimeSeriesLabelChanged(b);
}

} // namespace tomviz

#endif
