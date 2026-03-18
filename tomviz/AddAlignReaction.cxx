/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "AddAlignReaction.h"

#include "DataSource.h"
#include "Pipeline.h"
#include "TranslateAlignOperator.h"
#include "Utilities.h"

#include <QDebug>

namespace tomviz {

AddAlignReaction::AddAlignReaction(QAction* parentObject)
  : Reaction(parentObject)
{
}

void AddAlignReaction::align(DataSource* source)
{
  // TODO: migrate to new pipeline
  Q_UNUSED(source);
}
} // namespace tomviz
