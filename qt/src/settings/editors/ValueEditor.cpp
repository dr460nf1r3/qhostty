#include "settings/editors/ValueEditor.h"

#include "settings/ConfigSchema.h"
#include "settings/editors/FontFamilyEditor.h"
#include "settings/editors/KeybindEditor.h"
#include "settings/editors/PaletteEditor.h"
#include "settings/editors/ThemeEditor.h"

namespace qhostty {

ValueEditor* createValueEditor(const ConfigOption& option, QWidget* parent) {
  if (option.kind == QStringLiteral("theme")) {
    return new ThemeEditor(parent);
  }
  if (option.kind == QStringLiteral("keybind")) {
    return new KeybindEditor(parent);
  }
  if (option.kind == QStringLiteral("palette")) {
    return new PaletteEditor(parent);
  }
  if (option.key.startsWith(QStringLiteral("font-family"))) {
    return new FontFamilyEditor(parent);
  }
  return nullptr;
}

}  // namespace qhostty
