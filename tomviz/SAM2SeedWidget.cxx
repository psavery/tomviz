/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SAM2SeedWidget.h"

#include "Utilities.h"
#include "pipeline/NodePropertiesWidget.h"
#include "pipeline/data/VolumeData.h"

#include <vtkColorTransferFunction.h>
#include <vtkCommand.h>
#include <vtkImageData.h>
#include <vtkSmartPointer.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {

// 2D slice display that reports left clicks in image-pixel
// coordinates. The mouse wheel zooms about the cursor and a
// right-button drag pans; both only alter imageRect(), which all
// click/marker mapping goes through. The marker is the current seed
// point. Plain QWidget on purpose - no Q_OBJECT, the click is
// delivered through a callback.
class SliceClickView : public QWidget
{
public:
  using ClickCallback = std::function<void(int px, int py)>;

  SliceClickView(QWidget* parent = nullptr) : QWidget(parent)
  {
    setMinimumHeight(220);
    setCursor(Qt::CrossCursor);
  }

  void setImage(const QImage& image)
  {
    // A new slice orientation gets a fresh view; same-size updates
    // (slice scrubbing) keep the current zoom and pan.
    if (image.size() != m_image.size()) {
      m_zoom = 1.0;
      m_pan = QPoint();
    }
    m_image = image;
    update();
  }

  // Marker in image-pixel coordinates; (-1, -1) hides it.
  void setMarker(int px, int py)
  {
    m_marker = QPoint(px, py);
    update();
  }

  void setClickCallback(ClickCallback cb) { m_callback = std::move(cb); }

  QSize sizeHint() const override { return QSize(400, 400); }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (m_image.isNull()) {
      return;
    }
    QRect target = imageRect();
    painter.drawImage(target, m_image);

    if (m_marker.x() >= 0 && m_marker.y() >= 0 &&
        m_marker.x() < m_image.width() && m_marker.y() < m_image.height()) {
      double sx = target.width() / double(m_image.width());
      double sy = target.height() / double(m_image.height());
      QPointF c(target.x() + (m_marker.x() + 0.5) * sx,
                target.y() + (m_marker.y() + 0.5) * sy);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setPen(QPen(QColor(255, 60, 60), 1.5));
      painter.drawLine(QPointF(c.x() - 12, c.y()), QPointF(c.x() - 4, c.y()));
      painter.drawLine(QPointF(c.x() + 4, c.y()), QPointF(c.x() + 12, c.y()));
      painter.drawLine(QPointF(c.x(), c.y() - 12), QPointF(c.x(), c.y() - 4));
      painter.drawLine(QPointF(c.x(), c.y() + 4), QPointF(c.x(), c.y() + 12));
      painter.drawEllipse(c, 3.0, 3.0);
    }
  }

  void mousePressEvent(QMouseEvent* event) override
  {
    if (event->button() == Qt::LeftButton) {
      deliverClick(event->pos());
    } else if (event->button() == Qt::RightButton) {
      m_panAnchor = event->pos();
    }
  }

  void mouseMoveEvent(QMouseEvent* event) override
  {
    if (event->buttons() & Qt::LeftButton) {
      deliverClick(event->pos());
    } else if (event->buttons() & Qt::RightButton) {
      m_pan += event->pos() - m_panAnchor;
      m_panAnchor = event->pos();
      clampPan();
      update();
    }
  }

  void wheelEvent(QWheelEvent* event) override
  {
    QRect before = imageRect();
    if (before.width() <= 0 || before.height() <= 0) {
      return;
    }
    // Zoom about the cursor: remember which image fraction is under it
    // and shift the pan so that point stays put at the new zoom.
    QPointF pos = event->position();
    double rx = (pos.x() - before.x()) / before.width();
    double ry = (pos.y() - before.y()) / before.height();
    m_zoom =
      std::clamp(m_zoom * std::pow(1.0015, event->angleDelta().y()), 1.0, 32.0);
    QRect after = imageRect();
    m_pan += QPoint(int(std::lround(pos.x() - (after.x() + rx * after.width()))),
                    int(std::lround(pos.y() - (after.y() + ry * after.height()))));
    clampPan();
    update();
  }

  void resizeEvent(QResizeEvent* event) override
  {
    QWidget::resizeEvent(event);
    clampPan();
  }

