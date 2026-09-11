/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizThresholdRangeWidget_h
#define tomvizThresholdRangeWidget_h

#include "CustomPythonNodeWidget.h"
#include "PortData.h"

#include <QMap>

namespace tomviz {

class DoubleSliderWidget;

/// Lower/upper threshold sliders that span the input's actual scalar
/// range, for the Binary Threshold operator. Fresh nodes start from the
/// first visible Threshold visualization on the same data, if there is
/// one, so the segmentation matches what the user has been looking at.
class ThresholdRangeWidget : public pipeline::CustomPythonNodeWidget
{
  Q_OBJECT

public:
  ThresholdRangeWidget(const QMap<QString, pipeline::PortData>& inputs,
                       QWidget* parent = nullptr);

  void getValues(QMap<QString, QVariant>& map) override;
  void setValues(const QMap<QString, QVariant>& map) override;
  void setNodeContext(pipeline::Node* node,
                      pipeline::Pipeline* pipeline) override;
  void setJSONDescription(const QString& json) override;

private:
  void setThresholds(double lower, double upper);

  DoubleSliderWidget* m_lower = nullptr;
  DoubleSliderWidget* m_upper = nullptr;
  double m_dataRange[2] = { 0.0, 1.0 };
  // Range of a visible Threshold visualization on the same data
  bool m_haveSuggestion = false;
  double m_suggested[2] = { 0.0, 1.0 };
  // Defaults declared in the JSON, to recognise a never-edited node
  QMap<QString, double> m_jsonDefaults;
};

} // namespace tomviz

#endif
