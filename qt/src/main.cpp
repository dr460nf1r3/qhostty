#include "GhosttyApp.h"
#include "SingleInstance.h"

#include <QApplication>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QOpenGLContext>
#include <QStandardPaths>
#include <QSurfaceFormat>

#include <cstdlib>
#include <cstring>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#endif

#include <ghostty.h>

int main(int argc, char** argv) {
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

  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

  QSurfaceFormat format;
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(4, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  format.setColorSpace(QColorSpace::SRgb);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("qhostty"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("Qhostty"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  QCoreApplication::setOrganizationName(QStringLiteral("angristan"));
  QCoreApplication::setOrganizationDomain(
      QStringLiteral("angristan.github.io"));
  QApplication::setDesktopFileName(
      QStringLiteral("io.github.angristan.qhostty"));
  QIcon bundledIcon;
  for (const int size : {16, 32, 64, 128, 256, 512}) {
    bundledIcon.addFile(QStringLiteral(":/icons/ghostty-%1.png").arg(size),
                        QSize(size, size));
  }
  QApplication::setWindowIcon(QIcon::fromTheme(
      QStringLiteral("io.github.angristan.qhostty"), bundledIcon));

  QString runtimeDirectory =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtimeDirectory.isEmpty()) {
    runtimeDirectory = QDir::tempPath();
  }
  QByteArray endpointIdentity("io.github.angristan.qhostty.v1");
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
  ghostty.createWindow();

  return application.exec();
}
