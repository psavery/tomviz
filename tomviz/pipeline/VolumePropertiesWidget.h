/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineVolumePropertiesWidget_h
#define tomvizPipelineVolumePropertiesWidget_h

#include "tomviz_pipeline_export.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;

namespace tomviz {
namespace pipeline {

class OutputPort;
class VolumeData;
class VolumeScalarsModel;

class TOMVIZ_PIPELINE_EXPORT VolumePropertiesWidget : public QWidget
{
  Q_OBJECT

public:
  explicit VolumePropertiesWidget(QWidget* parent = nullptr);
  ~VolumePropertiesWidget() override;

  void setOutputPort(OutputPort* port);
  OutputPort* outputPort() const;

signals:
  void volumeDataModified();

private:
  void updateData();
  void clear();
  VolumeData* volumeData() const;
  void gatherAndUpdateArraysInfo();
  static QWidget* createSectionHeader(const QString& title, QWidget* parent);
  static QString formatSize(size_t num, bool labelAsBytes = false);

  // Edit slots
  void onLabelEdited();
  void onActiveScalarsChanged(const QString& name);
  void onComponentNameEdited(const QString& name);
  void onUnitsEdited();
  void onLengthEdited(int axis);
  void onVoxelSizeEdited(int axis);
  void onOriginEdited(int axis);
  void onScalarsRenamed(const QString& oldName, const QString& newName);

  OutputPort* m_port = nullptr;
  VolumeScalarsModel* m_scalarsModel;
  QList<int> m_scalarIndexes;

  // Widgets
  QLineEdit* m_labelEdit;
  QComboBox* m_activeScalarsCombo;
  QLabel* m_componentNamesLabel;
  QComboBox* m_componentNamesCombo;
  QLabel* m_dimensionsLabel;
  QLabel* m_voxelsLabel;
  QLabel* m_memoryLabel;
  QTableView* m_scalarsTable;
  QLineEdit* m_lengthBoxes[3];
  QLineEdit* m_voxelSizeBoxes[3];
  QLineEdit* m_originBoxes[3];
  QLineEdit* m_unitBox;
};

} // namespace pipeline
} // namespace tomviz

#endif
