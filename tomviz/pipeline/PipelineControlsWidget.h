/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineControlsWidget_h
#define tomvizPipelineControlsWidget_h

#include <QWidget>

class QLabel;
class QToolButton;
class QTimer;

namespace tomviz {
namespace pipeline {

class Pipeline;

class PipelineControlsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit PipelineControlsWidget(QWidget* parent = nullptr);

  void setPipeline(Pipeline* pipeline);
  Pipeline* pipeline() const;

  bool isDimmingEnabled() const;

signals:
  void dimmingToggled(bool enabled);

private:
  void updateState();
  void onButtonClicked();
  void paintSpinner(QPainter& painter, const QRect& rect);

  Pipeline* m_pipeline = nullptr;
  QToolButton* m_button = nullptr;
  QLabel* m_statusLabel = nullptr;
  QLabel* m_spinnerLabel = nullptr;
  QTimer* m_spinnerTimer = nullptr;
  int m_spinnerAngle = 0;
  bool m_stopping = false;
  bool m_dimmingEnabled = true;
  QToolButton* m_dimmingButton = nullptr;

  static constexpr int SpinnerSize = 14;
};

} // namespace pipeline
} // namespace tomviz

#endif
