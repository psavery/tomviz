/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "LoadDataReaction.h"

#include "ActiveObjects.h"
#include "DataExchangeFormat.h"
#include "EmdFormat.h"
#include "FileFormatManager.h"
#include "FxiFormat.h"
#include "GenericHDF5Format.h"
#include "ImageStackDialog.h"
#include "ImageStackModel.h"
#include "LoadStackReaction.h"
#include "MainWindow.h"
#include "legacy/modules/ModuleManager.h"
#include "MoleculeSource.h"
#include "legacy/Pipeline.h"
#include "legacy/PipelineManager.h"
#include "PythonReader.h"
#include "PythonUtilities.h"
#include "RAWFileReaderDialog.h"
#include "RecentFilesMenu.h"
#include "Utilities.h"
#include "vtkOMETiffReader.h"

#include "pipeline/Pipeline.h"
#include "pipeline/PortData.h"
#include "pipeline/PortType.h"
#include "pipeline/PortUtils.h"
#include "pipeline/SourceNode.h"
#include "pipeline/data/VolumeData.h"
#include "pipeline/sinks/OutlineSink.h"
#include "pipeline/sinks/VolumeSink.h"
#include "ColorMap.h"

#include <pqActiveObjects.h>
#include <pqLoadDataReaction.h>
#include <pqPipelineSource.h>
#include <pqProxyWidgetDialog.h>
#include <pqRenderView.h>
#include <pqSMAdaptor.h>
#include <pqView.h>
#include <vtkSMCoreUtilities.h>
#include <vtkSMParaViewPipelineController.h>
#include <vtkSMPropertyHelper.h>
#include <vtkSMSessionProxyManager.h>
#include <vtkSMSourceProxy.h>
#include <vtkSMStringVectorProperty.h>
#include <vtkSMViewProxy.h>

#include <vtkImageData.h>
#include <vtkMolecule.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTIFFReader.h>
#include <vtkTrivialProducer.h>
#include <vtkXYZMolReader2.h>

#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QMessageBox>

#include <sstream>

namespace {
bool hasData(vtkSMProxy* reader)
{
  vtkSMSourceProxy* dataSource = vtkSMSourceProxy::SafeDownCast(reader);
  if (!dataSource) {
    return false;
  }

  dataSource->UpdatePipeline();
  vtkAlgorithm* vtkalgorithm =
    vtkAlgorithm::SafeDownCast(dataSource->GetClientSideObject());
  if (!vtkalgorithm) {
    return false;
  }

  // Create a clone and release the reader data.
  vtkImageData* data =
    vtkImageData::SafeDownCast(vtkalgorithm->GetOutputDataObject(0));
  if (!data) {
    return false;
  }

  int extent[6];
  data->GetExtent(extent);
  if (extent[0] > extent[1] || extent[2] > extent[3] ||
      extent[4] > extent[5]) {
    return false;
  }

  vtkPointData* pd = data->GetPointData();
  if (!pd) {
    return false;
  }

  if (pd->GetNumberOfArrays() < 1) {
    return false;
  }
  return true;
}
} // namespace

