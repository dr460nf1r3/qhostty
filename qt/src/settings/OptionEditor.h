#pragma once

#include "settings/ConfigSchema.h"

#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QToolButton;

namespace qhostty {

// One row of the settings window: the name, an editor that fits the kind
// of the option, a button for the documentation, and a reset button.
class OptionEditor final : public QWidget {
  Q_OBJECT

 public:
  OptionEditor(const ConfigOption& option, QWidget* parent = nullptr);

  [[nodiscard]] const ConfigOption& option() const { return m_option; }
  [[nodiscard]] QString label() const;

  // Values in config file syntax: one entry, or one per line for
  // repeatable options. Empty means unset.
  [[nodiscard]] QStringList values() const;
  void setValues(const QStringList& values);
  void setDefaults(const QStringList& defaults);
  [[nodiscard]] bool isDefault() const;

  // Shows an error from validation, or clears it with an empty string.
  void setError(const QString& error);
  [[nodiscard]] bool matches(const QString& text) const;

 signals:
  void edited();
  void resetRequested();

 private:
  QWidget* createEditor();
  void updateState();

  ConfigOption m_option;
  QStringList m_defaults;
  QLabel* m_name;
  QLabel* m_docs;
  QLabel* m_error;
  QToolButton* m_reset;
  QWidget* m_editor = nullptr;
  std::function<QStringList()> m_get;
  std::function<void(const QStringList&)> m_set;
  bool m_updating = false;
};

}  // namespace qhostty
