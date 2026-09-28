/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineThreadUtils_h
#define tomvizPipelineThreadUtils_h

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QThread>

#include <type_traits>
#include <utility>

namespace tomviz {
namespace pipeline {

/// Run \a fn synchronously on \a context's thread, then return.
///
/// Pipeline node execution - execute(), consume(), postConsume() - runs on the
/// worker thread (see ThreadedExecutor). But GUI, OpenGL and render-window
/// state may only be touched on the thread that owns it: the GUI thread, which
/// owns every Node and port (they are never moveToThread'd). Calling
/// MakeCurrent / Render / SM-proxy methods directly from a worker thread aborts
/// with the Qt fatal "Cannot make QOpenGLContext current in a different thread"
/// or crashes inside VTK.
///
/// Wrap any such access in runOnThread(): it runs \a fn inline when already on
/// \a context's thread, otherwise marshals it via the event loop and blocks
/// until it completes. This replaces the hand-rolled idiom
///   if (QThread::currentThread() == thread()) { apply(); }
///   else { QMetaObject::invokeMethod(this, apply, Qt::BlockingQueuedConnection); }
/// that was previously duplicated across the pipeline - prefer this helper so
/// the threading contract lives in one place.
template <typename Fn>
void runOnThread(QObject* context, Fn&& fn)
{
  if (!context || QThread::currentThread() == context->thread()) {
    std::forward<Fn>(fn)();
  } else {
    QMetaObject::invokeMethod(context, std::forward<Fn>(fn),
                              Qt::BlockingQueuedConnection);
  }
}

/// Like runOnThread() but returns \a fn's result. The result type must be
/// default-constructible; use runOnThread() for void functors.
template <typename Fn>
auto callOnThread(QObject* context, Fn&& fn) -> decltype(fn())
{
  using Result = decltype(fn());
  static_assert(!std::is_reference_v<Result> &&
                  std::is_default_constructible_v<Result>,
                "callOnThread's functor must return a default-constructible, "
                "non-reference type; use runOnThread() for void functors.");
  if (!context || QThread::currentThread() == context->thread()) {
    return std::forward<Fn>(fn)();
  }
  Result result{};
  QMetaObject::invokeMethod(
    context, [&]() { result = fn(); }, Qt::BlockingQueuedConnection);
  return result;
}

} // namespace pipeline
} // namespace tomviz

/// Debug-only guard for functions that touch OpenGL or render-window state.
/// Place at the top of any such function (or inside the marshaled lambda that
/// performs the access): it turns an accidental off-GUI-thread call into an
/// immediate, clearly-labeled assertion during development, instead of a
/// cryptic Qt/VTK abort deep in the call stack. Compiles to nothing in release
/// builds, and is a no-op before QCoreApplication exists (e.g. in unit tests).
#ifndef NDEBUG
#define TOMVIZ_ASSERT_GUI_THREAD()                                             \
  Q_ASSERT_X(QCoreApplication::instance() == nullptr ||                        \
               QThread::currentThread() ==                                     \
                 QCoreApplication::instance()->thread(),                       \
             __func__, "OpenGL/render access must be on the GUI thread")
#else
#define TOMVIZ_ASSERT_GUI_THREAD() ((void)0)
#endif

#endif
