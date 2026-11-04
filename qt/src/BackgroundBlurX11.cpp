#include "BackgroundBlur.h"

#include <QGuiApplication>
#include <QWindow>

#include <xcb/xcb.h>

#include <cstdlib>

namespace qhostty {

namespace detail {
bool setX11Blur(QWindow* window, bool enabled) {
  auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
  if (x11 == nullptr || x11->connection() == nullptr) {
    return false;
  }

  xcb_connection_t* connection = x11->connection();
  static constexpr char name[] = "_KDE_NET_WM_BLUR_BEHIND_REGION";
  xcb_intern_atom_reply_t* atom = xcb_intern_atom_reply(
      connection,
      xcb_intern_atom(connection, false, sizeof(name) - 1, name), nullptr);
  if (atom == nullptr) {
    return true;
  }

  const auto id = static_cast<xcb_window_t>(window->winId());
  if (enabled) {
    // An empty region blurs the whole window.
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, id, atom->atom,
                        XCB_ATOM_CARDINAL, 32, 0, nullptr);
  } else {
    xcb_delete_property(connection, id, atom->atom);
  }
  xcb_flush(connection);
  free(atom);
  return true;
}

}  // namespace detail

#if !defined(QHOSTTY_GENERIC_WAYLAND_BLUR)
// Without the generic Wayland blur, only X11 windows get blur.
void setBackgroundBlur(QWindow* window, bool enabled) {
  if (window != nullptr && window->handle() != nullptr) {
    detail::setX11Blur(window, enabled);
  }
}

void forgetBackgroundBlur(QWindow*) {}
#endif

}  // namespace qhostty
