/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineNode_h
#define tomvizPipelineNode_h

#include "tomviz_pipeline_export.h"

#include "NodeState.h"
#include "PortType.h"

#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace tomviz {
namespace pipeline {

class InputPort;
class OutputPort;

class TOMVIZ_PIPELINE_EXPORT Node : public QObject
{
  Q_OBJECT

public:
  Node(QObject* parent = nullptr);
  ~Node() override = default;

  QString label() const;
  void setLabel(const QString& label);

  NodeState state() const;
  void markStale();
  void markCurrent();

  bool hasBreakpoint() const;
  void setBreakpoint(bool enabled);

  QList<InputPort*> inputPorts() const;
  QList<OutputPort*> outputPorts() const;
  InputPort* inputPort(const QString& name) const;
  OutputPort* outputPort(const QString& name) const;

  bool allInputsCurrent() const;
  bool anyInputStale() const;

  QList<Node*> upstreamNodes() const;
  QList<Node*> downstreamNodes() const;

  /// Arbitrary metadata properties.
  void setProperty(const QString& key, const QVariant& value);
  QVariant property(const QString& key,
                    const QVariant& defaultValue = {}) const;
  QVariantMap properties() const;

  virtual QIcon icon() const;

  /// Optional action button icon shown on the node card (e.g. visibility
  /// toggle). Return a null QIcon to indicate no action button.
  virtual QIcon actionIcon() const;

  /// Called when the user clicks the action button.
  virtual void triggerAction();

  virtual bool execute();

signals:
  void stateChanged(NodeState state);
  void labelChanged();
  void breakpointChanged();
  void executionStarted();
  void executionFinished(bool success);

protected:
  InputPort* addInputPort(const QString& name, PortTypes acceptedTypes);
  OutputPort* addOutputPort(const QString& name, PortType type);

private:
  QString m_label;
  NodeState m_state = NodeState::New;
  bool m_breakpoint = false;
  QList<InputPort*> m_inputPorts;
  QList<OutputPort*> m_outputPorts;
  QVariantMap m_properties;
};

} // namespace pipeline
} // namespace tomviz

#endif
