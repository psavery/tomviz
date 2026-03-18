/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ConvertToFloatReaction.h"

#include <QDebug>

#include "ConvertToFloatOperator.h"
#include "DataSource.h"

namespace tomviz {

ConvertToFloatReaction::ConvertToFloatReaction(QAction* parentObject)
  : Reaction(parentObject)
{
}

void ConvertToFloatReaction::convertToFloat()
{
  // TODO: migrate to new pipeline
  DataSource* source = nullptr; // was: ActiveObjects::instance().activeParentDataSource()
  if (!source) {
    qDebug() << "Exiting early - no data found.";
    return;
  }
  Operator* Op = new ConvertToFloatOperator();

  source->addOperator(Op);
}
} // namespace tomviz
