#include "settings/editors/KeybindText.h"

#include <QHash>

namespace qhostty {

std::pair<QString, QString> splitKeybind(const QString& entry) {
  for (qsizetype index = 1; index < entry.size(); ++index) {
    if (entry.at(index) == u'=' && entry.at(index - 1) != u'+') {
      return {entry.left(index).trimmed(), entry.mid(index + 1).trimmed()};
    }
  }
  return {entry.trimmed(), QString()};
}

QString triggerFromKey(QKeyCombination combination) {
  // Names of input.Key for keys without a printable character.
  static const QHash<int, QString> names{
      {Qt::Key_Escape, QStringLiteral("escape")},
      {Qt::Key_Return, QStringLiteral("enter")},
      {Qt::Key_Enter, QStringLiteral("numpad_enter")},
      {Qt::Key_Tab, QStringLiteral("tab")},
      {Qt::Key_Backtab, QStringLiteral("tab")},
      {Qt::Key_Backspace, QStringLiteral("backspace")},
      {Qt::Key_Space, QStringLiteral("space")},
      {Qt::Key_Insert, QStringLiteral("insert")},
      {Qt::Key_Delete, QStringLiteral("delete")},
      {Qt::Key_Home, QStringLiteral("home")},
      {Qt::Key_End, QStringLiteral("end")},
      {Qt::Key_PageUp, QStringLiteral("page_up")},
      {Qt::Key_PageDown, QStringLiteral("page_down")},
      {Qt::Key_Left, QStringLiteral("arrow_left")},
      {Qt::Key_Right, QStringLiteral("arrow_right")},
      {Qt::Key_Up, QStringLiteral("arrow_up")},
      {Qt::Key_Down, QStringLiteral("arrow_down")},
      {Qt::Key_Print, QStringLiteral("print_screen")},
      {Qt::Key_Pause, QStringLiteral("pause")},
      {Qt::Key_Menu, QStringLiteral("context_menu")},
  };

  const int key = combination.key();
  QString name = names.value(key);
  if (name.isEmpty() && key >= Qt::Key_F1 && key <= Qt::Key_F24) {
    name = QStringLiteral("f%1").arg(key - Qt::Key_F1 + 1);
  }
  if (name.isEmpty() && key >= 0x20 && key < 0x110000) {
    name = QString(QChar::fromUcs4(char32_t(key))).toLower();
  }
  if (name.isEmpty()) {
    return {};
  }

  QString result;
  const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
  if (modifiers & Qt::ControlModifier) {
    result += QStringLiteral("ctrl+");
  }
  if (modifiers & Qt::AltModifier) {
    result += QStringLiteral("alt+");
  }
  if (modifiers & Qt::ShiftModifier || key == Qt::Key_Backtab) {
    result += QStringLiteral("shift+");
  }
  if (modifiers & Qt::MetaModifier) {
    result += QStringLiteral("super+");
  }
  return result + name;
}

}  // namespace qhostty
