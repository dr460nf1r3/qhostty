#pragma once

class QAction;
class QMenu;
class QString;

namespace qhostty {

// Inserts a "Search for '<text>' with" submenu before the given action,
// through the KDE web shortcuts. Builds without KIO add nothing. Returns
// the submenu, or null.
QMenu* addWebShortcuts(QMenu* menu, QAction* before, const QString& text);

}  // namespace qhostty
