#pragma once

#include <QStringList>
#include <QWidget>

namespace qhostty {

struct ConfigOption;

// A custom editor for options that need more than a text field.
class ValueEditor : public QWidget {
  Q_OBJECT

 public:
  using QWidget::QWidget;

  [[nodiscard]] virtual QStringList values() const = 0;
  virtual void setValues(const QStringList& values) = 0;
  // Values to show next to the edited ones: the defaults for keybinds,
  // the current colors for the palette.
  virtual void setReference(const QStringList&) {}
  // True if the editor edits only the lines of the user's config file, not
  // the effective values. Ghostty adds these to the defaults.
  [[nodiscard]] virtual bool editsFileLines() const { return false; }

 signals:
  void changed();
};

// Returns a custom editor for the option, or null for the generic editors.
ValueEditor* createValueEditor(const ConfigOption& option, QWidget* parent);

}  // namespace qhostty
