/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizManualManipulationWidget_h
#define tomvizManualManipulationWidget_h

#include "CustomPythonTransformWidget.h"
#include <vtkSmartPointer.h>
#include <QScopedPointer>

class vtkImageData;
class vtkSMProxy;

namespace tomviz {

class ManualManipulationWidget : public pipeline::CustomPythonTransformWidget
{
  Q_OBJECT

public:
  ManualManipulationWidget(vtkSmartPointer<vtkImageData> image,
                           vtkSMProxy* sourceColorMap,
                           QWidget* parent = nullptr);
  ~ManualManipulationWidget();

  void getValues(QMap<QString, QVariant>& map) override;
  void setValues(const QMap<QString, QVariant>& map) override;

private:
  Q_DISABLE_COPY(ManualManipulationWidget)
  class Internal;
  QScopedPointer<Internal> m_internal;
};

} // namespace tomviz
#endif
