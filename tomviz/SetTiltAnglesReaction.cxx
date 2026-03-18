/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SetTiltAnglesReaction.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "SetTiltAnglesOperator.h"

#include <QMainWindow>

namespace tomviz {

SetTiltAnglesReaction::SetTiltAnglesReaction(QAction* p, QMainWindow* mw)
  : pqReaction(p), m_mainWindow(mw)
{
  connect(&ActiveObjects::instance(), &ActiveObjects::activeNodeChanged, this,
          [this]() { updateEnableState(); });
  updateEnableState();
}

void SetTiltAnglesReaction::updateEnableState()
{
  // TODO: query active node/port for tilt series type
  parentAction()->setEnabled(false);
}

void SetTiltAnglesReaction::showSetTiltAnglesUI(QMainWindow* window,
                                                DataSource* source)
{
  // TODO: migrate to new pipeline
  Q_UNUSED(window);
  Q_UNUSED(source);
}
} // namespace tomviz