private:
  // The image's on-screen rect: letterboxed fit, scaled by the zoom,
  // shifted by the pan.
  QRect imageRect() const
  {
    if (m_image.isNull()) {
      return QRect();
    }
    QSize scaled =
      (QSizeF(m_image.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio) *
       m_zoom).toSize();
    return QRect(QPoint((width() - scaled.width()) / 2 + m_pan.x(),
                        (height() - scaled.height()) / 2 + m_pan.y()),
                 scaled);
  }

  // Keep the image edge-to-edge at most: no panning while it fits the
  // viewport, and no revealing black past an edge once zoomed in.
  void clampPan()
  {
    QRect target = imageRect();
    int mx = std::max(0, (target.width() - width()) / 2);
    int my = std::max(0, (target.height() - height()) / 2);
    m_pan = QPoint(std::clamp(m_pan.x(), -mx, mx),
                   std::clamp(m_pan.y(), -my, my));
  }

  void deliverClick(const QPoint& pos)
  {
    QRect target = imageRect();
    if (!m_callback || target.width() <= 0 || target.height() <= 0) {
      return;
    }
    int px = (pos.x() - target.x()) * m_image.width() / target.width();
    int py = (pos.y() - target.y()) * m_image.height() / target.height();
    m_callback(std::clamp(px, 0, m_image.width() - 1),
               std::clamp(py, 0, m_image.height() - 1));
  }

  QImage m_image;
  QPoint m_marker = QPoint(-1, -1);
  double m_zoom = 1.0;
  QPoint m_pan;
  QPoint m_panAnchor;
  ClickCallback m_callback;
};

} // anonymous namespace

namespace tomviz {

class SAM2SeedWidget::Internal
{
public:
  pipeline::VolumeDataPtr volume;
  vtkSmartPointer<vtkImageData> image;
  int dims[3] = { 0, 0, 0 };

  // The volume's shared color transfer function (null outside a full
  // ParaView session); the slice is rendered through it so the dialog
  // matches the main window's colormap, and a ModifiedEvent observer
  // keeps it live while the user edits the colormap there.
  vtkColorTransferFunction* ctf = nullptr;
  unsigned long ctfObserverId = 0;
  QTimer* refreshTimer = nullptr;

  SliceClickView* view = nullptr;
  QSlider* slider = nullptr;
  QLabel* sliderLabel = nullptr;
  QVBoxLayout* layout = nullptr;

  pipeline::NodePropertiesWidget* form = nullptr;
  QSpinBox* seedX = nullptr;
  QSpinBox* seedY = nullptr;
  QSpinBox* seedZ = nullptr;
  QComboBox* zAxisCombo = nullptr;

  bool syncing = false;

