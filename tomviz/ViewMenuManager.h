/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizViewMenuManager_h
#define tomvizViewMenuManager_h

#include <pqViewMenuManager.h>

#include <QPointer>
#include <QScopedPointer>

#include <vtkNew.h>

class QDialog;
class QAction;

class vtkCallbackCommand;
class vtkObject;
class vtkSliderRepresentation2D;
class vtkSliderWidget;
class vtkSMViewProxy;

namespace tomviz {

class DataSource;
class SliceViewDialog;

enum class ScaleLegendStyle : unsigned int;

class ViewMenuManager : public pqViewMenuManager
{
  Q_OBJECT
public:
  ViewMenuManager(QMainWindow* mainWindow, QMenu* menu);
  ~ViewMenuManager();

private slots:
  void setProjectionModeToPerspective();
  void setProjectionModeToOrthographic();
  void onViewPropertyChanged();
  void onViewChanged();

  void setShowCenterAxes(bool show);
  void setShowOrientationAxes(bool show);
  void setImageViewerMode(bool b);

  void showDarkWhiteData();

private:
  void setScaleLegendStyle(ScaleLegendStyle);
  void setScaleLegendVisibility(bool);

  void updateDataSource(DataSource* s);
  void updateDataSourceEnableStates();

  static void sliderChangedCallback(vtkObject* object, unsigned long event,
                                    void* clientdata, void* calldata);

  QPointer<QAction> m_perspectiveProjectionAction;
  QPointer<QAction> m_orthographicProjectionAction;
  QPointer<QAction> m_showCenterAxesAction;
  QPointer<QAction> m_showOrientationAxesAction;
  QPointer<QAction> m_imageViewerModeAction;
  QPointer<QAction> m_showDarkWhiteDataAction;

  QScopedPointer<SliceViewDialog> m_sliceViewDialog;

  vtkNew<vtkSliderWidget> m_sliderWidget;
  vtkNew<vtkSliderRepresentation2D> m_sliderRepresentation;
  vtkNew<vtkCallbackCommand> m_sliderCallbackCommand;

  DataSource* m_dataSource = nullptr;
  vtkSMViewProxy* m_view;
  unsigned long m_viewObserverId;
};
} // namespace tomviz

#endif
