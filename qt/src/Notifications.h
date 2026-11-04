#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class QDBusPendingCallWatcher;
class TerminalWidget;

// Desktop notifications through org.freedesktop.Notifications. A click on a
// notification shows the terminal that sent it, and focusing that terminal
// closes the notification.
class Notifications final : public QObject {
  Q_OBJECT

 public:
  explicit Notifications(QObject* parent = nullptr);

  // source can be null for notifications that no terminal sent.
  void send(TerminalWidget* source, const QString& title, const QString& body);
  void dismiss(TerminalWidget* source);

 private slots:
  void onActionInvoked(uint id, const QString& action);
  void onActivationToken(uint id, const QString& token);
  void onClosed(uint id, uint reason);

 private:
  void onSent(QDBusPendingCallWatcher* watcher, QPointer<TerminalWidget> source);

  QHash<uint, QPointer<TerminalWidget>> m_sources;
  QHash<uint, QString> m_tokens;
  QElapsedTimer m_lastSent;
  bool m_actions = false;
  bool m_markup = false;
};
