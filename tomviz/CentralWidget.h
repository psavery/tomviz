/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizCentralWidget_h
#define tomvizCentralWidget_h

#include <QMap>
#include <QPointer>
#include <QScopedPointer>
#include <QWidget>

#include <vtkSmartPointer.h>
#include <vtkTable.h>

#include <memory>

class vtkImageData;
class vtkPVDiscretizableColorTransferFunction;


class QThread;
class QTimer;

namespace Ui {
class CentralWidget;
}

namespace tomviz {
class DataSource;
class HistogramMaker;
class Module;
class Transfer2DModel;
class Operator;

namespace pipeline {
class LegacyModuleSink;
class VolumeData;
using VolumeDataPtr = std::shared_ptr<VolumeData>;
} // namespace pipeline

/// CentralWidget is a QWidget that is used as the central widget
/// for the application. This include a histogram at the top and a
/// ParaView view-layout widget at the bottom.
class CentralWidget : public QWidget
{
  Q_OBJECT

public:
  CentralWidget(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
  ~CentralWidget() override;

public slots:
  /// Set the data source that is shown and color by the data source's
  /// color map
  void setActiveColorMapDataSource(DataSource*);

  /// Set the data source that is shown to the module's data source and color
  /// by the module's color map
  void setActiveModule(Module*);
  void setActiveOperator(Operator*);
  void onColorMapUpdated();
  void onColorLegendToggled(bool visibility);

  void setImageViewerMode(bool b);

  /// New pipeline: set the active sink node for color map editing.
  void setActiveSinkNode(pipeline::LegacyModuleSink* sink);

  /// New pipeline: set the active VolumeData for color map editing.
  void setActiveVolumeData(pipeline::VolumeDataPtr volumeData);

private slots:
  void histogramReady(vtkSmartPointer<vtkImageData>, vtkSmartPointer<vtkTable>);
  void histogram2DReady(vtkSmartPointer<vtkImageData> input,
                        vtkSmartPointer<vtkImageData> output);
  void onColorMapDataSourceChanged();
  void refreshHistogram();

  /// The active transfer mode is tracked through the tab index of the TabWidget
  /// holding the 1D/2D histograms (tabs are expected to follow the order of
  /// Module::TransferMode).
  void onTransferModeChanged(const int mode);

private:
  Q_DISABLE_COPY(CentralWidget)

  /// Set of input checks shared between 1D and 2D histograms.
  vtkImageData* getInputImage(vtkSmartPointer<vtkImageData> input);

  /// Set the data source to from which the data is "histogrammed" and shown
  /// in the histogram view.
  void setColorMapDataSource(DataSource*);
  void setHistogramTable(vtkTable* table);

  QScopedPointer<Ui::CentralWidget> m_ui;
  QScopedPointer<QTimer> m_timer;

  QPointer<DataSource> m_activeColorMapDataSource;
  QPointer<Module> m_activeModule;
  Transfer2DModel* m_transfer2DModel;

  // New pipeline color map state
  pipeline::LegacyModuleSink* m_activeSink = nullptr;
  pipeline::VolumeDataPtr m_activeVolumeData;
};
} // namespace tomviz

#endif
