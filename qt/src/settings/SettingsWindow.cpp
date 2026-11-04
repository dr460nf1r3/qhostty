#include "settings/SettingsWindow.h"

#include "settings/ConfigFile.h"
#include "settings/ConfigSchema.h"
#include "settings/OptionEditor.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTemporaryFile>
#include <QVBoxLayout>

#include <ghostty_ext.h>

#include <algorithm>

namespace qhostty {
namespace {

// The titles in hints.json, for translation.
[[maybe_unused]] constexpr const char* categoryTitles[] = {
    QT_TRANSLATE_NOOP("SettingsCategory", "Fonts"),
    QT_TRANSLATE_NOOP("SettingsCategory", "Colors and Theme"),
    QT_TRANSLATE_NOOP("SettingsCategory", "Cursor"),
    QT_TRANSLATE_NOOP("SettingsCategory", "Window"),
    QT_TRANSLATE_NOOP("SettingsCategory", "Keyboard and Mouse"),
    QT_TRANSLATE_NOOP("SettingsCategory", "Clipboard"),
    QT_TRANSLATE_NOOP("SettingsCategory", "Shell and Commands"),
    QT_TRANSLATE_NOOP("SettingsCategory", "Bell and Notifications"),
    QT_TRANSLATE_NOOP("SettingsCategory", "Quick Terminal"),
    QT_TRANSLATE_NOOP("SettingsCategory", "System"),
    QT_TRANSLATE_NOOP("SettingsCategory", "Other"),
};

const QString otherCategory = QStringLiteral("other");

QString configPath() {
  const ghostty_string_s path = ghostty_config_open_path();
  QString result = path.ptr == nullptr
                       ? QString()
                       : QString::fromUtf8(path.ptr, qsizetype(path.len));
  ghostty_string_free(path);
  return result;
}

struct Diagnostic {
  QString key;
  QString message;
};

// Loads the text with libghostty and returns its errors. The file goes next
// to the real one, so relative includes resolve the same way.
QList<Diagnostic> validate(const QString& text, const QString& realPath) {
  QTemporaryFile file(QFileInfo(realPath).dir().filePath(
      QStringLiteral(".qhostty-check-XXXXXX")));
  if (!file.open()) {
    return {};
  }
  file.write(text.toUtf8());
  file.flush();

  QList<Diagnostic> result;
  ghostty_config_t config = ghostty_config_new();
  if (config == nullptr) {
    return {};
  }
  const QByteArray path = file.fileName().toUtf8();
  ghostty_config_load_file(config, path.constData());
  ghostty_config_finalize(config);
  // Messages look like "path:line:key: message".
  const QString prefix = file.fileName() + u':';
  for (uint32_t index = 0; index < ghostty_config_diagnostics_count(config);
       ++index) {
    const QString message = QString::fromUtf8(
        ghostty_config_get_diagnostic(config, index).message);
    if (!message.startsWith(prefix)) {
      continue;
    }
    const QStringList parts = message.mid(prefix.size()).split(u':');
    if (parts.size() >= 3) {
      result.append({parts.at(1).trimmed(), parts.mid(2).join(u':').trimmed()});
    } else {
      result.append({QString(), message.mid(prefix.size())});
    }
  }
  ghostty_config_free(config);
  return result;
}

}  // namespace

SettingsWindow::SettingsWindow(std::function<void()> reload, QWidget* parent)
    : QWidget(parent, Qt::Window), m_reload(std::move(reload)) {
  setWindowTitle(tr("Settings"));
  setObjectName(QStringLiteral("qhostty-settings"));
  setAttribute(Qt::WA_DeleteOnClose);
  resize(980, 700);

  auto* layout = new QVBoxLayout(this);
  m_search = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Search settings"));
  m_search->setClearButtonEnabled(true);
  layout->addWidget(m_search);

  auto* splitter = new QSplitter(this);
  m_categoryList = new QListWidget(splitter);
  m_categoryList->setMaximumWidth(260);
  m_scroll = new QScrollArea(splitter);
  m_scroll->setWidgetResizable(true);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scroll->setFrameShape(QFrame::NoFrame);
  auto* page = new QWidget(m_scroll);
  m_rows = new QVBoxLayout(page);
  m_rows->setContentsMargins(12, 0, 12, 0);
  m_scroll->setWidget(page);
  splitter->setStretchFactor(1, 1);
  layout->addWidget(splitter, 1);

  auto* bottom = new QHBoxLayout();
  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  bottom->addWidget(m_status, 1);
  m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                       QDialogButtonBox::Apply |
                                       QDialogButtonBox::Cancel,
                                   this);
  bottom->addWidget(m_buttons);
  layout->addLayout(bottom);

  connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
          this, &SettingsWindow::apply);
  connect(m_buttons, &QDialogButtonBox::accepted, this, [this]() {
    if (apply()) {
      close();
    }
  });
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QWidget::close);
  connect(m_search, &QLineEdit::textChanged, this, &SettingsWindow::filter);
  connect(m_categoryList, &QListWidget::currentRowChanged, this,
          &SettingsWindow::filter);

  buildCategories();
  buildEditors();
  updateButtons();
  loadValues();
}

void SettingsWindow::buildCategories() {
  QFile file(QStringLiteral(":/settings/hints.json"));
  if (file.open(QIODevice::ReadOnly)) {
    const QJsonObject hints = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue& prefix :
         hints.value(QStringLiteral("hidden")).toArray()) {
      m_hidden.append(prefix.toString());
    }
    for (const QJsonValue& value :
         hints.value(QStringLiteral("categories")).toArray()) {
      const QJsonObject object = value.toObject();
      Category category{object.value(QStringLiteral("id")).toString(),
                        object.value(QStringLiteral("title")).toString(),
                        object.value(QStringLiteral("icon")).toString(),
                        {}};
      for (const QJsonValue& prefix :
           object.value(QStringLiteral("prefixes")).toArray()) {
        category.prefixes.append(prefix.toString());
      }
      m_categories.append(category);
    }
  }
  m_categories.append({otherCategory, QStringLiteral("Other"),
                       QStringLiteral("preferences-other"), {}});

  for (const Category& category : m_categories) {
    auto* item = new QListWidgetItem(
        QIcon::fromTheme(category.icon),
        QCoreApplication::translate("SettingsCategory",
                                    category.title.toUtf8().constData()),
        m_categoryList);
    item->setData(Qt::UserRole, category.id);
  }
}

QString SettingsWindow::categoryOf(const QString& key) const {
  // The longest matching prefix wins, so "cursor-color" can go to colors
  // while the other "cursor-" options go to the cursor page.
  QString result = otherCategory;
  qsizetype longest = 0;
  for (const Category& category : m_categories) {
    for (const QString& prefix : category.prefixes) {
      if (key.startsWith(prefix) && prefix.size() > longest) {
        result = category.id;
        longest = prefix.size();
      }
    }
  }
  return result;
}

void SettingsWindow::buildEditors() {
  for (const ConfigOption& option : loadConfigSchema()) {
    if (std::any_of(m_hidden.cbegin(), m_hidden.cend(),
                    [&option](const QString& prefix) {
                      return option.key.startsWith(prefix);
                    })) {
      continue;
    }
    auto* editor = new OptionEditor(option, m_scroll->widget());
    editor->setEnabled(false);
    m_rows->addWidget(editor);
    m_editors.insert(option.key, editor);
    m_editorCategory.insert(option.key, categoryOf(option.key));
    const QString key = option.key;
    connect(editor, &OptionEditor::edited, this, [this, key]() {
      m_edited.insert(key);
      updateButtons();
    });
    connect(editor, &OptionEditor::resetRequested, this, [this, key]() {
      m_edited.insert(key);
      updateButtons();
    });
  }
  m_rows->addStretch();
  m_categoryList->setCurrentRow(0);
}

void SettingsWindow::loadValues() {
  m_status->setText(tr("Loading the configuration…"));
  m_pendingLoads = 2;
  loadConfigValues(true, this, [this](ConfigValues values) {
    m_defaults = std::move(values);
    if (--m_pendingLoads == 0) {
      showValues();
    }
  });
  loadConfigValues(false, this, [this](ConfigValues values) {
    m_current = std::move(values);
    if (--m_pendingLoads == 0) {
      showValues();
    }
  });
}

