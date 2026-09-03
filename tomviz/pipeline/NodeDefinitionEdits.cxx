/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "NodeDefinitionEdits.h"

#include <QJsonDocument>

namespace tomviz {
namespace pipeline {

ParameterFields readParameterFields(const QJsonObject& param)
{
  ParameterFields fields;
  fields.name = param.value(QStringLiteral("name")).toString();
  fields.label = param.value(QStringLiteral("label")).toString();
  fields.type = param.value(QStringLiteral("type")).toString();
  fields.description = param.value(QStringLiteral("description")).toString();
  fields.defaultValue = param.value(QStringLiteral("default"));
  fields.minimum = param.value(QStringLiteral("minimum"));
  fields.maximum = param.value(QStringLiteral("maximum"));
  fields.step = param.value(QStringLiteral("step"));
  fields.precision = param.value(QStringLiteral("precision"));
  fields.options = param.value(QStringLiteral("options")).toArray();
  fields.visibleIf = param.value(QStringLiteral("visible_if")).toString();
  fields.enableIf = param.value(QStringLiteral("enable_if")).toString();
  return fields;
}

void applyParameterFields(QJsonObject& param, const ParameterFields& fields)
{
  setOrClear(param, QStringLiteral("name"), fields.name);
  setOrClear(param, QStringLiteral("label"), fields.label);
  setOrClear(param, QStringLiteral("type"), fields.type);
  setOrClear(param, QStringLiteral("description"), fields.description);
  setOrClearValue(param, QStringLiteral("default"), fields.defaultValue);
  setOrClearValue(param, QStringLiteral("minimum"), fields.minimum);
  setOrClearValue(param, QStringLiteral("maximum"), fields.maximum);
  setOrClearValue(param, QStringLiteral("step"), fields.step);
  setOrClearValue(param, QStringLiteral("precision"), fields.precision);
  setOrClear(param, QStringLiteral("visible_if"), fields.visibleIf);
  setOrClear(param, QStringLiteral("enable_if"), fields.enableIf);

  if (fields.options.isEmpty()) {
    param.remove(QStringLiteral("options"));
  } else {
    param[QStringLiteral("options")] = fields.options;
  }
}

void setOrClear(QJsonObject& obj, const QString& key, const QString& value)
{
  if (value.isEmpty()) {
    obj.remove(key);
  } else {
    obj[key] = value;
  }
}

void setOrClearBool(QJsonObject& obj, const QString& key, bool value,
                    bool schemaDefault)
{
  if (value == schemaDefault) {
    obj.remove(key);
  } else {
    obj[key] = value;
  }
}

void setOrClearValue(QJsonObject& obj, const QString& key,
                     const QJsonValue& value)
{
  if (value.isUndefined() || value.isNull()) {
    obj.remove(key);
  } else {
    obj[key] = value;
  }
}

QJsonValue parseJsonValue(const QString& text)
{
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return QJsonValue(QJsonValue::Undefined);
  }
  // QJsonDocument only parses documents, not bare values, so wrap the
  // text in an array and unwrap the single element.
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(
    QStringLiteral("[%1]").arg(trimmed).toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isArray()) {
    return QJsonValue(QJsonValue::Undefined);
  }
  QJsonArray array = doc.array();
  if (array.size() != 1) {
    return QJsonValue(QJsonValue::Undefined);
  }
  return array.at(0);
}

QString formatJsonValue(const QJsonValue& value)
{
  if (value.isUndefined() || value.isNull()) {
    return {};
  }
  // Same wrap-and-unwrap trick in reverse: serialize a one-element
  // array and strip the brackets, so numbers keep their JSON spelling
  // and strings come back quoted.
  QJsonArray array{ value };
  QString text = QString::fromUtf8(
    QJsonDocument(array).toJson(QJsonDocument::Compact));
  text = text.trimmed();
  if (text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']'))) {
    text = text.mid(1, text.size() - 2);
  }
  return text;
}

} // namespace pipeline
} // namespace tomviz
