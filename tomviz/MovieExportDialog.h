/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizMovieExportDialog_h
#define tomvizMovieExportDialog_h

#include <QDialog>
#include <QScopedPointer>

namespace tomviz {

/// Export the current animation to a movie file or image sequence.
/// MP4 (H.264) output renders PNG frames offscreen and encodes them
/// with the ffmpeg executable, so it does not depend on which movie
/// writers ParaView was built with. Ogg Theora and PNG sequences go
/// through ParaView's writers directly.
class MovieExportDialog : public QDialog
{
  Q_OBJECT

public:
  explicit MovieExportDialog(QWidget* parent = nullptr);
  ~MovieExportDialog() override;

  void accept() override;

protected:
  void showEvent(QShowEvent* e) override;

private:
  class Internal;
  QScopedPointer<Internal> m_internal;
};

} // namespace tomviz

#endif
