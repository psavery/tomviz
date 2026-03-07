/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ReaderSourceNode.h"

#include "DataReader.h"
#include "OutputPort.h"
#include "PortData.h"
#include "data/VolumeData.h"

#include <vtkImageData.h>

#include <QFileInfo>

namespace tomviz {
namespace pipeline {

ReaderSourceNode::ReaderSourceNode(QObject* parent) : SourceNode(parent)
{
  addOutput("volume", PortType::Volume);
  setLabel("Reader Source");
}

void ReaderSourceNode::setFileNames(const QStringList& fileNames)
{
  m_fileNames = fileNames;
  m_reader = createReader(m_fileNames);

  if (!m_fileNames.isEmpty()) {
    setLabel(QFileInfo(m_fileNames.first()).fileName());
  }
}

QStringList ReaderSourceNode::fileNames() const
{
  return m_fileNames;
}

void ReaderSourceNode::setReader(std::unique_ptr<DataReader> reader)
{
  m_reader = std::move(reader);
}

DataReader* ReaderSourceNode::reader() const
{
  return m_reader.get();
}

bool ReaderSourceNode::execute()
{
  emit executionStarted();

  if (!m_reader) {
    qWarning("ReaderSourceNode: no reader available for '%s'",
             m_fileNames.isEmpty()
               ? ""
               : qPrintable(m_fileNames.first()));
    emit executionFinished(false);
    return false;
  }

  auto imageData = m_reader->read(m_fileNames);
  if (!imageData) {
    qWarning("ReaderSourceNode: reader returned null for '%s'",
             qPrintable(m_fileNames.first()));
    emit executionFinished(false);
    return false;
  }

  auto volume = std::make_shared<VolumeData>(imageData);
  if (!m_fileNames.isEmpty()) {
    volume->setLabel(QFileInfo(m_fileNames.first()).baseName());
  }

  setOutputData("volume",
                PortData(std::any(volume), PortType::Volume));

  emit executionFinished(true);
  return true;
}

} // namespace pipeline
} // namespace tomviz
