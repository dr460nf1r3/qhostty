#include "settings/editors/KeybindText.h"

#include <QtTest>

class KeybindTextTest final : public QObject {
  Q_OBJECT

 private slots:
  void splitsEntries();
  void namesKeys();
};

void KeybindTextTest::splitsEntries() {
  using Pair = std::pair<QString, QString>;
  QCOMPARE(qhostty::splitKeybind(QStringLiteral("ctrl+shift+t=new_tab")),
           Pair(QStringLiteral("ctrl+shift+t"), QStringLiteral("new_tab")));
  QCOMPARE(qhostty::splitKeybind(
               QStringLiteral("ctrl+==increase_font_size:1")),
           Pair(QStringLiteral("ctrl+="), QStringLiteral("increase_font_size:1")));
  QCOMPARE(qhostty::splitKeybind(QStringLiteral("==text:x")),
           Pair(QStringLiteral("="), QStringLiteral("text:x")));
  QCOMPARE(qhostty::splitKeybind(QStringLiteral("ctrl+a>n=new_window")),
           Pair(QStringLiteral("ctrl+a>n"), QStringLiteral("new_window")));
  QCOMPARE(qhostty::splitKeybind(QStringLiteral("clear")),
           Pair(QStringLiteral("clear"), QString()));
}

void KeybindTextTest::namesKeys() {
  QCOMPARE(qhostty::triggerFromKey(Qt::ControlModifier | Qt::ShiftModifier |
                                   Qt::Key_T),
           QStringLiteral("ctrl+shift+t"));
  QCOMPARE(qhostty::triggerFromKey(Qt::AltModifier | Qt::Key_Left),
           QStringLiteral("alt+arrow_left"));
  QCOMPARE(qhostty::triggerFromKey(Qt::ControlModifier | Qt::Key_Comma),
           QStringLiteral("ctrl+,"));
  QCOMPARE(qhostty::triggerFromKey(QKeyCombination(Qt::Key_F11)),
           QStringLiteral("f11"));
  QCOMPARE(qhostty::triggerFromKey(Qt::MetaModifier | Qt::Key_PageUp),
           QStringLiteral("super+page_up"));
  QCOMPARE(qhostty::triggerFromKey(QKeyCombination(Qt::Key_unknown)),
           QString());
}

QTEST_GUILESS_MAIN(KeybindTextTest)
#include "tst_KeybindText.moc"
