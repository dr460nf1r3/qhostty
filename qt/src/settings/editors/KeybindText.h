#pragma once

#include <QKeyCombination>
#include <QString>

#include <utility>

namespace qhostty {

// Splits "trigger=action". The key of a trigger can itself be "=", as in
// "ctrl+==increase_font_size:1", so the split is at the first "=" that
// does not follow "+" or start the text. Entries without an action, such
// as "clear", return an empty action.
[[nodiscard]] std::pair<QString, QString> splitKeybind(const QString& entry);

// The Ghostty trigger for a key, for example "ctrl+shift+t" or
// "alt+arrow_left". Returns an empty string for keys without a name.
[[nodiscard]] QString triggerFromKey(QKeyCombination key);

}  // namespace qhostty
