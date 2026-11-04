#pragma once

#include "settings/ConfigValues.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QWidget>

#include <functional>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QVBoxLayout;

namespace qhostty {

class OptionEditor;

// The settings window. The options come from the generated config schema,
// the values from +show-config, and saving edits the user's config file.
class SettingsWindow final : public QWidget {
  Q_OBJECT

 public:
  // reload is called after a save, to reload the configuration.
  explicit SettingsWindow(std::function<void()> reload,
                          QWidget* parent = nullptr);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  struct Category {
    QString id;
    QString title;
    QString icon;
    QStringList prefixes;
  };

  void buildCategories();
  void buildEditors();
  void loadValues();
  void showValues();
  void filter();
  bool apply();
  void updateButtons();
  [[nodiscard]] QString categoryOf(const QString& key) const;

  std::function<void()> m_reload;
  QList<Category> m_categories;
  QStringList m_hidden;
  QHash<QString, OptionEditor*> m_editors;
  QHash<QString, QString> m_editorCategory;
  QSet<QString> m_edited;
  ConfigValues m_defaults;
  ConfigValues m_current;
  int m_pendingLoads = 0;

  QLineEdit* m_search;
  QListWidget* m_categoryList;
  QScrollArea* m_scroll;
  QVBoxLayout* m_rows;
  QLabel* m_status;
  QDialogButtonBox* m_buttons;
};

}  // namespace qhostty
