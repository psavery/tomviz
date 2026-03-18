/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SetDataTypeReaction.h"

#include "ActiveObjects.h"
#include "OperatorFactory.h"
#include "SetTiltAnglesReaction.h"

#include <cassert>

namespace tomviz {

SetDataTypeReaction::SetDataTypeReaction(QAction* action, QMainWindow* mw,
                                         DataSource::DataSourceType t)
  : pqReaction(action), m_mainWindow(mw), m_type(t)
{
  // TODO: migrate to new pipeline
  // Old code connected to ActiveObjects::dataSourceChanged
  connect(&ActiveObjects::instance(), &ActiveObjects::activeNodeChanged, this,
          &SetDataTypeReaction::updateEnableState);
  setWidgetText(t);
  updateEnableState();
}

void SetDataTypeReaction::setDataType(QMainWindow* mw, DataSource* dsource,
                                      DataSource::DataSourceType t)
{
  // TODO: migrate to new pipeline
  // Old code used ActiveObjects::activeDataSource() as fallback
  if (dsource == nullptr) {
    return;
  }
  if (t == DataSource::TiltSeries) {
    SetTiltAnglesReaction::showSetTiltAnglesUI(mw, dsource);
  } else {
    // If it was a TiltSeries convert to volume
    // if (dsource->type() == DataSource::TiltSeries) {
    Operator* op = OperatorFactory::instance().createConvertToVolumeOperator(t);
    dsource->addOperator(op);
    // dsource->setType(t);
    // }
  }
}

void SetDataTypeReaction::onTriggered()
{
  // TODO: migrate to new pipeline
  // Old code used ActiveObjects::activeParentDataSource()
  DataSource* dsource = nullptr;
  setDataType(m_mainWindow, dsource, m_type);
}

void SetDataTypeReaction::updateEnableState()
{
  // TODO: migrate to new pipeline
  // Old code used activePipeline()->transformedDataSource() to check type
  parentAction()->setEnabled(false);
}

void SetDataTypeReaction::setWidgetText(DataSource::DataSourceType t)
{
  if (t == DataSource::Volume) {
    parentAction()->setText("Mark Data As Volume");
  } else if (t == DataSource::TiltSeries) {
    parentAction()->setText("Mark Data As Tilt Series");
  } else if (t == DataSource::FIB) {
    parentAction()->setText("Mark Data As Focused Ion Beam (FIB)");
  } else {
    assert("Unknown data source type" && false);
  }
}
} // namespace tomviz
