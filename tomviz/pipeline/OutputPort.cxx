/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "OutputPort.h"

#include "Link.h"

namespace tomviz {
namespace pipeline {

OutputPort::OutputPort(const QString& name, PortType type, QObject* parent)
  : Port(name, parent), m_declaredType(type), m_effectiveType(type)
{}

PortType OutputPort::type() const
{
  return m_effectiveType;
}

PortType OutputPort::declaredType() const
{
  return m_declaredType;
}

void OutputPort::setDeclaredType(PortType type)
{
  m_declaredType = type;
  setEffectiveType(type);
}

void OutputPort::setEffectiveType(PortType type)
{
  if (m_effectiveType != type) {
    m_effectiveType = type;
    emit effectiveTypeChanged(type);
  }
}

bool OutputPort::isTransient() const
{
  return m_transient;
}

void OutputPort::setTransient(bool transient)
{
  m_transient = transient;
}

PortData OutputPort::data() const
{
  return m_data;
}

void OutputPort::setData(const PortData& data)
{
  m_data = data;
  m_stale = false;
  emit dataChanged();
}

void OutputPort::clearData()
{
  m_data.clear();
  emit dataChanged();
}

bool OutputPort::hasData() const
{
  return m_data.isValid();
}

void OutputPort::setIntermediateData(const PortData& /*data*/) {}

bool OutputPort::isStale() const
{
  return m_stale;
}

void OutputPort::setStale(bool stale)
{
  if (m_stale != stale) {
    m_stale = stale;
    emit staleChanged(stale);
  }
}

QList<Link*> OutputPort::links() const
{
  return m_links;
}

void OutputPort::addLink(Link* link)
{
  m_links.append(link);
  emit connectionChanged();
}

void OutputPort::removeLink(Link* link)
{
  m_links.removeOne(link);
  emit connectionChanged();
}

} // namespace pipeline
} // namespace tomviz
