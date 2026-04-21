/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "Tvh5Format.h"

#include "EmdFormat.h"

#include "pipeline/Node.h"
#include "pipeline/OutputPort.h"
#include "pipeline/Pipeline.h"
#include "pipeline/PipelineStateIO.h"
#include "pipeline/PortData.h"
#include "pipeline/SourceNode.h"
#include "pipeline/data/VolumeData.h"

#include <h5cpp/h5readwrite.h>

#include <QByteArray>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <vtkImageData.h>

namespace tomviz {

namespace {

/// For every node with a non-transient output port carrying a valid
/// VolumeData, write the voxel bytes into @a writer under
/// `/data/<nodeId>/<portName>` and stamp `dataRef` on the matching
/// port entry in @a pipelineJson. Transient ports (typically transform
/// outputs that get consumed and released downstream) are skipped —
/// the port's "persistent" flag is the contract.
bool writePersistentPayloads(pipeline::Pipeline* pipeline,
                             h5::H5ReadWrite& writer,
                             QJsonObject& pipelineJson)
{
  writer.createGroup("/data");

  auto nodesArray = pipelineJson.value(QStringLiteral("nodes")).toArray();

  // Build an id -> index map so we can locate the right node entry fast.
  QHash<int, int> idToIndex;
  for (int i = 0; i < nodesArray.size(); ++i) {
    int id = nodesArray[i].toObject().value(QStringLiteral("id")).toInt(-1);
    if (id >= 0) {
      idToIndex.insert(id, i);
    }
  }

  for (auto* node : pipeline->nodes()) {
    int nodeId = pipeline->nodeId(node);
    auto nodeIt = idToIndex.constFind(nodeId);
    if (nodeIt == idToIndex.constEnd()) {
      continue;
    }
    auto nodeEntry = nodesArray[nodeIt.value()].toObject();
    auto outputs = nodeEntry.value(QStringLiteral("outputPorts")).toObject();

    bool modified = false;
    for (auto* port : node->outputPorts()) {
      if (port->isTransient() || !port->hasData()) {
        continue;
      }
      auto volume = port->data().value<pipeline::VolumeDataPtr>();
      if (!volume || !volume->isValid()) {
        continue;
      }
      std::string portName = port->name().toStdString();
      std::string nodeGroup = "/data/" + std::to_string(nodeId);
      std::string portGroup = nodeGroup + "/" + portName;
      if (!writer.isGroup(nodeGroup)) {
        writer.createGroup(nodeGroup);
      }
      writer.createGroup(portGroup);
      if (!EmdFormat::writeNode(writer, portGroup, volume->imageData())) {
        qWarning() << "Tvh5Format: failed to write data for node" << nodeId
                   << "port" << port->name();
        return false;
      }

      QJsonObject portEntry = outputs.value(port->name()).toObject();
      QJsonObject dataRef;
      dataRef[QStringLiteral("container")] = QStringLiteral("h5");
      dataRef[QStringLiteral("path")] =
        QString::fromStdString(portGroup);
      portEntry[QStringLiteral("dataRef")] = dataRef;
      outputs[port->name()] = portEntry;
      modified = true;
    }

    if (modified) {
      nodeEntry[QStringLiteral("outputPorts")] = outputs;
      nodesArray[nodeIt.value()] = nodeEntry;
    }
  }

  pipelineJson[QStringLiteral("nodes")] = nodesArray;
  return true;
}

} // namespace

bool Tvh5Format::write(const std::string& fileName,
                       pipeline::Pipeline* pipeline,
                       const QJsonObject& extraState)
{
  if (!pipeline) {
    qWarning() << "Tvh5Format::write: null pipeline";
    return false;
  }

  QJsonObject state;
  if (!pipeline::PipelineStateIO::save(pipeline, state)) {
    qWarning() << "Tvh5Format::write: PipelineStateIO::save failed";
    return false;
  }

  // Merge caller-supplied views/layouts/palette. Later keys in
  // extraState win on conflict, matching the expected caller intent.
  for (auto it = extraState.constBegin(); it != extraState.constEnd();
       ++it) {
    state.insert(it.key(), it.value());
  }

  // Create the HDF5 container.
  using h5::H5ReadWrite;
  H5ReadWrite writer(fileName, H5ReadWrite::OpenMode::WriteOnly);

  // Embed per-port voxels and stamp dataRef entries in the pipeline
  // section before serializing the final JSON.
  auto pipelineJson = state.value(QStringLiteral("pipeline")).toObject();
  if (!writePersistentPayloads(pipeline, writer, pipelineJson)) {
    return false;
  }
  state[QStringLiteral("pipeline")] = pipelineJson;

  // Write the final JSON as a string dataset at /tomviz_state.
  QByteArray stateBytes = QJsonDocument(state).toJson();
  if (!writer.writeData("/", "tomviz_state",
                        { static_cast<int>(stateBytes.size()) },
                        stateBytes.data())) {
    qWarning() << "Tvh5Format::write: failed to write /tomviz_state";
    return false;
  }

  return true;
}

QJsonObject Tvh5Format::readState(const std::string& fileName)
{
  using h5::H5ReadWrite;
  H5ReadWrite reader(fileName, H5ReadWrite::OpenMode::ReadOnly);
  if (!reader.isDataSet("/tomviz_state")) {
    return {};
  }
  auto bytes = reader.readData<char>("tomviz_state");
  QJsonDocument doc =
    QJsonDocument::fromJson(QByteArray(bytes.data(), bytes.size()));
  if (!doc.isObject()) {
    return {};
  }
  return doc.object();
}

void Tvh5Format::populatePayloadData(pipeline::Pipeline* pipeline,
                                     const QJsonObject& pipelineJson,
                                     const std::string& fileName)
{
  if (!pipeline) {
    return;
  }
  using h5::H5ReadWrite;
  H5ReadWrite reader(fileName, H5ReadWrite::OpenMode::ReadOnly);

  auto nodesJson = pipelineJson.value(QStringLiteral("nodes")).toArray();
  for (const auto& nv : nodesJson) {
    auto nodeEntry = nv.toObject();
    int nodeId = nodeEntry.value(QStringLiteral("id")).toInt(-1);
    if (nodeId < 0) {
      continue;
    }
    auto* node = pipeline->nodeById(nodeId);
    if (!node) {
      continue;
    }
    auto outputs =
      nodeEntry.value(QStringLiteral("outputPorts")).toObject();
    for (auto it = outputs.constBegin(); it != outputs.constEnd(); ++it) {
      auto portEntry = it.value().toObject();
      auto dataRef = portEntry.value(QStringLiteral("dataRef")).toObject();
      if (dataRef.value(QStringLiteral("container")).toString() !=
          QLatin1String("h5")) {
        continue;
      }
      std::string path =
        dataRef.value(QStringLiteral("path")).toString().toStdString();
      if (path.empty() || !reader.isGroup(path)) {
        qWarning() << "Tvh5Format::populatePayloadData: missing voxel group"
                   << QString::fromStdString(path);
        continue;
      }
      auto* port = node->outputPort(it.key());
      if (!port) {
        continue;
      }
      vtkNew<vtkImageData> image;
      QVariantMap options = { { "askForSubsample", false } };
      if (!EmdFormat::readNode(reader, path, image, options)) {
        qWarning() << "Tvh5Format::populatePayloadData: failed to read"
                   << QString::fromStdString(path);
        continue;
      }
      auto volume = std::make_shared<pipeline::VolumeData>(image.Get());
      auto type = port->declaredType();
      pipeline::PortData pd(std::any(volume), type);
      port->setData(pd);
      node->markCurrent();
    }
  }
}

} // namespace tomviz
