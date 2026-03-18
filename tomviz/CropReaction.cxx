/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "CropReaction.h"

#include <QAction>
#include <QMainWindow>

#include "DataSource.h"

namespace tomviz {

CropReaction::CropReaction(QAction* parentObject, QMainWindow* mw)
  : Reaction(parentObject), m_mainWindow(mw)
{
}

void CropReaction::crop(DataSource* source)
{
  // TODO: migrate to new pipeline
  Q_UNUSED(source);
}
} // namespace tomviz
