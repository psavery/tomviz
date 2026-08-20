/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "LabelMapSink.h"

#include "LabelTableWidget.h"
#include "VolumeSinkWidget.h"
#include "InputPort.h"
#include "data/LabelMapData.h"

#include <QIcon>
#include <QVBoxLayout>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>
#include <vtkVolumeProperty.h>

namespace tomviz {
namespace pipeline {

LabelMapSink::LabelMapSink(QObject* parent) : VolumeSink(parent)
{
  setLabel("Label Map");

  // Any volume is accepted, not just a port already typed as a label
  // map. A segmentation loaded from a file arrives as an ordinary
  // volume, because the reader cannot know the numbers are labels, and
  // refusing it would leave no way to say so. What the menu offers is
  // filtered by canInterpretAsLabelMap() instead; a link made by hand
  // to something continuous is allowed and simply lists no labels.
  inputPorts()[0]->setAcceptedTypes(PortType::ImageData);

  // Switching the detached color map on hands us a brand new transfer
  // function pair, which starts out as ParaView's default ramp. Project
  // the labels onto it so the render does not flash a rainbow.
  connect(this, &LegacyModuleSink::colorMapChanged, this, [this]() {
    applyLabels();
    emit labelsChanged();
  });
}

LabelMapSink::~LabelMapSink() = default;

QIcon LabelMapSink::icon() const
{
  return QIcon(QStringLiteral(":/pipeline/port_labelmap.svg"));
}

LabelMapDataPtr LabelMapSink::labelMap() const
{
  if (auto labels = labelMapData(volumeData())) {
    return labels;
  }
  return m_adopted;
}

bool LabelMapSink::consume(const QMap<QString, PortData>& inputs)
{
  if (!VolumeSink::consume(inputs)) {
    return false;
  }

  auto volume = volumeData();
  if (labelMapData(volume)) {
    // The port carries a real label map, so its own table is the one to
    // use and any view built for an earlier input is stale.
    m_adopted.reset();
  } else if (canInterpretAsLabelMap(volume)) {
    // Read a plain volume as labels. The view wraps the same voxels, so
    // nothing is copied, but the table and the transfer functions it
    // drives belong to this sink alone.
    if (!m_adopted || m_adopted->imageData() != volume->imageData()) {
      m_adopted = std::make_shared<LabelMapData>(
        vtkSmartPointer<vtkImageData>(volume->imageData()));
      // A table restored from a state file describes voxels this sink
      // had not seen yet. Now that it has them, hand it over; the
      // refresh below reconciles it against what is actually there.
      if (!m_restoredAdopted.isEmpty()) {
        m_adopted->labels().deserialize(m_restoredAdopted);
        m_restoredAdopted = QJsonObject();
      }
    }
    // Label bands would otherwise be written into the color map the
    // port's plain volume shares with every other sink reading it,
    // turning an ordinary Volume module next door into a segmentation.
    if (!useDetachedColorMap()) {
      setUseDetachedColorMap(true);
    }
  } else {
    m_adopted.reset();
  }

  // Linear interpolation between labels 2 and 6 samples 4, colored as
  // whichever label owns that value. The control is hidden in this
  // sink's panel, so pin the setting rather than trust what a state
  // file or a base-class default left behind.
  setInterpolationType(VTK_NEAREST_INTERPOLATION);

  // The producing node normally refreshes the table before publishing
  // (see inheritOutputMetadata), but a payload can reach us without
  // having gone through that - a source node's own output, or a state
  // file's restored data. The rescan is cached on the image's
  // modification time, so repeating it here costs nothing.
  if (auto labels = labelMap()) {
    labels->refreshLabels();
    applyLabels();
  }

  emit labelsChanged();
  return true;
}

QJsonObject LabelMapSink::serialize() const
{
  auto json = VolumeSink::serialize();
  // A real label map carries its table in its own payload. An adopted
  // one has nowhere else to put it: the port's payload is a plain
  // volume shared with other sinks, so the colors and names the user
  // gave these labels live here or nowhere.
  if (m_adopted) {
    json["adoptedLabelMap"] = m_adopted->labels().serialize();
  } else if (!m_restoredAdopted.isEmpty()) {
    // Restored but never consumed, so it is still owed a home.
    json["adoptedLabelMap"] = m_restoredAdopted;
  }
  return json;
}

bool LabelMapSink::deserialize(const QJsonObject& json)
{
  if (!VolumeSink::deserialize(json)) {
    return false;
  }
  m_restoredAdopted = json.value("adoptedLabelMap").toObject();
  return true;
}

void LabelMapSink::applyLabels()
{
  auto labels = labelMap();
  if (!labels || labels->labels().isEmpty()) {
    return;
  }

  if (useDetachedColorMap()) {
    LabelMapData::applyLabelsToProxy(labels->labels(), colorMap());
  } else {
    labels->applyLabels();
  }

  updateColorMap();
  emit renderNeeded();
}

QWidget* LabelMapSink::createSinkPropertiesWidget(QWidget* parent)
{
  auto* widget = VolumeSink::createSinkPropertiesWidget(parent);
  auto* volumeWidget = qobject_cast<VolumeSinkWidget*>(widget);
  if (!volumeWidget) {
    return widget;
  }

  volumeWidget->setCategoricalMode(true);

  // Directly under the form holding Active Scalars, which is what
  // decides where the labels are read from in the first place.
  auto* table = new LabelTableWidget(this, volumeWidget);
  volumeWidget->mainLayout()->insertWidget(1, table);

  return widget;
}

} // namespace pipeline
} // namespace tomviz
