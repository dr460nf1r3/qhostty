#pragma once

#include <QDBusAbstractAdaptor>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class GhosttyApp;

// org.freedesktop.Application, which desktops use to launch a
// DBusActivatable application and its desktop actions.
class DBusApplication final : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

 public:
  explicit DBusApplication(GhosttyApp* app);

  // Takes the bus name of the application. Returns false if another
  // process owns it.
  bool registerOnSessionBus(const QString& appId);

 public slots:
  void Activate(const QVariantMap& platformData);
  void Open(const QStringList& uris, const QVariantMap& platformData);
  void ActivateAction(const QString& name,
                      const QVariantList& parameter,
                      const QVariantMap& platformData);

 private:
  void useActivationToken(const QVariantMap& platformData);

  GhosttyApp* m_app;
};
