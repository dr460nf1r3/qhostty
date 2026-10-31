#include "GlobalShortcuts.h"

#include "GhosttyApp.h"

#include <QAction>
#include <QCryptographicHash>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDebug>
#include <QKeySequence>
#include <QRandomGenerator>

#include <xkbcommon/xkbcommon.h>

#ifdef QHOSTTY_HAS_GLOBAL_ACCEL
#include <KGlobalAccel>
#endif

namespace {
constexpr auto portalService = "org.freedesktop.portal.Desktop";
constexpr auto portalPath = "/org/freedesktop/portal/desktop";
constexpr auto portalInterface = "org.freedesktop.portal.GlobalShortcuts";

// One entry of BindShortcuts: (sa{sv}).
struct PortalShortcut {
  QString id;
  QVariantMap properties;
};

QDBusArgument& operator<<(QDBusArgument& argument,
                          const PortalShortcut& shortcut) {
  argument.beginStructure();
  argument << shortcut.id << shortcut.properties;
  argument.endStructure();
  return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument,
                                PortalShortcut& shortcut) {
  argument.beginStructure();
  argument >> shortcut.id >> shortcut.properties;
  argument.endStructure();
  return argument;
}

QString randomToken() {
  return QStringLiteral("qhostty_%1")
      .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
}

xkb_keysym_t keysymForKey(int key) {
  if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
    return XKB_KEY_F1 + static_cast<xkb_keysym_t>(key - Qt::Key_F1);
  }
  switch (key) {
    case Qt::Key_Backspace:
      return XKB_KEY_BackSpace;
    case Qt::Key_Return:
      return XKB_KEY_Return;
    case Qt::Key_Tab:
      return XKB_KEY_Tab;
    case Qt::Key_Delete:
      return XKB_KEY_Delete;
    case Qt::Key_End:
      return XKB_KEY_End;
    case Qt::Key_Home:
      return XKB_KEY_Home;
    case Qt::Key_Insert:
      return XKB_KEY_Insert;
    case Qt::Key_PageDown:
      return XKB_KEY_Page_Down;
    case Qt::Key_PageUp:
      return XKB_KEY_Page_Up;
    case Qt::Key_Down:
      return XKB_KEY_Down;
    case Qt::Key_Left:
      return XKB_KEY_Left;
    case Qt::Key_Right:
      return XKB_KEY_Right;
    case Qt::Key_Up:
      return XKB_KEY_Up;
    case Qt::Key_Escape:
      return XKB_KEY_Escape;
    case Qt::Key_Print:
      return XKB_KEY_Print;
    case Qt::Key_ScrollLock:
      return XKB_KEY_Scroll_Lock;
    case Qt::Key_Pause:
      return XKB_KEY_Pause;
    default:
      break;
  }
  if (key >= 0x20 && key < 0x01000000) {
    return xkb_utf32_to_keysym(QChar::toLower(static_cast<char32_t>(key)));
  }
  return XKB_KEY_NoSymbol;
}
}  // namespace

Q_DECLARE_METATYPE(PortalShortcut)

