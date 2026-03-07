/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineVolumeStatsSink_h
#define tomvizPipelineVolumeStatsSink_h

#include "tomviz_pipeline_export.h"

#include "SinkNode.h"

namespace tomviz {
namespace pipeline {

/// Sink node that computes basic statistics on an input volume:
/// min, max, mean, and voxel count.
class TOMVIZ_PIPELINE_EXPORT VolumeStatsSink : public SinkNode
{
  Q_OBJECT

public:
  VolumeStatsSink(QObject* parent = nullptr);
  ~VolumeStatsSink() override = default;

  double min() const;
  double max() const;
  double mean() const;
  int voxelCount() const;
  bool hasResults() const;

protected:
  bool consume(const QMap<QString, PortData>& inputs) override;

private:
  double m_min = 0.0;
  double m_max = 0.0;
  double m_mean = 0.0;
  int m_voxelCount = 0;
  bool m_hasResults = false;
};

} // namespace pipeline
} // namespace tomviz

#endif
