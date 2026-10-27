#include "BackgroundBlur.h"

#include <QGuiApplication>
#include <QHash>
#include <QWindow>

#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>
#include <xcb/xcb.h>

#include "ext-background-effect-v1-client-protocol.h"

#include <cstdlib>
#include <cstring>
#include <limits>

namespace qhostty {
namespace {

struct WaylandBlur {
  wl_display* display = nullptr;
  wl_event_queue* queue = nullptr;
  ext_background_effect_manager_v1* manager = nullptr;
  uint32_t capabilities = 0;

  struct Surface {
    wl_surface* surface = nullptr;
    ext_background_effect_surface_v1* effect = nullptr;
  };
  QHash<QWindow*, Surface> surfaces;
};

void onCapabilities(void* data,
                    ext_background_effect_manager_v1*,
                    uint32_t flags) {
  static_cast<WaylandBlur*>(data)->capabilities = flags;
}

const ext_background_effect_manager_v1_listener managerListener = {
    .capabilities = onCapabilities,
};

void onGlobal(void* data,
              wl_registry* registry,
              uint32_t name,
              const char* interface,
              uint32_t) {
  auto* blur = static_cast<WaylandBlur*>(data);
  if (std::strcmp(interface, ext_background_effect_manager_v1_interface.name) ==
      0) {
    blur->manager = static_cast<ext_background_effect_manager_v1*>(
        wl_registry_bind(registry, name,
                         &ext_background_effect_manager_v1_interface, 1));
    ext_background_effect_manager_v1_add_listener(blur->manager,
                                                  &managerListener, blur);
  }
}

void onGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener registryListener = {
    .global = onGlobal,
    .global_remove = onGlobalRemove,
};

// Binds the protocol on a private event queue, so that Qt's queue does not
// dispatch our events.
WaylandBlur* waylandBlur() {
  static WaylandBlur* blur = []() -> WaylandBlur* {
    auto* wayland =
        qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (wayland == nullptr || wayland->display() == nullptr) {
      return nullptr;
    }

    auto* result = new WaylandBlur;
    result->display = wayland->display();
    result->queue = wl_display_create_queue(result->display);
    auto* wrapper = static_cast<wl_display*>(
        wl_proxy_create_wrapper(result->display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), result->queue);
    wl_registry* registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    wl_registry_add_listener(registry, &registryListener, result);
    // The first roundtrip gets the globals, the second the capabilities.
    wl_display_roundtrip_queue(result->display, result->queue);
    wl_display_roundtrip_queue(result->display, result->queue);
    wl_registry_destroy(registry);
    return result;
  }();
  return blur;
}

wl_surface* waylandSurface(QWindow* window) {
  QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();
  return native == nullptr ? nullptr
                           : static_cast<wl_surface*>(
                                 native->nativeResourceForWindow(
                                     QByteArrayLiteral("surface"), window));
}

bool setWaylandBlur(QWindow* window, bool enabled) {
  WaylandBlur* blur = waylandBlur();
  if (blur == nullptr) {
    return false;
  }
  wl_display_dispatch_queue_pending(blur->display, blur->queue);

  WaylandBlur::Surface& state = blur->surfaces[window];
  wl_surface* surface = waylandSurface(window);
  if (state.surface != surface && state.effect != nullptr) {
    // Qt made a new wl_surface. The old effect object is inert.
    ext_background_effect_surface_v1_destroy(state.effect);
    state.effect = nullptr;
  }
  state.surface = surface;

  const bool supported =
      blur->manager != nullptr &&
      (blur->capabilities & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
  if (surface == nullptr || !supported) {
    return true;
  }

  if (state.effect == nullptr) {
    if (!enabled) {
      return true;
    }
    state.effect = ext_background_effect_manager_v1_get_background_effect(
        blur->manager, surface);
  }

  if (!enabled) {
    ext_background_effect_surface_v1_set_blur_region(state.effect, nullptr);
  } else {
    auto* wayland =
        qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    wl_region* region = wl_compositor_create_region(wayland->compositor());
    // The compositor clips the region to the surface size.
    wl_region_add(region, 0, 0, std::numeric_limits<int32_t>::max(),
                  std::numeric_limits<int32_t>::max());
    ext_background_effect_surface_v1_set_blur_region(state.effect, region);
    wl_region_destroy(region);
  }
  wl_display_flush(blur->display);
  return true;
}
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

}  // namespace

void setBackgroundBlur(QWindow* window, bool enabled) {
  if (window == nullptr || window->handle() == nullptr) {
    return;
  }
  if (!setWaylandBlur(window, enabled)) {
    setX11Blur(window, enabled);
  }
}

void forgetBackgroundBlur(QWindow* window) {
  WaylandBlur* blur = waylandBlur();
  if (blur == nullptr) {
    return;
  }
  const WaylandBlur::Surface state = blur->surfaces.take(window);
  if (state.effect != nullptr) {
    ext_background_effect_surface_v1_destroy(state.effect);
  }
}

}  // namespace qhostty