namespace {
Qt::Key physicalKey(ghostty_input_key_e key) {
  if (key >= GHOSTTY_KEY_DIGIT_0 && key <= GHOSTTY_KEY_DIGIT_9) {
    return static_cast<Qt::Key>(static_cast<int>(Qt::Key_0) +
                                static_cast<int>(key) -
                                static_cast<int>(GHOSTTY_KEY_DIGIT_0));
  }
  if (key >= GHOSTTY_KEY_A && key <= GHOSTTY_KEY_Z) {
    return static_cast<Qt::Key>(static_cast<int>(Qt::Key_A) +
                                static_cast<int>(key) -
                                static_cast<int>(GHOSTTY_KEY_A));
  }
  if (key >= GHOSTTY_KEY_F1 && key <= GHOSTTY_KEY_F25) {
    return static_cast<Qt::Key>(static_cast<int>(Qt::Key_F1) +
                                static_cast<int>(key) -
                                static_cast<int>(GHOSTTY_KEY_F1));
  }

  switch (key) {
    case GHOSTTY_KEY_BACKQUOTE:
      return Qt::Key_QuoteLeft;
    case GHOSTTY_KEY_BACKSLASH:
      return Qt::Key_Backslash;
    case GHOSTTY_KEY_BRACKET_LEFT:
      return Qt::Key_BracketLeft;
    case GHOSTTY_KEY_BRACKET_RIGHT:
      return Qt::Key_BracketRight;
    case GHOSTTY_KEY_COMMA:
      return Qt::Key_Comma;
    case GHOSTTY_KEY_EQUAL:
      return Qt::Key_Equal;
    case GHOSTTY_KEY_MINUS:
      return Qt::Key_Minus;
    case GHOSTTY_KEY_PERIOD:
      return Qt::Key_Period;
    case GHOSTTY_KEY_QUOTE:
      return Qt::Key_Apostrophe;
    case GHOSTTY_KEY_SEMICOLON:
      return Qt::Key_Semicolon;
    case GHOSTTY_KEY_SLASH:
      return Qt::Key_Slash;
    case GHOSTTY_KEY_BACKSPACE:
      return Qt::Key_Backspace;
    case GHOSTTY_KEY_ENTER:
      return Qt::Key_Return;
    case GHOSTTY_KEY_SPACE:
      return Qt::Key_Space;
    case GHOSTTY_KEY_TAB:
      return Qt::Key_Tab;
    case GHOSTTY_KEY_DELETE:
      return Qt::Key_Delete;
    case GHOSTTY_KEY_END:
      return Qt::Key_End;
    case GHOSTTY_KEY_HOME:
      return Qt::Key_Home;
    case GHOSTTY_KEY_INSERT:
      return Qt::Key_Insert;
    case GHOSTTY_KEY_PAGE_DOWN:
      return Qt::Key_PageDown;
    case GHOSTTY_KEY_PAGE_UP:
      return Qt::Key_PageUp;
    case GHOSTTY_KEY_ARROW_DOWN:
      return Qt::Key_Down;
    case GHOSTTY_KEY_ARROW_LEFT:
      return Qt::Key_Left;
    case GHOSTTY_KEY_ARROW_RIGHT:
      return Qt::Key_Right;
    case GHOSTTY_KEY_ARROW_UP:
      return Qt::Key_Up;
    case GHOSTTY_KEY_ESCAPE:
      return Qt::Key_Escape;
    case GHOSTTY_KEY_PRINT_SCREEN:
      return Qt::Key_Print;
    case GHOSTTY_KEY_SCROLL_LOCK:
      return Qt::Key_ScrollLock;
    case GHOSTTY_KEY_PAUSE:
      return Qt::Key_Pause;
    default:
      return Qt::Key_unknown;
  }
}
}  // namespace

QKeySequence GlobalShortcuts::sequenceForTrigger(
    ghostty_input_trigger_s trigger) {
  Qt::KeyboardModifiers modifiers;
  if (trigger.mods & GHOSTTY_MODS_SHIFT) {
    modifiers |= Qt::ShiftModifier;
  }
  if (trigger.mods & GHOSTTY_MODS_CTRL) {
    modifiers |= Qt::ControlModifier;
  }
  if (trigger.mods & GHOSTTY_MODS_ALT) {
    modifiers |= Qt::AltModifier;
  }
  if (trigger.mods & GHOSTTY_MODS_SUPER) {
    modifiers |= Qt::MetaModifier;
  }

  Qt::Key key = Qt::Key_unknown;
  if (trigger.tag == GHOSTTY_TRIGGER_PHYSICAL) {
    key = physicalKey(trigger.key.physical);
  } else if (trigger.tag == GHOSTTY_TRIGGER_UNICODE &&
             trigger.key.unicode >= 0x20 && trigger.key.unicode <= 0x00ffffff) {
    uint32_t codepoint = trigger.key.unicode;
    if (codepoint >= 'a' && codepoint <= 'z') {
      codepoint = codepoint - 'a' + 'A';
    }
    key = static_cast<Qt::Key>(codepoint);
  }
  return key == Qt::Key_unknown ? QKeySequence()
                                : QKeySequence(QKeyCombination(modifiers, key));
}

