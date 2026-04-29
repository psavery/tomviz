/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "Tvh5Format.h"

#include "EmdFormat.h"

#include "pipeline/Node.h"
#include "pipeline/OutputPort.h"
#include "pipeline/Pipeline.h"
#include "pipeline/PipelineStateIO.h"
#include "pipeline/PortData.h"
#include "pipeline/PortType.h"
#include "pipeline/SourceNode.h"
#include "pipeline/data/VolumeData.h"

#include <h5cpp/h5readwrite.h>

#include <QByteArray>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <vtkAbstractArray.h>
#include <vtkDataArray.h>
#include <vtkImageData.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTable.h>
#include <vtkType.h>

namespace tomviz {

namespace {

/// Write a numeric vtkDataArray column as a 1-D typed dataset.  Returns
/// false (without warning) for VTK scalar types we don't have an h5cpp
/// primitive overload for — the caller logs.
bool writeNumericColumn(h5::H5ReadWrite& writer,
                        const std::string& parentGroup,
                        const std::string& datasetName, vtkDataArray* arr)
{
  if (!arr) {
    return false;
  }
  std::vector<int> dims = {
    static_cast<int>(arr->GetNumberOfTuples() * arr->GetNumberOfComponents())
  };
  void* raw = arr->GetVoidPointer(0);
  switch (arr->GetDataType()) {
    case VTK_CHAR:
    case VTK_SIGNED_CHAR:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const char*>(raw));
    case VTK_UNSIGNED_CHAR:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const unsigned char*>(raw));
    case VTK_SHORT:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const short*>(raw));
    case VTK_UNSIGNED_SHORT:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const unsigned short*>(raw));
    case VTK_INT:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const int*>(raw));
    case VTK_UNSIGNED_INT:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const unsigned int*>(raw));
    case VTK_LONG:
      if constexpr (sizeof(long) == sizeof(long long)) {
        return writer.writeData(parentGroup, datasetName, dims,
                                static_cast<const long long*>(raw));
      } else {
        return writer.writeData(parentGroup, datasetName, dims,
                                static_cast<const int*>(raw));
      }
    case VTK_UNSIGNED_LONG:
      if constexpr (sizeof(unsigned long) == sizeof(unsigned long long)) {
        return writer.writeData(parentGroup, datasetName, dims,
                                static_cast<const unsigned long long*>(raw));
      } else {
        return writer.writeData(parentGroup, datasetName, dims,
                                static_cast<const unsigned int*>(raw));
      }
    case VTK_LONG_LONG:
    case VTK_ID_TYPE:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const long long*>(raw));
    case VTK_UNSIGNED_LONG_LONG:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const unsigned long long*>(raw));
    case VTK_FLOAT:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const float*>(raw));
    case VTK_DOUBLE:
      return writer.writeData(parentGroup, datasetName, dims,
                              static_cast<const double*>(raw));
    default:
      return false;
  }
}

/// Write a vtkStringArray column as a JSON array stored in a `char`
/// dataset.  HDF5 variable-length strings would be a more native
/// encoding, but h5cpp's writeData<> doesn't expose them — and the JSON
/// blob round-trips cleanly through readData<char>.
bool writeStringColumn(h5::H5ReadWrite& writer,
                       const std::string& parentGroup,
                       const std::string& datasetName, vtkStringArray* arr)
{
  QJsonArray values;
  vtkIdType n = arr->GetNumberOfValues();
  for (vtkIdType i = 0; i < n; ++i) {
    values.append(QString::fromStdString(arr->GetValue(i)));
  }
  QByteArray bytes = QJsonDocument(values).toJson(QJsonDocument::Compact);
  std::vector<int> dims = { static_cast<int>(bytes.size()) };
  return writer.writeData(parentGroup, datasetName, dims, bytes.constData());
}

/// Serialize @a table under @a portGroup.  Each column is stored as a
/// sub-dataset `c0`, `c1`, … with attributes `name` (column name) and
/// `vtkDataType` (the int returned by vtkAbstractArray::GetDataType, or
/// VTK_STRING for string columns).  Group-level attributes record the
/// payload kind and the column count for the reader.
bool writeTablePayload(h5::H5ReadWrite& writer, const std::string& portGroup,
                       vtkTable* table)
{
  if (!table) {
    return false;
  }
  writer.setAttribute(portGroup, "kind", "table");

  vtkIdType numColumns = table->GetNumberOfColumns();
  vtkIdType numRows = table->GetNumberOfRows();
  writer.setAttribute(portGroup, "numColumns",
                      static_cast<long long>(numColumns));
  writer.setAttribute(portGroup, "numRows",
                      static_cast<long long>(numRows));

  for (vtkIdType i = 0; i < numColumns; ++i) {
    vtkAbstractArray* col = table->GetColumn(i);
    if (!col) {
      continue;
    }
    std::string datasetName = "c" + std::to_string(i);
    std::string columnPath = portGroup + "/" + datasetName;
    bool wrote = false;
    int vtkDataType = col->GetDataType();
    if (auto* numeric = vtkDataArray::SafeDownCast(col)) {
      wrote = writeNumericColumn(writer, portGroup, datasetName, numeric);
    } else if (auto* strings = vtkStringArray::SafeDownCast(col)) {
      wrote = writeStringColumn(writer, portGroup, datasetName, strings);
      vtkDataType = VTK_STRING;
    }
    if (!wrote) {
      qWarning() << "Tvh5Format: skipping unsupported column"
                 << QString::fromStdString(col->GetName() ? col->GetName()
                                                          : "")
                 << "of type" << col->GetDataType();
      continue;
    }
    const char* name = col->GetName();
    writer.setAttribute(columnPath, "name", name ? name : "");
    writer.setAttribute(columnPath, "vtkDataType", vtkDataType);
    writer.setAttribute(columnPath, "numberOfComponents",
                        col->GetNumberOfComponents());
  }
  return true;
}

