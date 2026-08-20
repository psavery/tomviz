/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizScalarOpacityAnimation_h
#define tomvizScalarOpacityAnimation_h

#include "ModuleAnimation.h"
#include "OpacityInterpolation.h"

#include "Utilities.h"
#include "pipeline/data/VolumeData.h"
#include "pipeline/sinks/VolumeSink.h"

#include <vtkNew.h>
#include <vtkPiecewiseFunction.h>

namespace tomviz {

/// Morphs a volume between two captured opacity curves, so a feature can
/// dissolve into view over a stretch of the animation.
///
/// The blended curve is handed to the sink separately rather than written
/// into the function the histogram editor owns: playback would otherwise
/// leave a curve the user hand-authored replaced by a sampled one, and a
/// morph that was interrupted would leave it that way for good.
class ScalarOpacityAnimation : public ModuleAnimation
{
  Q_OBJECT

public:
  ScalarOpacityAnimation(pipeline::VolumeSink* sink,
                         vtkPiecewiseFunction* from, vtkPiecewiseFunction* to)
    : ModuleAnimation(sink)
  {
    if (from) {
      m_from->DeepCopy(from);
    }
    if (to) {
      m_to->DeepCopy(to);
    }
  }

  ~ScalarOpacityAnimation() override
  {
    // Hand the volume back to the curve in the histogram editor.
    if (auto* target = sink()) {
      target->setAnimatedScalarOpacity(nullptr);
    }
  }

  /// True if @a node has a scalar opacity worth animating. Only volume
  /// rendering reads one; the surface sinks have a flat opacity instead,
  /// which OpacityAnimation covers.
  static bool supports(pipeline::Node* node)
  {
    return qobject_cast<pipeline::VolumeSink*>(node) != nullptr;
  }

  pipeline::VolumeSink* sink()
  {
    return qobject_cast<pipeline::VolumeSink*>(baseNode.data());
  }

  vtkPiecewiseFunction* startCurve() { return m_from; }
  vtkPiecewiseFunction* stopCurve() { return m_to; }

  QString type() const override { return "scalarOpacity"; }

  QJsonObject serialize() const override
  {
    QJsonObject json;
    json["from"] = tomviz::serialize(m_from.Get());
    json["to"] = tomviz::serialize(m_to.Get());
    return json;
  }

  void onTimeChanged() override
  {
    auto* target = sink();
    if (!timeKeeper() || !target) {
      return;
    }

    // Both curves are read over the volume's own window, which is what
    // makes two curves captured at different times comparable.
    double range[2] = { 0.0, 1.0 };
    auto volume = target->volumeData();
    if (volume && volume->isValid()) {
      auto volumeRange = volume->colorMapRange();
      range[0] = volumeRange[0];
      range[1] = volumeRange[1];
    }

    interpolateOpacity(m_from, m_to, progress(), range, m_current);
    target->setAnimatedScalarOpacity(m_current);
  }

private:
  vtkNew<vtkPiecewiseFunction> m_from;
  vtkNew<vtkPiecewiseFunction> m_to;
  vtkNew<vtkPiecewiseFunction> m_current;
};

} // namespace tomviz

#endif
