#include "KeyboardLayout.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

namespace qhostty {
namespace {

class Layout final : public QObject {
 public:
  // Returns null without a QGuiApplication on Wayland or X11.
  static Layout* instance() {
    static Layout* layout = []() -> Layout* {
      if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) ==
          nullptr) {
        return nullptr;
      }
      auto* result = new Layout();
      if (!result->init()) {
        delete result;
        return nullptr;
      }
      result->setParent(qGuiApp);
      return result;
    }();
    return layout;
  }

  ~Layout() override {
    if (m_keyboard != nullptr) {
      wl_keyboard_destroy(m_keyboard);
    }
    xkb_keymap_unref(m_keymap);
    xkb_context_unref(m_context);
  }

  uint32_t keysym(uint32_t keycode, uint32_t nativeModifiers) {
    if (m_x11 != nullptr) {
      if (m_x11Stale) {
        loadX11Keymap();
      }
      // Bits 13 and 14 of the X11 key state are the XKB group.
      m_group = (nativeModifiers >> 13) & 0x3;
    }
    if (m_keymap == nullptr) {
      return 0;
    }

    const xkb_layout_index_t layouts =
        xkb_keymap_num_layouts_for_key(m_keymap, keycode);
    if (layouts == 0) {
      return 0;
    }
    const xkb_keysym_t* syms = nullptr;
    const int count = xkb_keymap_key_get_syms_by_level(
        m_keymap, keycode, m_group % layouts, 0, &syms);
    return count > 0 ? syms[0] : 0;
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyboardLayoutChange) {
      m_x11Stale = true;
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  bool init() {
    m_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (m_context == nullptr) {
      return false;
    }

    if (auto* wayland =
            qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        wayland != nullptr) {
      // A second keyboard object of Qt's seat gets the same keymap and
      // layout events as Qt.
      if (wayland->seat() == nullptr || wayland->keyboard() == nullptr) {
        return false;
      }
      m_keyboard = wl_seat_get_keyboard(wayland->seat());
      wl_keyboard_add_listener(m_keyboard, &keyboardListener, this);
      wl_display_roundtrip(wayland->display());
      return true;
    }

    if (auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        x11 != nullptr && x11->connection() != nullptr) {
      if (xkb_x11_setup_xkb_extension(
              x11->connection(), XKB_X11_MIN_MAJOR_XKB_VERSION,
              XKB_X11_MIN_MINOR_XKB_VERSION,
              XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, nullptr, nullptr,
              nullptr, nullptr) == 0) {
        return false;
      }
      m_x11 = x11->connection();
      // Qt sends KeyboardLayoutChange when the X11 keymap changes.
      qGuiApp->installEventFilter(this);
      return true;
    }
    return false;
  }

  void loadX11Keymap() {
    m_x11Stale = false;
    const int32_t device = xkb_x11_get_core_keyboard_device_id(m_x11);
    if (device < 0) {
      return;
    }
    if (xkb_keymap* keymap = xkb_x11_keymap_new_from_device(
            m_context, m_x11, device, XKB_KEYMAP_COMPILE_NO_FLAGS)) {
      xkb_keymap_unref(m_keymap);
      m_keymap = keymap;
    }
  }

  static void onKeymap(void* data,
                       wl_keyboard*,
                       uint32_t format,
                       int32_t fd,
                       uint32_t size) {
    auto* self = static_cast<Layout*>(data);
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size > 0) {
      void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
      if (map != MAP_FAILED) {
        // The keymap string ends with a null byte.
        if (xkb_keymap* keymap = xkb_keymap_new_from_buffer(
                self->m_context, static_cast<const char*>(map), size - 1,
                XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS)) {
          xkb_keymap_unref(self->m_keymap);
          self->m_keymap = keymap;
        }
        munmap(map, size);
      }
    }
    close(fd);
  }

  static void onModifiers(void* data,
                          wl_keyboard*,
                          uint32_t,
                          uint32_t,
                          uint32_t,
                          uint32_t,
                          uint32_t group) {
    static_cast<Layout*>(data)->m_group = group;
  }

  static void onEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
  static void onLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}
  static void onKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t,
                    uint32_t) {}
  static void onRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

  static constexpr wl_keyboard_listener keyboardListener = {
      .keymap = onKeymap,
      .enter = onEnter,
      .leave = onLeave,
      .key = onKey,
      .modifiers = onModifiers,
      .repeat_info = onRepeatInfo,
  };

  xkb_context* m_context = nullptr;
  xkb_keymap* m_keymap = nullptr;
  xkb_layout_index_t m_group = 0;
  wl_keyboard* m_keyboard = nullptr;
  xcb_connection_t* m_x11 = nullptr;
  bool m_x11Stale = true;
};

}  // namespace

uint32_t baseKeysym(const QKeyEvent& event) {
  Layout* layout = Layout::instance();
  if (layout == nullptr || event.nativeScanCode() == 0) {
    return 0;
  }
  return layout->keysym(event.nativeScanCode(), event.nativeModifiers());
}

}  // namespace qhostty
