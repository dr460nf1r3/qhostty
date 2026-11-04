#pragma once

#include <cstdint>

class QKeyEvent;

namespace qhostty {

// Returns the XKB keysym at the base shift level of the key in the active
// keyboard layout, or 0 if it is not known. Qt gives only the shifted key,
// for example '"' for Shift+2 on a German layout.
//
// The keymap comes from the compositor (Wayland) or the X server (X11)
// through public Qt and xkbcommon API.
uint32_t baseKeysym(const QKeyEvent& event);

}  // namespace qhostty
