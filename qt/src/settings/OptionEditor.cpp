#include "settings/OptionEditor.h"

#include "settings/editors/ValueEditor.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>

#include <algorithm>
#include <limits>

namespace qhostty {
namespace {

// "font-size" becomes "Font size".
QString humanize(const QString& key) {
  QString result = key;
  result.replace(u'-', u' ');
  if (!result.isEmpty()) {
    result[0] = result[0].toUpper();
  }
  return result;
}

QString first(const QStringList& values) {
  return values.isEmpty() ? QString() : values.constFirst();
}

QStringList single(const QString& value) {
  return value.isEmpty() ? QStringList() : QStringList{value};
}

}  // namespace

OptionEditor::OptionEditor(const ConfigOption& option, QWidget* parent)
    : QWidget(parent), m_option(option) {
  auto* layout = new QGridLayout(this);
  layout->setContentsMargins(0, 6, 0, 6);
  layout->setHorizontalSpacing(8);
  layout->setColumnStretch(1, 1);

  m_name = new QLabel(label(), this);
  m_name->setToolTip(m_option.key);
  m_name->setMinimumWidth(220);
  m_name->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  layout->addWidget(m_name, 0, 0);

  m_editor = createEditor();
  layout->addWidget(m_editor, 0, 1);

  auto* help = new QToolButton(this);
  help->setIcon(QIcon::fromTheme(QStringLiteral("help-about")));
  help->setText(QStringLiteral("?"));
  help->setToolTip(tr("Show the documentation"));
  help->setCheckable(true);
  help->setAutoRaise(true);
  layout->addWidget(help, 0, 2, Qt::AlignTop);

  m_reset = new QToolButton(this);
  m_reset->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
  m_reset->setText(tr("Reset"));
  m_reset->setToolTip(tr("Reset to the default value"));
  m_reset->setAutoRaise(true);
  layout->addWidget(m_reset, 0, 3, Qt::AlignTop);
  connect(m_reset, &QToolButton::clicked, this, [this]() {
    setValues(m_defaults);
    emit resetRequested();
  });

  m_error = new QLabel(this);
  m_error->setWordWrap(true);
  m_error->setStyleSheet(QStringLiteral("color: palette(highlight);"));
  m_error->hide();
  layout->addWidget(m_error, 1, 1, 1, 3);

  m_docs = new QLabel(m_option.docs.trimmed(), this);
  m_docs->setWordWrap(true);
  m_docs->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_docs->setForegroundRole(QPalette::PlaceholderText);
  m_docs->hide();
  layout->addWidget(m_docs, 2, 0, 1, 4);
  connect(help, &QToolButton::toggled, m_docs, &QWidget::setVisible);

  updateState();
}

QString OptionEditor::label() const {
  return humanize(m_option.key);
}

QWidget* OptionEditor::createEditor() {
  const QString& kind = m_option.kind;
  const auto changed = [this]() {
    if (!m_updating) {
      updateState();
      emit edited();
    }
  };

  if (ValueEditor* custom = createValueEditor(m_option, this)) {
    m_custom = custom;
    m_get = [custom]() { return custom->values(); };
    m_set = [custom](const QStringList& values) { custom->setValues(values); };
    connect(custom, &ValueEditor::changed, this, changed);
    return custom;
  }

  if (m_option.repeatable) {
    // One value per line. Keybinds and palettes get their own editors
    // later; this works for every repeatable option.
    auto* edit = new QPlainTextEdit(this);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->setTabChangesFocus(true);
    const int lines = kind == QStringLiteral("keybind") ||
                              kind == QStringLiteral("palette")
                          ? 10
                          : 3;
    edit->setFixedHeight(edit->fontMetrics().lineSpacing() * lines + 12);
    edit->setPlaceholderText(tr("One value per line"));
    m_get = [edit]() {
      QStringList values;
      for (const QString& line : edit->toPlainText().split(u'\n')) {
        if (!line.trimmed().isEmpty()) {
          values.append(line.trimmed());
        }
      }
      return values;
    };
    m_set = [edit](const QStringList& values) {
      edit->setPlainText(values.join(u'\n'));
    };
    connect(edit, &QPlainTextEdit::textChanged, this, changed);
    return edit;
  }

  if (kind == QStringLiteral("bool")) {
    auto* box = new QCheckBox(this);
    m_get = [box]() {
      return QStringList{box->isChecked() ? QStringLiteral("true")
                                          : QStringLiteral("false")};
    };
    m_set = [box](const QStringList& values) {
      box->setChecked(first(values) == QStringLiteral("true"));
    };
    connect(box, &QCheckBox::toggled, this, changed);
    return box;
  }

  if (kind == QStringLiteral("enum")) {
    auto* combo = new QComboBox(this);
    if (m_option.optional) {
      combo->addItem(tr("(not set)"), QString());
    }
    for (const QString& value : m_option.values) {
      combo->addItem(value, value);
    }
    m_get = [combo]() { return single(combo->currentData().toString()); };
    m_set = [combo](const QStringList& values) {
      const QString value = first(values);
      int index = combo->findData(value);
      if (index < 0 && !value.isEmpty()) {
        // A value that the schema does not know, for example an alias.
        combo->addItem(value, value);
        index = combo->count() - 1;
      }
      combo->setCurrentIndex(std::max(index, 0));
    };
    connect(combo, &QComboBox::currentIndexChanged, this, changed);
    return combo;
  }

  if (kind == QStringLiteral("flags")) {
    // Ghostty writes all flags: "name" when set, "no-name" when not.
    auto* box = new QWidget(this);
    auto* grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    constexpr int columns = 3;
    QList<QCheckBox*> checks;
    for (const QString& name : m_option.values) {
      auto* check = new QCheckBox(name, box);
      grid->addWidget(check, int(checks.size()) / columns,
                      int(checks.size()) % columns);
      checks.append(check);
      connect(check, &QCheckBox::toggled, this, changed);
    }
    const QStringList names = m_option.values;
    m_get = [checks, names]() {
      QStringList parts;
      for (qsizetype i = 0; i < checks.size(); ++i) {
        parts.append(checks.at(i)->isChecked() ? names.at(i)
                                               : QStringLiteral("no-") +
                                                     names.at(i));
      }
      return QStringList{parts.join(u',')};
    };
    m_set = [checks, names](const QStringList& values) {
      const QStringList parts = first(values).split(u',', Qt::SkipEmptyParts);
      for (qsizetype i = 0; i < checks.size(); ++i) {
        checks.at(i)->setChecked(parts.contains(names.at(i)));
      }
    };
    return box;
  }

  if (kind == QStringLiteral("int") && !m_option.optional) {
    auto* spin = new QSpinBox(this);
    const double low = m_option.minimum.value_or(0);
    const double high = m_option.maximum.value_or(0);
    spin->setRange(int(std::max<double>(low, std::numeric_limits<int>::min())),
                   int(std::min<double>(high, std::numeric_limits<int>::max())));
    m_get = [spin]() { return QStringList{QString::number(spin->value())}; };
    m_set = [spin](const QStringList& values) {
      spin->setValue(first(values).toInt());
    };
    connect(spin, &QSpinBox::valueChanged, this, changed);
    return spin;
  }

  // Text editors, with a validator or a picker for some kinds.
  auto* box = new QWidget(this);
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  auto* edit = new QLineEdit(box);
  edit->setClearButtonEnabled(true);
  row->addWidget(edit, 1);
  if (kind == QStringLiteral("int")) {
    edit->setValidator(new QIntValidator(edit));
  } else if (kind == QStringLiteral("float")) {
    auto* validator = new QDoubleValidator(edit);
    validator->setLocale(QLocale::c());
    edit->setValidator(validator);
  } else if (kind == QStringLiteral("color")) {
    auto* pick = new QPushButton(box);
    pick->setFixedWidth(40);
    pick->setToolTip(tr("Choose a color"));
    row->addWidget(pick);
    const auto showColor = [pick, edit]() {
      const QColor color(edit->text());
      pick->setStyleSheet(color.isValid()
                              ? QStringLiteral("background: %1;")
                                    .arg(color.name())
                              : QString());
    };
    connect(edit, &QLineEdit::textChanged, pick, showColor);
    connect(pick, &QPushButton::clicked, this, [this, edit]() {
      const QColor color = QColorDialog::getColor(QColor(edit->text()), this,
                                                  label());
      if (color.isValid()) {
        edit->setText(color.name());
      }
    });
  } else if (kind == QStringLiteral("path")) {
    auto* browse = new QToolButton(box);
    browse->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
    browse->setText(QStringLiteral("…"));
    browse->setToolTip(tr("Choose a file"));
    row->addWidget(browse);
    connect(browse, &QToolButton::clicked, this, [this, edit]() {
      const QString path =
          QFileDialog::getOpenFileName(this, label(), edit->text());
      if (!path.isEmpty()) {
        edit->setText(path);
      }
    });
  }
  m_get = [edit]() { return single(edit->text().trimmed()); };
  m_set = [edit](const QStringList& values) { edit->setText(first(values)); };
  connect(edit, &QLineEdit::textEdited, this, changed);
  return box;
}

QStringList OptionEditor::values() const {
  return m_get();
}

void OptionEditor::setValues(const QStringList& values) {
  m_updating = true;
  m_set(values);
  m_updating = false;
  updateState();
}

void OptionEditor::setDefaults(const QStringList& defaults) {
  m_defaults = defaults;
  updateState();
}

bool OptionEditor::editsFileLines() const {
  return m_custom != nullptr && m_custom->editsFileLines();
}

void OptionEditor::setReference(const QStringList& values) {
  if (m_custom != nullptr) {
    m_custom->setReference(values);
  }
}

bool OptionEditor::isDefault() const {
  return values() == m_defaults;
}

void OptionEditor::setError(const QString& error) {
  m_error->setText(error);
  m_error->setVisible(!error.isEmpty());
}

bool OptionEditor::matches(const QString& text) const {
  return m_option.key.contains(text, Qt::CaseInsensitive) ||
         label().contains(text, Qt::CaseInsensitive) ||
         m_option.docs.contains(text, Qt::CaseInsensitive);
}

void OptionEditor::updateState() {
  const bool changed = !isDefault();
  QFont font = m_name->font();
  font.setBold(changed);
  m_name->setFont(font);
  m_reset->setEnabled(changed);
}

}  // namespace qhostty
