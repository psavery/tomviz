/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SaveDataReaction.h"

#include "ConvertToFloatOperator.h"
#include "DataExchangeFormat.h"
#include "EmdFormat.h"
#include "Utilities.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "FileFormatManager.h"
#include "ModuleManager.h"
#include "PythonWriter.h"

#include <pqActiveObjects.h>
#include <pqPipelineSource.h>
#include <pqProxyWidgetDialog.h>
#include <pqSaveDataReaction.h>
#include <vtkSMCoreUtilities.h>
#include <vtkSMParaViewPipelineController.h>
#include <vtkSMPropertyHelper.h>
#include <vtkSMProxyManager.h>
#include <vtkSMSessionProxyManager.h>
#include <vtkSMSourceProxy.h>
#include <vtkSMWriterFactory.h>

#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkTIFFWriter.h>
#include <vtkTrivialProducer.h>

#include <cassert>

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace tomviz {

SaveDataReaction::SaveDataReaction(QAction* parentObject)
  : pqReaction(parentObject)
{
  // TODO: migrate to new pipeline
  // Old code connected to ActiveObjects::dataSourceChanged
  connect(&ActiveObjects::instance(), &ActiveObjects::activeNodeChanged, this,
          &SaveDataReaction::updateEnableState);
  updateEnableState();
}

void SaveDataReaction::updateEnableState()
{
  // TODO: migrate to new pipeline
  // Old code checked activeDataSource() != nullptr
  parentAction()->setEnabled(false);
}

void SaveDataReaction::onTriggered()
{
  QStringList filters;
  filters << "TIFF format (*.tiff)"
          << "EMD format (*.emd *.hdf5)"
          << "HDF5 format (*.h5)"
          << "CSV File (*.csv)"
          << "Exodus II File (*.e *.ex2 *.ex2v2 *.exo *.exoII *.exoii *.g)"
          << "Legacy VTK Files (*.vtk)"
          << "Meta Image Files (*.mhd)"
          << "ParaView Data Files (*.pvd)"
          << "VTK ImageData Files (*.vti)"
          << "XDMF Data File (*.xmf)"
          << "JSON Image Files (*.json)";

  foreach (auto writer, FileFormatManager::instance().pythonWriterFactories()) {
    filters << writer->getFileDialogFilter();
  }

  QFileDialog dialog(nullptr);
  dialog.setFileMode(QFileDialog::AnyFile);
  dialog.setNameFilters(filters);
  dialog.setObjectName("FileOpenDialog-tomviz"); // avoid name collision?
  dialog.setAcceptMode(QFileDialog::AcceptSave);

  if (dialog.exec() == QDialog::Accepted) {
    QStringList filenames = dialog.selectedFiles();
    QString format = dialog.selectedNameFilter();
    QString filename = filenames[0];
    int startPos = format.indexOf("(") + 1;
    int n = format.indexOf(")") - startPos;
    QString extensionString = format.mid(startPos, n);
    QStringList extensions = extensionString.split(QRegularExpression(" ?\\*"),
                                                   Qt::SkipEmptyParts);
    bool hasExtension = false;
    for (QString& str : extensions) {
      if (filename.endsWith(str)) {
        hasExtension = true;
      }
    }
    if (!hasExtension) {
      filename = QString("%1%2").arg(filename, extensions[0]);
    }
    saveData(filename);
  }
}

bool SaveDataReaction::saveData(const QString& filename)
{
  // TODO: migrate to new pipeline
  // Old code used ActiveObjects::activeDataSource() and activeOperatorResult()
  // to get the data to save. Stubbed out until new pipeline provides equivalent.
  Q_UNUSED(filename);
  qCritical("SaveDataReaction::saveData not yet migrated to new pipeline.");
  return false;
}

} // end of namespace tomviz
