/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineDataReader_h
#define tomvizPipelineDataReader_h

#include "tomviz_pipeline_export.h"

#include <QStringList>

#include <vtkSmartPointer.h>

#include <memory>

class vtkImageData;

namespace tomviz {
namespace pipeline {

/// Abstract reader: takes file paths, produces vtkImageData.
class TOMVIZ_PIPELINE_EXPORT DataReader
{
public:
  virtual ~DataReader() = default;
  virtual vtkSmartPointer<vtkImageData> read(const QStringList& fileNames) = 0;
};

/// VTK-based reader for common image formats (TIFF, PNG, JPEG, VTI, MRC).
/// Selects the correct vtkImageReader2 subclass based on file extension.
class TOMVIZ_PIPELINE_EXPORT VTKReader : public DataReader
{
public:
  vtkSmartPointer<vtkImageData> read(const QStringList& fileNames) override;
};

/// Python-based reader. Wraps a tomviz.io.Reader subclass and calls
/// reader.read(path) via pybind11.
class TOMVIZ_PIPELINE_EXPORT PythonDataReader : public DataReader
{
public:
  explicit PythonDataReader(const QString& readerClassName);
  vtkSmartPointer<vtkImageData> read(const QStringList& fileNames) override;

private:
  QString m_readerClassName;
};

/// Factory: inspects file names and returns the appropriate DataReader.
/// - Checks extension against known VTK-readable formats first
/// - Falls back to checking registered Python readers
/// - Returns nullptr if no reader matches
TOMVIZ_PIPELINE_EXPORT std::unique_ptr<DataReader> createReader(
  const QStringList& fileNames);

} // namespace pipeline
} // namespace tomviz

#endif
