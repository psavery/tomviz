/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizSaveDataReaction_h
#define tomvizSaveDataReaction_h

#include "SaveDataDialog.h"

#include <pqReaction.h>

#include <QList>

namespace tomviz {

namespace pipeline {
class Pipeline;
}

/// SaveDataReaction handles the "Save Data" action in tomviz. On trigger,
/// it opens SaveDataDialog and writes the output-port payloads the user
/// selected into the destination directory.
class SaveDataReaction : public pqReaction
{
  Q_OBJECT

public:
  SaveDataReaction(QAction* parentAction);

protected:
  /// Called when the pipeline changes to enable/disable the menu item
  void updateEnableState() override;

  /// Called when the action is triggered.
  void onTriggered() override;

private:
  Q_DISABLE_COPY(SaveDataReaction)

  /// Subscribe to the pipeline events that change what there is to save.
  void connectToPipeline(pipeline::Pipeline* pipeline);
};
} // namespace tomviz
#endif
