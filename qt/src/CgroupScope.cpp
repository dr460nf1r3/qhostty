#include "CgroupScope.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QDebug>
#include <QList>
#include <QString>
#include <QVariant>

#include <cstdint>

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
struct UnitProperty {
  QString name;
  QDBusVariant value;
};
using UnitProperties = QList<UnitProperty>;

struct AuxUnit {
  QString name;
  UnitProperties properties;
};
using AuxUnits = QList<AuxUnit>;

QDBusArgument& operator<<(QDBusArgument& argument, const UnitProperty& value) {
  argument.beginStructure();
  argument << value.name << value.value;
  argument.endStructure();
  return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument,
                                UnitProperty& value) {
  argument.beginStructure();
  argument >> value.name >> value.value;
  argument.endStructure();
  return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const AuxUnit& value) {
  argument.beginStructure();
  argument << value.name << value.properties;
  argument.endStructure();
  return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, AuxUnit& value) {
  argument.beginStructure();
  argument >> value.name >> value.properties;
  argument.endStructure();
  return argument;
}
}  // namespace

Q_DECLARE_METATYPE(UnitProperty)
Q_DECLARE_METATYPE(AuxUnit)

namespace {
QDBusMessage startScope(const char* name,
                        uint64_t memoryHigh,
                        uint64_t tasksMax,
                        const UnitProperty& process) {
  UnitProperties properties;
  if (memoryHigh != 0) {
    properties.append({QStringLiteral("MemoryHigh"),
                       QDBusVariant(QVariant::fromValue<quint64>(memoryHigh))});
  }
  if (tasksMax != 0) {
    properties.append({QStringLiteral("TasksMax"),
                       QDBusVariant(QVariant::fromValue<quint64>(tasksMax))});
  }
  // systemd-oomd kills this scope, not the application.
  properties.append({QStringLiteral("ManagedOOMMemoryPressure"),
                     QDBusVariant(QStringLiteral("kill"))});
  properties.append(process);

  QDBusMessage message = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.systemd1"),
      QStringLiteral("/org/freedesktop/systemd1"),
      QStringLiteral("org.freedesktop.systemd1.Manager"),
      QStringLiteral("StartTransientUnit"));
  message << QString::fromUtf8(name) << QStringLiteral("fail")
          << QVariant::fromValue(properties)
          << QVariant::fromValue(AuxUnits{});
  return QDBusConnection::sessionBus().call(message, QDBus::Block, 1000);
}

// Runs on the terminal IO thread of libghostty, after fork.
bool createScope(void*,
                 const char* name,
                 uint32_t pid,
                 uint64_t memoryHigh,
                 uint64_t tasksMax) {
  QDBusMessage reply;
  // A pidfd names the process even if its PID is reused before systemd
  // reads it. systemd 253 and newer accept PIDFDs.
  const int pidfd = static_cast<int>(
      syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0));
  if (pidfd >= 0) {
    const QDBusUnixFileDescriptor descriptor(pidfd);
    close(pidfd);
    reply = startScope(
        name, memoryHigh, tasksMax,
        {QStringLiteral("PIDFDs"),
         QDBusVariant(QVariant::fromValue(
             QList<QDBusUnixFileDescriptor>{descriptor}))});
  }
  if (pidfd < 0 || (reply.type() == QDBusMessage::ErrorMessage &&
                    reply.errorName() ==
                        QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"))) {
    reply = startScope(
        name, memoryHigh, tasksMax,
        {QStringLiteral("PIDs"),
         QDBusVariant(QVariant::fromValue(QList<uint>{pid}))});
  }
  if (reply.type() != QDBusMessage::ReplyMessage) {
    qWarning().noquote() << "Cannot create systemd scope" << name << ":"
                         << reply.errorMessage();
    return false;
  }
  return true;
}
}  // namespace

void installCgroupScopeCallback(ghostty_app_t app) {
  qDBusRegisterMetaType<UnitProperty>();
  qDBusRegisterMetaType<UnitProperties>();
  qDBusRegisterMetaType<AuxUnit>();
  qDBusRegisterMetaType<AuxUnits>();
  qDBusRegisterMetaType<QList<QDBusUnixFileDescriptor>>();
  ghostty_app_set_cgroup_scope_cb(app, &createScope);
}
