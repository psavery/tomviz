/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "TransposeDataReaction.h"

#include <QAction>
#include <QMainWindow>

#include "ActiveObjects.h"
#include "TransposeDataOperator.h"
#include "DataSource.h"

namespace tomviz {

TransposeDataReaction::TransposeDataReaction(QAction* parentObject,
                                             QMainWindow* mw)
  : Reaction(parentObject), m_mainWindow(mw)
{
}

void TransposeDataReaction::transposeData(DataSource* source)
{
  // TODO: migrate to new pipeline
  Q_UNUSED(source);
}
} // namespace tomviz
