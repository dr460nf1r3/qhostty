#include "settings/ConfigSchema.h"

#include <QtTest>

class ConfigSchemaTest final : public QObject {
  Q_OBJECT

 private slots:
  void loadsAllOptions();
  void mapsKinds();
};

namespace {
qhostty::ConfigOption find(const QList<qhostty::ConfigOption>& options,
                           const QString& key) {
  for (const auto& option : options) {
    if (option.key == key) {
      return option;
    }
  }
  return {};
}
}  // namespace

void ConfigSchemaTest::loadsAllOptions() {
  const auto options = qhostty::loadConfigSchema();
  QVERIFY(options.size() > 150);
  for (const auto& option : options) {
    QVERIFY2(!option.kind.isEmpty(), qPrintable(option.key));
    QVERIFY2(!option.docs.isEmpty(), qPrintable(option.key));
  }
}

void ConfigSchemaTest::mapsKinds() {
  const auto options = qhostty::loadConfigSchema();
  QCOMPARE(find(options, QStringLiteral("font-size")).kind,
           QStringLiteral("float"));
  QCOMPARE(find(options, QStringLiteral("background")).kind,
           QStringLiteral("color"));
  const auto bell = find(options, QStringLiteral("bell-features"));
  QCOMPARE(bell.kind, QStringLiteral("flags"));
  QVERIFY(bell.values.contains(QStringLiteral("audio")));
  const auto close = find(options, QStringLiteral("confirm-close-surface"));
  QCOMPARE(close.kind, QStringLiteral("enum"));
  QVERIFY(close.values.contains(QStringLiteral("always")));
  QVERIFY(find(options, QStringLiteral("font-family")).repeatable);
  QVERIFY(find(options, QStringLiteral("keybind")).repeatable);
  QVERIFY(qhostty::loadSchemaNames(QStringLiteral("actions"))
              .contains(QStringLiteral("new_split")));
  QVERIFY(qhostty::loadSchemaNames(QStringLiteral("keys"))
              .contains(QStringLiteral("arrow_left")));
  const auto cursor = find(options, QStringLiteral("cursor-style"));
  QCOMPARE(cursor.kind, QStringLiteral("enum"));
  QVERIFY(cursor.values.contains(QStringLiteral("block")));
}

QTEST_GUILESS_MAIN(ConfigSchemaTest)
#include "tst_ConfigSchema.moc"
