/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "LegacyPythonTransform.h"

#include "CustomPythonTransformWidget.h"
#include "InputPort.h"
#include "OutputPort.h"
#include "VolumeOutputPort.h"
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
#include <vtkMolecule.h>
#include <vtkNew.h>
#include <vtkPythonUtil.h>
#include <vtkTable.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

PYBIND11_VTK_TYPECASTER(vtkImageData)

namespace py = pybind11;

namespace tomviz {
namespace pipeline {

static PortType portTypeFromString(const QString& str)
{
  if (str == "TiltSeries")
    return PortType::TiltSeries;
  if (str == "Volume")
    return PortType::Volume;
  if (str == "ImageData")
    return PortType::ImageData;
  return PortType::None;
}

QMap<QString, CustomWidgetInfo> LegacyPythonTransform::s_customWidgetMap;

void LegacyPythonTransform::registerCustomWidget(const QString& id,
                                                  const CustomWidgetInfo& info)
{
  s_customWidgetMap[id] = info;
}

QString LegacyPythonTransform::customWidgetID() const
{
  return m_customWidgetID;
}

LegacyPythonTransform::LegacyPythonTransform(QObject* parent)
  : TransformNode(parent)
{
  // Always have volume in/out.  Use VolumeOutputPort so that
  // setIntermediateData() can deep-copy live updates from Python.
  addInput("volume", PortType::ImageData);
  addOutputPort(new VolumeOutputPort("volume", PortType::ImageData));
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

  // Detect cancel/completion support from operator base class usage.
  if (script.contains("CompletableOperator")) {
    setSupportsCancel(true);
    setSupportsCompletion(true);
  } else if (script.contains("CancelableOperator")) {
    setSupportsCancel(true);
    setSupportsCompletion(false);
  } else {
    setSupportsCancel(false);
    setSupportsCompletion(false);
  }
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
  if (!m_customWidgetID.isEmpty() &&
      s_customWidgetMap.contains(m_customWidgetID)) {
    return s_customWidgetMap[m_customWidgetID].needsData;
  }
  return false;
}

EditTransformWidget* LegacyPythonTransform::createPropertiesWidget(
  QWidget* parent)
{
  CustomPythonTransformWidget* customWidget = nullptr;

  if (!m_customWidgetID.isEmpty() &&
      s_customWidgetMap.contains(m_customWidgetID)) {
    const auto& info = s_customWidgetMap[m_customWidgetID];

    // Get input data and color map if the widget needs them
    vtkSmartPointer<vtkImageData> inputImage;
    vtkSMProxy* colorMap = nullptr;
    if (info.needsData) {
      auto* input = inputPort("volume");
      if (input && input->hasData()) {
        auto vol = input->data().value<VolumeDataPtr>();
        if (vol && vol->isValid()) {
          inputImage = vol->imageData();
          vol->initColorMap();
          colorMap = vol->colorMap();
        }
      }
    }

    if (info.create) {
      customWidget = info.create(parent, inputImage, colorMap);
      if (customWidget) {
        customWidget->setValues(m_parameters);
        customWidget->setScript(m_script);
      }
    }
  }

  auto* widget = new PythonTransformEditorWidget(
    label(), m_script, m_jsonDescription, m_parameters, customWidget, parent);

  connect(widget, &PythonTransformEditorWidget::applied, this,
          [this, customWidget](const QString& newLabel,
                               const QString& newScript,
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

            // Get values from the custom widget if present,
            // otherwise use the auto-generated parameter values.
            QMap<QString, QVariant> finalValues = values;
            if (customWidget) {
              customWidget->getValues(finalValues);
              customWidget->writeSettings();
            }

            for (auto it = finalValues.constBegin();
                 it != finalValues.constEnd(); ++it) {
              if (m_parameters.value(it.key()) != it.value()) {
                changed = true;
              }
              setParameter(it.key(), it.value());
            }

            if (changed) {
              emit parametersApplied();
            }
          });

  return widget;
}

QJsonObject LegacyPythonTransform::serialize() const
{
  auto json = TransformNode::serialize();
  json["description"] = m_jsonDescription;
  json["script"] = m_script;
  if (!m_parameters.isEmpty()) {
    json["arguments"] = QJsonObject::fromVariantMap(m_parameters);
  }
  return json;
}

bool LegacyPythonTransform::deserialize(const QJsonObject& json)
{
  // setJSONDescription / setScript have side effects (parseJSON,
  // detecting CancelableOperator etc.), so we call the setters rather
  // than assigning members directly. Order: description first so
  // parseJSON can rename the output port and set up result ports,
  // then script, then arguments.
  if (json.contains("description")) {
    setJSONDescription(json.value("description").toString());
  }
  if (json.contains("script")) {
    setScript(json.value("script").toString());
  }
  // TransformNode::deserialize applies label last — call it after
  // parseJSON, which may have set a label from the JSON description's
  // "label" field, so the explicitly-saved label wins.
  if (!TransformNode::deserialize(json)) {
    return false;
  }
  auto args = json.value("arguments").toObject();
  for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
    setParameter(it.key(), it.value().toVariant());
  }
  return true;
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

