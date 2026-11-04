#include "ConfigSchema.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace qhostty {

QList<ConfigOption> loadConfigSchema(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  if (document.object().value(QStringLiteral("version")).toInt() != 1) {
    return {};
  }

  QList<ConfigOption> options;
  for (const QJsonValue& value :
       document.object().value(QStringLiteral("options")).toArray()) {
    const QJsonObject object = value.toObject();
    ConfigOption option;
    option.key = object.value(QStringLiteral("key")).toString();
    option.kind = object.value(QStringLiteral("kind")).toString();
    option.type = object.value(QStringLiteral("type")).toString();
    option.optional = object.value(QStringLiteral("optional")).toBool();
    option.repeatable = object.value(QStringLiteral("repeatable")).toBool();
    if (object.contains(QStringLiteral("min"))) {
      option.minimum = object.value(QStringLiteral("min")).toDouble();
      option.maximum = object.value(QStringLiteral("max")).toDouble();
    }
    for (const QJsonValue& name :
         object.value(QStringLiteral("values")).toArray()) {
      option.values.append(name.toString());
    }
    option.docs = object.value(QStringLiteral("docs")).toString();
    if (!option.key.isEmpty()) {
      options.append(option);
    }
  }
  return options;
}

}  // namespace qhostty
