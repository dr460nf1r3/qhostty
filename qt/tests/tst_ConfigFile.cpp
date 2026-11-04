#include "settings/ConfigFile.h"

#include <QtTest>

class ConfigFileTest final : public QObject {
  Q_OBJECT

 private slots:
  void keepsCommentsAndOrder();
  void replacesRepeatableKeysInPlace();
  void appendsNewKeys();
  void removesKeys();
  void readsValues();
};

void ConfigFileTest::keepsCommentsAndOrder() {
  qhostty::ConfigFile file(QStringLiteral(
      "# My config\nfont-size = 12\n\n# Colors\nbackground = #000000\n"));
  file.setValues(QStringLiteral("font-size"), {QStringLiteral("14")});
  QCOMPARE(file.text(), QStringLiteral("# My config\nfont-size = 14\n\n"
                                       "# Colors\nbackground = #000000\n"));
}

void ConfigFileTest::replacesRepeatableKeysInPlace() {
  qhostty::ConfigFile file(QStringLiteral(
      "keybind = ctrl+a=copy\ntheme = x\nkeybind = ctrl+b=paste\n"));
  file.setValues(QStringLiteral("keybind"),
                 {QStringLiteral("ctrl+c=copy_to_clipboard"),
                  QStringLiteral("ctrl+v=paste_from_clipboard")});
  QCOMPARE(file.text(),
           QStringLiteral("keybind = ctrl+c=copy_to_clipboard\n"
                          "keybind = ctrl+v=paste_from_clipboard\n"
                          "theme = x\n"));
}

void ConfigFileTest::appendsNewKeys() {
  qhostty::ConfigFile file(QStringLiteral("font-size = 12"));
  file.setValues(QStringLiteral("theme"), {QStringLiteral("Breeze")});
  // A file without a final newline keeps that.
  QCOMPARE(file.text(), QStringLiteral("font-size = 12\ntheme = Breeze"));

  qhostty::ConfigFile empty;
  empty.setValues(QStringLiteral("font-family"), {});
  QCOMPARE(empty.text(), QStringLiteral("font-family = \n"));
}

void ConfigFileTest::removesKeys() {
  qhostty::ConfigFile file(QStringLiteral(
      "font-size = 12\n#font-size = 9\npalette = 0=#000000\n"
      "palette = 1=#ff0000\n"));
  file.remove(QStringLiteral("palette"));
  file.remove(QStringLiteral("font-size"));
  QCOMPARE(file.text(), QStringLiteral("#font-size = 9\n"));
}

void ConfigFileTest::readsValues() {
  qhostty::ConfigFile file(QStringLiteral(
      "  keybind=ctrl+a=copy\nkeybind = ctrl+b = paste\n# keybind = x\n"));
  QCOMPARE(file.values(QStringLiteral("keybind")),
           QStringList({QStringLiteral("ctrl+a=copy"),
                        QStringLiteral("ctrl+b = paste")}));
}

QTEST_GUILESS_MAIN(ConfigFileTest)
#include "tst_ConfigFile.moc"
