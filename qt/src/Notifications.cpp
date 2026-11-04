#include "Notifications.h"

#include "TerminalWidget.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QStringList>
#include <QVariantMap>

namespace {
const QString service = QStringLiteral("org.freedesktop.Notifications");
const QString path = QStringLiteral("/org/freedesktop/Notifications");

constexpr qint64 minimumIntervalMs = 2000;
constexpr qsizetype maximumTitleLength = 256;
constexpr qsizetype maximumBodyLength = 8192;

QDBusMessage call(const QString& method) {
  return QDBusMessage::createMethodCall(service, path, service, method);
}
}  // namespace

Notifications::Notifications(QObject* parent) : QObject(parent) {
  QDBusConnection bus = QDBusConnection::sessionBus();
  bus.connect(service, path, service, QStringLiteral("ActionInvoked"), this,
              SLOT(onActionInvoked(uint, QString)));
  bus.connect(service, path, service, QStringLiteral("ActivationToken"), this,
              SLOT(onActivationToken(uint, QString)));
  bus.connect(service, path, service, QStringLiteral("NotificationClosed"),
              this, SLOT(onClosed(uint, uint)));

  auto* watcher = new QDBusPendingCallWatcher(
      bus.asyncCall(call(QStringLiteral("GetCapabilities"))), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this](QDBusPendingCallWatcher* finished) {
            const QDBusPendingReply<QStringList> reply = *finished;
            if (reply.isValid()) {
              m_actions = reply.value().contains(QStringLiteral("actions"));
              m_markup =
                  reply.value().contains(QStringLiteral("body-markup"));
            }
            finished->deleteLater();
          });
}

void Notifications::send(TerminalWidget* source,
                         const QString& title,
                         const QString& body) {
  // A program in the terminal decides when and what to notify. Limit how
  // often and how much, and do not let it inject markup.
  if (m_lastSent.isValid() && m_lastSent.elapsed() < minimumIntervalMs) {
    return;
  }
  m_lastSent.start();

  QString text = body.left(maximumBodyLength);
  if (m_markup) {
    text = text.toHtmlEscaped();
  }

  QStringList actions;
  if (m_actions && source != nullptr) {
    actions << QStringLiteral("default") << tr("Show Terminal");
  }
  QVariantMap hints;
  hints.insert(QStringLiteral("desktop-entry"),
               QGuiApplication::desktopFileName());

  QDBusMessage message = call(QStringLiteral("Notify"));
  message << QGuiApplication::applicationDisplayName() << uint(0)
          << QGuiApplication::desktopFileName()
          << title.left(maximumTitleLength) << text << actions << hints
          << int(-1);
  auto* watcher = new QDBusPendingCallWatcher(
      QDBusConnection::sessionBus().asyncCall(message), this);
  const QPointer<TerminalWidget> owner = source;
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, owner](QDBusPendingCallWatcher* finished) {
            onSent(finished, owner);
          });
}

void Notifications::onSent(QDBusPendingCallWatcher* watcher,
                           QPointer<TerminalWidget> source) {
  const QDBusPendingReply<uint> reply = *watcher;
  watcher->deleteLater();
  if (reply.isValid() && source != nullptr) {
    m_sources.insert(reply.value(), source);
  }
}

void Notifications::dismiss(TerminalWidget* source) {
  for (auto it = m_sources.begin(); it != m_sources.end();) {
    if (it.value() == source || it.value() == nullptr) {
      if (it.value() == source) {
        QDBusMessage message = call(QStringLiteral("CloseNotification"));
        message << it.key();
        QDBusConnection::sessionBus().asyncCall(message);
      }
      m_tokens.remove(it.key());
      it = m_sources.erase(it);
    } else {
      ++it;
    }
  }
}

void Notifications::onActivationToken(uint id, const QString& token) {
  if (m_sources.contains(id)) {
    m_tokens.insert(id, token);
  }
}

void Notifications::onActionInvoked(uint id, const QString& action) {
  const QPointer<TerminalWidget> source = m_sources.value(id);
  const QString token = m_tokens.take(id);
  if (action != QStringLiteral("default") || source == nullptr) {
    return;
  }
  // Qt uses the token when it activates the window, so the compositor lets
  // the window take focus.
  if (!token.isEmpty()) {
    qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
  }
  source->present();
}

void Notifications::onClosed(uint id, uint) {
  m_sources.remove(id);
  m_tokens.remove(id);
}
