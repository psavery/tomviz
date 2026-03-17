/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "InputPort.h"

#include "Link.h"
#include "OutputPort.h"

namespace tomviz {
namespace pipeline {

InputPort::InputPort(const QString& name, PortTypes acceptedTypes,
                     QObject* parent)
  : Port(name, parent), m_acceptedTypes(acceptedTypes)
{}

PortTypes InputPort::acceptedTypes() const
{
  return m_acceptedTypes;
}

bool InputPort::canConnectTo(const OutputPort* output) const
{
  if (!output) {
    return false;
  }
  return isPortTypeCompatible(output->type(), m_acceptedTypes);
}

Link* InputPort::link() const
{
  return m_link;
}

PortData InputPort::data() const
{
  if (m_link) {
    return m_link->from()->data();
  }
  return PortData();
}

bool InputPort::hasData() const
{
  if (m_link) {
    return m_link->from()->hasData();
  }
  return false;
}

bool InputPort::isStale() const
{
  if (m_link) {
    return m_link->from()->isStale();
  }
  return false;
}

void InputPort::setLink(Link* link)
{
  m_link = link;
  emit connectionChanged();
}

} // namespace pipeline
} // namespace tomviz
