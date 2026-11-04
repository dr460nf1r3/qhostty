#include "DBusApplication.h"
#include "GhosttyApp.h"
#include "SingleInstance.h"
#include "SystemdIntegration.h"
#include "TerminalAccessible.h"

#include <QAccessible>
#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QOpenGLContext>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTranslator>

#include <cstdlib>
#include <cstring>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#endif

#include <ghostty_ext.h>

namespace {
struct AppIdentity {
  QString appId = QStringLiteral("io.github.dr460nf1r3.qhostty");
  QByteArray instanceName = "qhostty";
};

// The rules of GApplication IDs, which GTK Ghostty uses for class.
bool validAppId(const QString& id) {
  static const QRegularExpression pattern(QStringLiteral(
      "^[A-Za-z_-][A-Za-z0-9_-]*(\\.[A-Za-z_-][A-Za-z0-9_-]*)+$"));
  return id.size() <= 255 && pattern.match(id).hasMatch();
}

// class and x11-instance-name must be known before QApplication starts.
AppIdentity readAppIdentity() {
  AppIdentity identity;
  ghostty_config_t config = ghostty_config_new();
  if (config == nullptr) {
    return identity;
  }
  ghostty_config_load_default_files(config);
  ghostty_config_load_cli_args(config);
  ghostty_config_load_recursive_files(config);
  ghostty_config_finalize(config);

  const char* value = nullptr;
  if (ghostty_config_get(config, static_cast<void*>(&value), "class",
                         std::strlen("class")) &&
      value != nullptr) {
    const QString appId = QString::fromUtf8(value);
    if (validAppId(appId)) {
      identity.appId = appId;
    } else {
      qWarning().noquote() << "Ignoring invalid class" << appId;
    }
  }
  value = nullptr;
  if (ghostty_config_get(config, static_cast<void*>(&value),
                         "x11-instance-name",
                         std::strlen("x11-instance-name")) &&
      value != nullptr && value[0] != '\0') {
    identity.instanceName = value;
  }
  ghostty_config_free(config);
  return identity;
}
}  // namespace

int main(int argc, char** argv) {
  SystemdIntegration::takeNotifySocket();

  // The running instance handles +new-window and +new-tab. Ghostty's own
  // handlers for these actions do not support embedded apps.
  QString openAction;
  if (argc > 1 && (std::strcmp(argv[1], "+new-window") == 0 ||
                   std::strcmp(argv[1], "+new-tab") == 0)) {
    openAction = QString::fromLatin1(argv[1]);
    for (int index = 1; index < argc; ++index) {
      argv[index] = argv[index + 1];
    }
    --argc;
  }

  if (ghostty_init(static_cast<uintptr_t>(argc), argv) != 0) {
    return EXIT_FAILURE;
  }
  ghostty_cli_try_action();

  const AppIdentity identity = readAppIdentity();
  // Qt uses this for the instance name of WM_CLASS on X11.
  qputenv("RESOURCE_NAME", identity.instanceName);

  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

  QSurfaceFormat format;
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(4, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  // Windows need an alpha channel for background-opacity.
  format.setAlphaBufferSize(8);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication application(argc, argv);
  QAccessible::installFactory(qhostty::terminalAccessibleFactory);
  QTranslator qtTranslator;
  if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"),
                        QStringLiteral("_"),
                        QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
    QCoreApplication::installTranslator(&qtTranslator);
  }
  QTranslator translator;
  if (translator.load(QLocale(), QStringLiteral("qhostty"),
                      QStringLiteral("_"), QStringLiteral(":/i18n"))) {
    QCoreApplication::installTranslator(&translator);
  }
  // The application name is the class of WM_CLASS on X11, and the desktop
  // file name is the Wayland app ID and the D-Bus name.
  QCoreApplication::setApplicationName(identity.appId);
  QGuiApplication::setApplicationDisplayName(QStringLiteral("Qhostty"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  QCoreApplication::setOrganizationName(QStringLiteral("dr460nf1r3"));
  QCoreApplication::setOrganizationDomain(
      QStringLiteral("dr460nf1r3.github.io"));
  QApplication::setDesktopFileName(identity.appId);
  QIcon bundledIcon;
  for (const int size : {16, 32, 64, 128, 256, 512}) {
    bundledIcon.addFile(QStringLiteral(":/icons/ghostty-%1.png").arg(size),
                        QSize(size, size));
  }
  QApplication::setWindowIcon(QIcon::fromTheme(
      QStringLiteral("io.github.dr460nf1r3.qhostty"), bundledIcon));

  QString runtimeDirectory =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtimeDirectory.isEmpty()) {
    runtimeDirectory = QDir::tempPath();
  }
  // Each class runs its own instance, as in GTK Ghostty.
  QByteArray endpointIdentity = identity.appId.toUtf8() + ".v1";
#if defined(Q_OS_LINUX)
  endpointIdentity += ':' + QByteArray::number(geteuid());
#endif
  endpointIdentity += ':' + qgetenv("XDG_SESSION_ID");
  endpointIdentity += ':' + qgetenv("WAYLAND_DISPLAY");
  endpointIdentity += ':' + qgetenv("DISPLAY");
  const QByteArray endpointHash =
      QCryptographicHash::hash(endpointIdentity, QCryptographicHash::Sha256)
          .toHex()
          .left(24);
  SingleInstance instance(QDir(runtimeDirectory)
                              .filePath(QStringLiteral("qhostty-") +
                                        QString::fromLatin1(endpointHash)));
  QStringList arguments = application.arguments();
  if (!openAction.isEmpty()) {
    arguments.insert(1, openAction);
  }
  const SingleInstance::StartResult instanceResult =
      instance.start(arguments, QDir::currentPath());
  if (instanceResult == SingleInstance::StartResult::Forwarded) {
    return EXIT_SUCCESS;
  }
  if (instanceResult == SingleInstance::StartResult::Failed) {
    qCritical().noquote() << "Unable to start Qhostty:"
                          << instance.errorString();
    return EXIT_FAILURE;
  }

  GhosttyApp ghostty;
  if (!ghostty.initialize()) {
    return EXIT_FAILURE;
  }

  QObject::connect(&instance, &SingleInstance::activationRequested, &ghostty,
                   &GhosttyApp::activate);

  SystemdIntegration systemd;
  QObject::connect(&systemd, &SystemdIntegration::reloadRequested, &ghostty,
                   [&ghostty]() {
                     SystemdIntegration::notifyReloading();
                     ghostty.reloadConfig();
                     SystemdIntegration::notifyReady();
                   });

  auto* dbus = new DBusApplication(&ghostty);
  if (!dbus->registerOnSessionBus(QGuiApplication::desktopFileName())) {
    qInfo("D-Bus activation is unavailable");
  }

  // The systemd service starts without a window and waits for activation.
  bool initialWindow = true;
  ghostty_config_get(ghostty.config(), &initialWindow, "initial-window",
                     std::strlen("initial-window"));
  if (initialWindow) {
    ghostty.createWindow();
  }
  SystemdIntegration::notifyReady();

  return application.exec();
}
