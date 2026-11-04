#include "settings/editors/FontFamilyEditor.h"

#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace qhostty {

FontFamilyEditor::FontFamilyEditor(QWidget* parent) : ValueEditor(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  m_rows = new QVBoxLayout();
  layout->addLayout(m_rows);
  auto* add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                              tr("Add Font"), this);
  add->setToolTip(tr("Fonts after the first are fallbacks for missing "
                     "characters"));
  layout->addWidget(add, 0, Qt::AlignLeft);
  connect(add, &QPushButton::clicked, this, [this]() {
    addRow(QString())->setFocus();
    emit changed();
  });
}

QFontComboBox* FontFamilyEditor::addRow(const QString& family) {
  auto* row = new QWidget(this);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* combo = new QFontComboBox(row);
  combo->setFontFilters(QFontComboBox::MonospacedFonts);
  combo->setEditable(true);
  combo->setCurrentText(family);
  // An empty family means the built-in default font.
  combo->lineEdit()->setPlaceholderText(tr("Default font"));
  auto* remove = new QToolButton(row);
  remove->setIcon(QIcon::fromTheme(QStringLiteral("list-remove")));
  remove->setText(QStringLiteral("−"));
  remove->setToolTip(tr("Remove this font"));
  layout->addWidget(combo, 1);
  layout->addWidget(remove);
  m_rows->addWidget(row);
  m_combos.append(combo);

  connect(combo, &QComboBox::currentTextChanged, this,
          &ValueEditor::changed);
  connect(remove, &QToolButton::clicked, this, [this, row, combo]() {
    m_combos.removeOne(combo);
    row->deleteLater();
    emit changed();
  });
  return combo;
}

QStringList FontFamilyEditor::values() const {
  QStringList result;
  for (const QFontComboBox* combo : m_combos) {
    if (!combo->currentText().trimmed().isEmpty()) {
      result.append(combo->currentText().trimmed());
    }
  }
  return result;
}

void FontFamilyEditor::setValues(const QStringList& values) {
  const QSignalBlocker blocker(this);
  while (!m_combos.isEmpty()) {
    QFontComboBox* combo = m_combos.takeLast();
    combo->parentWidget()->deleteLater();
  }
  for (const QString& family : values) {
    const QSignalBlocker comboBlocker(addRow(family));
  }
  if (values.isEmpty()) {
    addRow(QString());
  }
}

}  // namespace qhostty
