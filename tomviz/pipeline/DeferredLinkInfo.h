/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineDeferredLinkInfo_h
#define tomvizPipelineDeferredLinkInfo_h

#include "InputPort.h"
#include "Node.h"
#include "NodeState.h"
#include "OutputPort.h"

#include <QList>
#include <QPointer>

namespace tomviz {
namespace pipeline {

/// Captures everything needed to undo an eagerly-performed transform
/// insertion if the user cancels the edit dialog.
///
/// The insertion is applied to the pipeline immediately so the preview shows
/// the transform in its final, inserted position rather than branching off
/// the upstream port.  If the user cancels, the new node is removed (which
/// drops its own links) and this struct is used to put the pipeline back
/// exactly as it was: the original links are recreated and the node/port
/// states captured before the insertion are restored, so cancel is a true
/// no-op and nothing is left spuriously stale.
///
/// For source-shaped insertions (no links to break) this is left empty; the
/// dialog then just removes the node on cancel.
struct DeferredLinkInfo
{
  struct LinkEndpoints
  {
    QPointer<OutputPort> from;
    QPointer<InputPort> to;
  };

  struct NodeStateSnapshot
  {
    QPointer<Node> node;
    NodeState state;
  };

  struct PortStaleSnapshot
  {
    QPointer<OutputPort> port;
    bool stale;
  };

  /// Original links to recreate on cancel.
  QList<LinkEndpoints> linksToRestore;

  /// Node states captured before the insertion, restored on cancel.
  QList<NodeStateSnapshot> nodeStates;

  /// Output-port stale flags captured before the insertion, restored on
  /// cancel.
  QList<PortStaleSnapshot> portStaleStates;

  bool isEmpty() const { return linksToRestore.isEmpty(); }
};

} // namespace pipeline
} // namespace tomviz

#endif
