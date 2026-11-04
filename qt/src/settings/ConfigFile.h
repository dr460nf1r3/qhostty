#pragma once

#include <QString>
#include <QStringList>

namespace qhostty {

// Edits the text of a Ghostty config file. Only the lines of the changed
// keys change; comments, empty lines and the order of the other lines stay.
class ConfigFile {
 public:
  explicit ConfigFile(QString text = {});

  [[nodiscard]] QString text() const;
  // The values of the key in this file, in order.
  [[nodiscard]] QStringList values(const QString& key) const;
  // Replaces the lines of the key. The new lines go where the first old
  // line was, or to the end of the file.
  void setValues(const QString& key, const QStringList& values);
  // Removes all lines of the key, so the default applies.
  void remove(const QString& key);

 private:
  [[nodiscard]] QString keyOf(const QString& line) const;

  QStringList m_lines;
  bool m_finalNewline = true;
};

}  // namespace qhostty
