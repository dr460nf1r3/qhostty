#pragma once

#include <QDBusObjectPath>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QVariantMap>

#include <ghostty.h>

class QAction;
class GhosttyApp;

class GlobalShortcuts final : public QObject {
  Q_OBJECT

 public:
  explicit GlobalShortcuts(GhosttyApp* app, QObject* parent = nullptr);
  ~GlobalShortcuts() override;

  void refresh(ghostty_config_t config);
  [[nodiscard]] bool supported() const;
  [[nodiscard]] int count() const { return static_cast<int>(m_actions.size()); }
  [[nodiscard]] static QKeySequence sequenceForTrigger(
      ghostty_input_trigger_s trigger);
  // The trigger in the format of the XDG GlobalShortcuts portal, for
  // example "SHIFT+CTRL+g". Empty if the key has no keysym.
  [[nodiscard]] static QString portalTrigger(ghostty_input_trigger_s trigger);

 private slots:
  void onSessionCreated(uint response, const QVariantMap& results);
  void onShortcutsBound(uint response, const QVariantMap& results);
  void onActivated(const QDBusObjectPath& session,
                   const QString& id,
                   qulonglong timestamp,
                   const QVariantMap& options);

 private:
  static bool enumerate(void* userdata,
                        ghostty_input_trigger_s trigger,
                        const char* action,
                        uintptr_t actionLength);
  bool add(ghostty_input_trigger_s trigger, const QString& action);
  void clear();
  [[nodiscard]] static bool useGlobalAccel();
  void startPortalSession();
  QString portalRequest(const char* slot);

  GhosttyApp* m_app;
  QList<QAction*> m_actions;
  // Portal shortcut ID (the trigger) to action.
  QHash<QString, QString> m_portalActions;
  QString m_portalSession;
};
