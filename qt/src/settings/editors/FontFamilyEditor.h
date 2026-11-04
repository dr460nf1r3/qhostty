#pragma once

#include "settings/editors/ValueEditor.h"

class QFontComboBox;
class QVBoxLayout;

namespace qhostty {

// A list of monospace font families: the first is the font, the others are
// fallbacks.
class FontFamilyEditor final : public ValueEditor {
  Q_OBJECT

 public:
  explicit FontFamilyEditor(QWidget* parent = nullptr);

  [[nodiscard]] QStringList values() const override;
  void setValues(const QStringList& values) override;

 private:
  QFontComboBox* addRow(const QString& family);

  QVBoxLayout* m_rows;
  QList<QFontComboBox*> m_combos;
};

}  // namespace qhostty