QString GlobalShortcuts::portalTrigger(ghostty_input_trigger_s trigger) {
  const QKeySequence sequence = sequenceForTrigger(trigger);
  if (sequence.isEmpty()) {
    return {};
  }
  const xkb_keysym_t keysym = keysymForKey(sequence[0].key());
  char name[64];
  if (keysym == XKB_KEY_NoSymbol ||
      xkb_keysym_get_name(keysym, name, sizeof(name)) <= 0) {
    return {};
  }

  // Same order and names as GTK Ghostty.
  QString result;
  if (trigger.mods & GHOSTTY_MODS_SHIFT) {
    result += QStringLiteral("SHIFT+");
  }
  if (trigger.mods & GHOSTTY_MODS_CTRL) {
    result += QStringLiteral("CTRL+");
  }
  if (trigger.mods & GHOSTTY_MODS_ALT) {
    result += QStringLiteral("ALT+");
  }
  if (trigger.mods & GHOSTTY_MODS_SUPER) {
    result += QStringLiteral("LOGO+");
  }
  return result + QString::fromLatin1(name);
}

GlobalShortcuts::GlobalShortcuts(GhosttyApp* app, QObject* parent)
    : QObject(parent), m_app(app) {
  qDBusRegisterMetaType<PortalShortcut>();
  qDBusRegisterMetaType<QList<PortalShortcut>>();
}

GlobalShortcuts::~GlobalShortcuts() {
  clear();
}

void GlobalShortcuts::refresh(ghostty_config_t config) {
  clear();
  if (config == nullptr ||
      qEnvironmentVariableIntValue("QHOSTTY_DISABLE_GLOBAL_SHORTCUTS") != 0) {
    return;
  }
  ghostty_config_enumerate_global_keybinds(config, this,
                                           &GlobalShortcuts::enumerate);
  if (!m_portalActions.isEmpty()) {
    startPortalSession();
  }
}

bool GlobalShortcuts::supported() const {
  return true;
}

// KGlobalAccel only works where the KDE daemon runs. Everywhere else, and
// in builds without KDE Frameworks, use the XDG portal like GTK Ghostty.
bool GlobalShortcuts::useGlobalAccel() {
#ifdef QHOSTTY_HAS_GLOBAL_ACCEL
  const QDBusConnectionInterface* bus =
      QDBusConnection::sessionBus().interface();
  return bus != nullptr &&
         bus->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel"));
#else
  return false;
#endif
}

QString GlobalShortcuts::portalRequest(const char* slot) {
  // Subscribe to the Response signal before the call, so that it cannot
  // arrive first. The request path is defined by the portal.
  QDBusConnection bus = QDBusConnection::sessionBus();
  const QString token = randomToken();
  QString sender = bus.baseService().mid(1);
  sender.replace(QLatin1Char('.'), QLatin1Char('_'));
  const QString path =
      QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
          .arg(sender, token);
  bus.connect(QString::fromLatin1(portalService), path,
              QStringLiteral("org.freedesktop.portal.Request"),
              QStringLiteral("Response"), this, slot);
  return token;
}

void GlobalShortcuts::startPortalSession() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    qWarning() << "Global shortcuts need D-Bus";
    return;
  }
  bus.connect(QString::fromLatin1(portalService),
              QString::fromLatin1(portalPath),
              QString::fromLatin1(portalInterface), QStringLiteral("Activated"),
              this,
              SLOT(onActivated(QDBusObjectPath, QString, qulonglong,
                               QVariantMap)));

  const QString token = portalRequest(SLOT(onSessionCreated(uint, QVariantMap)));
  QDBusMessage message = QDBusMessage::createMethodCall(
      QString::fromLatin1(portalService), QString::fromLatin1(portalPath),
      QString::fromLatin1(portalInterface), QStringLiteral("CreateSession"));
  message << QVariantMap{
      {QStringLiteral("handle_token"), token},
      {QStringLiteral("session_handle_token"), randomToken()},
  };
  bus.asyncCall(message);
}

void GlobalShortcuts::onSessionCreated(uint response,
                                       const QVariantMap& results) {
  if (response != 0) {
    qWarning() << "The desktop portal did not create a shortcut session";
    return;
  }
  m_portalSession = results.value(QStringLiteral("session_handle")).toString();
  if (m_portalSession.isEmpty()) {
    return;
  }

  QList<PortalShortcut> shortcuts;
  for (auto it = m_portalActions.cbegin(); it != m_portalActions.cend(); ++it) {
    shortcuts.append({it.key(),
                      {{QStringLiteral("description"), it.value()},
                       {QStringLiteral("preferred_trigger"), it.key()}}});
  }

  const QString token = portalRequest(SLOT(onShortcutsBound(uint, QVariantMap)));
  QDBusMessage message = QDBusMessage::createMethodCall(
      QString::fromLatin1(portalService), QString::fromLatin1(portalPath),
      QString::fromLatin1(portalInterface), QStringLiteral("BindShortcuts"));
  message << QVariant::fromValue(QDBusObjectPath(m_portalSession))
          << QVariant::fromValue(shortcuts) << QString()
          << QVariantMap{{QStringLiteral("handle_token"), token}};
  QDBusConnection::sessionBus().asyncCall(message);
}

