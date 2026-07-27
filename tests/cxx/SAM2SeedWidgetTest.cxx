/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <gtest/gtest.h>

#include "SAM2SeedWidget.h"
#include "pipeline/PortData.h"
#include "pipeline/PortType.h"
#include "pipeline/data/VolumeData.h"

#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>

#include <QApplication>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QTest>

#include <unistd.h>

using tomviz::SAM2SeedWidget;
using tomviz::pipeline::PortData;
using tomviz::pipeline::PortType;
using tomviz::pipeline::VolumeData;
using tomviz::pipeline::VolumeDataPtr;

namespace {

// The tests share one process-wide QApplication (gtest_main owns main).
// argv[0] must be the real executable path: Qt resolves
// applicationDirPath() from it, which readInJSONDescription() needs to
// find the operator JSON in the build tree.
void ensureApp()
{
  if (!QApplication::instance()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static char arg0[4096];
    ssize_t n = readlink("/proc/self/exe", arg0, sizeof(arg0) - 1);
    arg0[n > 0 ? n : 0] = '\0';
    static int argc = 1;
    static char* argv[] = { arg0, nullptr };
    new QApplication(argc, argv);
  }
}

// Distinct dims per axis so axis mix-ups fail loudly.
const int DIMS[3] = { 10, 20, 30 };

QMap<QString, PortData> makeInputs()
{
  vtkNew<vtkImageData> image;
  image->SetDimensions(DIMS[0], DIMS[1], DIMS[2]);
  image->AllocateScalars(VTK_FLOAT, 1);
  auto* p = static_cast<float*>(image->GetScalarPointer());
  for (int i = 0; i < DIMS[0] * DIMS[1] * DIMS[2]; ++i) {
    p[i] = float(i % 251);
  }
  auto volume = std::make_shared<VolumeData>(
    vtkSmartPointer<vtkImageData>(image.GetPointer()));
  QMap<QString, PortData> inputs;
  inputs["volume"] = PortData(VolumeDataPtr(volume), PortType::Volume);
  return inputs;
}

// Replicates SliceClickView's letterboxed image placement so tests can
// convert an image pixel to a widget click position.
QPoint pixelToWidgetPos(const QWidget* view, int imgW, int imgH, int px,
                        int py)
{
  QSize scaled = QSize(imgW, imgH).scaled(view->size(), Qt::KeepAspectRatio);
  QRect target(QPoint((view->width() - scaled.width()) / 2,
                      (view->height() - scaled.height()) / 2),
               scaled);
  return QPoint(target.x() + int((px + 0.5) * target.width() / imgW),
                target.y() + int((py + 0.5) * target.height() / imgH));
}

} // namespace

TEST(SAM2SeedWidgetTest, EmbedsParameterForm)
{
  ensureApp();
  SAM2SeedWidget widget(makeInputs());
  widget.setValues({});

  auto* seedX = widget.findChild<QSpinBox*>("seed_x");
  auto* seedY = widget.findChild<QSpinBox*>("seed_y");
  auto* seedZ = widget.findChild<QSpinBox*>("seed_z");
  auto* zAxis = widget.findChild<QComboBox*>("z_axis");
  ASSERT_NE(seedX, nullptr);
  ASSERT_NE(seedY, nullptr);
  ASSERT_NE(seedZ, nullptr);
  ASSERT_NE(zAxis, nullptr);

  // JSON defaults survive the embedding.
  EXPECT_EQ(seedX->value(), -1);
  EXPECT_EQ(seedY->value(), -1);
  EXPECT_EQ(seedZ->value(), -1);
  EXPECT_EQ(zAxis->currentData().toInt(), 0);

  // The full form is present, not just the seed controls.
  EXPECT_NE(widget.findChild<QComboBox*>("prompt_mode"), nullptr);
  EXPECT_NE(widget.findChild<QComboBox*>("model_size"), nullptr);
}

TEST(SAM2SeedWidgetTest, ClickSetsSeedAndGetValues)
{
  ensureApp();
  SAM2SeedWidget widget(makeInputs());
  widget.setValues({});
  widget.resize(500, 700);
  widget.show();
  QApplication::processEvents();

  auto* view = widget.findChild<QWidget*>("sam2SeedSliceView");
  ASSERT_NE(view, nullptr);
  ASSERT_GT(view->width(), 0);
  ASSERT_GT(view->height(), 0);

  // Default z_axis is 0, so the slice plane is axes (1, 2):
  // width = DIMS[1] (seed_x), height = DIMS[2] (seed_y).
  QPoint pos = pixelToWidgetPos(view, DIMS[1], DIMS[2], 4, 7);
  QTest::mouseClick(view, Qt::LeftButton, Qt::KeyboardModifiers(), pos);

  auto* seedX = widget.findChild<QSpinBox*>("seed_x");
  auto* seedY = widget.findChild<QSpinBox*>("seed_y");
  EXPECT_EQ(seedX->value(), 4);
  EXPECT_EQ(seedY->value(), 7);

  QMap<QString, QVariant> values;
  widget.getValues(values);
  EXPECT_EQ(values["seed_x"].toInt(), 4);
  EXPECT_EQ(values["seed_y"].toInt(), 7);
  // Untouched parameters still come through the embedded form.
  EXPECT_TRUE(values.contains("model_size"));
  EXPECT_TRUE(values.contains("checkpoint_dir"));
}

