/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizSetDataTypeReaction_h
#define tomvizSetDataTypeReaction_h

#include <pqReaction.h>

#include "pipeline/PortType.h"

class QMainWindow;

namespace tomviz {

class DataSource;

class SetDataTypeReaction : public pqReaction
{
  Q_OBJECT

public:
  SetDataTypeReaction(QAction* action, QMainWindow* mw,
                      pipeline::PortType t = pipeline::PortType::Volume);

  static void setDataType(QMainWindow* mw, DataSource* source = nullptr,
                          pipeline::PortType t = pipeline::PortType::Volume);

protected:
  /// Called when the action is triggered.
  void onTriggered() override;
  void updateEnableState() override;

private:
  QMainWindow* m_mainWindow;
  pipeline::PortType m_type;

  void setWidgetText(pipeline::PortType t);

  Q_DISABLE_COPY(SetDataTypeReaction)
};
} // namespace tomviz

#endif
