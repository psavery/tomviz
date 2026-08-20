/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizScalarOpacityAnimation_h
#define tomvizScalarOpacityAnimation_h

#include "CameraViewpoints.h"
#include "ModuleAnimation.h"
#include "OpacityInterpolation.h"

#include "Utilities.h"
#include "pipeline/data/VolumeData.h"
#include "pipeline/sinks/VolumeSink.h"

#include <QJsonArray>

#include <vtkNew.h>
#include <vtkPiecewiseFunction.h>
#include <vtkSmartPointer.h>

namespace tomviz {

/// An opacity curve captured for one point of the animation. The anchor
/// is a viewpoint index, resolved to a time through the camera path's
/// stops, so retiming a leg moves the curve change with it. With no
/// camera path, anchor 0 is the start of the animation and anything
/// later is the end.
struct OpacityKeyframe
{
  int anchor = 0;
  vtkSmartPointer<vtkPiecewiseFunction> curve;
};

/// Morphs a volume through a sequence of opacity curves, one keyed to
/// each chosen viewpoint, so a feature can dissolve away leg by leg as
/// the camera flies.
///
/// One animation holds the whole sequence rather than one animation per
/// leg: independent morphs on the same volume would each write the
/// volume's opacity every tick, and whichever wrote last would win even
/// while another one's leg was playing.
///
/// The blended curve is handed to the sink separately rather than
/// written into the function the histogram editor owns: playback would
/// otherwise leave a curve the user hand-authored replaced by a sampled
/// one, and a morph that was interrupted would leave it that way for
/// good.
class ScalarOpacityAnimation : public ModuleAnimation
{
  Q_OBJECT

public:
  ScalarOpacityAnimation(pipeline::VolumeSink* sink,
                         const QList<OpacityKeyframe>& keyframes)
    : ModuleAnimation(sink)
  {
    for (const auto& keyframe : keyframes) {
      if (!keyframe.curve || keyframe.curve->GetSize() == 0) {
        continue;
      }
      OpacityKeyframe copy;
      copy.anchor = keyframe.anchor;
      copy.curve = vtkSmartPointer<vtkPiecewiseFunction>::New();
      copy.curve->DeepCopy(keyframe.curve);
      m_keyframes.append(copy);
    }
    std::sort(m_keyframes.begin(), m_keyframes.end(),
              [](const OpacityKeyframe& a, const OpacityKeyframe& b) {
                return a.anchor < b.anchor;
              });
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

  const QList<OpacityKeyframe>& keyframes() const { return m_keyframes; }

  QString type() const override { return "scalarOpacity"; }

  QString describeParameters() const override
  {
    return QString("opacity curve, %1 keyframes").arg(m_keyframes.size());
  }

  QJsonObject serialize() const override
  {
    QJsonArray array;
    for (const auto& keyframe : m_keyframes) {
      QJsonObject entry;
      entry["anchor"] = keyframe.anchor;
      entry["curve"] = tomviz::serialize(keyframe.curve.Get());
      array.append(entry);
    }
    QJsonObject json;
    json["keyframes"] = array;
    return json;
  }

  void onTimeChanged() override
  {
    auto* target = sink();
    if (!timeKeeper() || !target || m_keyframes.isEmpty()) {
      return;
    }

    auto& viewpoints = CameraViewpoints::instance();
    const double t = progress();

    // The keyframe pair whose window contains the current time. Before
    // the first keyframe the first curve holds; after the last, the last.
    int next = 0;
    while (next < m_keyframes.size() &&
           viewpoints.anchorTime(m_keyframes[next].anchor) <= t) {
      ++next;
    }

    if (next == 0) {
      m_current->DeepCopy(m_keyframes.first().curve);
    } else if (next == m_keyframes.size()) {
      m_current->DeepCopy(m_keyframes.last().curve);
    } else {
      const auto& from = m_keyframes[next - 1];
      const auto& to = m_keyframes[next];
      const double start = viewpoints.anchorTime(from.anchor);
      const double stop = viewpoints.anchorTime(to.anchor);
      const double u = stop > start ? (t - start) / (stop - start) : 1.0;

      // Both curves are read over the volume's own window, which is what
      // makes two curves captured at different times comparable.
      double range[2] = { 0.0, 1.0 };
      auto volume = target->volumeData();
      if (volume && volume->isValid()) {
        auto volumeRange = volume->colorMapRange();
        range[0] = volumeRange[0];
        range[1] = volumeRange[1];
      }

      interpolateOpacity(from.curve, to.curve, u, range, m_current);
    }

    target->setAnimatedScalarOpacity(m_current);
  }

private:
  QList<OpacityKeyframe> m_keyframes;
  vtkNew<vtkPiecewiseFunction> m_current;
};

} // namespace tomviz

#endif
