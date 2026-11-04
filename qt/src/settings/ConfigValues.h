#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

class QObject;

namespace qhostty {

// Config values by key, in config file syntax. Repeatable keys have one
// entry per line, for example one per keybind.
using ConfigValues = QHash<QString, QStringList>;

// Parses "key = value" lines as +show-config prints them.
[[nodiscard]] ConfigValues parseConfigValues(const QByteArray& text);

// Runs this executable with +show-config, with --default for the default
// values, and calls done with the result. Ghostty prints the values, so
// they always match the running version.
void loadConfigValues(bool defaults,
                      QObject* context,
                      std::function<void(ConfigValues)> done);

}  // namespace qhostty
