#include "SystemdIntegration.h"

#include <QByteArray>
#include <QSocketNotifier>
#include <QtGlobal>

#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
QByteArray& notifySocket() {
  static QByteArray path;
  return path;
}

int signalPipe[2] = {-1, -1};

void onSignal(int) {
  const char byte = 1;
  [[maybe_unused]] const ssize_t written = write(signalPipe[1], &byte, 1);
}

void notify(const QByteArray& message) {
  const QByteArray& path = notifySocket();
  if (path.isEmpty()) {
    return;
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (static_cast<size_t>(path.size()) >= sizeof(address.sun_path)) {
    qWarning("NOTIFY_SOCKET path is too long");
    return;
  }
  std::memcpy(address.sun_path, path.constData(),
              static_cast<size_t>(path.size()));
  // A leading '@' names an abstract socket.
  if (address.sun_path[0] == '@') {
    address.sun_path[0] = '\0';
  }

  const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return;
  }
  const auto length = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + static_cast<size_t>(path.size()));
  sendto(fd, message.constData(), static_cast<size_t>(message.size()),
         MSG_NOSIGNAL, reinterpret_cast<const sockaddr*>(&address), length);
  close(fd);
}
}  // namespace

void SystemdIntegration::takeNotifySocket() {
  notifySocket() = qgetenv("NOTIFY_SOCKET");
  qunsetenv("NOTIFY_SOCKET");
}

SystemdIntegration::SystemdIntegration(QObject* parent) : QObject(parent) {
  if (pipe2(signalPipe, O_CLOEXEC | O_NONBLOCK) != 0) {
    return;
  }
  m_notifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, this);
  connect(m_notifier, &QSocketNotifier::activated, this, [this]() {
    char buffer[16];
    while (read(signalPipe[0], buffer, sizeof(buffer)) > 0) {
    }
    emit reloadRequested();
  });

  struct sigaction action{};
  action.sa_handler = &onSignal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  sigaction(SIGUSR2, &action, nullptr);
}

SystemdIntegration::~SystemdIntegration() {
  signal(SIGUSR2, SIG_DFL);
  if (signalPipe[0] >= 0) {
    close(signalPipe[0]);
    close(signalPipe[1]);
    signalPipe[0] = signalPipe[1] = -1;
  }
}

void SystemdIntegration::notifyReady() {
  notify("READY=1");
}

void SystemdIntegration::notifyReloading() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  const qint64 microseconds =
      static_cast<qint64>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
  notify("RELOADING=1\nMONOTONIC_USEC=" + QByteArray::number(microseconds));
}
