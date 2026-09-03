/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizCameraAnimation_h
#define tomvizCameraAnimation_h

#include "CameraViewpoints.h"
#include "ModuleAnimation.h"

#include <pqRenderView.h>

#include <vtkCamera.h>
#include <vtkRenderer.h>
#include <vtkSMRenderViewProxy.h>

namespace tomviz {

/// Flies the camera through the saved viewpoints as the animation plays.
///
/// The camera orbit is a real ParaView camera cue, but this is not: a
/// keyframe cue interpolates at a constant rate, and the whole point of
/// the viewpoint path is that each segment can have its own length and
/// ease in and out. So the camera is driven directly, like the other
/// tomviz animations. Only one of the two should exist at a time, or
/// they fight over the camera on every tick.
class CameraAnimation : public ModuleAnimation
{
  Q_OBJECT

public:
  CameraAnimation(pqRenderView* view) : ModuleAnimation(nullptr), m_view(view)
  {
    if (m_view) {
      connect(m_view.data(), &QObject::destroyed, this, &QObject::deleteLater);
    }
  }

  void onTimeChanged() override
  {
    if (!timeKeeper() || !m_view) {
      return;
    }

    auto& viewpoints = CameraViewpoints::instance();
    if (viewpoints.size() < 2) {
      return;
    }

    auto* proxy = m_view->getRenderViewProxy();
    auto* camera = proxy ? proxy->GetActiveCamera() : nullptr;
    if (!camera) {
      return;
    }

    viewpoints.interpolate(viewpoints.remapProgress(progress()), camera);

    // The interpolated clipping range is blended from the saved
    // viewpoints, which can clip the data at positions in between.
    if (auto* renderer = proxy->GetRenderer()) {
      renderer->ResetCameraClippingRange();
    }

    m_view->render();
  }

private:
  QPointer<pqRenderView> m_view;
};

} // namespace tomviz

#endif
