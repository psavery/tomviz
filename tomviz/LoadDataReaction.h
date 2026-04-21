/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizLoadDataReaction_h
#define tomvizLoadDataReaction_h

#include <pqReaction.h>

#include <QJsonObject>

#include "ImageStackModel.h"

class vtkImageData;
class vtkSMProxy;

namespace tomviz {
class MoleculeSource;

namespace pipeline {
class SourceNode;
}

class PythonReaderFactory;

/// LoadDataReaction handles the "Load Data" action in tomviz. On trigger,
/// this will open the data file and necessary subsequent actions, including:
/// \li make the data source "active".
///
class LoadDataReaction : public pqReaction
{
  Q_OBJECT

public:
  LoadDataReaction(QAction* parentAction);
  ~LoadDataReaction() override;

  static QList<pipeline::SourceNode*> loadData(bool isTimeSeries = false);

  /// Convenience method, adds defaultModules, addToRecent, and child to the
  /// JSON object before passing it to the loadData methods.
  static pipeline::SourceNode* loadData(const QString& fileName,
                                        bool defaultModules, bool addToRecent,
                                        bool child,
                                        const QJsonObject& options =
                                          QJsonObject());

  /// Load a data file from the specified location, options can be used to pass
  /// additional parameters to the method, such as defaultModules, addToRecent,
  /// and child, or pvXML to pass to the ParaView reader.
  static pipeline::SourceNode* loadData(
    const QString& fileName, const QJsonObject& options = QJsonObject());

  /// Load data files from the specified locations, options can be used to pass
  /// additional parameters to the method, such as defaultModules, addToRecent,
  /// and child, or pvXML to pass to the ParaView reader.
  static pipeline::SourceNode* loadData(
    const QStringList& fileNames, const QJsonObject& options = QJsonObject());

  static QList<MoleculeSource*> loadMolecule(
    const QStringList& fileNames, const QJsonObject& options = QJsonObject());
  static MoleculeSource* loadMolecule(
    const QString& fileName, const QJsonObject& options = QJsonObject());

  /// Handle creation of a new source node (data already set on the node).
  static void sourceNodeAdded(pipeline::SourceNode* source,
                              bool defaultModules = true, bool child = false,
                              bool createCameraOrbit = true);

  /// Create a SourceNode from a ParaView reader proxy.
  static pipeline::SourceNode* createFromParaViewReader(
    vtkSMProxy* reader, bool defaultModules = true, bool child = false,
    bool addToPipeline = true);

  /// Capture reader-proxy property values for round-tripping.
  static QJsonObject readerProperties(vtkSMProxy* reader);

  /// Apply filename-related properties from a reader descriptor onto a
  /// ParaView proxy.
  static void setFileNameProperties(const QJsonObject& props,
                                    vtkSMProxy* reader);

protected:
  /// Called when the action is triggered.
  void onTriggered() override;

private:
  Q_DISABLE_COPY(LoadDataReaction)

  static pipeline::SourceNode* createSourceFromImageData(
    vtkImageData* image, const QString& label,
    const QStringList& fileNames = {});
};
} // namespace tomviz

#endif
