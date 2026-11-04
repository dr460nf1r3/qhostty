#pragma once

#include <QElapsedTimer>
#include <QObject>

class QTimer;
class TerminalWidget;

// Watches a terminal for new output (activity) or for no new output after
// output (silence), as Konsole does. It compares the shown text once per
// second, so cursor blinking is not activity.
class OutputMonitor final : public QObject {
  Q_OBJECT

 public:
  static constexpr int silenceSeconds = 10;

  explicit OutputMonitor(TerminalWidget* terminal);

  void setActivityEnabled(bool enabled);
  void setSilenceEnabled(bool enabled);
  // Call when the user looks at the terminal. The next alerts need new
  // output first.
  void reset();

 signals:
  void activity();
  void silence();

 private:
  void poll();
  void updateTimer();

  TerminalWidget* m_terminal;
  QTimer* m_timer;
  QElapsedTimer m_lastChange;
  size_t m_hash = 0;
  bool m_activity = false;
  bool m_silence = false;
  bool m_activityAlerted = false;
  bool m_silenceArmed = false;
};