TEST(SAM2SeedWidgetTest, SliderSyncsWithSeedZ)
{
  ensureApp();
  SAM2SeedWidget widget(makeInputs());
  widget.setValues({});

  auto* slider = widget.findChild<QSlider*>("sam2SeedSliceSlider");
  auto* seedZ = widget.findChild<QSpinBox*>("seed_z");
  ASSERT_NE(slider, nullptr);
  ASSERT_NE(seedZ, nullptr);

  // Default axis 0 → slice count DIMS[0]; seed_z of -1 shows the middle.
  EXPECT_EQ(slider->maximum(), DIMS[0] - 1);
  EXPECT_EQ(slider->value(), DIMS[0] / 2);
  EXPECT_EQ(seedZ->value(), -1);

  slider->setValue(7);
  EXPECT_EQ(seedZ->value(), 7);

  seedZ->setValue(2);
  EXPECT_EQ(slider->value(), 2);

  seedZ->setValue(-1);
  EXPECT_EQ(slider->value(), DIMS[0] / 2);
}

TEST(SAM2SeedWidgetTest, AxisChangeUpdatesSliderRange)
{
  ensureApp();
  SAM2SeedWidget widget(makeInputs());
  widget.setValues({});

  auto* slider = widget.findChild<QSlider*>("sam2SeedSliceSlider");
  auto* zAxis = widget.findChild<QComboBox*>("z_axis");
  ASSERT_NE(slider, nullptr);
  ASSERT_NE(zAxis, nullptr);

  zAxis->setCurrentIndex(2); // axis 2 → DIMS[2] slices
  EXPECT_EQ(slider->maximum(), DIMS[2] - 1);
  EXPECT_EQ(slider->value(), DIMS[2] / 2);

  zAxis->setCurrentIndex(1); // axis 1 → DIMS[1] slices
  EXPECT_EQ(slider->maximum(), DIMS[1] - 1);
}

TEST(SAM2SeedWidgetTest, SetValuesAppliesStoredParameters)
{
  ensureApp();
  SAM2SeedWidget widget(makeInputs());
  QMap<QString, QVariant> stored;
  stored["seed_x"] = 3;
  stored["seed_y"] = 12;
  stored["seed_z"] = 25;
  stored["z_axis"] = 2;
  widget.setValues(stored);

  EXPECT_EQ(widget.findChild<QSpinBox*>("seed_x")->value(), 3);
  EXPECT_EQ(widget.findChild<QSpinBox*>("seed_y")->value(), 12);
  EXPECT_EQ(widget.findChild<QSpinBox*>("seed_z")->value(), 25);

  auto* slider = widget.findChild<QSlider*>("sam2SeedSliceSlider");
  EXPECT_EQ(slider->maximum(), DIMS[2] - 1);
  EXPECT_EQ(slider->value(), 25);
}


TEST(SAM2SeedWidgetTest, ZoomKeepsClickMappingAnchored)
{
  ensureApp();
  SAM2SeedWidget widget(makeInputs());
  widget.setValues({});
  widget.resize(500, 700);
  widget.show();
  QApplication::processEvents();

  auto* view = widget.findChild<QWidget*>("sam2SeedSliceView");
  auto* seedX = widget.findChild<QSpinBox*>("seed_x");
  auto* seedY = widget.findChild<QSpinBox*>("seed_y");
  ASSERT_NE(view, nullptr);

  auto clickAt = [&](const QPoint& pos) {
    QTest::mouseClick(view, Qt::LeftButton, Qt::KeyboardModifiers(), pos);
  };
  auto wheelAt = [&](const QPoint& pos, int delta) {
    QWheelEvent event(pos, view->mapToGlobal(pos), QPoint(), QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view, &event);
  };

  // Establish the unzoomed mapping for an interior pixel. (Edge
  // pixels are the wrong probe here: the pan clamp that keeps the
  // image edge-to-edge deliberately overrides anchoring near edges.)
  QPoint pos = pixelToWidgetPos(view, DIMS[1], DIMS[2], 10, 15);
  clickAt(pos);
  ASSERT_EQ(seedX->value(), 10);
  ASSERT_EQ(seedY->value(), 15);

  // Zoom in about that same position: the image point under the
  // cursor must stay put, so clicking there again hits the same voxel.
  wheelAt(pos, 480);
  clickAt(pos);
  EXPECT_EQ(seedX->value(), 10);
  EXPECT_EQ(seedY->value(), 15);

  // A big zoom-out clamps back to the letterboxed fit (zoom 1, pan 0),
  // restoring the original mapping everywhere.
  wheelAt(pos, -8000);
  QPoint other = pixelToWidgetPos(view, DIMS[1], DIMS[2], 13, 21);
  clickAt(other);
  EXPECT_EQ(seedX->value(), 13);
  EXPECT_EQ(seedY->value(), 21);
}

