#pragma once

#include <QString>

#include <ghostty_ext.h>

namespace qhostty {

// The text that the terminal shows now, with one line per row.
[[nodiscard]] QString viewportText(ghostty_surface_t surface);

// The selected text, or an empty string.
[[nodiscard]] QString selectionText(ghostty_surface_t surface);

}  // namespace qhostty
