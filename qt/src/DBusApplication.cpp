#include "DBusApplication.h"

#include "GhosttyApp.h"
#include "MainWindow.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QUrl>

#include <ghostty_ext.h>

DBusApplication::DBusApplication(GhosttyApp* app)
    : QDBusAbstractAdaptor(app), m_app(app) {}

bool DBusApplication::registerOnSessionBus(const QString& appId) {
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    return false;
  }

  // The object path is the application ID with dots as slashes.
  QString path = QLatin1Char('/') + appId;
  path.replace(QLatin1Char('.'), QLatin1Char('/'));
  path.replace(QLatin1Char('-'), QLatin1Char('_'));
  if (!bus.registerObject(path, m_app)) {
    return false;
  }
  return bus.registerService(appId);
}

void DBusApplication::useActivationToken(const QVariantMap& platformData) {
  // Qt reads these when it activates the next window, so the compositor
  // gives the new window focus.
  const QString token =
      platformData.value(QStringLiteral("activation-token")).toString();
  if (!token.isEmpty()) {
    qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
  }
  const QString startupId =
      platformData.value(QStringLiteral("desktop-startup-id")).toString();
  if (!startupId.isEmpty()) {
    qputenv("DESKTOP_STARTUP_ID", startupId.toUtf8());
  }
}

void DBusApplication::Activate(const QVariantMap& platformData) {
  useActivationToken(platformData);
  MainWindow* window = m_app->createWindow();
  window->raise();
  window->activateWindow();
}

void DBusApplication::Open(const QStringList& uris,
                           const QVariantMap& platformData) {
  useActivationToken(platformData);
  for (const QString& uri : uris) {
    const QUrl url(uri);
    if (!url.isLocalFile()) {
      continue;
    }
    // Open a window in the directory, or in the directory of the file.
    const QFileInfo info(url.toLocalFile());
    const QByteArray directory =
        (info.isDir() ? info.absoluteFilePath() : info.absolutePath())
            .toUtf8();
    ghostty_surface_config_s config = ghostty_surface_config_new();
    config.context = GHOSTTY_SURFACE_CONTEXT_WINDOW;
    config.working_directory = directory.constData();
    MainWindow* window = m_app->createWindow(&config);
    window->raise();
    window->activateWindow();
  }
}

void DBusApplication::ActivateAction(const QString& name,
                                     const QVariantList&,
                                     const QVariantMap& platformData) {
  if (name == QStringLiteral("new-window")) {
    Activate(platformData);
  }
}
