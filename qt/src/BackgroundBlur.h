#pragma once

class QWindow;

// Asks the compositor to blur the background behind a window. With KDE
// Frameworks, this uses KWindowEffects. Else X11 uses the KDE blur property,
// and Wayland uses ext-background-effect-v1 only in builds with
// QHOSTTY_GENERIC_WAYLAND_BLUR. Without compositor support, this does
// nothing.
namespace qhostty {
void setBackgroundBlur(QWindow* window, bool enabled);
void forgetBackgroundBlur(QWindow* window);

namespace detail {
// Returns false if the window is not an X11 window.
bool setX11Blur(QWindow* window, bool enabled);
}  // namespace detail
}  // namespace qhostty
