#pragma once

#include <QMainWindow>
#include <QPointer>

#include <ghostty.h>

#include <optional>

class CommandPalette;
class GhosttyApp;
class QCloseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QLabel;
class QTimer;
class QShowEvent;
class QStackedWidget;
class QTabBar;
class QVBoxLayout;
class TerminalTab;
class TerminalWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  enum class Role {
    Normal,
    QuickTerminal,
  };

  explicit MainWindow(GhosttyApp* app,
                      QWidget* parent = nullptr,
                      const ghostty_surface_config_s* baseConfig = nullptr,
                      bool createInitialTab = true,
                      Role role = Role::Normal);
  ~MainWindow() override;

  int addTab(const ghostty_surface_config_s* baseConfig = nullptr);
  int adoptTab(TerminalTab* tab, const QString& title);
  [[nodiscard]] std::optional<bool> handleAction(
      TerminalWidget* source,
      const ghostty_action_s& action);
  [[nodiscard]] TerminalTab* currentTab() const;
  [[nodiscard]] bool canClose() const;
  [[nodiscard]] bool isQuickTerminal() const {
    return m_role == Role::QuickTerminal;
  }
  void setQuickTerminalAutohide(bool enabled) {
    m_quickTerminalAutohide = enabled;
  }
  void applyConfig(ghostty_config_t config);
  void showToast(const QString& message);
  // Returns the window that holds the tab, or nullptr.
  [[nodiscard]] static MainWindow* ownerOf(const TerminalTab* tab);
  void closeConfirmed();

 protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private:
  enum class TabBarVisibility {
    Always,
    Auto,
    Never,
  };

  TerminalTab* tabFor(TerminalWidget* terminal) const;
  void closeTab(int index);
  void detachTab(int index, std::optional<QPoint> position = std::nullopt);
  TerminalTab* takeTab(int index, QString* title);
  void closeIfEmpty();
  void startTabDrag(int index);
  void connectTab(TerminalTab* tab);
  void updateTabTitle(TerminalTab* tab, const QString& title);
  void updateWindowTitle();
  void applyBackgroundBlur();
  void applyStartupState(ghostty_config_t config);
  bool resizeTerminal(TerminalTab* tab,
                      TerminalWidget* terminal,
                      const ghostty_action_resize_window_s& size);
  void updateTabBarVisibility();
  bool selectTab(int value);
  void toggleCommandPalette(TerminalWidget* source);

  GhosttyApp* m_app;
  QTabBar* m_tabBar;
  QStackedWidget* m_stack;
  Role m_role;
  TabBarVisibility m_tabBarVisibility = TabBarVisibility::Auto;
  QPointer<CommandPalette> m_commandPalette;
  bool m_quickTerminalAutohide = false;
  QString m_titleOverride;
  QPoint m_tabDragStart;
  int m_tabDragIndex = -1;
  QLabel* m_toast = nullptr;
  QTimer* m_toastTimer = nullptr;
  bool m_backgroundBlur = false;
  bool m_newTabAtEnd = false;
  QVBoxLayout* m_layout = nullptr;
};
