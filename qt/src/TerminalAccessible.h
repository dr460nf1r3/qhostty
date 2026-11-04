#pragma once

class QAccessibleInterface;
class QObject;
class QString;

namespace qhostty {

// Gives screen readers the text, cursor and selection of terminal widgets.
// Install with QAccessible::installFactory.
QAccessibleInterface* terminalAccessibleFactory(const QString& className,
                                                QObject* object);

}  // namespace qhostty
