#include "ConfigValues.h"

#include <QCoreApplication>
#include <QProcess>

#include <memory>

namespace qhostty {

ConfigValues parseConfigValues(const QByteArray& text) {
  ConfigValues values;
  for (const QByteArray& rawLine : text.split('\n')) {
    const QString line = QString::fromUtf8(rawLine);
    const qsizetype separator = line.indexOf(u'=');
    if (line.startsWith(u'#') || separator <= 0) {
      continue;
    }
    const QString key = line.left(separator).trimmed();
    const QString value = line.mid(separator + 1).trimmed();
    QStringList& list = values[key];
    // An empty value means unset. Repeatable keys print one empty line.
    if (!value.isEmpty()) {
      list.append(value);
    }
  }
  return values;
}

void loadConfigValues(bool defaults,
                      QObject* context,
                      std::function<void(ConfigValues)> done) {
  auto* process = new QProcess(context);
  QStringList arguments{QStringLiteral("+show-config")};
  if (defaults) {
    arguments.append(QStringLiteral("--default"));
  }
  auto callback =
      std::make_shared<std::function<void(ConfigValues)>>(std::move(done));
  QObject::connect(process, &QProcess::finished, context,
                   [process, callback](int, QProcess::ExitStatus) {
                     (*callback)(parseConfigValues(
                         process->readAllStandardOutput()));
                     process->deleteLater();
                   });
  QObject::connect(process, &QProcess::errorOccurred, context,
                   [process, callback](QProcess::ProcessError error) {
                     if (error == QProcess::FailedToStart) {
                       (*callback)({});
                       process->deleteLater();
                     }
                   });
  process->start(QCoreApplication::applicationFilePath(), arguments);
}

}  // namespace qhostty
