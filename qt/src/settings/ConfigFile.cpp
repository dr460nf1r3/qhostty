#include "settings/ConfigFile.h"

namespace qhostty {

ConfigFile::ConfigFile(QString text) {
  m_finalNewline = text.isEmpty() || text.endsWith(u'\n');
  if (text.endsWith(u'\n')) {
    text.chop(1);
  }
  if (!text.isEmpty()) {
    m_lines = text.split(u'\n');
  }
}

QString ConfigFile::text() const {
  QString result = m_lines.join(u'\n');
  if (m_finalNewline && !m_lines.isEmpty()) {
    result.append(u'\n');
  }
  return result;
}

QString ConfigFile::keyOf(const QString& line) const {
  const QString trimmed = line.trimmed();
  if (trimmed.isEmpty() || trimmed.startsWith(u'#')) {
    return {};
  }
  const qsizetype separator = trimmed.indexOf(u'=');
  return separator < 0 ? trimmed : trimmed.left(separator).trimmed();
}

QStringList ConfigFile::values(const QString& key) const {
  QStringList result;
  for (const QString& line : m_lines) {
    if (keyOf(line) == key) {
      const qsizetype separator = line.indexOf(u'=');
      result.append(separator < 0 ? QString()
                                  : line.mid(separator + 1).trimmed());
    }
  }
  return result;
}

void ConfigFile::setValues(const QString& key, const QStringList& values) {
  qsizetype position = -1;
  for (qsizetype index = m_lines.size() - 1; index >= 0; --index) {
    if (keyOf(m_lines.at(index)) == key) {
      m_lines.removeAt(index);
      position = index;
    }
  }
  QStringList lines;
  for (const QString& value : values) {
    lines.append(key + QStringLiteral(" = ") + value);
  }
  // An empty value sets the key to its empty state, for example no font.
  if (values.isEmpty()) {
    lines.append(key + QStringLiteral(" = "));
  }
  if (position < 0) {
    position = m_lines.size();
  }
  for (qsizetype index = 0; index < lines.size(); ++index) {
    m_lines.insert(position + index, lines.at(index));
  }
}

void ConfigFile::remove(const QString& key) {
  for (qsizetype index = m_lines.size() - 1; index >= 0; --index) {
    if (keyOf(m_lines.at(index)) == key) {
      m_lines.removeAt(index);
    }
  }
}

}  // namespace qhostty
