#pragma once

#include "settings/editors/ValueEditor.h"

#include <QColor>
#include <QMap>

class QPushButton;

namespace qhostty {

// The 16 basic palette colors. Only the colors the user sets go to the
// config file; the others show the current color, for example the theme's.
class PaletteEditor final : public ValueEditor {
  Q_OBJECT

 public:
  explicit PaletteEditor(QWidget* parent = nullptr);

  [[nodiscard]] QStringList values() const override;
  void setValues(const QStringList& values) override;
  void setReference(const QStringList& values) override;
  [[nodiscard]] bool editsFileLines() const override { return true; }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void updateButtons();

  QList<QPushButton*> m_buttons;
  // Palette index to color text, as in "palette = 1=#ff0000".
  QMap<int, QString> m_user;
  QMap<int, QString> m_reference;
};

}  // namespace qhostty
