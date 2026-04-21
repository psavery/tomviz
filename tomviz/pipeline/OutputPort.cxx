/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "OutputPort.h"

#include "Link.h"
#include "data/VolumeData.h"

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
  // If a metadata blob arrived via deserialize() before data was
  // populated, apply it now to the freshly-set payload — e.g. user
  // edits to a source's colormap / scalar renames that must survive
  // a state-file save+load+execute cycle.
  if (!m_pendingData.isEmpty()) {
    QJsonObject pending = m_pendingData;
    m_pendingData = {};
    deserialize(pending);
  }
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

bool OutputPort::canAcceptLink(InputPort* /*to*/) const
{
  return true;
}

QJsonObject OutputPort::serialize() const
{
  if (!hasData()) {
    return {};
  }
  // Known round-trippable payloads. Extend as new PortData types
  // (Molecule, Table, ...) acquire serialize()/deserialize() support.
  // Use std::any_cast's nothrow pointer form so ports carrying
  // non-matching payloads (e.g. Molecule, Table) don't throw here.
  if (auto* volume =
        std::any_cast<VolumeDataPtr>(&m_data.data())) {
    return (*volume)->serialize();
  }
  return {};
}

bool OutputPort::deserialize(const QJsonObject& json)
{
  if (json.isEmpty()) {
    return true;
  }
  if (auto* volume =
        std::any_cast<VolumeDataPtr>(&m_data.data())) {
    return (*volume)->deserialize(json);
  }
  // No payload yet — stash so the next setData() can apply this JSON
  // on top of the freshly-populated data (e.g. source node execute
  // that produces a fresh VolumeData, to which we then reattach the
  // user's colormap / scalar renames from the state file).
  m_pendingData = json;
  return true;
}

} // namespace pipeline
} // namespace tomviz
