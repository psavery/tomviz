/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <QApplication>
#include <QTest>

#include <pqApplicationCore.h>
#include <pqObjectBuilder.h>
#include <pqPVApplicationCore.h>
#include <pqServerResource.h>

#include <vtkColorTransferFunction.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPiecewiseFunction.h>
#include <vtkSmartPointer.h>

#include "pipeline/PortDataMetadata.h"
#include "pipeline/data/VolumeData.h"

#include <map>
#include <set>
#include <vector>
#include <tuple>

using namespace tomviz;
using namespace tomviz::pipeline;

class SegmentationColorMapTest : public QObject
{
  Q_OBJECT

private slots:
  // The segmentation preset must land in data coordinates immediately:
  // no rescale (manual "Reset data range" or otherwise) may be needed
  // for each label to get its own color. Regression test for the
  // ApplyPreset rescale-to-current-range bug that squeezed the per-label
  // node positions into the fresh colormap's default [0, 1] range.
  void segmentationColorMapUsesDataCoordinates()
  {
    const int numLabels = 152; // labels 0..151
    vtkNew<vtkImageData> image;
    image->SetDimensions(40, 40, 40);
    image->AllocateScalars(VTK_INT, 1);
    auto* p = static_cast<int*>(image->GetScalarPointer());
    for (int i = 0; i < 40 * 40 * 40; ++i) {
      p[i] = i % numLabels;
    }
    auto vol = std::make_shared<VolumeData>(
      vtkSmartPointer<vtkImageData>(image.GetPointer()));

    QVERIFY(applySegmentationColorMap(*vol));

    auto* ctf = vol->colorTransferFunction();
    QVERIFY(ctf);
    QCOMPARE(ctf->GetSize(), 2 * numLabels);

    double range[2];
    ctf->GetRange(range);
    QCOMPARE(range[0], 0.0);
    QCOMPARE(range[1], double(numLabels - 1));

    // Every label maps to its own color, straight after the apply.
    std::map<std::tuple<int, int, int>, std::vector<int>> seen;
    for (int v = 0; v < numLabels; ++v) {
      double rgb[3];
      ctf->GetColor(double(v), rgb);
      seen[{ int(rgb[0] * 255), int(rgb[1] * 255),
             int(rgb[2] * 255) }].push_back(v);
    }
    for (auto& entry : seen) {
      if (entry.second.size() > 1) {
        QString labels;
        for (int v : entry.second) {
          labels += QString::number(v) + " ";
        }
        qWarning("color (%d %d %d) shared by labels: %s",
                 std::get<0>(entry.first), std::get<1>(entry.first),
                 std::get<2>(entry.first), qPrintable(labels));
      }
    }
    QCOMPARE(int(seen.size()), numLabels);

    // Rescaling to the data range (what "Reset data range" and the
    // per-node post-execution rescale do) must be an identity: every
    // label keeps its own color afterwards.
    vol->rescaleColorMap();
    std::set<std::tuple<int, int, int>> after;
    for (int v = 0; v < numLabels; ++v) {
      double rgb[3];
      ctf->GetColor(double(v), rgb);
      after.insert({ int(rgb[0] * 255), int(rgb[1] * 255),
                     int(rgb[2] * 255) });
    }
    QCOMPARE(int(after.size()), numLabels);

    // Background label 0 renders transparent.
    auto* opacity = vol->scalarOpacity();
    QVERIFY(opacity);
    QCOMPARE(opacity->GetValue(0.0), 0.0);
    QCOMPARE(opacity->GetValue(1.0), 1.0);
  }
};

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  pqPVApplicationCore appCore(argc, argv);
  pqApplicationCore::instance()->getObjectBuilder()->createServer(
    pqServerResource("builtin:"));
  SegmentationColorMapTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "SegmentationColorMapTest.moc"