bool writeVolumePayload(h5::H5ReadWrite& writer, const std::string& portGroup,
                        const pipeline::PortData& data)
{
  auto volume = data.value<pipeline::VolumeDataPtr>();
  if (!volume || !volume->isValid()) {
    return false;
  }
  return EmdFormat::writeNode(writer, portGroup, volume->imageData());
}

/// For every node with a non-transient output port carrying serializable
/// data, write the payload into @a writer under `/data/<nodeId>/<portName>`
/// and stamp `dataRef` on the matching port entry in @a pipelineJson.
/// Transient ports (typically transform outputs that get consumed and
/// released downstream) are skipped — the port's "persistent" flag is the
/// contract.  Volume-typed and Table-typed ports are persisted; other
/// types fall through silently and are simply re-executed on load.
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
      pipeline::PortType declared = port->declaredType();
      bool isVolume = pipeline::isVolumeType(declared);
      bool isTable = (declared == pipeline::PortType::Table);
      if (!isVolume && !isTable) {
        continue;
      }

      std::string portName = port->name().toStdString();
      std::string nodeGroup = "/data/" + std::to_string(nodeId);
      std::string portGroup = nodeGroup + "/" + portName;
      if (!writer.isGroup(nodeGroup)) {
        writer.createGroup(nodeGroup);
      }
      writer.createGroup(portGroup);

      bool wrote = false;
      if (isVolume) {
        wrote = writeVolumePayload(writer, portGroup, port->data());
      } else if (isTable) {
        auto table = port->data().value<vtkSmartPointer<vtkTable>>();
        wrote = writeTablePayload(writer, portGroup, table.GetPointer());
      }
      if (!wrote) {
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

namespace {

/// Read a numeric column written by writeNumericColumn back into a fresh
/// vtkAbstractArray of the requested @a vtkDataType.  Returns null if the
/// type isn't supported or the read fails.
vtkSmartPointer<vtkAbstractArray> readNumericColumn(h5::H5ReadWrite& reader,
                                                    const std::string& path,
                                                    int vtkDataType,
                                                    int numberOfComponents)
{
  auto array =
    vtkSmartPointer<vtkAbstractArray>::Take(vtkAbstractArray::CreateArray(
      vtkDataType == VTK_ID_TYPE ? VTK_LONG_LONG : vtkDataType));
  if (!array) {
    return nullptr;
  }
  auto dims = reader.getDimensions(path);
  if (dims.empty()) {
    return nullptr;
  }
  vtkIdType total = 1;
  for (int d : dims) {
    total *= d;
  }
  array->SetNumberOfComponents(numberOfComponents > 0 ? numberOfComponents : 1);
  array->SetNumberOfTuples(total / array->GetNumberOfComponents());
  auto* dataArray = vtkDataArray::SafeDownCast(array);
  if (!dataArray) {
    return nullptr;
  }
  void* dst = dataArray->GetVoidPointer(0);
  bool ok = false;
  switch (vtkDataType) {
    case VTK_CHAR:
    case VTK_SIGNED_CHAR:
      ok = reader.readData(path, static_cast<char*>(dst));
      break;
    case VTK_UNSIGNED_CHAR:
      ok = reader.readData(path, static_cast<unsigned char*>(dst));
      break;
    case VTK_SHORT:
      ok = reader.readData(path, static_cast<short*>(dst));
      break;
    case VTK_UNSIGNED_SHORT:
      ok = reader.readData(path, static_cast<unsigned short*>(dst));
      break;
    case VTK_INT:
      ok = reader.readData(path, static_cast<int*>(dst));
      break;
    case VTK_UNSIGNED_INT:
      ok = reader.readData(path, static_cast<unsigned int*>(dst));
      break;
    case VTK_LONG:
      if constexpr (sizeof(long) == sizeof(long long)) {
        ok = reader.readData(path, static_cast<long long*>(dst));
      } else {
        ok = reader.readData(path, static_cast<int*>(dst));
      }
      break;
    case VTK_UNSIGNED_LONG:
      if constexpr (sizeof(unsigned long) == sizeof(unsigned long long)) {
        ok = reader.readData(path, static_cast<unsigned long long*>(dst));
      } else {
        ok = reader.readData(path, static_cast<unsigned int*>(dst));
      }
      break;
    case VTK_LONG_LONG:
    case VTK_ID_TYPE:
      ok = reader.readData(path, static_cast<long long*>(dst));
      break;
    case VTK_UNSIGNED_LONG_LONG:
      ok = reader.readData(path, static_cast<unsigned long long*>(dst));
      break;
    case VTK_FLOAT:
      ok = reader.readData(path, static_cast<float*>(dst));
      break;
    case VTK_DOUBLE:
      ok = reader.readData(path, static_cast<double*>(dst));
      break;
    default:
      return nullptr;
  }
  return ok ? array : nullptr;
}

vtkSmartPointer<vtkStringArray> readStringColumn(h5::H5ReadWrite& reader,
                                                 const std::string& path)
{
  auto bytes = reader.readData<char>(path);
  QJsonDocument doc =
    QJsonDocument::fromJson(QByteArray(bytes.data(), bytes.size()));
  if (!doc.isArray()) {
    return nullptr;
  }
  auto values = doc.array();
  auto array = vtkSmartPointer<vtkStringArray>::New();
  array->SetNumberOfValues(values.size());
  for (int i = 0; i < values.size(); ++i) {
    array->SetValue(i, values.at(i).toString().toStdString());
  }
  return array;
}

vtkSmartPointer<vtkTable> readTablePayload(h5::H5ReadWrite& reader,
                                           const std::string& portGroup)
{
  bool ok = false;
  auto numColumns =
    reader.attribute<long long>(portGroup, "numColumns", &ok);
  if (!ok) {
    return nullptr;
  }
  auto table = vtkSmartPointer<vtkTable>::New();
  for (long long i = 0; i < numColumns; ++i) {
    std::string datasetName = "c" + std::to_string(i);
    std::string columnPath = portGroup + "/" + datasetName;
    if (!reader.isDataSet(columnPath)) {
      qWarning() << "Tvh5Format: missing column dataset"
                 << QString::fromStdString(columnPath);
      continue;
    }
    int vtkDataType =
      reader.attribute<int>(columnPath, "vtkDataType", &ok);
    if (!ok) {
      continue;
    }
    int numberOfComponents = 1;
    if (reader.hasAttribute(columnPath, "numberOfComponents")) {
      numberOfComponents =
        reader.attribute<int>(columnPath, "numberOfComponents", &ok);
      if (!ok) {
        numberOfComponents = 1;
      }
    }
    auto name =
      reader.attribute<std::string>(columnPath, "name", &ok);
    if (!ok) {
      name.clear();
    }
    vtkSmartPointer<vtkAbstractArray> column;
    if (vtkDataType == VTK_STRING) {
      column = readStringColumn(reader, columnPath);
    } else {
      column =
        readNumericColumn(reader, columnPath, vtkDataType, numberOfComponents);
    }
    if (!column) {
      qWarning() << "Tvh5Format: failed to read column"
                 << QString::fromStdString(columnPath);
      continue;
    }
    if (!name.empty()) {
      column->SetName(name.c_str());
    }
    table->AddColumn(column);
  }
  return table;
}

} // namespace

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
        qWarning() << "Tvh5Format::populatePayloadData: missing payload group"
                   << QString::fromStdString(path);
        continue;
      }
      auto* port = node->outputPort(it.key());
      if (!port) {
        continue;
      }
      pipeline::PortType type = port->declaredType();
      if (pipeline::isVolumeType(type)) {
        vtkNew<vtkImageData> image;
        QVariantMap options = { { "askForSubsample", false } };
        if (!EmdFormat::readNode(reader, path, image, options)) {
          qWarning() << "Tvh5Format::populatePayloadData: failed to read"
                     << QString::fromStdString(path);
          continue;
        }
        auto volume = std::make_shared<pipeline::VolumeData>(image.Get());
        port->setData(pipeline::PortData(std::any(volume), type));
        node->markCurrent();
      } else if (type == pipeline::PortType::Table) {
        auto table = readTablePayload(reader, path);
        if (!table) {
          qWarning() << "Tvh5Format::populatePayloadData: failed to read table"
                     << QString::fromStdString(path);
          continue;
        }
        port->setData(pipeline::PortData(std::any(table), type));
        node->markCurrent();
      } else {
        qWarning() << "Tvh5Format::populatePayloadData: unsupported port type"
                   << pipeline::portTypeToString(type) << "at"
                   << QString::fromStdString(path);
      }
    }
  }
}

} // namespace tomviz
