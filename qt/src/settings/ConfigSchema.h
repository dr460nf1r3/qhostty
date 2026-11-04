#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace qhostty {

// One Ghostty configuration option, from config-schema.json. The build
// generates that file from Config.zig, see src/qhostty_schema.zig.
struct ConfigOption {
  QString key;
  // bool, int, float, enum, flags, string, color, duration, path, theme,
  // palette, keybind, or text for all other types.
  QString kind;
  // The Zig type name, for example "BellFeatures".
  QString type;
  bool optional = false;
  bool repeatable = false;
  std::optional<double> minimum;
  std::optional<double> maximum;
  // Enum values or flag names.
  QStringList values;
  QString docs;
};

// Loads the options from the given JSON file. Returns an empty list if the
// file is missing or not valid.
QList<ConfigOption> loadConfigSchema(
    const QString& path = QStringLiteral(":/settings/config-schema.json"));

}  // namespace qhostty
