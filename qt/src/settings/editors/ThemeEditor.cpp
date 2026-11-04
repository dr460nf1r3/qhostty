#include "settings/editors/ThemeEditor.h"

#include "settings/ConfigValues.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalBlocker>

namespace qhostty {

// The background, the foreground and the 16 palette colors of a theme.
class ThemePreview final : public QWidget {
 public:
  explicit ThemePreview(QWidget* parent) : QWidget(parent) {
    setFixedSize(16 * 12 + 40, 24);
  }

  void setThemeFile(const QString& path) {
    m_colors.clear();
    m_background = QColor();
    m_foreground = QColor();
    QFile file(path);
    if (!path.isEmpty() && file.open(QIODevice::ReadOnly)) {
      const ConfigValues values = parseConfigValues(file.readAll());
      m_background = QColor(values.value(QStringLiteral("background")).value(0));
      m_foreground = QColor(values.value(QStringLiteral("foreground")).value(0));
      m_colors.fill(QColor(), 16);
      for (const QString& entry : values.value(QStringLiteral("palette"))) {
        const qsizetype separator = entry.indexOf(u'=');
        const int index = entry.left(separator).toInt();
        if (separator > 0 && index >= 0 && index < 16) {
          m_colors[index] = QColor(entry.mid(separator + 1));
        }
      }
    }
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    if (!m_background.isValid()) {
      return;
    }
    QPainter painter(this);
    painter.fillRect(rect(), m_background);
    painter.setPen(m_foreground);
    painter.drawText(QRect(0, 0, 36, height()), Qt::AlignCenter,
                     QStringLiteral("Ab"));
    for (qsizetype index = 0; index < m_colors.size(); ++index) {
      painter.fillRect(QRect(40 + int(index) * 12, 6, 10, 12),
                       m_colors.at(index));
    }
  }

 private:
  QColor m_background;
  QColor m_foreground;
  QList<QColor> m_colors;
};

ThemeEditor::ThemeEditor(QWidget* parent) : ValueEditor(parent) {
  auto* layout = new QGridLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  m_light = new QComboBox(this);
  m_light->setEditable(true);
  m_light->setInsertPolicy(QComboBox::NoInsert);
  m_lightPreview = new ThemePreview(this);
  m_split = new QCheckBox(tr("Use a different theme in dark mode"), this);
  m_dark = new QComboBox(this);
  m_dark->setEditable(true);
  m_dark->setInsertPolicy(QComboBox::NoInsert);
  m_darkPreview = new ThemePreview(this);
  layout->addWidget(m_light, 0, 0);
  layout->addWidget(m_lightPreview, 0, 1);
  layout->addWidget(m_split, 1, 0, 1, 2);
  layout->addWidget(m_dark, 2, 0);
  layout->addWidget(m_darkPreview, 2, 1);
  layout->setColumnStretch(0, 1);
  m_dark->setVisible(false);
  m_darkPreview->setVisible(false);

  const auto edited = [this]() {
    updatePreviews();
    emit changed();
  };
  connect(m_light, &QComboBox::currentTextChanged, this, edited);
  connect(m_dark, &QComboBox::currentTextChanged, this, edited);
  connect(m_split, &QCheckBox::toggled, this, [this, edited](bool split) {
    m_light->setPlaceholderText(split ? tr("Light mode") : QString());
    m_dark->setVisible(split);
    m_darkPreview->setVisible(split);
    edited();
  });
  loadThemes();
}

void ThemeEditor::loadThemes() {
  auto* process = new QProcess(this);
  connect(process, &QProcess::finished, this, [this, process]() {
    // Lines are "name (location) path".
    static const QRegularExpression line(
        QStringLiteral("^(.+) \\((\\w+)\\) (/.+)$"));
    QStringList names;
    for (const QByteArray& raw : process->readAllStandardOutput().split('\n')) {
      const QRegularExpressionMatch match = line.match(QString::fromUtf8(raw));
      if (match.hasMatch() && !m_paths.contains(match.captured(1))) {
        names.append(match.captured(1));
        m_paths.insert(match.captured(1), match.captured(3));
      }
    }
    for (QComboBox* combo : {m_light, m_dark}) {
      const QString current = combo->currentText();
      const QSignalBlocker blocker(combo);
      combo->clear();
      combo->addItem(QString());
      combo->addItems(names);
      combo->setCurrentText(current);
    }
    updatePreviews();
    process->deleteLater();
  });
  process->start(QCoreApplication::applicationFilePath(),
                 {QStringLiteral("+list-themes"), QStringLiteral("--plain"),
                  QStringLiteral("--path")});
}

void ThemeEditor::updatePreviews() {
  m_lightPreview->setThemeFile(m_paths.value(m_light->currentText()));
  m_darkPreview->setThemeFile(m_paths.value(m_dark->currentText()));
}

QStringList ThemeEditor::values() const {
  const QString light = m_light->currentText().trimmed();
  const QString dark = m_dark->currentText().trimmed();
  if (m_split->isChecked() && !light.isEmpty() && !dark.isEmpty()) {
    return {QStringLiteral("light:%1,dark:%2").arg(light, dark)};
  }
  return light.isEmpty() ? QStringList() : QStringList{light};
}

void ThemeEditor::setValues(const QStringList& values) {
  const QString value = values.value(0);
  QString light = value;
  QString dark;
  // "light:A,dark:B" in any order.
  if (value.contains(QStringLiteral("light:")) ||
      value.contains(QStringLiteral("dark:"))) {
    light.clear();
    for (const QString& part : value.split(u',')) {
      const QString trimmed = part.trimmed();
      if (trimmed.startsWith(QStringLiteral("light:"))) {
        light = trimmed.mid(6).trimmed();
      } else if (trimmed.startsWith(QStringLiteral("dark:"))) {
        dark = trimmed.mid(5).trimmed();
      }
    }
  }
  const QSignalBlocker lightBlocker(m_light);
  const QSignalBlocker darkBlocker(m_dark);
  const QSignalBlocker splitBlocker(m_split);
  m_light->setCurrentText(light);
  m_dark->setCurrentText(dark);
  m_split->setChecked(!dark.isEmpty());
  m_dark->setVisible(!dark.isEmpty());
  m_darkPreview->setVisible(!dark.isEmpty());
  updatePreviews();
}

}  // namespace qhostty