  // The two volume axes lying in the displayed slice plane, in
  // ascending order. This matches the operator: it moves the chosen
  // z_axis to the end, so seed_x indexes the lower remaining axis and
  // seed_y the higher one.
  void inPlaneAxes(int zAxis, int& a, int& b) const
  {
    a = (zAxis == 0) ? 1 : 0;
    b = (zAxis == 2) ? 1 : 2;
  }
};

SAM2SeedWidget::SAM2SeedWidget(
  const QMap<QString, pipeline::PortData>& inputs, QWidget* parent)
  : CustomPythonNodeWidget(parent), m_internal(new Internal)
{
  // Registered with needsData=true, so the host gates creation on
  // input availability.
  if (auto it = inputs.constFind(QStringLiteral("volume"));
      it != inputs.constEnd()) {
    if (auto vol = it.value().value<pipeline::VolumeDataPtr>();
        vol && vol->isValid()) {
      m_internal->volume = vol;
      m_internal->image = vol->imageData();
    }
  }
  if (m_internal->image) {
    m_internal->image->GetDimensions(m_internal->dims);
  }

  // Render slices through the volume's own color map so the dialog
  // matches the main window, and follow live edits made there.
  // colorMap() initializes lazily and safely no-ops without a ParaView
  // session, in which case refreshSlice() falls back to grayscale.
  if (m_internal->volume) {
    m_internal->volume->colorMap();
    m_internal->ctf = m_internal->volume->colorTransferFunction();
  }
  if (m_internal->ctf) {
    m_internal->refreshTimer = new QTimer(this);
    m_internal->refreshTimer->setSingleShot(true);
    m_internal->refreshTimer->setInterval(0);
    connect(m_internal->refreshTimer, &QTimer::timeout, this,
            &SAM2SeedWidget::refreshSlice);
    m_internal->ctfObserverId = m_internal->ctf->AddObserver(
      vtkCommand::ModifiedEvent, this, &SAM2SeedWidget::onColorMapModified);
  }

  m_internal->layout = new QVBoxLayout(this);

  auto* hint = new QLabel(
    tr("Click the slice to set the seed point. Scroll to zoom, "
       "right-drag to pan, and use the slider to pick the seed slice."),
    this);
  hint->setWordWrap(true);
  m_internal->layout->addWidget(hint);

  m_internal->view = new SliceClickView(this);
  m_internal->view->setObjectName("sam2SeedSliceView");
  m_internal->layout->addWidget(m_internal->view, 1);
  m_internal->view->setClickCallback([this](int px, int py) {
    if (m_internal->seedX && m_internal->seedY) {
      m_internal->seedX->setValue(px);
      m_internal->seedY->setValue(py);
    }
  });

  auto* sliderRow = new QHBoxLayout;
  sliderRow->addWidget(new QLabel(tr("Slice"), this));
  m_internal->slider = new QSlider(Qt::Horizontal, this);
  m_internal->slider->setObjectName("sam2SeedSliceSlider");
  sliderRow->addWidget(m_internal->slider, 1);
  m_internal->sliderLabel = new QLabel(this);
  sliderRow->addWidget(m_internal->sliderLabel);
  m_internal->layout->addLayout(sliderRow);

  connect(m_internal->slider, &QSlider::valueChanged, this, [this](int value) {
    if (m_internal->syncing) {
      return;
    }
    if (m_internal->seedZ) {
      m_internal->syncing = true;
      m_internal->seedZ->setValue(value);
      m_internal->syncing = false;
    }
    refreshSlice();
  });

  // The parameter form is built in setValues(), which the host calls
  // with the node's current values right after construction.
}

SAM2SeedWidget::~SAM2SeedWidget()
{
  // The CTF is owned by the volume, which m_internal->volume keeps
  // alive until after this runs.
  if (m_internal->ctf && m_internal->ctfObserverId) {
    m_internal->ctf->RemoveObserver(m_internal->ctfObserverId);
  }
}

// Coalesce the bursts of ModifiedEvents fired while the user drags
// colormap controls into one repaint per event-loop pass.
void SAM2SeedWidget::onColorMapModified()
{
  m_internal->refreshTimer->start();
}

void SAM2SeedWidget::getValues(QMap<QString, QVariant>& map)
{
  if (m_internal->form) {
    map.insert(m_internal->form->values());
  }
}

void SAM2SeedWidget::setValues(const QMap<QString, QVariant>& map)
{
  delete m_internal->form;
  m_internal->form = new pipeline::NodePropertiesWidget(
    readInJSONDescription(QStringLiteral("SAM2Segment3D")), map, {}, this);
  m_internal->layout->addWidget(m_internal->form);
  wireForm();
}

void SAM2SeedWidget::wireForm()
{
  auto* form = m_internal->form;

  // The builder's description label uses an Ignored size policy, which
  // collapses to zero height here because the form is not the layout's
  // stretch widget (the slice view is). Let it request its wrapped
  // height instead; Ignored stays on the horizontal axis so a long
  // description cannot widen the dialog.
  for (auto* label : form->findChildren<QLabel*>()) {
    if (label->wordWrap() &&
        label->sizePolicy().verticalPolicy() == QSizePolicy::Ignored) {
      label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    }
  }

  m_internal->seedX = form->findChild<QSpinBox*>("seed_x");
  m_internal->seedY = form->findChild<QSpinBox*>("seed_y");
  m_internal->seedZ = form->findChild<QSpinBox*>("seed_z");
  m_internal->zAxisCombo = form->findChild<QComboBox*>("z_axis");

  auto resync = [this]() {
    if (!m_internal->syncing) {
      syncSliderAndSlice();
    }
  };
  if (m_internal->seedX) {
    connect(m_internal->seedX, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SAM2SeedWidget::refreshMarker);
  }
  if (m_internal->seedY) {
    connect(m_internal->seedY, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SAM2SeedWidget::refreshMarker);
  }
  if (m_internal->seedZ) {
    connect(m_internal->seedZ, QOverload<int>::of(&QSpinBox::valueChanged),
            this, resync);
  }
  if (m_internal->zAxisCombo) {
    connect(m_internal->zAxisCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this, resync);
  }
  syncSliderAndSlice();
}

// Bring the slider's range and position in line with the current
// Z Axis and Seed Z form values, then re-render.
void SAM2SeedWidget::syncSliderAndSlice()
{
  int n = m_internal->dims[sliceAxis()];
  m_internal->syncing = true;
  m_internal->slider->setRange(0, qMax(0, n - 1));
  m_internal->slider->setValue(effectiveSliceIndex());
  m_internal->syncing = false;
  refreshSlice();
}

int SAM2SeedWidget::sliceAxis() const
{
  if (!m_internal->zAxisCombo) {
    return 0;
  }
  return std::clamp(m_internal->zAxisCombo->currentData().toInt(), 0, 2);
}

int SAM2SeedWidget::effectiveSliceIndex() const
{
  int n = m_internal->dims[sliceAxis()];
  if (n <= 0) {
    return 0;
  }
  int k = m_internal->seedZ ? m_internal->seedZ->value() : -1;
  // Negative means "middle slice", matching the operator.
  return k < 0 ? n / 2 : std::clamp(k, 0, n - 1);
}

void SAM2SeedWidget::refreshSlice()
{
  auto* image = m_internal->image.GetPointer();
  int* dims = m_internal->dims;
  if (!image || dims[0] <= 0 || dims[1] <= 0 || dims[2] <= 0) {
    return;
  }

  int zAxis = sliceAxis();
  int a, b;
  m_internal->inPlaneAxes(zAxis, a, b);
  int k = effectiveSliceIndex();
  int w = dims[a];
  int h = dims[b];

  std::vector<double> values(size_t(w) * h);
  int coords[3];
  coords[zAxis] = k;
  for (int py = 0; py < h; ++py) {
    coords[b] = py;
    for (int px = 0; px < w; ++px) {
      coords[a] = px;
      values[size_t(py) * w + px] = image->GetScalarComponentAsDouble(
        coords[0], coords[1], coords[2], 0);
    }
  }

  QImage img;
  if (m_internal->ctf) {
    // Map values through the volume's color transfer function so the
    // slice matches the main window's colormap, including any
    // brightness/contrast edits to its control points.
    img = QImage(w, h, QImage::Format_RGB888);
    for (int py = 0; py < h; ++py) {
      uchar* line = img.scanLine(py);
      for (int px = 0; px < w; ++px) {
        double rgb[3];
        m_internal->ctf->GetColor(values[size_t(py) * w + px], rgb);
        for (int c = 0; c < 3; ++c) {
          line[3 * px + c] = uchar(std::clamp(rgb[c], 0.0, 1.0) * 255.0);
        }
      }
    }
  } else {
    // No colormap (no ParaView session): robust 1%/99% grayscale
    // stretch so dim features stay visible next to bright cores.
    std::vector<double> sorted(values);
    auto percentile = [&sorted](double frac) {
      auto nth = sorted.begin() + size_t(frac * (sorted.size() - 1));
      std::nth_element(sorted.begin(), nth, sorted.end());
      return *nth;
    };
    double vlo = percentile(0.01);
    double vhi = percentile(0.99);
    if (vhi <= vlo) {
      vlo = percentile(0.0);
      vhi = percentile(1.0);
    }
    double scale = vhi > vlo ? 255.0 / (vhi - vlo) : 0.0;

    img = QImage(w, h, QImage::Format_Grayscale8);
    for (int py = 0; py < h; ++py) {
      uchar* line = img.scanLine(py);
      for (int px = 0; px < w; ++px) {
        double v = (values[size_t(py) * w + px] - vlo) * scale;
        line[px] = uchar(std::clamp(v, 0.0, 255.0));
      }
    }
  }

  m_internal->view->setImage(img);
  m_internal->sliderLabel->setText(
    QString("%1 / %2").arg(k).arg(dims[zAxis] - 1));
  refreshMarker();
}

void SAM2SeedWidget::refreshMarker()
{
  int* dims = m_internal->dims;
  int a, b;
  m_internal->inPlaneAxes(sliceAxis(), a, b);

  // Negative seed means "center", matching the operator.
  int px = m_internal->seedX ? m_internal->seedX->value() : -1;
  int py = m_internal->seedY ? m_internal->seedY->value() : -1;
  px = px < 0 ? dims[a] / 2 : std::clamp(px, 0, qMax(0, dims[a] - 1));
  py = py < 0 ? dims[b] / 2 : std::clamp(py, 0, qMax(0, dims[b] - 1));
  m_internal->view->setMarker(px, py);
}

} // namespace tomviz
