/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineSinkNode_h
#define tomvizPipelineSinkNode_h

#include "Node.h"
#include "PortData.h"

#include <QMap>
#include <QString>

#include <memory>

namespace tomviz {
namespace pipeline {

class SinkNode : public Node
{
  Q_OBJECT

public:
  SinkNode(QObject* parent = nullptr);
  ~SinkNode() override = default;

  InputPort* addInput(const QString& name, PortTypes acceptedTypes);

  bool execute() override;

protected:
  virtual bool consume(const QMap<QString, PortData>& inputs) = 0;

  /// Hook fired before consume() in both execute() and
  /// onIntermediateData() paths. Default no-op.
  virtual void prepareConsume(const QMap<QString, PortData>& /*inputs*/)
  {}

  /// Hook fired after consume() in both execute() and
  /// onIntermediateData() paths. Default no-op.
  virtual void postConsume(bool /*success*/) {}

  /// Hook fired when an input port loses its incoming link.
  /// Default no-op.
  virtual void onInputDisconnected(InputPort* /*port*/) {}

  /// Opt-in for sinks that own GL / render-window / SM-proxy state:
  /// return true to run the whole prepareConsume()/consume()/
  /// postConsume() trio on the GUI thread, instead of marshaling each
  /// individual access from the worker thread (see ThreadUtils.h).
  /// Default false — the trio runs inline on the pipeline worker thread.
  virtual bool consumeOnGuiThread() const { return false; }

private slots:
  void onIntermediateData();

private:
  void connectUpstreamIntermediate(InputPort* port);

  /// Run the prepareConsume()/consume()/postConsume() trio, marshaling
  /// it to the GUI thread (blocking the worker until it completes) when
  /// consumeOnGuiThread() is true. Returns consume()'s success.
  bool runConsume(const QMap<QString, PortData>& inputs);

  /// Retained handles to the input payloads seen during the last
  /// successful consume(). Two effects: the shared_ptr keeps the
  /// upstream port's heap PortData alive (so the producer port's
  /// hasData() stays honest for UI inspection — a value-copy of
  /// PortData alone would let the heap object die and m_weak expire,
  /// even though the heavy payload inside the std::any survives), and
  /// it keeps the heavy payload pinned so the visualization works
  /// after the executor's in-flight refs have dropped. Cleared on
  /// disconnect.
  QMap<QString, std::shared_ptr<PortData>> m_retainedInputs;
};

} // namespace pipeline
} // namespace tomviz

#endif
