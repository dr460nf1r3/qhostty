#include "KeyEventTranslation.h"

#include <QChar>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QString>

#include <private/qkeymapper_p.h>

#include <type_traits>

namespace {
// Qt 6.7 returns QKeyCombination from QKeyMapper, older versions return int.
template <typename T>
int combinedKey(const T& key) {
  if constexpr (std::is_same_v<T, int>) {
    return key;
  } else {
    return key.toCombined();
  }
}

// Returns the key without Shift in the active keyboard layout. key() gives
// the shifted symbol, for example '"' for Shift+2 on a German layout. The
// first key from QKeyMapper is the key at the unshifted level.
int baseKey(const QKeyEvent& event) {
  // QKeyMapper needs the platform integration of a QGuiApplication.
  if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) {
    return event.key();
  }

  const auto keys = QKeyMapper::possibleKeys(const_cast<QKeyEvent*>(&event));
  if (!keys.isEmpty()) {
    const int key = combinedKey(keys.first()) & ~Qt::KeyboardModifierMask;
    if (key != 0 && key != Qt::Key_unknown) {
      return key;
    }
  }
  return event.key();
}
}  // namespace

uint32_t ghosttyUnshiftedCodepoint(const QKeyEvent& event) {
  const int qtKey = baseKey(event);
  if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
    return static_cast<uint32_t>('a' + qtKey - Qt::Key_A);
  }
  if (qtKey >= 0x20 && qtKey < 0x01000000) {
    // Qt uses the upper case codepoint for letters such as Ä.
    return QChar::toLower(static_cast<char32_t>(qtKey));
  }
  return 0;
}

QByteArray ghosttyKeyText(const QKeyEvent& event, uint32_t unshiftedCodepoint) {
  QByteArray text = event.text().toUtf8();
  if (!event.modifiers().testFlag(Qt::ControlModifier) || text.size() != 1 ||
      unshiftedCodepoint < 0x20) {
    return text;
  }

  const auto byte = static_cast<unsigned char>(text.front());
  if (byte >= 0x20 && byte != 0x7f) {
    return text;
  }

  char32_t codepoint = static_cast<char32_t>(unshiftedCodepoint);
  if (event.modifiers().testFlag(Qt::ShiftModifier) && codepoint >= U'a' &&
      codepoint <= U'z') {
    codepoint = codepoint - U'a' + U'A';
  }
  return QString::fromUcs4(&codepoint, 1).toUtf8();
}

bool ghosttyShouldSendKeyRelease(const QKeyEvent& event) {
  return !event.isAutoRepeat();
}

ghostty_input_key_e ghosttyLogicalKey(const QKeyEvent& event) {
  const int key = baseKey(event);
  if (key >= Qt::Key_A && key <= Qt::Key_Z) {
    return static_cast<ghostty_input_key_e>(GHOSTTY_KEY_A + key - Qt::Key_A);
  }
  if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    return static_cast<ghostty_input_key_e>(GHOSTTY_KEY_DIGIT_0 + key -
                                            Qt::Key_0);
  }
  if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
    return static_cast<ghostty_input_key_e>(GHOSTTY_KEY_F1 + key - Qt::Key_F1);
  }

  switch (key) {
    case Qt::Key_Escape:
      return GHOSTTY_KEY_ESCAPE;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
      return GHOSTTY_KEY_TAB;
    case Qt::Key_Backspace:
      return GHOSTTY_KEY_BACKSPACE;
    case Qt::Key_Return:
      return GHOSTTY_KEY_ENTER;
    case Qt::Key_Enter:
      return GHOSTTY_KEY_NUMPAD_ENTER;
    case Qt::Key_Insert:
      return GHOSTTY_KEY_INSERT;
    case Qt::Key_Delete:
      return GHOSTTY_KEY_DELETE;
    case Qt::Key_Home:
      return GHOSTTY_KEY_HOME;
    case Qt::Key_End:
      return GHOSTTY_KEY_END;
    case Qt::Key_PageUp:
      return GHOSTTY_KEY_PAGE_UP;
    case Qt::Key_PageDown:
      return GHOSTTY_KEY_PAGE_DOWN;
    case Qt::Key_Left:
      return GHOSTTY_KEY_ARROW_LEFT;
    case Qt::Key_Right:
      return GHOSTTY_KEY_ARROW_RIGHT;
    case Qt::Key_Up:
      return GHOSTTY_KEY_ARROW_UP;
    case Qt::Key_Down:
      return GHOSTTY_KEY_ARROW_DOWN;
    case Qt::Key_Space:
      return GHOSTTY_KEY_SPACE;
    case Qt::Key_CapsLock:
      return GHOSTTY_KEY_CAPS_LOCK;
    case Qt::Key_NumLock:
      return GHOSTTY_KEY_NUM_LOCK;
    case Qt::Key_ScrollLock:
      return GHOSTTY_KEY_SCROLL_LOCK;
    case Qt::Key_Pause:
      return GHOSTTY_KEY_PAUSE;
    case Qt::Key_Print:
      return GHOSTTY_KEY_PRINT_SCREEN;
    case Qt::Key_Menu:
      return GHOSTTY_KEY_CONTEXT_MENU;
    default:
      return GHOSTTY_KEY_UNIDENTIFIED;
  }
}
