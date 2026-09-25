/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizPipelineNodeDefinitionEdits_h
#define tomvizPipelineNodeDefinitionEdits_h

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace tomviz {
namespace pipeline {

/// The fields of one ``parameters[]`` entry that the definition form
/// renders. Deliberately not the whole entry: descriptions in the wild
/// also carry ``filter``, ``bindToSink``, ``input``,
/// ``apply_to_each_array`` and friends, all of which are read somewhere
/// in the codebase. applyParameterFields() writes only the keys listed
/// here and leaves everything else in place, so editing a parameter
/// through the form can never silently drop one.
struct ParameterFields
{
  QString name;
  QString label;
  QString type;
  QString description;
  QJsonValue defaultValue = QJsonValue(QJsonValue::Undefined);
  QJsonValue minimum = QJsonValue(QJsonValue::Undefined);
  QJsonValue maximum = QJsonValue(QJsonValue::Undefined);
  QJsonValue step = QJsonValue(QJsonValue::Undefined);
  QJsonValue precision = QJsonValue(QJsonValue::Undefined);
  QJsonArray options;
  QString visibleIf;
  QString enableIf;
};

ParameterFields readParameterFields(const QJsonObject& param);

/// Write @a fields onto @a param in place, preserving every key the
/// form doesn't own. Fields that are empty / undefined remove their key
/// rather than writing a blank, so the form doesn't litter documents
/// with `"help": ""`.
void applyParameterFields(QJsonObject& param, const ParameterFields& fields);

/// Set @a key to @a value, or remove it when @a value is empty.
void setOrClear(QJsonObject& obj, const QString& key, const QString& value);

/// Set @a key to @a value, or remove it when @a value matches the
/// schema's own default for that key (an absent `supportsCancel` and a
/// `false` one mean the same thing).
void setOrClearBool(QJsonObject& obj, const QString& key, bool value,
                    bool schemaDefault);

/// Set @a key to @a value, or remove it when @a value is undefined or
/// null.
void setOrClearValue(QJsonObject& obj, const QString& key,
                     const QJsonValue& value);

/// Parse @a text as a single JSON value — a number, string, bool, or
/// array, so `1.0`, `"nearest"` and `[128, 128, 128]` all work. Returns
/// Undefined for blank or unparseable input, which callers treat as
/// "remove the key".
QJsonValue parseJsonValue(const QString& text);

/// Render @a value in the form parseJsonValue() accepts back — strings
/// come out quoted. Undefined and null both render as an empty string.
QString formatJsonValue(const QJsonValue& value);

} // namespace pipeline
} // namespace tomviz

#endif
