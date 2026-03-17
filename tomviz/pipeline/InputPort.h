/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineInputPort_h
#define tomvizPipelineInputPort_h

#include "tomviz_pipeline_export.h"

#include "Port.h"
#include "PortData.h"
#include "PortType.h"

namespace tomviz {
namespace pipeline {

class Link;
class OutputPort;

class TOMVIZ_PIPELINE_EXPORT InputPort : public Port
{
  Q_OBJECT

public:
  InputPort(const QString& name, PortTypes acceptedTypes,
            QObject* parent = nullptr);
  ~InputPort() override = default;

  PortTypes acceptedTypes() const;
  void setAcceptedTypes(PortTypes types);
  bool canConnectTo(const OutputPort* output) const;
  Link* link() const;
  PortData data() const;
  bool hasData() const;
  bool isStale() const;

private:
  friend class Link;
  void setLink(Link* link);

  PortTypes m_acceptedTypes;
  Link* m_link = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
