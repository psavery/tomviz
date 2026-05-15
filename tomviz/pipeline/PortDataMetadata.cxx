/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PortDataMetadata.h"

#include "ColorMap.h"
#include "PortType.h"
#include "data/VolumeData.h"

#include <QMetaObject>
#include <QObject>
#include <QThread>

namespace tomviz {
namespace pipeline {

void applySegmentationColorMap(VolumeData& vol)
{
  // Always build a fresh colormap from the output's actual scalars —
  // the label set can change between executions, so inheriting from
  // upstream or reusing a prior preset would yield wrong colors.
  vol.initColorMap();
  auto preset = tomviz::buildSegmentationPreset(vol.scalars());
  if (!preset.isEmpty()) {
    tomviz::applyPresetToProxy(preset, vol.colorMap());
  }
}

namespace {

/// Volume-specific inheritance: copy colormap + gradient opacity from
/// the first volume-typed input that has them onto each volume-typed
/// output that doesn't. LabelMap outputs are handled separately —
/// they always get a freshly-built segmentation colormap from their
/// own scalars, never an inherited one.
void inheritVolumeMetadata(const QMap<QString, PortData>& inputs,
                           const QMap<QString, PortData>& outputs)
{
  for (auto outIt = outputs.constBegin(); outIt != outputs.constEnd();
       ++outIt) {
    if (!isVolumeType(outIt.value().type())) {
      continue;
    }
    VolumeDataPtr outVolume;
    try {
      outVolume = outIt.value().value<VolumeDataPtr>();
    } catch (const std::bad_any_cast&) {
      continue;
    }
    if (!outVolume) {
      continue;
    }
    if (outIt.value().type() == PortType::LabelMap) {
      applySegmentationColorMap(*outVolume);
      continue;
    }
    if (outVolume->hasColorMap()) {
      // Already initialized (e.g. the producer reused an existing
      // VolumeData) — leave its colormap alone.
      continue;
    }
    for (auto inIt = inputs.constBegin(); inIt != inputs.constEnd();
         ++inIt) {
      if (!isVolumeType(inIt.value().type())) {
        continue;
      }
      VolumeDataPtr inVolume;
      try {
        inVolume = inIt.value().value<VolumeDataPtr>();
      } catch (const std::bad_any_cast&) {
        continue;
      }
      if (!inVolume || !inVolume->hasColorMap()) {
        continue;
      }
      outVolume->initColorMap();
      outVolume->copyColorMapFrom(*inVolume);
      break;
    }
  }
}

} // namespace

void inheritOutputMetadata(QObject* threadOwner,
                           const QMap<QString, PortData>& inputs,
                           const QMap<QString, PortData>& outputs)
{
  auto apply = [&]() {
    inheritVolumeMetadata(inputs, outputs);
    // Future payload types with inheritable metadata: dispatch here.
  };

  if (!threadOwner || QThread::currentThread() == threadOwner->thread()) {
    apply();
  } else {
    QMetaObject::invokeMethod(threadOwner, apply,
                              Qt::BlockingQueuedConnection);
  }
}

} // namespace pipeline
} // namespace tomviz
