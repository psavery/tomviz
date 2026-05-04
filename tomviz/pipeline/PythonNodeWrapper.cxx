/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PythonNodeWrapper.h"

#include "Node.h"
#include "OutputPort.h"
#include "PortData.h"
#include "PortType.h"
#include "data/VolumeData.h"

#pragma push_macro("slots")
#undef slots
#include <pybind11/embed.h>
#pragma pop_macro("slots")

#include <vtkImageData.h>
#include <vtkMolecule.h>
#include <vtkPythonUtil.h>
#include <vtkSmartPointer.h>
#include <vtkTable.h>

#include <QMap>
#include <QString>

namespace py = pybind11;

namespace tomviz {
namespace pipeline {

namespace {

/// Unwrap a Python value passed to `progress.data` into a typed
/// PortData suitable for @a port. Operators may hand us a
/// PipelineDataset wrapping a vtkObject in `_data_object` or the raw
/// vtkObject directly; both forms are supported. Returns an invalid
/// PortData when the conversion isn't applicable for the port's
/// effective type.
PortData pythonValueToPortData(py::object pyValue, OutputPort* port)
{
  if (!port || pyValue.is_none()) {
    return PortData();
  }
  py::object dataObj = pyValue;
  if (py::hasattr(pyValue, "_data_object")) {
    dataObj = pyValue.attr("_data_object");
  }
  void* raw =
    vtkPythonUtil::GetPointerFromObject(dataObj.ptr(), "vtkObjectBase");
  if (!raw) {
    return PortData();
  }
  PortType type = port->type();
  if (isVolumeType(type)) {
    if (auto* image =
          vtkImageData::SafeDownCast(static_cast<vtkObjectBase*>(raw))) {
      auto vol = std::make_shared<VolumeData>(image);
      return PortData(std::any(vol), type);
    }
  } else if (type == PortType::Table) {
    if (auto* table =
          vtkTable::SafeDownCast(static_cast<vtkObjectBase*>(raw))) {
      vtkSmartPointer<vtkTable> sp(table);
      return PortData(std::any(sp), type);
    }
  } else if (type == PortType::Molecule) {
    if (auto* mol =
          vtkMolecule::SafeDownCast(static_cast<vtkObjectBase*>(raw))) {
      vtkSmartPointer<vtkMolecule> sp(mol);
      return PortData(std::any(sp), type);
    }
  }
  return PortData();
}

} // namespace

py::object createNodeWrapper(Node* node, const QString& primaryPortName)
{
  py::object builtins = py::module_::import("builtins");
  py::object propertyFn = builtins.attr("property");
  py::object typeFn = builtins.attr("type");

  py::dict attrs;

  // Primary output port name — read by tomviz.operators.Progress._data
  // to map a bare `self.progress.data = X` to the explicit
  // {primary_port: X} form the C++ progress_data setter expects.
  if (!primaryPortName.isEmpty()) {
    attrs["primary_port"] = py::str(primaryPortName.toStdString());
  }

  attrs["progress_maximum"] = propertyFn(
    py::cpp_function(
      [node](py::object) -> int { return node->totalProgressSteps(); }),
    py::cpp_function(
      [node](py::object, int v) { node->setTotalProgressSteps(v); }));
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

  attrs["progress_data"] = propertyFn(
    py::cpp_function(
      [](py::object) -> py::object { return py::none(); }),
    py::cpp_function([node](py::object, py::object pyValue) {
      // The Python-side Progress wrapper has already translated any
      // bare-value form into a {port_name: payload} dict (see
      // tomviz.operators.Progress._data); we only handle the dict
      // form here.
      if (!py::isinstance<py::dict>(pyValue)) {
        return;
      }
      QMap<QString, PortData> updates;
      for (auto item : pyValue.cast<py::dict>()) {
        QString name =
          QString::fromStdString(item.first.cast<std::string>());
        auto* port = node->outputPort(name);
        PortData pd = pythonValueToPortData(
          py::reinterpret_borrow<py::object>(item.second), port);
        if (pd.isValid()) {
          updates.insert(name, pd);
        }
      }
      if (updates.isEmpty()) {
        return;
      }
      // Release GIL before blocking on the main thread.
      py::gil_scoped_release release;
      node->setIntermediateOutputs(updates);
    }));

  attrs["canceled"] = propertyFn(py::cpp_function(
    [node](py::object) -> bool { return node->isCanceled(); }));
  attrs["completed"] = propertyFn(py::cpp_function(
    [node](py::object) -> bool { return node->isCompleted(); }));

  py::object wrapperCls =
    typeFn(py::str("_NodeWrapper"), py::make_tuple(), attrs);
  return wrapperCls();
}

} // namespace pipeline
} // namespace tomviz