  // Custom widget ID (e.g. "RotationAlignWidget")
  if (obj.contains("widget")) {
    m_customWidgetID = obj.value("widget").toString();
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
        addInput(name, PortType::ImageData);
        m_datasetInputNames.append(name);
        continue;
      }

      QVariant value;

      // Array defaults (e.g. [0.0, 0.0, 0.0]) → QVariantList,
      // regardless of the declared scalar type.
      if (defaultVal.isArray()) {
        QVariantList list;
        for (const auto& item : defaultVal.toArray()) {
          if (type == "int" || type == "integer") {
            list.append(item.toInt());
          } else {
            list.append(item.toDouble());
          }
        }
        value = list;
      } else if (type == "double") {
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

  // If the operator declares a child dataset, remap the primary output port
  // to use the child's name and mark it as persistent.
  if (obj.contains("children")) {
    QJsonArray children = obj.value("children").toArray();
    if (!children.isEmpty()) {
      QJsonObject child = children.first().toObject();
      m_childName = child.value("name").toString();
      QString childLabel = child.value("label").toString();

      auto* out = outputPort(m_primaryOutputName);
      if (out && !m_childName.isEmpty()) {
        out->setName(m_childName);
        m_primaryOutputName = m_childName;
        out->setTransient(false);
      }
    }
  }

  // Override the primary volume port types if specified
  if (obj.contains("inputType")) {
    PortType pt = portTypeFromString(obj.value("inputType").toString());
    if (pt != PortType::None) {
      inputPort("volume")->setAcceptedTypes(pt);
    }
  }
  if (obj.contains("outputType")) {
    PortType pt = portTypeFromString(obj.value("outputType").toString());
    if (pt != PortType::None) {
      outputPort(m_primaryOutputName)->setDeclaredType(pt);
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
        // Create a wrapper object that bridges Python progress/cancel
        // property accesses back to the C++ Node API.
        auto* node = this;
        py::object builtins = py::module_::import("builtins");
        py::object propertyFn = builtins.attr("property");
        py::object typeFn = builtins.attr("type");

        py::dict attrs;
        attrs["progress_maximum"] = propertyFn(
          py::cpp_function(
            [node](py::object) -> int {
              return node->totalProgressSteps();
            }),
          py::cpp_function(
            [node](py::object, int v) {
              node->setTotalProgressSteps(v);
            }));
        attrs["progress_value"] = propertyFn(
          py::cpp_function(
            [node](py::object) -> int { return node->progressStep(); }),
          py::cpp_function(
            [node](py::object, int v) { node->setProgressStep(v); }));
        attrs["progress_message"] = propertyFn(
          py::cpp_function(
            [node](py::object) -> std::string {
              return node->progressMessage().toStdString();
            }),
          py::cpp_function(
            [node](py::object, const std::string& msg) {
              node->setProgressMessage(QString::fromStdString(msg));
            }));
        auto* outPort = node->outputPort(node->m_primaryOutputName);
        attrs["progress_data"] = propertyFn(
          py::cpp_function([](py::object) -> py::object {
            return py::none();
          }),
          py::cpp_function(
            [outPort](py::object, py::object pyChild) {
              if (pyChild.is_none() || !outPort) {
                return;
              }
              // The value may be a PipelineDataset (wrapping a
              // vtkImageData in _data_object) or a raw vtkDataObject
              // (after convert_to_vtk_data_object in operators.py).
              py::object dataObj = pyChild;
              if (py::hasattr(pyChild, "_data_object")) {
                dataObj = pyChild.attr("_data_object");
              }
              auto* childImage = vtkImageData::SafeDownCast(
                vtkPythonUtil::GetPointerFromObject(
                  dataObj.ptr(), "vtkObjectBase"));
              if (!childImage) {
                return;
              }
              auto vol = std::make_shared<VolumeData>(childImage);
              PortData pd(std::any(vol), outPort->type());
              // Release GIL before blocking on the main thread.
              py::gil_scoped_release release;
              outPort->setIntermediateData(pd);
            }));
        attrs["canceled"] = propertyFn(py::cpp_function(
          [node](py::object) -> bool { return node->isCanceled(); }));
        attrs["completed"] = propertyFn(py::cpp_function(
          [node](py::object) -> bool { return node->isCompleted(); }));

        py::object wrapperCls =
          typeFn(py::str("_NodeWrapper"), py::make_tuple(), attrs);
        py::object wrapper = wrapperCls();

        // Instantiate following the same pattern as find_transform_function:
        // cls.__new__(cls) → set _operator_wrapper → cls.__init__(o)
        py::object instance = opClass.attr("__new__")(opClass);
        instance.attr("_operator_wrapper") = wrapper;
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

    // Route through transform_method_wrapper so that
    // tomviz_pipeline_env (external execution) is honored.
    py::object wrapperFunc =
      py::module_::import("tomviz._internal")
        .attr("transform_method_wrapper");

    QJsonObject opJson;
    opJson["type"] = QStringLiteral("Python");
    opJson["description"] = m_jsonDescription;
    opJson["label"] = label();
    opJson["script"] = m_script;
    if (!m_parameters.isEmpty()) {
      opJson["arguments"] =
        QJsonObject::fromVariantMap(m_parameters);
    }
    QString opSerialized =
      QString::fromUtf8(QJsonDocument(opJson).toJson());

    py::object pyResult = wrapperFunc(
      transformFunc, py::str(opSerialized.toStdString()),
      dataset, **kwargs);

    // Determine the output vtkImageData.
    // Default: outputImage (deep copy of input, modified in-place by Python).
    vtkSmartPointer<vtkImageData> outputData = outputImage.Get();

    // If a child dataset was declared and returned in the dict, use its
    // vtkImageData as the primary output instead.
    if (!m_childName.isEmpty() && !pyResult.is_none() &&
        py::isinstance<py::dict>(pyResult)) {
      py::dict outputDict = pyResult.cast<py::dict>();
      std::string childKey = m_childName.toStdString();
      if (outputDict.contains(childKey)) {
        py::object childObj = outputDict[py::str(childKey)];
        if (py::hasattr(childObj, "_data_object")) {
          py::object dataObj = childObj.attr("_data_object");
          auto* childImage = vtkImageData::SafeDownCast(
            vtkPythonUtil::GetPointerFromObject(
              dataObj.ptr(), "vtkObjectBase"));
          if (childImage) {
            outputData = childImage;
          }
        }
      }
    }

    auto volume = std::make_shared<VolumeData>(outputData);
    volume->setLabel(inputVolume->label());
    volume->setUnits(inputVolume->units());

    auto* outPort = outputPort(m_primaryOutputName);
    PortType outType =
      outPort ? outPort->declaredType() : PortType::ImageData;
    result[m_primaryOutputName] = PortData(std::any(volume), outType);

    // Extract result outputs (tables, molecules) from Python return dict
    if (!pyResult.is_none() && py::isinstance<py::dict>(pyResult)) {
      py::dict outputDict = pyResult.cast<py::dict>();
      for (int i = 0; i < m_resultNames.size(); ++i) {
        std::string key = m_resultNames[i].toStdString();
        if (!outputDict.contains(key)) {
          qWarning("LegacyPythonTransform: No result named '%s' in "
                   "Python output dict",
                   qPrintable(m_resultNames[i]));
          continue;
        }
        py::object pyObj = outputDict[py::str(key)];
        if (pyObj.is_none()) {
          continue;
        }

        if (m_resultTypes[i] == "table") {
          auto* raw = vtkTable::SafeDownCast(
            vtkPythonUtil::GetPointerFromObject(
              pyObj.ptr(), "vtkObjectBase"));
          if (raw) {
            vtkSmartPointer<vtkTable> table = raw;
            result[m_resultNames[i]] =
              PortData(std::any(table), PortType::Table);
          } else {
            qWarning("LegacyPythonTransform: Result '%s' is not a "
                     "vtkTable",
                     qPrintable(m_resultNames[i]));
          }
        } else if (m_resultTypes[i] == "molecule") {
          auto* raw = vtkMolecule::SafeDownCast(
            vtkPythonUtil::GetPointerFromObject(
              pyObj.ptr(), "vtkObjectBase"));
          if (raw) {
            vtkSmartPointer<vtkMolecule> molecule = raw;
            result[m_resultNames[i]] =
              PortData(std::any(molecule), PortType::Molecule);
          } else {
            qWarning("LegacyPythonTransform: Result '%s' is not a "
                     "vtkMolecule",
                     qPrintable(m_resultNames[i]));
          }
        }
      }
    }

  } catch (const py::error_already_set& e) {
    qWarning("LegacyPythonTransform Python error: %s", e.what());
  } catch (const std::exception& e) {
    qWarning("LegacyPythonTransform error: %s", e.what());
  }

  return result;
}

} // namespace pipeline
} // namespace tomviz
