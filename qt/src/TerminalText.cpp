#include "TerminalText.h"

namespace qhostty {

QString viewportText(ghostty_surface_t surface) {
  if (surface == nullptr) {
    return {};
  }
  const ghostty_selection_s viewport{
      .top_left = {.tag = GHOSTTY_POINT_VIEWPORT,
                   .coord = GHOSTTY_POINT_COORD_TOP_LEFT,
                   .x = 0,
                   .y = 0},
      .bottom_right = {.tag = GHOSTTY_POINT_VIEWPORT,
                       .coord = GHOSTTY_POINT_COORD_BOTTOM_RIGHT,
                       .x = 0,
                       .y = 0},
      .rectangle = false,
  };
  ghostty_text_s text{};
  if (!ghostty_surface_read_text(surface, viewport, &text)) {
    return {};
  }
  QString result = QString::fromUtf8(text.text, qsizetype(text.text_len));
  ghostty_surface_free_text(surface, &text);
  return result;
}

QString selectionText(ghostty_surface_t surface) {
  if (surface == nullptr || !ghostty_surface_has_selection(surface)) {
    return {};
  }
  ghostty_text_s text{};
  if (!ghostty_surface_read_selection(surface, &text)) {
    return {};
  }
  QString result = QString::fromUtf8(text.text, qsizetype(text.text_len));
  ghostty_surface_free_text(surface, &text);
  return result;
}

}  // namespace qhostty
