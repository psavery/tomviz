/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineModuleMenu_h
#define tomvizPipelineModuleMenu_h

#include <QObject>
#include <QPointer>

class QAction;
class QMenu;
class QToolBar;

namespace tomviz {

namespace pipeline {
class Pipeline;
class SinkNode;
class LegacyModuleSink;
} // namespace pipeline

/// PipelineModuleMenu manages the Visualization menu and toolbar for the
/// new pipeline infrastructure. It creates sink nodes instead of old Modules.
class PipelineModuleMenu : public QObject
{
  Q_OBJECT

public:
  PipelineModuleMenu(QToolBar* toolBar, QMenu* parentMenu,
                     QObject* parent = nullptr);
  ~PipelineModuleMenu() override;

  static QList<QString> sinkTypes();
  static QIcon sinkIcon(const QString& type);
  static pipeline::LegacyModuleSink* createSink(const QString& type);

private slots:
  void updateActions();
  void triggered(QAction* action);

private:
  Q_DISABLE_COPY(PipelineModuleMenu)
  QPointer<QMenu> m_menu;
  QPointer<QToolBar> m_toolBar;
};

} // namespace tomviz

#endif
