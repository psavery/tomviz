/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "DataBrokerSaveReaction.h"

#include "Utilities.h"

#include <QDebug>

namespace tomviz {

DataBrokerSaveReaction::DataBrokerSaveReaction(QAction* parentObject,
                                               MainWindow* mainWindow)
  : pqReaction(parentObject), m_mainWindow(mainWindow)
{
  // TODO: migrate to new pipeline
  // was: connected to ActiveObjects::dataSourceChanged to enable/disable action
  parentAction()->setEnabled(false);
}

DataBrokerSaveReaction::~DataBrokerSaveReaction() = default;

void DataBrokerSaveReaction::onTriggered()
{
  saveData();
}

void DataBrokerSaveReaction::setDataBrokerInstalled(bool installed)
{
  m_dataBrokerInstalled = installed;
}

void DataBrokerSaveReaction::saveData()
{
  // TODO: migrate to new pipeline. Export to DataBroker is not yet supported
  // with the new pipeline; the action is disabled in the constructor.
  qWarning() << "Export to DataBroker is not supported in the new pipeline.";
}

} // namespace tomviz