void GlobalShortcuts::onShortcutsBound(uint response, const QVariantMap&) {
  if (response != 0) {
    qWarning() << "The desktop portal did not bind the global shortcuts";
  }
}

void GlobalShortcuts::onActivated(const QDBusObjectPath& session,
                                  const QString& id,
                                  qulonglong,
                                  const QVariantMap&) {
  if (session.path() != m_portalSession) {
    return;
  }
  const QString action = m_portalActions.value(id);
  if (!action.isEmpty()) {
    m_app->performBindingAction(action);
  }
}

bool GlobalShortcuts::enumerate(void* userdata,
                                ghostty_input_trigger_s trigger,
                                const char* action,
                                uintptr_t actionLength) {
  auto* self = static_cast<GlobalShortcuts*>(userdata);
  return self != nullptr &&
         self->add(trigger, QString::fromUtf8(
                                action, static_cast<qsizetype>(actionLength)));
}

bool GlobalShortcuts::add(ghostty_input_trigger_s trigger,
                          const QString& action) {
  const QKeySequence sequence = sequenceForTrigger(trigger);
  if (sequence.isEmpty() || action.isEmpty()) {
    return true;
  }

  if (!useGlobalAccel()) {
    const QString id = portalTrigger(trigger);
    if (!id.isEmpty()) {
      m_portalActions.insert(id, action);
    }
    return true;
  }

#ifdef QHOSTTY_HAS_GLOBAL_ACCEL
  auto* shortcut = new QAction(action, this);
  QByteArray identity = action.toUtf8();
  identity.append(':');
  identity.append(QByteArray::number(static_cast<int>(trigger.tag)));
  identity.append(':');
  identity.append(QByteArray::number(static_cast<int>(trigger.mods)));
  identity.append(':');
  identity.append(
      QByteArray::number(trigger.tag == GHOSTTY_TRIGGER_PHYSICAL
                             ? static_cast<uint32_t>(trigger.key.physical)
                             : trigger.key.unicode));
  const QByteArray digest =
      QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
  shortcut->setObjectName(QStringLiteral("ghostty-") +
                          QString::fromLatin1(digest.toHex().left(20)));
  shortcut->setText(action);
  connect(shortcut, &QAction::triggered, this,
          [this, action]() { m_app->performBindingAction(action); });
  KGlobalAccel* globalAccel = KGlobalAccel::self();
  const QList<QKeySequence> sequences{sequence};
  const bool defaultSet = globalAccel->setDefaultShortcut(
      shortcut, sequences, KGlobalAccel::NoAutoloading);
  const bool activeSet = globalAccel->setShortcut(shortcut, sequences,
                                                  KGlobalAccel::NoAutoloading);
  if (!defaultSet || !activeSet) {
    globalAccel->removeAllShortcuts(shortcut);
    shortcut->deleteLater();
    return true;
  }
  m_actions.append(shortcut);
#else
  Q_UNUSED(trigger);
  Q_UNUSED(action);
#endif
  return true;
}

void GlobalShortcuts::clear() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!m_portalSession.isEmpty()) {
    bus.asyncCall(QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService), m_portalSession,
        QStringLiteral("org.freedesktop.portal.Session"),
        QStringLiteral("Close")));
    m_portalSession.clear();
  }
  if (!m_portalActions.isEmpty()) {
    bus.disconnect(QString::fromLatin1(portalService),
                   QString::fromLatin1(portalPath),
                   QString::fromLatin1(portalInterface),
                   QStringLiteral("Activated"), this,
                   SLOT(onActivated(QDBusObjectPath, QString, qulonglong,
                                    QVariantMap)));
    m_portalActions.clear();
  }
  for (QAction* action : std::as_const(m_actions)) {
#ifdef QHOSTTY_HAS_GLOBAL_ACCEL
    KGlobalAccel::self()->removeAllShortcuts(action);
#endif
    delete action;
  }
  m_actions.clear();
}
