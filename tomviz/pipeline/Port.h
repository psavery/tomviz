/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelinePort_h
#define tomvizPipelinePort_h

#include "tomviz_pipeline_export.h"

#include <QObject>
#include <QString>

namespace tomviz {
namespace pipeline {

class Node;

class TOMVIZ_PIPELINE_EXPORT Port : public QObject
{
  Q_OBJECT

public:
  Port(const QString& name, QObject* parent = nullptr);
  ~Port() override = default;

  QString name() const;
  Node* node() const;

signals:
  void connectionChanged();

private:
  QString m_name;
};

} // namespace pipeline
} // namespace tomviz

#endif
