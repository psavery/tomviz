/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineLink_h
#define tomvizPipelineLink_h

#include "tomviz_pipeline_export.h"

#include <QObject>

namespace tomviz {
namespace pipeline {

class InputPort;
class OutputPort;

class TOMVIZ_PIPELINE_EXPORT Link : public QObject
{
  Q_OBJECT

public:
  Link(OutputPort* from, InputPort* to, QObject* parent = nullptr);
  ~Link() override;

  OutputPort* from() const;
  InputPort* to() const;
  bool isValid() const;

signals:
  void aboutToBeRemoved();

private:
  OutputPort* m_from;
  InputPort* m_to;
};

} // namespace pipeline
} // namespace tomviz

#endif
