/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineOutputPort_h
#define tomvizPipelineOutputPort_h

#include "Port.h"
#include "PortData.h"
#include "PortType.h"

#include <QList>

namespace tomviz {
namespace pipeline {

class Link;

class OutputPort : public Port
{
  Q_OBJECT

public:
  OutputPort(const QString& name, PortType type,
             QObject* parent = nullptr);
  ~OutputPort() override = default;

  /// Effective type (may differ from declared type due to inference).
  /// Most code should use this rather than declaredType().
  PortType type() const;

  /// The type set at construction time, before any inference.
  PortType declaredType() const;

  /// Change the declared type (also resets the effective type to match).
  void setDeclaredType(PortType type);

  /// Set the effective type.  Called by the owning Node during type
  /// inference.  Only meaningful when declaredType() is ImageData.
  void setEffectiveType(PortType type);

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
  void effectiveTypeChanged(PortType newType);

  /// Emitted when lightweight metadata (units, label, spacing, origin)
  /// changes on the data held by this port, without the image data itself
  /// being replaced. Sinks can listen to this to update annotations/UI
  /// without a full pipeline re-execution.
  void metadataChanged();

private:
  friend class Link;
  void addLink(Link* link);
  void removeLink(Link* link);

  PortType m_declaredType;
  PortType m_effectiveType;
  PortData m_data;
  bool m_transient = false;
  bool m_stale = false;
  QList<Link*> m_links;
};

} // namespace pipeline
} // namespace tomviz

#endif
