#pragma once

#include "settings/editors/ValueEditor.h"

class QTableWidget;
class QToolButton;

namespace qhostty {

// The user's keybinds as a table of triggers and actions. Ghostty adds
// them to the default keybinds, which show below for reference.
class KeybindEditor final : public ValueEditor {
  Q_OBJECT

 public:
  explicit KeybindEditor(QWidget* parent = nullptr);

  [[nodiscard]] QStringList values() const override;
  void setValues(const QStringList& values) override;
  void setReference(const QStringList& values) override;
  [[nodiscard]] bool editsFileLines() const override { return true; }

 private:
  void addRow(const QString& trigger, const QString& action);
  void record();

  QTableWidget* m_table;
  QTableWidget* m_defaults;
  QToolButton* m_showDefaults;
  QStringList m_actions;
};

}  // namespace qhostty
