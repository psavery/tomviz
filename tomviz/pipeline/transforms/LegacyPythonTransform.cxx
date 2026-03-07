/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "LegacyPythonTransform.h"

#include "data/VolumeData.h"

// Qt defines 'slots' as a macro which conflicts with Python's object.h.
// We must undef it before including any pybind11/Python headers.
#pragma push_macro("slots")
#undef slots
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include "pybind11/PybindVTKTypeCaster.h"
#pragma pop_macro("slots")

#include <vtkImageData.h>
#include <vtkNew.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

PYBIND11_VTK_TYPECASTER(vtkImageData)

namespace py = pybind11;

namespace tomviz {
namespace pipeline {

LegacyPythonTransform::LegacyPythonTransform(QObject* parent)
  : TransformNode(parent)
{
  // Always have volume in/out
  addInput("volume", PortType::Volume);
  addOutput("volume", PortType::Volume);
}

void LegacyPythonTransform::setJSONDescription(const QString& json)
{
  m_jsonDescription = json;
  parseJSON();
}

QString LegacyPythonTransform::jsonDescription() const
{
  return m_jsonDescription;
}

void LegacyPythonTransform::setScript(const QString& script)
{
  m_script = script;
}

QString LegacyPythonTransform::scriptSource() const
{
  return m_script;
}

void LegacyPythonTransform::setParameter(const QString& name,
                                                  const QVariant& value)
{
  m_parameters[name] = value;
}

QVariant LegacyPythonTransform::parameter(const QString& name) const
{
  return m_parameters.value(name);
}

QMap<QString, QVariant> LegacyPythonTransform::parameters() const
{
  return m_parameters;
}

QString LegacyPythonTransform::operatorName() const
{
  return m_operatorName;
}

void LegacyPythonTransform::parseJSON()
{
  QJsonDocument doc = QJsonDocument::fromJson(m_jsonDescription.toUtf8());
  if (!doc.isObject()) {
    return;
  }

  QJsonObject obj = doc.object();

  m_operatorName = obj.value("name").toString();

  if (obj.contains("label")) {
    setLabel(obj.value("label").toString());
  }

  // Parse parameters with defaults
  if (obj.contains("parameters")) {
    QJsonArray params = obj.value("parameters").toArray();
    for (const auto& paramVal : params) {
      QJsonObject param = paramVal.toObject();
      QString name = param.value("name").toString();
      QString type = param.value("type").toString();
      QJsonValue defaultVal = param.value("default");

      QVariant value;
      if (type == "double") {
        value = QVariant(defaultVal.toDouble());
      } else if (type == "int" || type == "integer") {
        value = QVariant(defaultVal.toInt());
      } else if (type == "bool" || type == "boolean") {
        value = QVariant(defaultVal.toBool());
      } else if (type == "string") {
        value = QVariant(defaultVal.toString());
      } else {
        value = QVariant(defaultVal.toDouble());
      }

      m_parameters[name] = value;
    }
  }

  // Parse results → additional output ports
  m_resultNames.clear();
  m_resultTypes.clear();
  if (obj.contains("results")) {
    QJsonArray results = obj.value("results").toArray();
    for (const auto& resultVal : results) {
      QJsonObject result = resultVal.toObject();
      QString name = result.value("name").toString();
      QString type = result.value("type").toString();

      m_resultNames.append(name);
      m_resultTypes.append(type);

      PortType portType = PortType::None;
      if (type == "table") {
        portType = PortType::Table;
      } else if (type == "molecule") {
        portType = PortType::Molecule;
      }

      if (portType != PortType::None) {
        addOutput(name, portType);
      }
    }
  }
}

QMap<QString, PortData> LegacyPythonTransform::transform(
  const QMap<QString, PortData>& inputs)
{
  QMap<QString, PortData> result;

  // Get the input volume
  auto inputVolume = inputs["volume"].value<VolumeDataPtr>();
  if (!inputVolume || !inputVolume->isValid()) {
    return result;
  }

  // Deep copy the vtkImageData (operators modify in-place)
  vtkNew<vtkImageData> outputImage;
  outputImage->DeepCopy(inputVolume->imageData());

  // Ensure Python is initialized
  bool ownInterpreter = false;
  if (!Py_IsInitialized()) {
    py::initialize_interpreter();
    ownInterpreter = true;
  }

  try {
    py::gil_scoped_acquire gil;

    // Import the pipeline_dataset module
    py::module_ datasetMod =
      py::module_::import("tomviz.pipeline_dataset");

    // Create PipelineDataset wrapping the copied vtkImageData
    py::object datasetCls = datasetMod.attr("PipelineDataset");
    py::object dataset =
      datasetCls(py::cast(static_cast<vtkImageData*>(outputImage.Get()),
                          py::return_value_policy::reference));

    // Load the Python script as a module
    py::module_ types = py::module_::import("types");
    py::object moduleType = types.attr("ModuleType");
    py::object scriptModule =
      moduleType(py::str(m_operatorName.toStdString()));

    // Execute the script in the module's namespace
    py::exec(py::str(m_script.toStdString()), scriptModule.attr("__dict__"));

    // Find the transform function: try transform() first, then
    // transform_scalars()
    py::object transformFunc = py::none();
    if (py::hasattr(scriptModule, "transform")) {
      transformFunc = scriptModule.attr("transform");
    } else if (py::hasattr(scriptModule, "transform_scalars")) {
      transformFunc = scriptModule.attr("transform_scalars");
    }

    if (transformFunc.is_none()) {
      qWarning("LegacyPythonTransform: No transform function found "
               "in script");
      return result;
    }

    // Build kwargs dict from parameters
    py::dict kwargs;
    for (auto it = m_parameters.constBegin(); it != m_parameters.constEnd();
         ++it) {
      std::string key = it.key().toStdString();
      const QVariant& val = it.value();
      switch (val.typeId()) {
        case QMetaType::Double:
          kwargs[py::str(key)] = py::float_(val.toDouble());
          break;
        case QMetaType::Int:
          kwargs[py::str(key)] = py::int_(val.toInt());
          break;
        case QMetaType::Bool:
          kwargs[py::str(key)] = py::bool_(val.toBool());
          break;
        case QMetaType::QString:
          kwargs[py::str(key)] = py::str(val.toString().toStdString());
          break;
        default:
          kwargs[py::str(key)] = py::float_(val.toDouble());
          break;
      }
    }

    // Call the transform function
    py::object pyResult = transformFunc(dataset, **kwargs);

    // If return is None: the vtkImageData was modified in-place
    // Wrap in new VolumeData and output
    auto volume = std::make_shared<VolumeData>(outputImage.Get());
    volume->setLabel(inputVolume->label());
    volume->setUnits(inputVolume->units());

    result["volume"] = PortData(std::any(volume), PortType::Volume);

  } catch (const py::error_already_set& e) {
    qWarning("LegacyPythonTransform Python error: %s", e.what());
  } catch (const std::exception& e) {
    qWarning("LegacyPythonTransform error: %s", e.what());
  }

  if (ownInterpreter) {
    py::finalize_interpreter();
  }

  return result;
}

} // namespace pipeline
} // namespace tomviz
