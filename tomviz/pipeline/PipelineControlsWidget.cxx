/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PipelineControlsWidget.h"

#include "Pipeline.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QToolButton>

namespace tomviz {
namespace pipeline {

PipelineControlsWidget::PipelineControlsWidget(QWidget* parent)
  : QWidget(parent)
{
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(4, 2, 4, 2);

  m_spinnerLabel = new QLabel(this);
  m_spinnerLabel->setFixedSize(SpinnerSize, SpinnerSize);
  m_spinnerLabel->hide();
  layout->addWidget(m_spinnerLabel);

  m_statusLabel = new QLabel(this);
  QPalette labelPal = m_statusLabel->palette();
  labelPal.setColor(QPalette::WindowText, palette().color(QPalette::Dark));
  m_statusLabel->setPalette(labelPal);
  layout->addWidget(m_statusLabel);

  layout->addStretch();

  m_button = new QToolButton(this);
  m_button->setAutoRaise(true);
  m_button->setIconSize(QSize(20, 20));
  connect(m_button, &QToolButton::clicked, this,
          &PipelineControlsWidget::onButtonClicked);
  layout->addWidget(m_button);

  m_spinnerTimer = new QTimer(this);
  m_spinnerTimer->setInterval(50);
  connect(m_spinnerTimer, &QTimer::timeout, this, [this]() {
    m_spinnerAngle = (m_spinnerAngle + 30) % 360;
    // Repaint the spinner label
    QPixmap pix(SpinnerSize, SpinnerSize);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPixmap spinner(QStringLiteral(":/pipeline/spinner.png"));
    p.translate(SpinnerSize / 2, SpinnerSize / 2);
    p.rotate(m_spinnerAngle);
    p.drawPixmap(-SpinnerSize / 2, -SpinnerSize / 2,
                 SpinnerSize, SpinnerSize, spinner);
    p.end();
    m_spinnerLabel->setPixmap(pix);
  });

  updateState();
}

void PipelineControlsWidget::setPipeline(Pipeline* pipeline)
{
  if (m_pipeline) {
    m_pipeline->disconnect(this);
  }
  m_pipeline = pipeline;
  if (m_pipeline) {
    connect(m_pipeline, &Pipeline::executionStarted, this,
            &PipelineControlsWidget::updateState);
    connect(m_pipeline, &Pipeline::executionFinished, this,
            &PipelineControlsWidget::updateState);
    connect(m_pipeline, &Pipeline::pausedChanged, this,
            &PipelineControlsWidget::updateState);
  }
  updateState();
}

Pipeline* PipelineControlsWidget::pipeline() const
{
  return m_pipeline;
}

void PipelineControlsWidget::updateState()
{
  bool executing = m_pipeline && m_pipeline->isExecuting();

  // Clear stopping flag when execution finishes.
  if (!executing) {
    m_stopping = false;
  }

  // Button icon
  if (executing) {
    m_button->setIcon(QIcon(":/icons/pqVcrStop.svg"));
    m_button->setToolTip(tr("Stop execution"));
  } else if (m_pipeline && m_pipeline->isPaused()) {
    m_button->setIcon(QIcon(":/pqWidgets/Icons/pqVcrPlay.svg"));
    m_button->setToolTip(tr("Resume automatic execution"));
  } else {
    m_button->setIcon(QIcon(":/pqWidgets/Icons/pqVcrPause.svg"));
    m_button->setToolTip(tr("Pause automatic execution"));
  }

  // Status message and spinner
  if (m_stopping && executing) {
    m_statusLabel->setText(tr("Stopping..."));
    m_statusLabel->show();
    if (!m_spinnerTimer->isActive()) {
      m_spinnerTimer->start();
    }
    m_spinnerLabel->show();
  } else if (executing) {
    m_statusLabel->hide();
    if (!m_spinnerTimer->isActive()) {
      m_spinnerTimer->start();
    }
    m_spinnerLabel->show();
  } else if (m_pipeline && m_pipeline->isPaused()) {
    m_statusLabel->setText(tr("Automatic execution paused"));
    m_statusLabel->show();
    m_spinnerTimer->stop();
    m_spinnerLabel->hide();
  } else {
    m_statusLabel->hide();
    m_spinnerTimer->stop();
    m_spinnerLabel->hide();
  }
}

void PipelineControlsWidget::onButtonClicked()
{
  if (!m_pipeline) {
    return;
  }
  if (m_pipeline->isExecuting()) {
    m_stopping = true;
    m_pipeline->cancelExecution();
    updateState();
  } else {
    m_pipeline->setPaused(!m_pipeline->isPaused());
  }
}

} // namespace pipeline
} // namespace tomviz
