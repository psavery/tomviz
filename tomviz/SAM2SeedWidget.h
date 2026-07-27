/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizSAM2SeedWidget_h
#define tomvizSAM2SeedWidget_h

#include "pipeline/CustomPythonNodeWidget.h"
#include "pipeline/PortData.h"

#include <QMap>
#include <QScopedPointer>

namespace tomviz {

/// Custom parameters widget for the SAM 2 segmentation operator. Shows
/// the standard JSON-driven parameter form plus an input-volume slice
/// viewer: clicking the slice sets Seed X/Y, a slider picks the seed
/// slice (Seed Z), and the Z Axis choice controls the slicing
/// direction. The seed round-trips through the same seed_x/y/z
/// parameters, so serialization and external execution are unchanged.
class SAM2SeedWidget : public pipeline::CustomPythonNodeWidget
{
  Q_OBJECT

public:
  SAM2SeedWidget(const QMap<QString, pipeline::PortData>& inputs,
                 QWidget* parent = nullptr);
  ~SAM2SeedWidget() override;

  void getValues(QMap<QString, QVariant>& map) override;
  void setValues(const QMap<QString, QVariant>& map) override;

private:
  void wireForm();
  void syncSliderAndSlice();
  void refreshSlice();
  void refreshMarker();
  int sliceAxis() const;
  int effectiveSliceIndex() const;

  class Internal;
  QScopedPointer<Internal> m_internal;
};

} // namespace tomviz

#endif
