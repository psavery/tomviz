/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineDefaultExecutor_h
#define tomvizPipelineDefaultExecutor_h

#include "tomviz_pipeline_export.h"

#include "PipelineExecutor.h"

namespace tomviz {
namespace pipeline {

class TOMVIZ_PIPELINE_EXPORT DefaultExecutor : public PipelineExecutor
{
  Q_OBJECT

public:
  DefaultExecutor(QObject* parent = nullptr);
  ~DefaultExecutor() override = default;

  void execute(const QList<Node*>& nodes, Pipeline* pipeline) override;
  void cancel() override;
  bool isRunning() const override;

private:
  bool m_cancelRequested = false;
  bool m_running = false;
};

} // namespace pipeline
} // namespace tomviz

#endif
