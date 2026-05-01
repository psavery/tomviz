/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizSelectCylinderWidget_h
#define tomvizSelectCylinderWidget_h

#include "CustomPythonTransformWidget.h"

#include <vtkSmartPointer.h>

#include <QScopedPointer>

class vtkImageData;
class vtkSMProxy;

namespace tomviz {

class SelectCylinderWidget : public pipeline::CustomPythonTransformWidget
{
  Q_OBJECT

public:
  SelectCylinderWidget(vtkSmartPointer<vtkImageData> image,
                       vtkSMProxy* colorMap, QWidget* parent = nullptr);
  ~SelectCylinderWidget();

  void getValues(QMap<QString, QVariant>& map) override;
  void setValues(const QMap<QString, QVariant>& map) override;
  void writeSettings() override;

private slots:
  void onInteractionEnd();
  void onSpinBoxChanged();

private:
  void disableWidget();

  Q_DISABLE_COPY(SelectCylinderWidget)

  class Internal;
  QScopedPointer<Internal> m_internal;
};

} // namespace tomviz

#endif
