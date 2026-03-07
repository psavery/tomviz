/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "DataReader.h"

// Qt defines 'slots' as a macro which conflicts with Python's object.h.
#pragma push_macro("slots")
#undef slots
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#pragma pop_macro("slots")

#include <vtkImageData.h>
#include <vtkJPEGReader.h>
#include <vtkMRCReader.h>
#include <vtkPNGReader.h>
#include <vtkPythonUtil.h>
#include <vtkStringArray.h>
#include <vtkTIFFReader.h>
#include <vtkXMLImageDataReader.h>

#include <QFileInfo>

namespace py = pybind11;

namespace tomviz {
namespace pipeline {

namespace {

QString fileExtension(const QStringList& fileNames)
{
  if (fileNames.isEmpty()) {
    return {};
  }
  return QFileInfo(fileNames.first()).suffix().toLower();
}

bool isVTKReadable(const QString& ext)
{
  static const QStringList supported = { "tif", "tiff", "png",
                                         "jpg", "jpeg", "vti",
                                         "mrc" };
  return supported.contains(ext);
}

} // namespace

// --- VTKReader ---

vtkSmartPointer<vtkImageData> VTKReader::read(const QStringList& fileNames)
{
  if (fileNames.isEmpty()) {
    return nullptr;
  }

  QString ext = fileExtension(fileNames);
  std::string path = fileNames.first().toStdString();

  vtkSmartPointer<vtkImageData> result;

  if (ext == "tif" || ext == "tiff") {
    auto reader = vtkSmartPointer<vtkTIFFReader>::New();
    if (fileNames.size() > 1) {
      // Image stack
      auto sa = vtkSmartPointer<vtkStringArray>::New();
      sa->SetNumberOfValues(static_cast<vtkIdType>(fileNames.size()));
      for (int i = 0; i < fileNames.size(); ++i) {
        sa->SetValue(static_cast<vtkIdType>(i),
                     fileNames[i].toStdString());
      }
      reader->SetFileNames(sa);
    } else {
      reader->SetFileName(path.c_str());
    }
    reader->Update();
    result = reader->GetOutput();
  } else if (ext == "png") {
    auto reader = vtkSmartPointer<vtkPNGReader>::New();
    reader->SetFileName(path.c_str());
    reader->Update();
    result = reader->GetOutput();
  } else if (ext == "jpg" || ext == "jpeg") {
    auto reader = vtkSmartPointer<vtkJPEGReader>::New();
    reader->SetFileName(path.c_str());
    reader->Update();
    result = reader->GetOutput();
  } else if (ext == "vti") {
    auto reader = vtkSmartPointer<vtkXMLImageDataReader>::New();
    reader->SetFileName(path.c_str());
    reader->Update();
    result = reader->GetOutput();
  } else if (ext == "mrc") {
    auto reader = vtkSmartPointer<vtkMRCReader>::New();
    reader->SetFileName(path.c_str());
    reader->Update();
    result = reader->GetOutput();
  }

  return result;
}

// --- PythonDataReader ---

PythonDataReader::PythonDataReader(const QString& readerClassName)
  : m_readerClassName(readerClassName)
{
}

vtkSmartPointer<vtkImageData> PythonDataReader::read(
  const QStringList& fileNames)
{
  if (fileNames.isEmpty() || m_readerClassName.isEmpty()) {
    return nullptr;
  }

  if (!Py_IsInitialized()) {
    qWarning("PythonDataReader: Python interpreter not initialized");
    return nullptr;
  }

  vtkSmartPointer<vtkImageData> result;

  try {
    py::gil_scoped_acquire gil;

    // Split "tomviz.io.formats.numpy.NumpyReader" into module and class
    std::string fullName = m_readerClassName.toStdString();
    auto lastDot = fullName.rfind('.');
    if (lastDot == std::string::npos) {
      qWarning("PythonDataReader: invalid class name '%s'",
               fullName.c_str());
      return nullptr;
    }

    std::string moduleName = fullName.substr(0, lastDot);
    std::string className = fullName.substr(lastDot + 1);

    py::module_ mod = py::module_::import(moduleName.c_str());
    py::object cls = mod.attr(className.c_str());
    py::object reader = cls();

    std::string path = fileNames.first().toStdString();
    py::object pyResult = reader.attr("read")(py::str(path));

    // Extract vtkImageData* from the Python VTK object
    auto* obj = vtkPythonUtil::GetPointerFromObject(
      pyResult.ptr(), "vtkImageData");
    if (obj) {
      result = vtkImageData::SafeDownCast(obj);
    }

  } catch (const py::error_already_set& e) {
    qWarning("PythonDataReader error: %s", e.what());
  } catch (const std::exception& e) {
    qWarning("PythonDataReader error: %s", e.what());
  }

  return result;
}

// --- Factory ---

std::unique_ptr<DataReader> createReader(const QStringList& fileNames)
{
  if (fileNames.isEmpty()) {
    return nullptr;
  }

  QString ext = fileExtension(fileNames);

  // Check VTK-supported formats first
  if (isVTKReadable(ext)) {
    return std::make_unique<VTKReader>();
  }

  // Check Python readers (requires Python to be initialized)
  if (!Py_IsInitialized()) {
    return nullptr;
  }

  std::unique_ptr<DataReader> reader;

  try {
    py::gil_scoped_acquire gil;

    py::module_ internal = py::module_::import("tomviz.io._internal");
    py::list readers = internal.attr("list_python_readers")();

    for (const auto& entry : readers) {
      // Each entry is [display_name, extensions_list, class]
      py::list item = py::cast<py::list>(entry);
      py::list extensions = py::cast<py::list>(item[1]);

      for (const auto& pyExt : extensions) {
        QString readerExt = QString::fromStdString(
          py::cast<std::string>(pyExt));
        if (readerExt.toLower() == ext) {
          // Get the fully qualified class name
          py::object cls = item[2];
          std::string moduleName =
            py::cast<std::string>(cls.attr("__module__"));
          std::string clsName =
            py::cast<std::string>(cls.attr("__qualname__"));
          QString fullName =
            QString::fromStdString(moduleName + "." + clsName);
          reader = std::make_unique<PythonDataReader>(fullName);
          break;
        }
      }
      if (reader) {
        break;
      }
    }
  } catch (const py::error_already_set& e) {
    qWarning("createReader: Python error checking readers: %s", e.what());
  } catch (const std::exception& e) {
    qWarning("createReader: error checking readers: %s", e.what());
  }

  return reader;
}

} // namespace pipeline
} // namespace tomviz
