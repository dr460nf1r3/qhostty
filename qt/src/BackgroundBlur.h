#pragma once

class QWindow;

// Asks the compositor to blur the background behind a window. Wayland uses
// the ext-background-effect-v1 protocol. X11 uses the KDE blur property.
// Without compositor support, this does nothing.
namespace qhostty {
void setBackgroundBlur(QWindow* window, bool enabled);
void forgetBackgroundBlur(QWindow* window);
}  // namespace qhostty
