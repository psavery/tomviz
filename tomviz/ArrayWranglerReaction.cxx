/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ArrayWranglerReaction.h"

#include <QAction>
#include <QMainWindow>

#include "ArrayWranglerOperator.h"
#include "DataSource.h"

namespace tomviz {

ArrayWranglerReaction::ArrayWranglerReaction(QAction* parentObject,
                                             QMainWindow* mw)
  : Reaction(parentObject), m_mainWindow(mw)
{
}

void ArrayWranglerReaction::wrangleArray(DataSource* source)
{
  // TODO: migrate to new pipeline
  Q_UNUSED(source);
}
} // namespace tomviz
