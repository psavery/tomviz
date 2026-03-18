/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "Reaction.h"

#include "ActiveObjects.h"

namespace tomviz {

Reaction::Reaction(QAction* parentObject) : pqReaction(parentObject)
{
  connect(&ActiveObjects::instance(), &ActiveObjects::activeNodeChanged, this,
          &Reaction::updateEnableState);

  updateEnableState();
}

void Reaction::updateEnableState()
{
  // TODO: migrate to new pipeline
  // Old code checked PipelineManager::executionMode() == Threaded
  parentAction()->setEnabled(ActiveObjects::instance().activeNode() != nullptr);
}

} // namespace tomviz
