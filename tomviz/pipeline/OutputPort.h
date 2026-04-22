/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineOutputPort_h
#define tomvizPipelineOutputPort_h

#include "Port.h"
#include "PortData.h"
#include "PortType.h"

#include <QJsonObject>
#include <QList>

namespace tomviz {
namespace pipeline {

class InputPort;
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

  /// Virtual so subclasses whose transient-ness is intrinsic to the type
  /// (e.g. PassthroughOutputPort — a pure forwarder with no data of its
  /// own) can pin the answer regardless of the m_transient flag, so an
  /// old state file's `persistent: true` can't reintroduce spurious
  /// data-serialization for them.
  virtual bool isTransient() const;
  void setTransient(bool transient);

  virtual PortData data() const;
  void setData(const PortData& data);
  void clearData();
  virtual bool hasData() const;

  /// Push intermediate data during execution. Thread-safe: can be called
  /// from a worker thread. The update is applied on the main thread
  /// (blocking the caller until complete). Emits intermediateDataApplied()
  /// after the data is applied. Subclasses override to handle specific
  /// data types; the default implementation is a no-op.
  virtual void setIntermediateData(const PortData& data);

  virtual bool isStale() const;
  void setStale(bool stale);

  /// Whether this port accepts a link to the given input port.
  /// Default returns true.  Subclasses can restrict (e.g. sinks only).
  virtual bool canAcceptLink(InputPort* to) const;

  QList<Link*> links() const;

  /// Serialize the metadata of the port's current payload (VolumeData,
  /// Molecule, Table, ...) as JSON. Does NOT include raw voxel / table
  /// byte payloads — those are embedded by container-level savers (e.g.
  /// Tvh5Format) under their own HDF5 groups. Returns an empty object
  /// if the port has no data or the payload type isn't round-trippable.
  /// Override in subclasses that carry specialized payloads.
  virtual QJsonObject serialize() const;

  /// Apply JSON produced by serialize() onto this port's current
  /// payload. No-op if the port has no payload yet (loaders that need
  /// to reconstruct the payload from scratch must do so before calling
  /// deserialize). Returns false on unrecoverable parse errors.
  virtual bool deserialize(const QJsonObject& json);

signals:
  void dataChanged();
  void staleChanged(bool stale);
  void effectiveTypeChanged(PortType newType);
  void intermediateDataApplied();

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
  /// Metadata deserialized before setData ran (typical load path:
  /// source node populates data only when it executes). Applied in
  /// setData() and then cleared.
  QJsonObject m_pendingData;
};

} // namespace pipeline
} // namespace tomviz

#endif
