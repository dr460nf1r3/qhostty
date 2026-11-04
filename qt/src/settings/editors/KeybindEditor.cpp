#include "settings/editors/KeybindEditor.h"

#include "settings/ConfigSchema.h"
#include "settings/editors/KeybindText.h"

#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace qhostty {
namespace {

// Completes action names in the action column.
class ActionDelegate final : public QStyledItemDelegate {
 public:
  ActionDelegate(QStringList actions, QObject* parent)
      : QStyledItemDelegate(parent), m_actions(std::move(actions)) {}

  QWidget* createEditor(QWidget* parent,
                        const QStyleOptionViewItem&,
                        const QModelIndex&) const override {
    auto* edit = new QLineEdit(parent);
    auto* completer = new QCompleter(m_actions, edit);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    edit->setCompleter(completer);
    return edit;
  }

 private:
  QStringList m_actions;
};

QTableWidget* createTable(QWidget* parent) {
  auto* table = new QTableWidget(0, 2, parent);
  table->setHorizontalHeaderLabels(
      {KeybindEditor::tr("Trigger"), KeybindEditor::tr("Action")});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->verticalHeader()->hide();
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setMinimumHeight(table->fontMetrics().lineSpacing() * 10);
  return table;
}

}  // namespace

KeybindEditor::KeybindEditor(QWidget* parent)
    : ValueEditor(parent),
      m_actions(loadSchemaNames(QStringLiteral("actions"))) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_table = createTable(this);
  m_table->setItemDelegateForColumn(1, new ActionDelegate(m_actions, this));
  layout->addWidget(m_table);

  auto* buttons = new QHBoxLayout();
  auto* add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                              tr("Add"), this);
  auto* recordButton = new QPushButton(
      QIcon::fromTheme(QStringLiteral("input-keyboard")), tr("Record Key…"),
      this);
  auto* remove = new QPushButton(
      QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove"), this);
  m_showDefaults = new QToolButton(this);
  m_showDefaults->setCheckable(true);
  m_showDefaults->setAutoRaise(true);
  buttons->addWidget(add);
  buttons->addWidget(recordButton);
  buttons->addWidget(remove);
  buttons->addStretch();
  buttons->addWidget(m_showDefaults);
  layout->addLayout(buttons);

  m_defaults = createTable(this);
  m_defaults->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_defaults->hide();
  layout->addWidget(m_defaults);

  connect(add, &QPushButton::clicked, this, [this]() {
    addRow(QString(), QString());
    m_table->setCurrentCell(m_table->rowCount() - 1, 0);
    record();
  });
  connect(recordButton, &QPushButton::clicked, this, &KeybindEditor::record);
  connect(remove, &QPushButton::clicked, this, [this]() {
    if (m_table->currentRow() >= 0) {
      m_table->removeRow(m_table->currentRow());
      emit changed();
    }
  });
  connect(m_showDefaults, &QToolButton::toggled, m_defaults,
          &QWidget::setVisible);
  connect(m_table, &QTableWidget::itemChanged, this, &ValueEditor::changed);
  setReference({});
}

void KeybindEditor::addRow(const QString& trigger, const QString& action) {
  const QSignalBlocker blocker(m_table);
  const int row = m_table->rowCount();
  m_table->insertRow(row);
  m_table->setItem(row, 0, new QTableWidgetItem(trigger));
  m_table->setItem(row, 1, new QTableWidgetItem(action));
}

void KeybindEditor::record() {
  const int row = m_table->currentRow();
  if (row < 0) {
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Record Key"));
  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(tr("Press the key combination."), &dialog));
  auto* hint = new QLabel(tr("Use the trigger field for sequences such as "
                             "ctrl+a>n and for prefixes such as global:."),
                          &dialog);
  hint->setWordWrap(true);
  hint->setForegroundRole(QPalette::PlaceholderText);
  layout->addWidget(hint);
  auto* edit = new QKeySequenceEdit(&dialog);
  edit->setMaximumSequenceLength(1);
  layout->addWidget(edit);
  auto* box = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(box);
  connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  // The edit takes all keys, Return too, so one recorded key finishes.
  connect(edit, &QKeySequenceEdit::editingFinished, &dialog, [&dialog, edit]() {
    if (!edit->keySequence().isEmpty()) {
      dialog.accept();
    }
  });
  edit->setFocus();
  if (dialog.exec() != QDialog::Accepted || edit->keySequence().isEmpty()) {
    return;
  }
  const QString trigger = triggerFromKey(edit->keySequence()[0]);
  if (!trigger.isEmpty()) {
    m_table->item(row, 0)->setText(trigger);
    m_table->setCurrentCell(row, 1);
    m_table->editItem(m_table->item(row, 1));
  }
}

QStringList KeybindEditor::values() const {
  QStringList result;
  for (int row = 0; row < m_table->rowCount(); ++row) {
    const QString trigger = m_table->item(row, 0)->text().trimmed();
    const QString action = m_table->item(row, 1)->text().trimmed();
    if (trigger.isEmpty()) {
      continue;
    }
    // Entries such as "clear" have no action.
    result.append(action.isEmpty() ? trigger : trigger + u'=' + action);
  }
  return result;
}

void KeybindEditor::setValues(const QStringList& values) {
  {
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(0);
  }
  for (const QString& entry : values) {
    const auto [trigger, action] = splitKeybind(entry);
    addRow(trigger, action);
  }
}

void KeybindEditor::setReference(const QStringList& values) {
  m_defaults->setRowCount(0);
  for (const QString& entry : values) {
    const auto [trigger, action] = splitKeybind(entry);
    const int row = m_defaults->rowCount();
    m_defaults->insertRow(row);
    m_defaults->setItem(row, 0, new QTableWidgetItem(trigger));
    m_defaults->setItem(row, 1, new QTableWidgetItem(action));
  }
  m_showDefaults->setText(tr("Default Keybinds (%1)").arg(values.size()));
}

}  // namespace qhostty
