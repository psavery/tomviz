/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <QApplication>
#include <QTemporaryFile>
#include <QTest>

#include <pqApplicationCore.h>
#include <pqObjectBuilder.h>
#include <pqPVApplicationCore.h>
#include <pqServerResource.h>

#include <vtkDoubleArray.h>
#include <vtkFieldData.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>

#include <h5cpp/h5readwrite.h>

#include "ActiveObjects.h"
#include "legacy/DataSource.h"
#include "EmdFormat.h"
#include "legacy/Pipeline.h"
#include "legacy/PipelineManager.h"
#include "Tvh5Format.h"
#include "legacy/modules/ModuleManager.h"

using namespace tomviz;

class Tvh5DataTest : public QObject
{
  Q_OBJECT

private:
  vtkSmartPointer<vtkImageData> createTestImage(int dim, double fillValue)
  {
    auto image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(dim, dim, dim);
    image->AllocateScalars(VTK_DOUBLE, 1);
    for (int z = 0; z < dim; ++z) {
      for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
          image->SetScalarComponentFromDouble(x, y, z, 0, fillValue);
        }
      }
    }
    return image;
  }

  // Get a temporary file path with .tvh5 extension.
  QString tempFilePath()
  {
    QTemporaryFile tmpFile(QDir::tempPath() + "/tomviz_test_XXXXXX.tvh5");
    tmpFile.setAutoRemove(false);
    if (!tmpFile.open()) {
      return {};
    }
    auto path = tmpFile.fileName();
    tmpFile.close();
    m_tempFiles.push_back(path);
    return path;
  }

  // Set up a DataSource registered with the application singletons.
  // Returns the DataSource (owned by the Pipeline).
  DataSource* setupDataSource(vtkImageData* image)
  {
    auto* ds = new DataSource(image);
    auto* pipeline = new Pipeline(ds);
    PipelineManager::instance().addPipeline(pipeline);
    ModuleManager::instance().addDataSource(ds);
    // LEGACY STUB: setActiveDataSource() removed from ActiveObjects
    // ActiveObjects::instance().setActiveDataSource(ds);
    return ds;
  }

  QStringList m_tempFiles;

private slots:
  void cleanup()
  {
    // Reset application state between tests
    ModuleManager::instance().reset();

    for (const auto& f : m_tempFiles) {
      QFile::remove(f);
    }
    m_tempFiles.clear();
  }

  void writeCreatesValidFile()
  {
    auto image = createTestImage(4, 42.0);
    setupDataSource(image);

    auto fileName = tempFilePath();
    QVERIFY(Tvh5Format::write(fileName.toStdString()));

    // Verify the file can be read back as EMD data
    auto readImage = vtkSmartPointer<vtkImageData>::New();
    QVERIFY(EmdFormat::read(fileName.toStdString(), readImage));

    int dims[3];
    readImage->GetDimensions(dims);
    QCOMPARE(dims[0], 4);
    QCOMPARE(dims[1], 4);
    QCOMPARE(dims[2], 4);

    for (int z = 0; z < 4; ++z) {
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          QCOMPARE(readImage->GetScalarComponentAsDouble(x, y, z, 0), 42.0);
        }
      }
    }
  }

  void writePreservesTiltAngles()
  {
    auto image = createTestImage(4, 1.0);

    vtkNew<vtkDoubleArray> tiltAngles;
    tiltAngles->SetName("tilt_angles");
    tiltAngles->SetNumberOfTuples(4);
    tiltAngles->SetValue(0, -60.0);
    tiltAngles->SetValue(1, -20.0);
    tiltAngles->SetValue(2, 20.0);
    tiltAngles->SetValue(3, 60.0);
    image->GetFieldData()->AddArray(tiltAngles);

    setupDataSource(image);

    auto fileName = tempFilePath();
    QVERIFY(Tvh5Format::write(fileName.toStdString()));

    auto readImage = vtkSmartPointer<vtkImageData>::New();
    QVERIFY(EmdFormat::read(fileName.toStdString(), readImage));

    auto* fd = readImage->GetFieldData();
    QVERIFY(fd->HasArray("tilt_angles"));
    auto* readAngles = fd->GetArray("tilt_angles");
    QCOMPARE(readAngles->GetNumberOfTuples(), static_cast<vtkIdType>(4));
    QCOMPARE(readAngles->GetTuple1(0), -60.0);
    QCOMPARE(readAngles->GetTuple1(1), -20.0);
    QCOMPARE(readAngles->GetTuple1(2), 20.0);
    QCOMPARE(readAngles->GetTuple1(3), 60.0);
  }

  void writePreservesScanIDs()
  {
    auto image = createTestImage(4, 1.0);

    QVector<int> scanIDs = { 10, 20, 30 };
    DataSource::setScanIDs(image, scanIDs);

    setupDataSource(image);

    auto fileName = tempFilePath();
    QVERIFY(Tvh5Format::write(fileName.toStdString()));

    auto readImage = vtkSmartPointer<vtkImageData>::New();
    QVERIFY(EmdFormat::read(fileName.toStdString(), readImage));

    QVERIFY(DataSource::hasScanIDs(readImage));
    auto readIDs = DataSource::getScanIDs(readImage);
    QCOMPARE(readIDs.size(), 3);
    QCOMPARE(readIDs[0], 10);
    QCOMPARE(readIDs[1], 20);
    QCOMPARE(readIDs[2], 30);
  }

  void roundtripPreservesData()
  {
    auto image = createTestImage(4, 99.0);
    setupDataSource(image);

    auto fileName = tempFilePath();
    QVERIFY(Tvh5Format::write(fileName.toStdString()));

    // Read back the data directly from the HDF5 file.
    // Tvh5Format::read() now creates SourceNodes in the new pipeline rather
    // than registering DataSources with ModuleManager, so we verify the
    // round-trip by reading the EMD node directly.
    using h5::H5ReadWrite;
    H5ReadWrite reader(fileName.toStdString(), H5ReadWrite::OpenMode::ReadOnly);

    // The data source id is stored in the state; read the first datasource
    // group from /tomviz_datasources/
    auto children = reader.children("/tomviz_datasources");
    QVERIFY(!children.empty());

    std::string path = "/tomviz_datasources/" + children[0];
    vtkNew<vtkImageData> loadedImage;
    QVariantMap options = { { "askForSubsample", false } };
    QVERIFY(EmdFormat::readNode(reader, path, loadedImage, options));

    int dims[3];
    loadedImage->GetDimensions(dims);
    QCOMPARE(dims[0], 4);
    QCOMPARE(dims[1], 4);
    QCOMPARE(dims[2], 4);

    for (int z = 0; z < 4; ++z) {
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          QCOMPARE(loadedImage->GetScalarComponentAsDouble(x, y, z, 0), 99.0);
        }
      }
    }
  }
};

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  pqPVApplicationCore appCore(argc, argv);

  auto* builder = pqApplicationCore::instance()->getObjectBuilder();
  builder->createServer(pqServerResource("builtin:"));

  Tvh5DataTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "Tvh5DataTest.moc"
