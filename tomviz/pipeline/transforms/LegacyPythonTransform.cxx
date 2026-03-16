/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "LegacyPythonTransform.h"

#include "PythonTransformEditorWidget.h"
#include "TransformPropertiesWidget.h"
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

bool LegacyPythonTransform::hasPropertiesWidget() const
{
  // Always has a widget — at minimum the script editor tab.
  return true;
}

bool LegacyPythonTransform::propertiesWidgetNeedsInput() const
{
  return false;
}

EditTransformWidget* LegacyPythonTransform::createPropertiesWidget(
  QWidget* parent)
{
  auto* widget = new PythonTransformEditorWidget(
    label(), m_script, m_jsonDescription, m_parameters, parent);

  connect(widget, &PythonTransformEditorWidget::applied, this,
          [this](const QString& newLabel, const QString& newScript,
                 const QMap<QString, QVariant>& values) {
            bool changed = false;

            if (label() != newLabel) {
              setLabel(newLabel);
              changed = true;
            }

            if (m_script != newScript) {
              m_script = newScript;
              changed = true;
            }

            for (auto it = values.constBegin(); it != values.constEnd();
                 ++it) {
              if (m_parameters.value(it.key()) != it.value()) {
                changed = true;
              }
              setParameter(it.key(), it.value());
            }

            if (changed) {
              markStale();
              emit parametersApplied();
            }
          });

  return widget;
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
  m_datasetInputNames.clear();
  if (obj.contains("parameters")) {
    QJsonArray params = obj.value("parameters").toArray();
    for (const auto& paramVal : params) {
      QJsonObject param = paramVal.toObject();
      QString name = param.value("name").toString();
      QString type = param.value("type").toString();
      QJsonValue defaultVal = param.value("default");

      // Dataset parameters become additional input ports, not parameters.
      if (type == "dataset") {
        addInput(name, PortType::Volume);
        m_datasetInputNames.append(name);
        continue;
      }

      QVariant value;
      if (type == "double") {
        value = QVariant(defaultVal.toDouble());
      } else if (type == "int" || type == "integer" ||
                 type == "enumeration") {
        value = QVariant(defaultVal.toInt());
      } else if (type == "bool" || type == "boolean") {
        value = QVariant(defaultVal.toBool());
      } else if (type == "string" || type == "file" ||
                 type == "save_file" || type == "directory") {
        value = QVariant(defaultVal.toString());
      } else {
        // Types like select_scalars, xyz_header, label_map,
        // reconstruction — skip unless there's an explicit default.
        // This lets the Python function's own default (usually None) work.
        if (defaultVal.isUndefined() || defaultVal.isNull()) {
          continue;
        }
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

  // Ensure Python is initialized. Never finalize — C extension modules
  // like numpy cannot be re-loaded after finalize_interpreter().
  if (!Py_IsInitialized()) {
    py::initialize_interpreter();
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

    // Find the transform function: try module-level transform() first, then
    // transform_scalars(), then look for an Operator subclass.
    py::object transformFunc = py::none();
    if (py::hasattr(scriptModule, "transform")) {
      transformFunc = scriptModule.attr("transform");
    } else if (py::hasattr(scriptModule, "transform_scalars")) {
      transformFunc = scriptModule.attr("transform_scalars");
    }

    // If no module-level function, look for an Operator subclass
    if (transformFunc.is_none()) {
      py::module_ internalMod =
        py::module_::import("tomviz._internal");
      py::object findOpClass = internalMod.attr("find_operator_class");
      py::object opClass = findOpClass(scriptModule);

      if (!opClass.is_none()) {
        // Create a stub _operator_wrapper so that Progress properties work.
        // SimpleNamespace supports arbitrary get/set attributes.
        py::object stubWrapper = types.attr("SimpleNamespace")(
          py::arg("progress_maximum") = 0,
          py::arg("progress_value") = 0,
          py::arg("progress_message") = py::str(""),
          py::arg("progress_data") = py::none(),
          py::arg("canceled") = false,
          py::arg("completed") = false);

        // Instantiate following the same pattern as find_transform_function:
        // cls.__new__(cls) → set _operator_wrapper → cls.__init__(o)
        py::object instance = opClass.attr("__new__")(opClass);
        instance.attr("_operator_wrapper") = stubWrapper;
        opClass.attr("__init__")(instance);

        // Check which transform method was actually implemented
        py::object implCheck =
          internalMod.attr("_operator_method_was_implemented");
        if (implCheck(instance, "transform").cast<bool>()) {
          transformFunc = instance.attr("transform");
        } else if (implCheck(instance, "transform_scalars")
                     .cast<bool>()) {
          transformFunc = instance.attr("transform_scalars");
        }
      }
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
        case QMetaType::QVariantList: {
          py::list pyList;
          for (const auto& item : val.toList()) {
            switch (item.typeId()) {
              case QMetaType::Int:
                pyList.append(py::int_(item.toInt()));
                break;
              case QMetaType::Double:
                pyList.append(py::float_(item.toDouble()));
                break;
              case QMetaType::Bool:
                pyList.append(py::bool_(item.toBool()));
                break;
              case QMetaType::QString:
                pyList.append(py::str(item.toString().toStdString()));
                break;
              default:
                pyList.append(py::str(item.toString().toStdString()));
                break;
            }
          }
          kwargs[py::str(key)] = pyList;
          break;
        }
        default:
          kwargs[py::str(key)] = py::float_(val.toDouble());
          break;
      }
    }

    // Wrap dataset input ports as PipelineDataset kwargs
    for (const auto& dsName : m_datasetInputNames) {
      auto it = inputs.find(dsName);
      if (it == inputs.end()) {
        continue;
      }
      auto dsVolume = it.value().template value<VolumeDataPtr>();
      if (!dsVolume || !dsVolume->isValid()) {
        continue;
      }
      py::object dsObj = datasetCls(
        py::cast(dsVolume->imageData(),
                 py::return_value_policy::reference));
      kwargs[py::str(dsName.toStdString())] = dsObj;
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

  return result;
}

} // namespace pipeline
} // namespace tomviz
