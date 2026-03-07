/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineReaderSourceNode_h
#define tomvizPipelineReaderSourceNode_h

#include "tomviz_pipeline_export.h"

#include "DataReader.h"
#include "SourceNode.h"

#include <QStringList>

#include <memory>

namespace tomviz {
namespace pipeline {

/// A source node that reads files and produces VolumeData on its output port.
class TOMVIZ_PIPELINE_EXPORT ReaderSourceNode : public SourceNode
{
  Q_OBJECT

public:
  ReaderSourceNode(QObject* parent = nullptr);
  ~ReaderSourceNode() override = default;

  /// Set the file path(s) to read. This also calls createReader()
  /// internally to select the appropriate reader.
  void setFileNames(const QStringList& fileNames);
  QStringList fileNames() const;

  /// Override the auto-detected reader with a specific one.
  void setReader(std::unique_ptr<DataReader> reader);

  /// Access the current reader (may be null if files not set or unrecognized).
  DataReader* reader() const;

  /// Read the file(s) and set the result on the output port.
  bool execute() override;

private:
  QStringList m_fileNames;
  std::unique_ptr<DataReader> m_reader;
};

} // namespace pipeline
} // namespace tomviz

#endif
