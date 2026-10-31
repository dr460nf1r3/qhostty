#pragma once

#include <QObject>

class QSocketNotifier;

// Talks to systemd when Qhostty runs as a Type=notify-reload user service,
// like the GTK app: it reports readiness and reloads, and turns SIGUSR2
// into a reload request.
class SystemdIntegration final : public QObject {
  Q_OBJECT

 public:
  // Reads and removes NOTIFY_SOCKET, so that shells do not inherit it.
  // Call this before Ghostty or Qt start other processes.
  static void takeNotifySocket();

  explicit SystemdIntegration(QObject* parent = nullptr);
  ~SystemdIntegration() override;

  static void notifyReady();
  static void notifyReloading();

 signals:
  void reloadRequested();

 private:
  QSocketNotifier* m_notifier = nullptr;
};
