/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineNodeDefinitionValidator_h
#define tomvizPipelineNodeDefinitionValidator_h

#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace tomviz {
namespace pipeline {

/// Which C++ shell owns the description being validated. A node's shell
/// class is fixed at construction (PythonSource never grows inputs,
/// PythonTransform always has them), so a candidate description that
/// would require the other shell is rejected rather than applied.
enum class NodeShape
{
  Source,
  Transform
};

/// The descriptor schema the node was built against. Also frozen: v1
/// descriptions run on LegacyPythonTransform, v2 on PythonSource /
/// PythonTransform, and a live node cannot migrate between them.
enum class DefinitionSchema
{
  V1,
  V2
};

struct DefinitionIssue
{
  enum class Severity
  {
    Error,
    Warning
  };

  Severity severity = Severity::Error;
  QString message;
};

/// Outcome of checking a candidate description against the one a node
/// is currently running. Errors block the edit; warnings describe
/// consequences the user should see before committing.
struct DefinitionValidation
{
  QList<DefinitionIssue> issues;

  /// Parameters declared by the candidate but not the current
  /// description. They will appear with their declared defaults.
  QStringList addedParameters;
  /// Parameters the candidate drops. Their current values are lost.
  QStringList removedParameters;
  /// Parameters whose declared type changed. Their current values
  /// cannot be carried over and revert to the new defaults.
  QStringList retypedParameters;

  bool hasErrors() const;
  QStringList messages(DefinitionIssue::Severity severity) const;
};

/// The schema a description declares. Empty / unparseable / missing
/// ``schemaVersion`` all read as V1, matching the routing in
/// AddPythonTransformReaction.
DefinitionSchema definitionSchema(const QString& json);

/// Check @a candidateJson as a replacement for @a currentJson on a live
/// node of the given shape and schema.
///
/// Rejects anything that would change the node's identity (schema,
/// source-vs-transform shape, custom widget) or its port topology —
/// ports are frozen because nothing in the pipeline can remove a port
/// or the links hanging off one. Everything else (parameters, labels,
/// help text, cancel/complete support, external-execution flags) is
/// free to change, with the parameter consequences reported as
/// warnings.
DefinitionValidation validateNodeDefinition(const QString& currentJson,
                                            const QString& candidateJson,
                                            NodeShape shape,
                                            DefinitionSchema schema);

/// Parse a description's ``parameters`` array into name → declared
/// type. Nameless layout entries (``xyz_header``) are skipped, as are
/// entries the node turns into ports rather than parameters (v1's
/// ``dataset``) unless @a includeDatasets is set.
QMap<QString, QString> parameterDeclaredTypes(const QString& json,
                                              bool includeDatasets = false);

/// Carry previously-set parameter values onto a freshly re-parsed
/// default set. A value survives only when its name is still declared
/// AND its declared type is unchanged; an enumeration value must also
/// still be one of the declared options. Everything else keeps the new
/// default and is appended to @a resetNames.
QMap<QString, QVariant> mergeParameterValues(
  const QMap<QString, QVariant>& previousValues,
  const QMap<QString, QString>& previousTypes,
  const QMap<QString, QVariant>& newDefaults,
  const QMap<QString, QString>& newTypes,
  const QMap<QString, QJsonArray>& newEnumOptions,
  QStringList* resetNames = nullptr);

} // namespace pipeline
} // namespace tomviz

#endif
