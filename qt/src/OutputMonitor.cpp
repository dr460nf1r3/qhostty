#include "OutputMonitor.h"

#include "TerminalText.h"
#include "TerminalWidget.h"

#include <QHashFunctions>
#include <QTimer>

OutputMonitor::OutputMonitor(TerminalWidget* terminal)
    : QObject(terminal), m_terminal(terminal), m_timer(new QTimer(this)) {
  m_timer->setInterval(1000);
  connect(m_timer, &QTimer::timeout, this, &OutputMonitor::poll);
}

void OutputMonitor::setActivityEnabled(bool enabled) {
  m_activity = enabled;
  updateTimer();
}

void OutputMonitor::setSilenceEnabled(bool enabled) {
  m_silence = enabled;
  updateTimer();
}

void OutputMonitor::reset() {
  m_activityAlerted = false;
  m_silenceArmed = false;
}

void OutputMonitor::updateTimer() {
  if (!m_activity && !m_silence) {
    m_timer->stop();
    return;
  }
  if (!m_timer->isActive()) {
    m_hash = qHash(qhostty::viewportText(m_terminal->surface()));
    reset();
    m_timer->start();
  }
}

void OutputMonitor::poll() {
  const size_t hash = qHash(qhostty::viewportText(m_terminal->surface()));
  if (hash != m_hash) {
    m_hash = hash;
    m_lastChange.start();
    m_silenceArmed = true;
    if (m_activity && !m_activityAlerted) {
      m_activityAlerted = true;
      emit activity();
    }
    return;
  }

  if (m_silence && m_silenceArmed && m_lastChange.isValid() &&
      m_lastChange.elapsed() >= qint64(silenceSeconds) * 1000) {
    m_silenceArmed = false;
    emit silence();
  }
}