void SettingsWindow::showValues() {
  if (m_defaults.isEmpty()) {
    m_status->setText(tr("Cannot read the configuration."));
    return;
  }
  QFile source(configPath());
  const ConfigFile file(source.open(QIODevice::ReadOnly)
                            ? QString::fromUtf8(source.readAll())
                            : QString());
  for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
    OptionEditor* editor = it.value();
    if (editor->editsFileLines()) {
      // Keybinds and palette colors add to the defaults, so the editor
      // shows the user's own lines: no lines is the default.
      editor->setDefaults({});
      editor->setReference(editor->option().kind == QStringLiteral("keybind")
                               ? m_defaults.value(it.key())
                               : m_current.value(it.key()));
      editor->setValues(file.values(it.key()));
    } else {
      editor->setDefaults(m_defaults.value(it.key()));
      editor->setValues(m_current.value(it.key(), m_defaults.value(it.key())));
    }
    editor->setEnabled(true);
  }
  m_edited.clear();
  m_status->clear();
  updateButtons();
}

void SettingsWindow::filter() {
  const QString text = m_search->text().trimmed();
  const QListWidgetItem* item = m_categoryList->currentItem();
  const QString category =
      item == nullptr ? QString() : item->data(Qt::UserRole).toString();
  // A search looks in all categories.
  m_categoryList->setEnabled(text.isEmpty());
  for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
    const bool visible = text.isEmpty()
                             ? m_editorCategory.value(it.key()) == category
                             : it.value()->matches(text);
    it.value()->setVisible(visible);
  }
  m_scroll->verticalScrollBar()->setValue(0);
}

void SettingsWindow::updateButtons() {
  const bool changed = !m_edited.isEmpty();
  m_buttons->button(QDialogButtonBox::Apply)->setEnabled(changed);
  setWindowModified(changed);
}

bool SettingsWindow::apply() {
  if (m_edited.isEmpty()) {
    return true;
  }
  const QString path = configPath();
  if (path.isEmpty()) {
    m_status->setText(tr("Cannot find the configuration file."));
    return false;
  }

  QFile current(path);
  QString original;
  if (current.open(QIODevice::ReadOnly)) {
    original = QString::fromUtf8(current.readAll());
    current.close();
  }
  ConfigFile file(original);
  for (const QString& key : std::as_const(m_edited)) {
    const OptionEditor* editor = m_editors.value(key);
    // The default value needs no line: removing it keeps the file small
    // and lets future default changes apply.
    if (editor->isDefault()) {
      file.remove(key);
    } else {
      file.setValues(key, editor->values());
    }
  }

  for (OptionEditor* editor : std::as_const(m_editors)) {
    editor->setError(QString());
  }
  const QList<Diagnostic> errors = validate(file.text(), path);
  if (!errors.isEmpty()) {
    QStringList general;
    for (const Diagnostic& error : errors) {
      if (OptionEditor* editor = m_editors.value(error.key)) {
        editor->setError(error.message);
        editor->setVisible(true);
      } else {
        general.append(error.message);
      }
    }
    m_status->setText(
        tr("Not saved: some settings are not valid.") +
        (general.isEmpty() ? QString() : u' ' + general.join(u' ')));
    return false;
  }

  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(file.text().toUtf8()) < 0 || !output.commit()) {
    m_status->setText(tr("Cannot write %1: %2").arg(path, output.errorString()));
    return false;
  }

  m_edited.clear();
  updateButtons();
  m_status->setText(tr("Saved to %1.").arg(path));
  if (m_reload) {
    m_reload();
  }
  return true;
}

void SettingsWindow::closeEvent(QCloseEvent* event) {
  if (!m_edited.isEmpty()) {
    const auto answer = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("Save the changed settings before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel ||
        (answer == QMessageBox::Save && !apply())) {
      event->ignore();
      return;
    }
  }
  event->accept();
}

}  // namespace qhostty
