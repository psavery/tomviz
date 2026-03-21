/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineReconstructionTransform_h
#define tomvizPipelineReconstructionTransform_h

#include "tomviz_pipeline_export.h"

#include "TransformNode.h"

namespace tomviz {
namespace pipeline {

/// Transform that performs tomographic reconstruction via unweighted
/// back projection on a tilt series input.
/// Output port is persistent (not transient).
class TOMVIZ_PIPELINE_EXPORT ReconstructionTransform : public TransformNode
{
  Q_OBJECT

public:
  ReconstructionTransform(QObject* parent = nullptr);
  ~ReconstructionTransform() override = default;

  QIcon icon() const override;

signals:
  /// Emitted after each slice is reconstructed.
  void intermediateResults(std::vector<float> resultSlice);

protected:
  QMap<QString, PortData> transform(
    const QMap<QString, PortData>& inputs) override;
};

} // namespace pipeline
} // namespace tomviz

#endif
