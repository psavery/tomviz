/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineLinkPropertiesWidget_h
#define tomvizPipelineLinkPropertiesWidget_h

#include <QWidget>

namespace tomviz {
namespace pipeline {

class Link;

/// Minimal properties panel for a selected Link: shows the source and
/// destination endpoints and offers a button to delete the link.
class LinkPropertiesWidget : public QWidget
{
  Q_OBJECT

public:
  explicit LinkPropertiesWidget(Link* link, QWidget* parent = nullptr);
  ~LinkPropertiesWidget() override = default;

signals:
  /// Emitted when the user clicks the delete button. The owner is
  /// responsible for actually removing the link from the pipeline.
  void deleteRequested(Link* link);

private:
  Link* m_link = nullptr;
};

} // namespace pipeline
} // namespace tomviz

#endif
