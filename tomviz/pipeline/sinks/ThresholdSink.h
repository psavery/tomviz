/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineThresholdSink_h
#define tomvizPipelineThresholdSink_h

#include "tomviz_pipeline_export.h"

#include "LegacyModuleSink.h"

#include <QPointer>
#include <QSet>
#include <vtkSmartPointer.h>

class vtkImageData;
class vtkPlane;
class vtkSMProxy;
class vtkSMSourceProxy;

namespace tomviz {

class ThresholdSinkWidget;

namespace pipeline {

/// Threshold visualization sink. Shows the region of a volume within
/// a specified scalar range, with configurable appearance properties
/// matching the old ModuleThreshold.
class TOMVIZ_PIPELINE_EXPORT ThresholdSink : public LegacyModuleSink
{
  Q_OBJECT

public:
  ThresholdSink(QObject* parent = nullptr);
  ~ThresholdSink() override;

  QIcon icon() const override;

  void setVisibility(bool visible) override;
  bool isColorMapNeeded() const override;

  bool initialize(vtkSMViewProxy* view) override;
  bool finalize() override;

  QJsonObject serialize() const override;
  bool deserialize(const QJsonObject& json) override;

  double lowerThreshold() const;
  double upperThreshold() const;
  void setThresholdRange(double lower, double upper);

  double opacity() const;
  void setOpacity(double value);

  double specular() const;
  void setSpecular(double value);

  /// Representation: VTK_SURFACE (2), VTK_WIREFRAME (1), VTK_POINTS (0).
  int representation() const;
  void setRepresentation(int rep);

  /// String-based representation: "Surface", "Wireframe", "Points".
  QString representationString() const;
  void setRepresentationString(const QString& rep);

  void addClippingPlane(vtkPlane* plane) override;
  void removeClippingPlane(vtkPlane* plane) override;

  /// Toggle scalar color mapping.
  bool mapScalars() const;
  void setMapScalars(bool map);

  /// Cached scalar range from the last consume().
  void scalarRange(double range[2]) const;

  QWidget* createPropertiesWidget(QWidget* parent) override;

protected:
  bool consume(const QMap<QString, PortData>& inputs) override;
  void updateColorMap() override;
  void updatePanel();

private slots:
  void onScalarArrayChanged();

private:
  /// Build or update the SM proxy pipeline on the main thread.
  void setupOrUpdatePipeline();
  void applyClippingPlanes();

  vtkSmartPointer<vtkSMSourceProxy> m_producer;
  vtkSmartPointer<vtkSMSourceProxy> m_thresholdFilter;
  vtkSmartPointer<vtkSMProxy> m_thresholdRepresentation;
  vtkSmartPointer<vtkImageData> m_pendingImage;
  double m_lower = 0.0;
  double m_upper = 1.0;
  bool m_rangeSet = false;
  bool m_mapScalars = true;
  double m_scalarRange[2] = { 0.0, 1.0 };
  QPointer<ThresholdSinkWidget> m_controllers;
  QSet<vtkPlane*> m_clippingPlanes;
};

} // namespace pipeline
} // namespace tomviz

#endif
