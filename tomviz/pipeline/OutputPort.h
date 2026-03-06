/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineOutputPort_h
#define tomvizPipelineOutputPort_h

#include "tomviz_pipeline_export.h"

#include "Port.h"
#include "PortData.h"
#include "PortType.h"

#include <QList>

namespace tomviz {
namespace pipeline {

class Link;

class TOMVIZ_PIPELINE_EXPORT OutputPort : public Port
{
  Q_OBJECT

public:
  OutputPort(const QString& name, PortType type,
             QObject* parent = nullptr);
  ~OutputPort() override = default;

  PortType type() const;

  bool isTransient() const;
  void setTransient(bool transient);

  PortData data() const;
  void setData(const PortData& data);
  void clearData();
  bool hasData() const;

  bool isStale() const;
  void setStale(bool stale);

  QList<Link*> links() const;

signals:
  void dataChanged();
  void staleChanged(bool stale);

private:
  friend class Link;
  void addLink(Link* link);
  void removeLink(Link* link);

  PortType m_type;
  PortData m_data;
  bool m_transient = false;
  bool m_stale = false;
  QList<Link*> m_links;
};

} // namespace pipeline
} // namespace tomviz

#endif
