/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "AnimationSerializer.h"

#include "ActiveObjects.h"
#include "Utilities.h"
#include "animations/CameraViewpoints.h"
#include "animations/ModuleAnimations.h"

#include <pqAnimationCue.h>
#include <pqAnimationManager.h>
#include <pqAnimationScene.h>
#include <pqPVApplicationCore.h>
#include <pqRenderView.h>

#include <vtkSMPropertyHelper.h>
#include <vtkSMProxy.h>
#include <vtkSMRenderViewProxy.h>

namespace tomviz {

namespace {

bool hasCameraOrbitCue()
{
  auto* core = pqPVApplicationCore::instance();
  auto* manager = core ? core->animationManager() : nullptr;
  auto* scene = manager ? manager->getActiveScene() : nullptr;
  if (!scene) {
    return false;
  }
  for (auto* cue : scene->getCues()) {
    if (cue->getSMName().startsWith("CameraAnimationCue")) {
      return true;
    }
  }
  return false;
}

vtkSMProxy* animationSceneProxy()
{
  auto* core = pqPVApplicationCore::instance();
  auto* manager = core ? core->animationManager() : nullptr;
  auto* scene = manager ? manager->getActiveScene() : nullptr;
  return scene ? scene->getProxy() : nullptr;
}

} // anonymous namespace

void AnimationSerializer::save(QJsonObject& doc)
{
  auto animation = CameraViewpoints::instance().serialize();

  auto modules =
    ModuleAnimations::instance().serialize(ActiveObjects::instance().pipeline());
  animation["modules"] = modules["modules"];

  if (auto* scene = animationSceneProxy()) {
    animation["numberOfFrames"] =
      vtkSMPropertyHelper(scene, "NumberOfFrames").GetAsInt();
  }

  // An orbit is a ParaView cue rather than anything of ours, so it is
  // recognised the same way the helper recognises it: by the name its
  // cue was registered under.
  animation["cameraOrbit"] = hasCameraOrbitCue();

  doc["animation"] = animation;
}

void AnimationSerializer::restore(const QJsonObject& doc,
                                  pipeline::Pipeline* pipeline)
{
  auto animation = doc["animation"].toObject();

  auto& viewpoints = CameraViewpoints::instance();
  if (!viewpoints.deserialize(animation)) {
    viewpoints.clear();
  }

  ModuleAnimations::instance().deserialize(animation, pipeline);

  // Restore what was driving the camera. Only one thing can, and the
  // viewpoint path wins if a file somehow claims both.
  auto* renderView = ActiveObjects::instance().activePqRenderView();
  viewpoints.stopFlight();
  if (renderView) {
    clearCameraCues(renderView->getRenderViewProxy());
    if (animation["flying"].toBool()) {
      viewpoints.startFlight(renderView);
    } else if (animation["cameraOrbit"].toBool()) {
      createCameraOrbit(renderView->getRenderViewProxy());
    }
  }

  // Loading a state file builds a fresh animation scene, which the view
  // restore leaves on the frame count used for newly loaded data. A file
  // that recorded its own count knows better; one that did not keeps it.
  int numberOfFrames = animation["numberOfFrames"].toInt(0);
  if (numberOfFrames > 0) {
    setAnimationNumberOfFrames(numberOfFrames);
  }
}

} // namespace tomviz
