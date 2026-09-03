/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineLabelMapSink_h
#define tomvizPipelineLabelMapSink_h

#include "VolumeSink.h"

#include <QJsonObject>

#include <memory>

namespace tomviz {
namespace pipeline {

class LabelMapData;
using LabelMapDataPtr = std::shared_ptr<LabelMapData>;

/// Label map visualization sink.
///
/// Renders through the same VTK pipeline as VolumeSink - a label map is
/// a volume, and everything about mappers, bricking, clipping and
/// lighting carries over unchanged. What differs is what the user gets
/// to control: the continuous-scalar settings are hidden, and in their
/// place the panel lists the labels present in the data, each with a
/// checkbox that shows or hides it and a color the user can pick.
///
/// Those choices live in the payload's LabelTable, which this sink
/// projects onto the color and scalar-opacity transfer functions. It
/// never edits those functions as state of their own: they are rebuilt
/// from the table on every edit and on every execution.
class LabelMapSink : public VolumeSink
{
  Q_OBJECT

public:
  LabelMapSink(QObject* parent = nullptr);
  ~LabelMapSink() override;

  QIcon icon() const override;

  QWidget* createSinkPropertiesWidget(QWidget* parent) override;

  QJsonObject serialize() const override;
  bool deserialize(const QJsonObject& json) override;

  /// The label map payload this sink last consumed. When the port
  /// carries a plain volume whose values read as labels, this is a view
  /// this sink built over the same voxels instead. Null when there is no
  /// data yet, or the data cannot be read as labels at all.
  LabelMapDataPtr labelMap() const;

  /// Project the label table onto whichever color map this sink renders
  /// through - the payload's shared one, or its own detached one - and
  /// request a redraw.
  void applyLabels();

signals:
  /// Emitted when the set of labels may have changed: after each
  /// execution, and after the color map this sink uses is swapped.
  void labelsChanged();

protected:
  bool consume(const QMap<QString, PortData>& inputs) override;

private:
  /// A label view over a port that is not typed as a label map. Held
  /// here rather than published, because the port's payload is shared
  /// with every other sink reading it and they are entitled to go on
  /// seeing a plain volume.
  LabelMapDataPtr m_adopted;

  /// An adopted table read from a state file, held until there is data
  /// to attach it to. Deserialize runs before the sink has consumed
  /// anything, so there is no view to put it in yet.
  QJsonObject m_restoredAdopted;
};

} // namespace pipeline
} // namespace tomviz

#endif
