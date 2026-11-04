#pragma once

#include "settings/editors/ValueEditor.h"

#include <QHash>

class QCheckBox;
class QComboBox;

namespace qhostty {

class ThemePreview;

// Picks a theme, or one theme for light and one for dark mode
// ("light:A,dark:B"). The list comes from +list-themes.
class ThemeEditor final : public ValueEditor {
  Q_OBJECT

 public:
  explicit ThemeEditor(QWidget* parent = nullptr);

  [[nodiscard]] QStringList values() const override;
  void setValues(const QStringList& values) override;

 private:
  void loadThemes();
  void updatePreviews();

  QComboBox* m_light;
  QComboBox* m_dark;
  QCheckBox* m_split;
  ThemePreview* m_lightPreview;
  ThemePreview* m_darkPreview;
  QHash<QString, QString> m_paths;
};

}  // namespace qhostty
