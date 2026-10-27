#include "DropText.h"

#include <QMimeData>
#include <QStringList>
#include <QUrl>

QString ghosttyShellEscape(const QString& value) {
  static const QString special = QStringLiteral("\\\"'$`*? |()");
  QString result;
  result.reserve(value.size());
  for (const QChar character : value) {
    if (special.contains(character)) {
      result.append(QLatin1Char('\\'));
    }
    result.append(character);
  }
  return result;
}

QString ghosttyDropText(const QMimeData& mime) {
  if (mime.hasUrls()) {
    QStringList parts;
    for (const QUrl& url : mime.urls()) {
      parts.append(ghosttyShellEscape(
          url.isLocalFile() ? url.toLocalFile() : url.toString()));
    }
    return parts.join(QLatin1Char(' '));
  }
  return mime.hasText() ? mime.text() : QString();
}
