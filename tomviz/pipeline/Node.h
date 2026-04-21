/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineNode_h
#define tomvizPipelineNode_h

#include "NodeExecState.h"
#include "NodeState.h"
#include "PortType.h"

#include <QIcon>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <atomic>

class QWidget;

namespace tomviz {
namespace pipeline {

class InputPort;
class OutputPort;

class Node : public QObject
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
  /// Set the node's state without any side effects on neighbors.
  /// markStale() cascades through the DAG; this is the escape hatch
  /// for loaders / restorers that already know the full state of the
  /// graph and don't want to re-cascade.
  void setStateNoCascade(NodeState state);

  NodeExecState execState() const;

  bool isEditing() const;
  void setEditing(bool editing);

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

  /// Return the total number of progress steps. Zero means indeterminate.
  int totalProgressSteps() const;
  void setTotalProgressSteps(int steps);

  /// Current progress step (0 to totalProgressSteps).
  int progressStep() const;
  void setProgressStep(int step);

  /// Optional progress message shown in the progress dialog title.
  QString progressMessage() const;
  void setProgressMessage(const QString& message);

  /// Reset progress state (steps, step, message) to defaults.
  void resetProgress();

  /// If the node has custom progress UI, return it parented to the given
  /// widget. Otherwise return nullptr and a default QProgressBar will be used.
  virtual QWidget* getCustomProgressWidget(QWidget*) const { return nullptr; }

  /// Whether the node supports canceling mid-execution via cancelExecution().
  bool supportsCancelingMidExecution() const;

  /// Whether the node supports early completion via completeExecution().
  bool supportsCompletionMidExecution() const;

  /// True if cancellation has been requested (thread-safe).
  bool isCanceled() const;

  /// True if early completion has been requested (thread-safe).
  bool isCompleted() const;

public slots:
  /// Request cancellation of an in-progress execution.
  virtual void cancelExecution();

  /// Request early completion of an in-progress execution.
  virtual void completeExecution();

  /// Declare that the effective type of an output port follows the effective
  /// type of an input port.  Only meaningful when the output port is declared
  /// as ImageData.  If no explicit mapping is set for an ImageData output,
  /// it defaults to following the first ImageData input port.
  void setTypeInferenceSource(const QString& outputPortName,
                              const QString& inputPortName);

  /// Recompute effective types for all output ports based on current input
  /// connections and inference rules.  Emits effectiveTypeChanged on any
  /// output whose effective type changed.
  void recomputeEffectiveTypes();

  /// True if any input link is invalid (type-incompatible).
  bool hasInvalidInputLinks() const;

  /// Serialize this node's persistent state to JSON (label, breakpoint,
  /// properties, typeInferenceSources, output/input port state).
  /// Subclasses should call the base and then add their own fields.
  virtual QJsonObject serialize() const;

  /// Apply JSON produced by serialize() to this node. Returns false on
  /// unrecoverable errors. Subclasses that create dynamic ports should
  /// create them before calling up to Node::deserialize so per-port
  /// state can be applied.
  virtual bool deserialize(const QJsonObject& json);

signals:
  void stateChanged(NodeState state);
  void execStateChanged(NodeExecState state);
  void editingChanged(bool editing);
  void labelChanged();
  void breakpointChanged();
  void progressStepChanged(int step);
  void totalProgressStepsChanged(int steps);
  void progressMessageChanged(const QString& message);
  void executionCanceled();
  void executionCompleted();

protected:
  void setSupportsCancel(bool b);
  void setSupportsCompletion(bool b);
  void resetExecutionFlags();

  void setExecState(NodeExecState state);
  InputPort* addInputPort(const QString& name, PortTypes acceptedTypes);
  OutputPort* addOutputPort(const QString& name, PortType type);
  void addOutputPort(OutputPort* port);

private:
  QString m_label;
  NodeState m_state = NodeState::New;
  NodeExecState m_execState = NodeExecState::Idle;
  bool m_editing = false;
  bool m_breakpoint = false;
  QList<InputPort*> m_inputPorts;
  QList<OutputPort*> m_outputPorts;
  QVariantMap m_properties;
  QMap<QString, QString> m_typeInferenceSources;
  int m_totalProgressSteps = 0;
  int m_progressStep = 0;
  QString m_progressMessage;
  bool m_supportsCancel = false;
  bool m_supportsCompletion = false;
  std::atomic<bool> m_canceled{false};
  std::atomic<bool> m_completed{false};
};

} // namespace pipeline
} // namespace tomviz

#endif
