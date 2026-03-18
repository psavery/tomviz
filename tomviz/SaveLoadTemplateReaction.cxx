/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SaveLoadTemplateReaction.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "ModuleManager.h"
#include "RecentFilesMenu.h"
#include "Utilities.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QString>

namespace tomviz {

SaveLoadTemplateReaction::SaveLoadTemplateReaction(QAction* parentObject, bool load, QString filename)
  : pqReaction(parentObject), m_load(load), m_filename(filename)
{}

void SaveLoadTemplateReaction::onTriggered()
{
  if (m_load) {
    loadTemplate(m_filename);
  } else {
    saveTemplate();
  }
}

bool SaveLoadTemplateReaction::saveTemplate()
{
  bool ok;
  QString text = QInputDialog::getText(tomviz::mainWidget(), tr("Save Pipeline Template"),
                                         tr("Template Name:"), QLineEdit::Normal, QString(), &ok);
  QString fileName = text.replace(" ", "_");
  if (ok && !text.isEmpty()) {
    QString path;
    // Save the template to the tomviz/templates directory if it exists
    if (!tomviz::userDataPath().isEmpty()) {
      path = tomviz::userDataPath() + "/templates";
      QDir dir(path);
      if (!dir.exists() && !dir.mkdir(path)) {
        QMessageBox::warning(
          tomviz::mainWidget(), "Could not create tomviz directory",
          QString("Could not create tomviz directory '%1'.").arg(path));
        return false;
      }
    }
    QString destination =
      QString("%1%2%3.tvsm").arg(path).arg(QDir::separator()).arg(fileName);
    return SaveLoadTemplateReaction::saveTemplate(destination);
  }
  return false;
}

bool SaveLoadTemplateReaction::loadTemplate(const QString& fileName)
{
  // TODO: migrate to new pipeline
  // Old code used ActiveObjects::activeParentDataSource() and
  // activeDataSource() to apply template to the current data source.
  Q_UNUSED(fileName);
  qWarning("SaveLoadTemplateReaction::loadTemplate not yet migrated to new pipeline.");
  return false;
}

bool SaveLoadTemplateReaction::saveTemplate(const QString& fileName)
{
  // TODO: migrate to new pipeline
  // Old code used ActiveObjects::activeParentDataSource() to serialize
  // the pipeline state into a template file.
  Q_UNUSED(fileName);
  qWarning("SaveLoadTemplateReaction::saveTemplate not yet migrated to new pipeline.");
  return false;
}

} // namespace tomviz
