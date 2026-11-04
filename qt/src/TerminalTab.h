#pragma once

#include <QWidget>

#include <ghostty_ext.h>

class GhosttyApp;
class QBoxLayout;
class QSplitter;
class TerminalWidget;

class TerminalTab final : public QWidget {
  Q_OBJECT

 public:
  explicit TerminalTab(GhosttyApp* app,
                       QWidget* parent = nullptr,
                       const ghostty_surface_config_s* baseConfig = nullptr);

  [[nodiscard]] TerminalWidget* activeTerminal() const { return m_active; }
  [[nodiscard]] QList<TerminalWidget*> terminals() const;
  [[nodiscard]] QString title() const;
  void setTitleOverride(const QString& title);
  bool present(TerminalWidget* terminal);

  TerminalWidget* newSplit(TerminalWidget* source,
                           ghostty_action_split_direction_e direction);
  bool closeSurface(TerminalWidget* surface);
  bool moveTerminal(TerminalWidget* terminal,
                    TerminalWidget* target,
                    ghostty_action_split_direction_e direction);
  static TerminalTab* tabOf(const QWidget* widget);
  bool focusSplit(ghostty_action_goto_split_e direction);
  bool resizeSplit(const ghostty_action_resize_split_s& resize);
  void equalizeSplits();
  void toggleZoom();
  bool canClose() const;
  void applyConfig(ghostty_config_t config);

 signals:
  void titleChanged(const QString& title);
  void closeRequested(TerminalTab* tab);
  void configChanged(ghostty_config_t config);
  void emptied(TerminalTab* tab);

 private:
  void connectTerminal(TerminalWidget* terminal);
  void placeTerminal(TerminalWidget* terminal,
                     TerminalWidget* target,
                     ghostty_action_split_direction_e direction);
  void releaseTerminal(TerminalWidget* terminal);
  void startDrag(TerminalWidget* terminal);
  void setActive(TerminalWidget* terminal);
  void collapseSplitter(QSplitter* splitter);
  void equalizeSplitter(QSplitter* splitter);
  void updateZoomVisibility();
  void updateSplitState();

  GhosttyApp* m_app;
  QBoxLayout* m_layout;
  TerminalWidget* m_active = nullptr;
  QString m_titleOverride;
  bool m_zoomed = false;
};
