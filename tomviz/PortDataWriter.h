/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPortDataWriter_h
#define tomvizPortDataWriter_h

#include "pipeline/PortType.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace tomviz {

namespace pipeline {
class PortData;
}

/// A file format a port payload can be written to.
struct PortFormat
{
  /// Stable key used to remember the user's choice in settings.
  QString id;
  QString description;
  /// Filename extension, without the leading dot.
  QString extension;
  /// Whether one file of this format can hold every scalar array of a
  /// volume. False formats keep only the active scalars, so callers
  /// must split a multi-array volume across one file per array.
  bool multiArray = false;

  QString label() const
  {
    return QStringLiteral("%1 (*.%2)").arg(description, extension);
  }
};

/// Writes a single pipeline output-port payload to a file, and answers
/// which formats each port type can be written to.
namespace PortDataWriter {

/// The type group @a type is offered under: every volume-like type
/// (TiltSeries, Volume, LabelMap, ImageData) collapses to ImageData, so
/// the user picks one format for all of them.
pipeline::PortType formatGroup(pipeline::PortType type);

/// Formats available for @a type, preferred default first. Empty for
/// types tomviz has no writer for.
QList<PortFormat> formats(pipeline::PortType type);

/// Look up a format of @a type by its id. Returns the group's default
/// format when @a id is unknown, or an empty format when the group has
/// no writers at all.
PortFormat formatById(pipeline::PortType type, const QString& id);

/// Names of the scalar arrays @a data carries. Volume payloads report
/// their point-data arrays; every other payload reports a single empty
/// name, meaning "the payload as a whole".
QStringList arrayNames(const pipeline::PortData& data);

/// Write @a data to @a path, choosing the writer from the path's
/// extension. For volume payloads @a arrayNames selects which scalar
/// arrays to include; an empty list means every array. Ignored for
/// payloads that aren't array-bearing.
bool write(const pipeline::PortData& data, const QStringList& arrayNames,
           const QString& path);

} // namespace PortDataWriter
} // namespace tomviz

#endif
