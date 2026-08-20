/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizCameraViewpoints_h
#define tomvizCameraViewpoints_h

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>

#include <vtkSmartPointer.h>

#include <array>

class vtkCamera;
class vtkCameraInterpolator;

namespace tomviz {

/// A saved camera position, plus how an animation leaves it for the next
/// one.
struct Viewpoint
{
  std::array<double, 3> position = { 0, 0, 1 };
  std::array<double, 3> focalPoint = { 0, 0, 0 };
  std::array<double, 3> viewUp = { 0, 1, 0 };
  double viewAngle = 30.0;
  double parallelScale = 1.0;
  bool parallelProjection = false;

  /// How long the segment leaving this viewpoint runs, relative to the
  /// other segments. The last viewpoint has no segment leaving it, so
  /// its value is unused.
  double duration = 1.0;

  /// Ease in and out of that segment, so the camera slows to a stop at
  /// each end instead of rounding the corner at full speed.
  bool eased = true;

  /// A small PNG of the view this was saved from. "Viewpoint 3" says
  /// nothing about which view it is; the picture does. Captured when the
  /// viewpoint is saved, because it cannot be regenerated later without
  /// moving the camera there and back.
  QByteArray thumbnail;

  void readFrom(vtkCamera* camera);
  void applyTo(vtkCamera* camera) const;

  QJsonObject serialize() const;
  static Viewpoint deserialize(const QJsonObject& json);
};

/// The camera viewpoints saved for the current session, and the timing
/// that turns them into a path.
///
/// The list outlives the Animation Helper dialog, which is created once
/// and only hidden, and is saved with the state file, so it lives here
/// rather than in the dialog.
class CameraViewpoints : public QObject
{
  Q_OBJECT

public:
  static CameraViewpoints& instance();

  const QList<Viewpoint>& viewpoints() const { return m_viewpoints; }
  int size() const { return m_viewpoints.size(); }
  const Viewpoint& at(int index) const { return m_viewpoints.at(index); }

  void append(const Viewpoint& viewpoint);
  void replace(int index, const Viewpoint& viewpoint);
  void removeAt(int index);
  void move(int from, int to);
  void clear();

  /// The progress at which each viewpoint is reached, with every segment
  /// weighted by its duration. The first entry is always 0 and the last
  /// always 1. Empty for fewer than two viewpoints, which have no path
  /// between them.
  QList<double> stops() const;

  /// Map animation progress in [0, 1] onto a time along the path, also
  /// in [0, 1], applying each segment's easing. Segment boundaries are
  /// fixed points, so this only changes the pacing within a segment,
  /// never which viewpoint is reached when.
  double remapProgress(double progress) const;

  /// How far through one leg of the path the animation is, given its
  /// overall progress. Before the leg starts this is 0 and after it ends
  /// it is 1, so a visualization bound to a leg does its whole sweep
  /// while the camera flies that leg and holds still either side of it.
  ///
  /// A leg that is not there any more (the viewpoints were deleted, or
  /// the file was saved with more of them) gives back `progress`
  /// unchanged, so the animation still runs over the whole timeline
  /// rather than freezing at one value.
  double segmentProgress(double progress, int segment) const;

  /// Move `camera` to time `t` in [0, 1] along the path. Interpolates
  /// position, focal point, view up, view angle and parallel scale;
  /// whether the camera is in parallel projection is left alone, since
  /// switching it mid-path would jump rather than move.
  void interpolate(double t, vtkCamera* camera);

  QJsonObject serialize() const;
  bool deserialize(const QJsonObject& json);

signals:
  /// The list or its timing changed. The path has to be rebuilt.
  void changed();

private:
  Q_DISABLE_COPY(CameraViewpoints)

  CameraViewpoints() = default;

  void rebuildInterpolator();

  QList<Viewpoint> m_viewpoints;
  // Held by pointer rather than by value so the header does not have to
  // pull in the interpolator to destroy it.
  vtkSmartPointer<vtkCameraInterpolator> m_interpolator;
  bool m_interpolatorStale = true;
};

} // namespace tomviz

#endif
