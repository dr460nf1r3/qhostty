#include "BackgroundBlur.h"

#include <KWindowEffects>
#include <QSet>
#include <QWindow>

// KWindowSystem keeps one blur object per window. Qt styles such as Kvantum
// also ask KWindowSystem for blur, so all requests must go through it. On
// Wayland it uses the ext-background-effect-v1 protocol.
namespace qhostty {
namespace {
QSet<QWindow*>& blurredWindows() {
  static QSet<QWindow*> windows;
  return windows;
}
}  // namespace

void setBackgroundBlur(QWindow* window, bool enabled) {
  if (window == nullptr) {
    return;
  }
  if (enabled) {
    blurredWindows().insert(window);
    KWindowEffects::enableBlurBehind(window, true);
  } else if (blurredWindows().remove(window)) {
    // Only remove a blur that Qhostty set, not the blur of the style.
    KWindowEffects::enableBlurBehind(window, false);
  }
}

void forgetBackgroundBlur(QWindow* window) {
  blurredWindows().remove(window);
}
}  // namespace qhostty