namespace tomviz {

LoadDataReaction::LoadDataReaction(QAction* parentObject)
  : pqReaction(parentObject)
{}

LoadDataReaction::~LoadDataReaction() = default;

void LoadDataReaction::onTriggered()
{
  loadData();
}

pipeline::SourceNode* LoadDataReaction::createSourceFromImageData(
  vtkImageData* image, const QString& label, const QStringList& fileNames)
{
  auto* source = new pipeline::SourceNode();
  QString nodeLabel = label;
  if (nodeLabel.isEmpty() && !fileNames.isEmpty()) {
    nodeLabel = QFileInfo(fileNames[0]).completeBaseName();
  }
  source->setLabel(nodeLabel);
  auto volumeData = std::make_shared<pipeline::VolumeData>(image);
  volumeData->setLabel(nodeLabel);
  pipeline::PortType dataType = volumeData->hasTiltAngles()
    ? pipeline::PortType::TiltSeries
    : pipeline::PortType::Volume;
  source->addOutput("volume", dataType);
  source->setOutputData("volume", pipeline::PortData(volumeData, dataType));
  return source;
}

QList<pipeline::SourceNode*> LoadDataReaction::loadData(bool isTimeSeries)
{
  QStringList filters;
  filters << "Common file types (*.emd *.jpg *.jpeg *.png *.tiff *.tif *.h5 "
             "*.raw *.dat *.bin *.txt *.mhd *.mha *.vti *.mrc *.st *.rec "
             "*.ali *.xmf *.xdmf)"
          << "EMD (*.emd)"
          << "JPeg Image files (*.jpg *.jpeg)"
          << "PNG Image files (*.png)"
          << "TIFF Image files (*.tiff *.tif)"
          << "HDF5 files (*.h5)"
          << "OME-TIFF Image files (*.ome.tif)"
          << "Raw data files (*.raw *.dat *.bin)"
          << "Meta Image files (*.mhd *.mha)"
          << "VTK ImageData Files (*.vti)"
          << "MRC files (*.mrc *.st *.rec *.ali)"
          << "XDMF files (*.xmf *.xdmf)"
          << "Molecule files (*.xyz)"
          << "Text files (*.txt)";

  foreach (auto reader,
           FileFormatManager::instance().pythonReaderFactories()) {
    filters << reader->getFileDialogFilter();
  }

  filters << "All files (*.*)";

  QFileDialog dialog(nullptr);
  dialog.setFileMode(QFileDialog::ExistingFiles);
  dialog.setNameFilters(filters);
  dialog.setObjectName("FileOpenDialog-tomviz"); // avoid name collision?

  if (!dialog.exec()) {
    return {};
  }

  QStringList filenames = dialog.selectedFiles();

  QJsonObject options;
  if (isTimeSeries) {
    // Sort the file names so we get consistent behavior.
    filenames.sort();
    options["createCameraOrbit"] = false;
  }

  QList<pipeline::SourceNode*> sources;
  QString fileName = filenames.size() > 0 ? filenames[0] : "";
  QFileInfo info(fileName);
  auto suffix = info.suffix().toLower();
  QStringList moleculeExt = { "xyz" };
  if (moleculeExt.contains(suffix)) {
    loadMolecule(filenames);
  } else {
    for (auto f : filenames) {
      sources << loadData(f, options);
      if (isTimeSeries) {
        // After loading the first source in a time series, don't
        // add any more to the pipeline. We'll delete them below.
        options["addToPipeline"] = false;
      }
    }
  }

  if (isTimeSeries && !sources.isEmpty()) {
    // Combine all sources into the first one via time steps.
    auto firstVol =
      pipeline::getOutputData<pipeline::VolumeDataPtr>(sources[0]);
    if (firstVol) {
      std::vector<double> times;
      QList<pipeline::VolumeData::TimeStep> timeSteps;

      for (int i = 0; i < sources.size(); ++i) {
        auto vol =
          pipeline::getOutputData<pipeline::VolumeDataPtr>(sources[i]);
        if (!vol) {
          continue;
        }
        double t = static_cast<double>(i);
        times.push_back(t);
        timeSteps.append({ vol->label(), vol->imageData(), t });

        if (i != 0) {
          // Delete all sources other than the first one. These were not
          // added to the pipeline.
          sources[i]->deleteLater();
        }
      }
      firstVol->setTimeSteps(timeSteps);
      sources = { sources[0] };

      // Set the animation time steps and change the play mode to
      // "Snap To TimeSteps".
      tomviz::snapAnimationToTimeSteps(times);

      // Also set the number of time steps in the sequence to match
      // (this only matters if the user switches to "Sequence" play mode)
      tomviz::setAnimationNumberOfFrames(times.size());
    }
  }

  return sources;
}

pipeline::SourceNode* LoadDataReaction::loadData(const QString& fileName,
                                                  bool defaultModules,
                                                  bool addToRecent, bool child,
                                                  const QJsonObject& options)
{
  QJsonObject opts = options;
  opts["defaultModules"] = defaultModules;
  opts["addToRecent"] = addToRecent;
  opts["child"] = child;
  return LoadDataReaction::loadData(fileName, opts);
}

pipeline::SourceNode* LoadDataReaction::loadData(const QString& fileName,
                                                  const QJsonObject& val)
{
  QStringList fileNames;
  fileNames << fileName;

  return loadData(fileNames, val);
}

pipeline::SourceNode* LoadDataReaction::loadData(const QStringList& fileNames,
                                                  const QJsonObject& options)
{
  bool defaultModules = options["defaultModules"].toBool(true);
  bool addToRecent = options["addToRecent"].toBool(true);
  bool addToPipeline = options["addToPipeline"].toBool(true);
  bool createCameraOrbit = options["createCameraOrbit"].toBool(true);
  bool child = options["child"].toBool(false);
  bool loadWithParaview = true;
  bool loadWithPython = false;

  pipeline::SourceNode* source = nullptr;
  QString fileName;
  if (fileNames.size() > 0) {
    fileName = fileNames[0];
  }
  QFileInfo info(fileName);
  if (info.suffix().toLower() == "tvh5") {
    // Need to specify a path inside the tvh5 file to load
    QString path = options["tvh5NodePath"].toString();
    if (path.isEmpty()) {
      qCritical() << "A path is required to read tvh5 as a data file";
      return nullptr;
    }

    loadWithParaview = false;
    // Do not prompt the user for subsample settings
    QVariantMap emdOptions = { { "askForSubsample", false } };
    vtkNew<vtkImageData> image;
    if (EmdFormat::readNode(fileName.toStdString(), path.toStdString(),
                            image, emdOptions)) {
      source = createSourceFromImageData(image, info.completeBaseName(),
                                         fileNames);
      // Save the node path in case we write the data again in the future
      source->setProperty("tvh5NodePath", path);
    }
  } else if (info.suffix().toLower() == "emd") {
    // Load the file using our simple EMD class.
    loadWithParaview = false;
    QVariantMap emdOptions;
    vtkNew<vtkImageData> imageData;
    if (options.contains("subsampleSettings")) {
      // Before we read into the image data, set subsample settings
      emdOptions["subsampleStrides"] =
        options["subsampleSettings"].toObject()["strides"].toVariant();
      emdOptions["subsampleVolumeBounds"] =
        options["subsampleSettings"].toObject()["volumeBounds"].toVariant();
      emdOptions["askForSubsample"] = false;
    }
    if (EmdFormat::read(fileName.toLatin1().data(), imageData, emdOptions)) {
      source = createSourceFromImageData(imageData,
                                         info.completeBaseName(), fileNames);
    }
  } else if (info.suffix().toLower() == "h5") {
    loadWithParaview = false;
    QVariantMap hdf5Options;
    if (options.contains("subsampleSettings")) {
      hdf5Options["subsampleStrides"] =
        options["subsampleSettings"].toObject()["strides"].toVariant();
      hdf5Options["subsampleVolumeBounds"] =
        options["subsampleSettings"].toObject()["volumeBounds"].toVariant();
      hdf5Options["askForSubsample"] = false;
    }
    // Check if it looks like data exchange
    if (GenericHDF5Format::isDataExchange(fileName.toStdString())) {
      DataExchangeFormat format;
      auto result = format.readAll(fileName.toLatin1().data(), hdf5Options);
      if (!result.imageData) {
        return nullptr;
      }
      source = createSourceFromImageData(result.imageData,
                                         info.completeBaseName(), fileNames);
      auto vol =
        pipeline::getOutputData<pipeline::VolumeDataPtr>(source);
      if (vol && !result.tiltAngles.isEmpty()) {
        vol->setTiltAngles(result.tiltAngles);
        source->outputPort("volume")->setDeclaredType(
          pipeline::PortType::TiltSeries);
        source->setProperty("dataType", "tiltSeries");
      }
      // Store dark/white data as node properties for later use
      if (result.darkData) {
        source->setProperty(
          "darkData",
          QVariant::fromValue(
            static_cast<void*>(result.darkData.GetPointer())));
      }
      if (result.whiteData) {
        source->setProperty(
          "whiteData",
          QVariant::fromValue(
            static_cast<void*>(result.whiteData.GetPointer())));
      }
    } else if (GenericHDF5Format::isFxi(fileName.toStdString())) {
      FxiFormat format;
      auto result = format.readAll(fileName.toLatin1().data(), hdf5Options);
      if (!result.imageData) {
        return nullptr;
      }
      source = createSourceFromImageData(result.imageData,
                                         info.completeBaseName(), fileNames);
      auto vol =
        pipeline::getOutputData<pipeline::VolumeDataPtr>(source);
      if (vol && !result.tiltAngles.isEmpty()) {
        vol->setTiltAngles(result.tiltAngles);
        source->outputPort("volume")->setDeclaredType(
          pipeline::PortType::TiltSeries);
        source->setProperty("dataType", "tiltSeries");
      }
      if (result.darkData) {
        source->setProperty(
          "darkData",
          QVariant::fromValue(
            static_cast<void*>(result.darkData.GetPointer())));
      }
      if (result.whiteData) {
        source->setProperty(
          "whiteData",
          QVariant::fromValue(
            static_cast<void*>(result.whiteData.GetPointer())));
      }
    } else {
      vtkNew<vtkImageData> imageData;
      GenericHDF5Format hdf5Format;
      if (!hdf5Format.read(fileName.toLatin1().data(), imageData,
                           hdf5Options)) {
        return nullptr;
      }
      source = createSourceFromImageData(imageData,
                                         info.completeBaseName(), fileNames);
    }
  } else if (info.completeSuffix().endsWith("ome.tif")) {
    loadWithParaview = false;
    vtkNew<vtkOMETiffReader> reader;
    reader->SetFileName(fileName.toLocal8Bit().constData());
    reader->Update();
    auto* imageData = reader->GetOutput();

    source =
      createSourceFromImageData(imageData, info.completeBaseName(), fileNames);
    QJsonObject rprops;
    rprops["name"] = "OMETIFFReader";
    source->setProperty("readerProperties",
                        QVariant::fromValue(rprops.toVariantMap()));
  } else if (FileFormatManager::instance().pythonReaderFactory(
               info.suffix().toLower()) != nullptr) {
    loadWithParaview = false;
    loadWithPython = true;
  } else if (options.contains("reader")) {
    loadWithParaview = false;
    // Create the ParaView reader and set its properties using the JSON
    // configuration.
    auto props = options["reader"].toObject();
    auto name = props["name"].toString();

    auto pxm = ActiveObjects::instance().proxyManager();
    vtkSmartPointer<vtkSMProxy> reader;
    reader.TakeReference(pxm->NewProxy("sources", name.toLatin1().data()));

    setProperties(props, reader);
    setFileNameProperties(props, reader);
    reader->UpdateVTKObjects();
    vtkSMSourceProxy::SafeDownCast(reader)->UpdatePipelineInformation();

    // We'll add it to the pipeline on our own later, if needed
    bool addToThePipeline = false;
    source = LoadDataReaction::createFromParaViewReader(
      reader, defaultModules, child, addToThePipeline);
    if (!source) {
      return nullptr;
    }

    source->setProperty("readerProperties",
                        QVariant::fromValue(props.toVariantMap()));
  }

  if (loadWithParaview) {
    // Use ParaView's file load infrastructure.
    pqPipelineSource* reader = pqLoadDataReaction::loadData(fileNames);
    if (!reader) {
      return nullptr;
    }

    // We'll add it to the pipeline on our own later, if needed
    bool addToThePipeline = false;
    source = createFromParaViewReader(reader->getProxy(), defaultModules,
                                      child, addToThePipeline);
    if (!source) {
      return nullptr;
    }

    QJsonObject props = readerProperties(reader->getProxy());
    props["name"] = reader->getProxy()->GetXMLName();

    source->setProperty("readerProperties",
                        QVariant::fromValue(props.toVariantMap()));

    vtkNew<vtkSMParaViewPipelineController> controller;
    controller->UnRegisterProxy(reader->getProxy());
  } else if (loadWithPython) {
    QString ext = info.suffix().toLower();
    auto factory = FileFormatManager::instance().pythonReaderFactory(ext);
    Q_ASSERT(factory != nullptr);
    auto reader = factory->createReader();
    auto imageData = reader.read(fileNames[0]);
    if (imageData == nullptr) {
      return nullptr;
    }
    source =
      createSourceFromImageData(imageData, info.completeBaseName(), fileNames);
  }

  // It is possible that the source will be null if, for example, loading
  // a VTI is cancelled in the array selection dialog. Guard against this.
  if (!source) {
    return nullptr;
  }

  // Set file names as a node property so the label is available.
  source->setProperty("fileNames", fileNames);

  if (addToPipeline) {
    // Add to the pipeline if needed...
    LoadDataReaction::sourceNodeAdded(source, defaultModules, child,
                                      createCameraOrbit);
  }

  if (addToRecent && source) {
    RecentFilesMenu::pushDataReader(source);
  }
  return source;
}

pipeline::SourceNode* LoadDataReaction::createFromParaViewReader(
  vtkSMProxy* reader, bool defaultModules, bool child, bool addToPipeline)
{
  // Prompt user for reader configuration, unless it is TIFF.
  QScopedPointer<QDialog> dialog(new pqProxyWidgetDialog(reader));
  bool hasVisibleWidgets =
    qobject_cast<pqProxyWidgetDialog*>(dialog.data())->hasVisibleWidgets();
  if (QString(reader->GetXMLName()) == "TVRawImageReader") {
    dialog.reset(new RAWFileReaderDialog(reader));
    hasVisibleWidgets = true;
    // This will show only a partial filename when reading multiple files as a
    // raw image
    vtkSMPropertyHelper fname(reader, "FilePrefix");
    QFileInfo info(fname.GetAsString());
    dialog->setWindowTitle(QString("Opening %1").arg(info.fileName()));
  } else {
    dialog->setWindowTitle("Configure Reader Parameters");
  }
  dialog->setObjectName("ConfigureReaderDialog");
  if (QString(reader->GetXMLName()) == "TIFFSeriesReader" ||
      hasVisibleWidgets == false || dialog->exec() == QDialog::Accepted) {

    if (!hasData(reader)) {
      qCritical() << "Error: failed to load file!";
      return nullptr;
    }

    auto pvSource = vtkSMSourceProxy::SafeDownCast(reader);
    pvSource->UpdatePipeline();
    auto algo =
      vtkAlgorithm::SafeDownCast(pvSource->GetClientSideObject());
    auto data = algo->GetOutputDataObject(0);
    auto image = vtkImageData::SafeDownCast(data);

    QString readerFileName;
    auto prop = reader->GetProperty("FileNames");
    if (prop != nullptr) {
      auto jsonProp = toJson(prop);
      if (jsonProp.toArray().size() > 0) {
        readerFileName = jsonProp.toArray()[0].toString();
      } else {
        readerFileName = jsonProp.toString();
      }
      QFileInfo fInfo(readerFileName);
      // Special case: mrc files store spacing in Angstrom
      QStringList mrcExt;
      mrcExt << "mrc"
             << "st"
             << "rec"
             << "ali";
      if (mrcExt.contains(fInfo.suffix().toLower())) {
        double spacing[3];
        image->GetSpacing(spacing);
        for (int i = 0; i < 3; ++i) {
          spacing[i] *= 0.1;
        }
        image->SetSpacing(spacing);
      }
    }

    QString label;
    if (!readerFileName.isEmpty()) {
      label = QFileInfo(readerFileName).completeBaseName();
    }

    auto* source = createSourceFromImageData(
      image, label, readerFileName.isEmpty() ? QStringList()
                                             : QStringList({ readerFileName }));

    // Do whatever we need to do with a new source node.
    if (addToPipeline) {
      LoadDataReaction::sourceNodeAdded(source, defaultModules, child);
    }
    return source;
  }
  return nullptr;
}

void LoadDataReaction::sourceNodeAdded(pipeline::SourceNode* source,
                                       bool defaultModules, bool /* child */,
                                       bool /* createCameraOrbit */)
{
  if (!source) {
    return;
  }

  auto* mainWindow = qobject_cast<MainWindow*>(QApplication::activeWindow());
  auto* pip = mainWindow ? mainWindow->pipeline() : nullptr;
  if (!pip) {
    return;
  }

  pip->addNode(source);

  // Initialize color/opacity maps with default preset and data range
  auto volumeData =
    pipeline::getOutputData<pipeline::VolumeDataPtr>(source);
  if (volumeData) {
    ColorMap::instance().applyPreset(volumeData->colorMap());
    volumeData->rescaleColorMap();
  }

  // Get view for visualization
  ActiveObjects::instance().createRenderViewIfNeeded();
  auto view = ActiveObjects::instance().activeView();

  if (!view || QString(view->GetXMLName()) != "RenderView") {
    ActiveObjects::instance().setActiveViewToFirstRenderView();
    view = ActiveObjects::instance().activeView();
  }

  if (defaultModules && view) {
    // Add default sinks (Outline + Volume)
    auto* outline = new pipeline::OutlineSink();
    outline->setLabel("Outline");
    outline->initialize(view);
    pip->addNode(outline);
    pip->createLink(source->outputPorts()[0], outline->inputPorts()[0]);

    auto* volume = new pipeline::VolumeSink();
    volume->setLabel("Volume");
    volume->initialize(view);
    pip->addNode(volume);
    pip->createLink(source->outputPorts()[0], volume->inputPorts()[0]);
  }

  // Execute the pipeline (renders the data)
  pip->execute();
}

QJsonObject LoadDataReaction::readerProperties(vtkSMProxy* reader)
{
  QStringList propNames({ "DataScalarType", "DataByteOrder",
                          "NumberOfScalarComponents", "DataExtent" });

  QJsonObject props;
  foreach (QString propName, propNames) {
    auto prop = reader->GetProperty(propName.toLatin1().data());
    if (prop != nullptr) {
      props[propName] = toJson(prop);
    }
  }

  // Special case file name related properties
  auto prop = reader->GetProperty("FileName");
  if (prop != nullptr) {
    props["fileName"] = toJson(prop);
  }
  prop = reader->GetProperty("FileNames");
  if (prop != nullptr) {
    auto fileNames = toJson(prop).toArray();
    if (fileNames.size() > 1) {
      props["fileNames"] = fileNames;
    }
    // Normalize to fileNames for single value.
    else if (fileNames.size() == 1) {
      props["fileName"] = fileNames[0];
    }
  }
  prop = reader->GetProperty("FilePrefix");
  if (prop != nullptr) {
    props["fileName"] = toJson(prop);
  }

  return props;
}

void LoadDataReaction::setFileNameProperties(const QJsonObject& props,
                                             vtkSMProxy* reader)
{
  auto prop = reader->GetProperty("FileName");
  if (prop != nullptr) {
    if (!props.contains("fileName")) {
      qCritical() << "Reader doesn't have 'fileName' property.";
      return;
    }
    tomviz::setProperty(props["fileName"], prop);
  }
  prop = reader->GetProperty("FileNames");
  if (prop != nullptr) {
    if (!props.contains("fileNames") && !props.contains("fileName")) {
      qCritical()
        << "Reader doesn't have 'fileName' or 'fileNames' property.";
      return;
    }

    if (props.contains("fileNames")) {
      tomviz::setProperty(props["fileNames"], prop);
    } else {
      QJsonArray fileNames;
      fileNames.append(props["fileName"]);
      tomviz::setProperty(fileNames, prop);
    }
  }
  prop = reader->GetProperty("FilePrefix");
  if (prop != nullptr) {
    if (!props.contains("fileName")) {
      qCritical() << "Reader doesn't have 'fileName' property.";
      return;
    }
    tomviz::setProperty(props["fileName"], prop);
  }
}

QList<MoleculeSource*> LoadDataReaction::loadMolecule(
  const QStringList& fileNames, const QJsonObject& options)
{
  QList<MoleculeSource*> moleculeSources;
  foreach (auto fileName, fileNames) {
    moleculeSources << loadMolecule(fileName, options);
  }
  return moleculeSources;
}

MoleculeSource* LoadDataReaction::loadMolecule(const QString& fileName,
                                               const QJsonObject& options)
{
  if (fileName.isEmpty()) {
    return nullptr;
  }

  bool addToRecent = options["addToRecent"].toBool(true);
  bool defaultModules = options["defaultModules"].toBool(true);

  vtkMolecule* molecule = vtkMolecule::New();
  vtkNew<vtkXYZMolReader2> reader;
  reader->SetFileName(fileName.toLatin1().data());
  reader->SetOutput(molecule);
  reader->Update();

  auto moleculeSource = new MoleculeSource(molecule);
  moleculeSource->setFileName(fileName);
  ModuleManager::instance().addMoleculeSource(moleculeSource);
  if (moleculeSource && defaultModules) {
    auto view = ActiveObjects::instance().activeView();
    ModuleManager::instance().createAndAddModule("Molecule", moleculeSource,
                                                 view);
  }
  if (moleculeSource && addToRecent) {
    RecentFilesMenu::pushMoleculeReader(moleculeSource);
  }
  return moleculeSource;
}

} // end of namespace tomviz
