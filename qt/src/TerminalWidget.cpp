#include "TerminalWidget.h"

#include "DragMime.h"
#include "DropText.h"
#include "GhosttyApp.h"
#include "InputCoordinates.h"
#include "InspectorWindow.h"
#include "KeyEventTranslation.h"
#include "MainWindow.h"
#include "Notifications.h"
#include "OutputMonitor.h"
#include "TerminalText.h"
#include "TerminalTab.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFocusEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

TerminalWidget::TerminalWidget(GhosttyApp* app,
                               QWidget* parent,
                               const ghostty_surface_config_s* baseConfig)
    : QOpenGLWidget(parent), m_app(app) {
  setObjectName(QStringLiteral("qhostty-terminal"));
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  setAcceptDrops(true);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

  m_config = baseConfig != nullptr ? *baseConfig : ghostty_surface_config_new();
  m_appliedConfig = ghostty_config_clone(m_app->config());
  ghostty_config_get(m_appliedConfig, &m_focusFollowsMouse,
                     "focus-follows-mouse", std::strlen("focus-follows-mouse"));
  if (m_config.working_directory != nullptr) {
    m_workingDirectory = QString::fromUtf8(m_config.working_directory);
    m_workingDirectoryUtf8 = m_workingDirectory.toUtf8();
    m_config.working_directory = nullptr;
  }
  if (m_config.command != nullptr) {
    m_commandUtf8 = QByteArray(m_config.command);
    m_config.command = nullptr;
  }
  if (m_config.initial_input != nullptr) {
    m_initialInputUtf8 = QByteArray(m_config.initial_input);
    m_config.initial_input = nullptr;
  }

  // Below the other overlays, so it does not dim them.
  m_dimOverlay = new QWidget(this);
  m_dimOverlay->setObjectName(QStringLiteral("qhostty-unfocused-split"));
  m_dimOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  m_dimOverlay->setAttribute(Qt::WA_StyledBackground, true);
  m_dimOverlay->hide();

  m_searchFrame = new QFrame(this);
  m_searchFrame->setObjectName(QStringLiteral("qhostty-search"));
  m_searchFrame->setFrameShape(QFrame::StyledPanel);
  auto* searchLayout = new QHBoxLayout(m_searchFrame);
  searchLayout->setContentsMargins(6, 4, 6, 4);
  searchLayout->setSpacing(4);
  m_searchEdit = new QLineEdit(m_searchFrame);
  m_searchEdit->setPlaceholderText(tr("Search"));
  m_searchEdit->installEventFilter(this);
  m_searchCount = new QLabel(m_searchFrame);
  auto* previous = new QPushButton(tr("Previous"), m_searchFrame);
  auto* next = new QPushButton(tr("Next"), m_searchFrame);
  auto* close = new QPushButton(tr("Close"), m_searchFrame);
  searchLayout->addWidget(m_searchEdit);
  searchLayout->addWidget(m_searchCount);
  searchLayout->addWidget(previous);
  searchLayout->addWidget(next);
  searchLayout->addWidget(close);
  m_searchFrame->hide();

  connect(m_searchEdit, &QLineEdit::textChanged, this,
          [this](const QString& needle) {
            runBindingAction(QStringLiteral("search:") + needle);
          });
  connect(previous, &QPushButton::clicked, this, [this]() {
    runBindingAction(QStringLiteral("navigate_search:previous"));
  });
  connect(next, &QPushButton::clicked, this, [this]() {
    runBindingAction(QStringLiteral("navigate_search:next"));
  });
  connect(close, &QPushButton::clicked, this,
          [this]() { runBindingAction(QStringLiteral("end_search")); });

  m_bellBorder = new QFrame(this);
  m_bellBorder->setObjectName(QStringLiteral("qhostty-bell-border"));
  m_bellBorder->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  m_bellBorder->setStyleSheet(
      QStringLiteral("#qhostty-bell-border { border: 2px solid %1; }")
          .arg(palette().color(QPalette::Highlight).name()));
  m_bellBorder->hide();

  m_resizeOverlay = new QLabel(this);
  m_resizeOverlay->setObjectName(QStringLiteral("qhostty-resize-overlay"));
  m_resizeOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  m_resizeOverlay->setStyleSheet(QStringLiteral(
      "#qhostty-resize-overlay { background: rgba(32, 32, 32, 220); "
      "color: white; border-radius: 6px; padding: 4px 10px; }"));
  m_resizeOverlay->hide();
  m_resizeOverlayTimer = new QTimer(this);
  m_resizeOverlayTimer->setSingleShot(true);
  connect(m_resizeOverlayTimer, &QTimer::timeout, m_resizeOverlay,
          &QWidget::hide);

  m_dropZone = new QWidget(this);
  m_dropZone->setObjectName(QStringLiteral("qhostty-drop-zone"));
  m_dropZone->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  m_dropZone->setAttribute(Qt::WA_StyledBackground, true);
  {
    const QColor highlight = palette().color(QPalette::Highlight);
    m_dropZone->setStyleSheet(
        QStringLiteral("#qhostty-drop-zone { background: rgba(%1, %2, %3, 0.35);"
                       " border: 2px solid %4; }")
            .arg(highlight.red())
            .arg(highlight.green())
            .arg(highlight.blue())
            .arg(highlight.name()));
  }
  m_dropZone->hide();

  // Drag the handle to move this split. drag-handle sets when it shows.
  m_dragHandle = new QLabel(QStringLiteral("\u2022\u2022\u2022"), this);
  m_dragHandle->setObjectName(QStringLiteral("qhostty-drag-handle"));
  m_dragHandle->setAlignment(Qt::AlignCenter);
  m_dragHandle->setCursor(Qt::OpenHandCursor);
  m_dragHandle->setToolTip(tr("Drag to move this split"));
  m_dragHandle->setStyleSheet(QStringLiteral(
      "#qhostty-drag-handle { background: rgba(32, 32, 32, 160); "
      "color: rgba(255, 255, 255, 200); border-bottom-left-radius: 6px; "
      "border-bottom-right-radius: 6px; padding: 0px 8px; }"));
  m_dragHandle->installEventFilter(this);
  m_dragHandle->hide();

  m_statusOverlay = new QLabel(this);
  m_statusOverlay->setObjectName(QStringLiteral("qhostty-status"));
  m_statusOverlay->setFrameShape(QFrame::StyledPanel);
  m_statusOverlay->setAlignment(Qt::AlignCenter);
  m_statusOverlay->setWordWrap(true);
  m_statusOverlay->hide();

  m_readonlyBadge = new QLabel(tr("Read-only"), this);
  m_readonlyBadge->setObjectName(QStringLiteral("qhostty-readonly"));
  m_readonlyBadge->setFrameShape(QFrame::StyledPanel);
  m_readonlyBadge->setContentsMargins(6, 3, 6, 3);
  m_readonlyBadge->hide();

  m_keyStateBadge = new QLabel(this);
  m_keyStateBadge->setObjectName(QStringLiteral("qhostty-key-state"));
  m_keyStateBadge->setFrameShape(QFrame::StyledPanel);
  m_keyStateBadge->setContentsMargins(6, 3, 6, 3);
  m_keyStateBadge->hide();

  m_progressBar = new QProgressBar(this);
  m_progressBar->setObjectName(QStringLiteral("qhostty-progress"));
  m_progressBar->setTextVisible(false);
  m_progressBar->setFixedHeight(4);
  m_progressBar->hide();
  m_progressTimer = new QTimer(this);
  m_progressTimer->setSingleShot(true);
  m_progressTimer->setInterval(15000);
  connect(m_progressTimer, &QTimer::timeout, m_progressBar, &QWidget::hide);

  m_scrollBar = new QScrollBar(Qt::Vertical, this);
  m_scrollBar->setObjectName(QStringLiteral("qhostty-scrollbar"));
  m_scrollBar->hide();
  connect(m_scrollBar, &QScrollBar::valueChanged, this, [this](int value) {
    if (m_scrollMaximum == 0 || m_scrollBar->maximum() == 0) {
      return;
    }
    const uint64_t row = static_cast<uint64_t>(
        std::llround(static_cast<double>(value) / m_scrollBar->maximum() *
                     static_cast<double>(m_scrollMaximum)));
    runBindingAction(QStringLiteral("scroll_to_row:%1").arg(row));
  });

  m_outputMonitor = new OutputMonitor(this);
  connect(m_outputMonitor, &OutputMonitor::activity, this, [this]() {
    alertOutput(tr("Activity"), tr("New output in %1").arg(displayName()));
  });
  connect(m_outputMonitor, &OutputMonitor::silence, this, [this]() {
    alertOutput(tr("Silence"), tr("No output in %1 for %2 seconds")
                                   .arg(displayName())
                                   .arg(OutputMonitor::silenceSeconds));
  });

  // Screen readers get text changes at most every 200 ms.
  m_accessibleTimer = new QTimer(this);
  m_accessibleTimer->setSingleShot(true);
  m_accessibleTimer->setInterval(200);
  connect(m_accessibleTimer, &QTimer::timeout, this,
          &TerminalWidget::updateAccessibleText);

  setupContextMenu();
  m_app->registerSurface(this);
}

void TerminalWidget::setupContextMenu() {
  m_contextMenu = new QMenu(this);
  m_contextMenu->setObjectName(QStringLiteral("qhostty-context-menu"));

  const auto addBindingAction = [this](QMenu* menu, const QString& label,
                                       const QString& binding) {
    QAction* item = menu->addAction(label);
    item->setData(binding);
    connect(item, &QAction::triggered, this,
            [this, item]() { runBindingAction(item->data().toString()); });
    return item;
  };

  addBindingAction(m_contextMenu, tr("Copy"),
                   QStringLiteral("copy_to_clipboard"));
  addBindingAction(m_contextMenu, tr("Paste"),
                   QStringLiteral("paste_from_clipboard"));
  m_notifyNextCommandAction =
      m_contextMenu->addAction(tr("Notify on Next Command Finish"));
  m_notifyNextCommandAction->setObjectName(
      QStringLiteral("qhostty-notify-next-command"));
  m_notifyNextCommandAction->setCheckable(true);
  QAction* monitorActivity = m_contextMenu->addAction(tr("Monitor for Activity"));
  monitorActivity->setCheckable(true);
  connect(monitorActivity, &QAction::toggled, m_outputMonitor,
          &OutputMonitor::setActivityEnabled);
  QAction* monitorSilence = m_contextMenu->addAction(tr("Monitor for Silence"));
  monitorSilence->setCheckable(true);
  connect(monitorSilence, &QAction::toggled, m_outputMonitor,
          &OutputMonitor::setSilenceEnabled);

  m_contextMenu->addSeparator();
  addBindingAction(m_contextMenu, tr("Clear"), QStringLiteral("clear_screen"));
  addBindingAction(m_contextMenu, tr("Reset"), QStringLiteral("reset"));

  m_contextMenu->addSeparator();
  QMenu* splitMenu = m_contextMenu->addMenu(tr("Split"));
  splitMenu->setObjectName(QStringLiteral("qhostty-context-split-menu"));
  addBindingAction(splitMenu, tr("Change Title…"),
                   QStringLiteral("prompt_surface_title"));
  addBindingAction(splitMenu, tr("Split Up"), QStringLiteral("new_split:up"));
  addBindingAction(splitMenu, tr("Split Down"),
                   QStringLiteral("new_split:down"));
  addBindingAction(splitMenu, tr("Split Left"),
                   QStringLiteral("new_split:left"));
  addBindingAction(splitMenu, tr("Split Right"),
                   QStringLiteral("new_split:right"));
  addBindingAction(splitMenu, tr("Close Split"),
                   QStringLiteral("close_surface"));

  QMenu* tabMenu = m_contextMenu->addMenu(tr("Tab"));
  tabMenu->setObjectName(QStringLiteral("qhostty-context-tab-menu"));
  addBindingAction(tabMenu, tr("Change Tab Title…"),
                   QStringLiteral("prompt_tab_title"));
  addBindingAction(tabMenu, tr("New Tab"), QStringLiteral("new_tab"));
  addBindingAction(tabMenu, tr("Close Tab"), QStringLiteral("close_tab"));

  QMenu* windowMenu = m_contextMenu->addMenu(tr("Window"));
  windowMenu->setObjectName(QStringLiteral("qhostty-context-window-menu"));
  addBindingAction(windowMenu, tr("New Window"), QStringLiteral("new_window"));
  addBindingAction(windowMenu, tr("Close Window"),
                   QStringLiteral("close_window"));

  m_contextMenu->addSeparator();
  QMenu* configMenu = m_contextMenu->addMenu(tr("Config"));
  configMenu->setObjectName(QStringLiteral("qhostty-context-config-menu"));
  addBindingAction(configMenu, tr("Open Configuration"),
                   QStringLiteral("open_config"));
  addBindingAction(configMenu, tr("Reload Configuration"),
                   QStringLiteral("reload_config"));

  connect(m_contextMenu, &QMenu::aboutToHide, this,
          [this]() { setFocus(Qt::PopupFocusReason); });
}

TerminalWidget::~TerminalWidget() {
  if (context() != nullptr) {
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
               &TerminalWidget::cleanupContext);
  }
  emit surfaceClosing();
  destroySurface();
  if (m_appliedConfig != nullptr) {
    ghostty_config_free(m_appliedConfig);
    m_appliedConfig = nullptr;
  }
  m_app->unregisterSurface(this);
}

void TerminalWidget::setTitle(const QString& title) {
  m_title = title;
  emit titleChanged(m_title);
}

ghostty_surface_config_s TerminalWidget::inheritedConfig(
    ghostty_surface_context_e context) const {
  if (m_surface != nullptr) {
    return ghostty_surface_inherited_config(m_surface, context);
  }

  ghostty_surface_config_s config = ghostty_surface_config_new();
  config.context = context;
  return config;
}

void TerminalWidget::freeInheritedConfig(
    ghostty_surface_config_s& config) const {
  if (m_surface != nullptr) {
    ghostty_surface_free_inherited_config(m_surface, &config);
  }
}

ghostty_clipboard_read_result_e TerminalWidget::readClipboard(
    ghostty_clipboard_e location,
    void* state,
    const char* const* mimes,
    size_t mimeCount,
    bool list) {
  if (m_surface == nullptr) {
    return GHOSTTY_CLIPBOARD_READ_UNAVAILABLE;
  }

  QClipboard* clipboard = QGuiApplication::clipboard();
  if (location == GHOSTTY_CLIPBOARD_SELECTION &&
      !clipboard->supportsSelection()) {
    return GHOSTTY_CLIPBOARD_READ_UNSUPPORTED;
  }
  const QClipboard::Mode mode = location == GHOSTTY_CLIPBOARD_SELECTION
                                    ? QClipboard::Selection
                                    : QClipboard::Clipboard;
  const QMimeData* mime = clipboard->mimeData(mode);
  if (mime == nullptr) {
    return GHOSTTY_CLIPBOARD_READ_UNAVAILABLE;
  }

  std::vector<QByteArray> types;
  std::vector<QByteArray> data;
  for (size_t index = 0; index < mimeCount; ++index) {
    const QByteArray type(mimes[index]);
    if (type == "text/plain") {
      if (mime->hasText()) {
        types.push_back(type);
        data.push_back(mime->text().toUtf8());
      }
    } else if (mime->hasFormat(QString::fromUtf8(type))) {
      types.push_back(type);
      data.push_back(mime->data(QString::fromUtf8(type)));
    }
  }

  std::vector<QByteArray> formats;
  if (list) {
    for (const QString& format : mime->formats()) {
      formats.push_back(format.toUtf8());
    }
    if (mime->hasText()) {
      formats.emplace_back("text/plain");
    }
  }
  if (data.empty() && formats.empty()) {
    return GHOSTTY_CLIPBOARD_READ_UNAVAILABLE;
  }

  std::vector<ghostty_clipboard_content_s> contents;
  for (size_t index = 0; index < data.size(); ++index) {
    contents.push_back({types[index].constData(), data[index].constData(),
                        static_cast<size_t>(data[index].size())});
  }
  std::vector<const char*> available;
  for (const QByteArray& format : formats) {
    available.push_back(format.constData());
  }

  ghostty_clipboard_complete_s complete{};
  complete.contents = contents.data();
  complete.contents_len = contents.size();
  complete.available = available.data();
  complete.available_len = available.size();
  ghostty_surface_complete_clipboard_request(m_surface, &complete, state);
  return GHOSTTY_CLIPBOARD_READ_STARTED;
}

void TerminalWidget::confirmReadClipboard(
    const ghostty_clipboard_confirm_s* confirm,
    void* state,
    ghostty_clipboard_request_e request) {
  if (m_surface == nullptr) {
    return;
  }

  QString operation;
  switch (request) {
    case GHOSTTY_CLIPBOARD_REQUEST_PASTE:
      operation = tr("Paste clipboard contents into the terminal?");
      break;
    case GHOSTTY_CLIPBOARD_REQUEST_OSC_52_WRITE:
    case GHOSTTY_CLIPBOARD_REQUEST_KITTY_WRITE:
      operation = tr("Allow the terminal to write to the clipboard?");
      break;
    default:
      operation = tr("Allow the terminal to read the clipboard?");
      break;
  }
  if (confirm->name != nullptr) {
    operation = tr("Request from %1").arg(QString::fromUtf8(confirm->name)) +
                QStringLiteral("\n") + operation;
  }

  QString preview;
  for (size_t index = 0; index < confirm->contents_len; ++index) {
    const ghostty_clipboard_content_s& content = confirm->contents[index];
    if (content.mime != nullptr && std::strcmp(content.mime, "text/plain") == 0) {
      preview = QString::fromUtf8(content.data,
                                  static_cast<qsizetype>(content.len));
      break;
    }
  }

  QMessageBox box(QMessageBox::Question, tr("Clipboard confirmation"),
                  operation + QStringLiteral("\n\n") + preview.left(500),
                  QMessageBox::Yes | QMessageBox::No, this);
  box.setDefaultButton(QMessageBox::No);
  if (confirm->can_remember) {
    box.setCheckBox(new QCheckBox(tr("Remember for this session")));
  }

  if (box.exec() != QMessageBox::Yes) {
    ghostty_surface_deny_clipboard_request(m_surface, state);
    return;
  }

  ghostty_clipboard_complete_s complete{};
  complete.contents = confirm->contents;
  complete.contents_len = confirm->contents_len;
  complete.available = confirm->available;
  complete.available_len = confirm->available_len;
  complete.confirmed = true;
  complete.remember = box.checkBox() != nullptr && box.checkBox()->isChecked();
  ghostty_surface_complete_clipboard_request(m_surface, &complete, state);
}

void TerminalWidget::writeClipboard(ghostty_clipboard_e location,
                                    const ghostty_clipboard_content_s* contents,
                                    size_t count,
                                    bool confirm) {
  if (contents == nullptr || count == 0) {
    return;
  }

  QString plainText;
  for (size_t index = 0; index < count; ++index) {
    if (contents[index].mime != nullptr && contents[index].data != nullptr &&
        std::strcmp(contents[index].mime, "text/plain") == 0) {
      plainText =
          QString::fromUtf8(contents[index].data,
                            static_cast<qsizetype>(contents[index].len));
      break;
    }
  }

  if (confirm) {
    const auto answer = QMessageBox::question(
        this, tr("Clipboard confirmation"),
        tr("Allow the terminal to write to the clipboard?\n\n%1")
            .arg(plainText.left(500)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return;
    }
  }

  QClipboard* clipboard = QGuiApplication::clipboard();
  if (location == GHOSTTY_CLIPBOARD_SELECTION &&
      !clipboard->supportsSelection()) {
    return;
  }

  auto* mime = new QMimeData();
  for (size_t index = 0; index < count; ++index) {
    if (contents[index].mime == nullptr || contents[index].data == nullptr) {
      continue;
    }

    const QByteArray type(contents[index].mime);
    const QByteArray data(contents[index].data,
                          static_cast<qsizetype>(contents[index].len));
    if (type == "text/plain") {
      mime->setText(QString::fromUtf8(data));
    } else if (type == "text/html") {
      mime->setHtml(QString::fromUtf8(data));
    } else {
      mime->setData(type, data);
    }
  }

  const QClipboard::Mode mode = location == GHOSTTY_CLIPBOARD_SELECTION
                                    ? QClipboard::Selection
                                    : QClipboard::Clipboard;
  const bool cleared = mime->text().isEmpty() && mime->formats().isEmpty();
  clipboard->setMimeData(mime, mode);

  // app-notifications: bit 0 is clipboard-copy. Only for the standard
  // clipboard, as in GTK.
  uint32_t notifications = 0b11;
  ghostty_config_get(m_appliedConfig, &notifications, "app-notifications",
                     std::strlen("app-notifications"));
  if (mode == QClipboard::Clipboard && (notifications & 0b01) != 0) {
    if (auto* mainWindow = qobject_cast<MainWindow*>(window())) {
      mainWindow->showToast(cleared ? tr("Cleared clipboard")
                                    : tr("Copied to clipboard"));
    }
  }
}

void TerminalWidget::requestClose(bool processAlive) {
  if (processAlive && m_surface != nullptr &&
      ghostty_surface_needs_confirm_quit(m_surface)) {
    const auto answer = QMessageBox::question(
        this, tr("Close terminal?"),
        tr("A process is still running in this terminal."),
        QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Close) {
      return;
    }
  }

  emit closeRequested(this);
}

std::optional<bool> TerminalWidget::handleAction(
    const ghostty_action_s& action) {
  switch (action.tag) {
    case GHOSTTY_ACTION_RENDER:
      update();
      return true;
    case GHOSTTY_ACTION_SET_TITLE:
      setTitle(QString::fromUtf8(action.action.set_title.title));
      return true;
    case GHOSTTY_ACTION_SET_TAB_TITLE:
      emit tabTitleChanged(
          QString::fromUtf8(action.action.set_tab_title.title));
      return true;
    case GHOSTTY_ACTION_PWD:
      m_workingDirectory = QString::fromUtf8(action.action.pwd.pwd);
      m_workingDirectoryUtf8 = m_workingDirectory.toUtf8();
      return true;
    case GHOSTTY_ACTION_CELL_SIZE:
      m_cellSize = QSize(static_cast<int>(action.action.cell_size.width),
                         static_cast<int>(action.action.cell_size.height));
      return true;
    case GHOSTTY_ACTION_SIZE_LIMIT: {
      const qreal scale = std::max<qreal>(1.0, devicePixelRatioF());
      setMinimumSize(qCeil(action.action.size_limit.min_width / scale),
                     qCeil(action.action.size_limit.min_height / scale));
      if (action.action.size_limit.max_width > 0 &&
          action.action.size_limit.max_height > 0) {
        setMaximumSize(qFloor(action.action.size_limit.max_width / scale),
                       qFloor(action.action.size_limit.max_height / scale));
      }
      return true;
    }
    case GHOSTTY_ACTION_INITIAL_SIZE: {
      if (window() != nullptr &&
          m_config.context == GHOSTTY_SURFACE_CONTEXT_WINDOW &&
          !window()->property("qhosttyInitialSizeApplied").toBool()) {
        const QSize size(static_cast<int>(action.action.initial_size.width),
                         static_cast<int>(action.action.initial_size.height));
        window()->setProperty("qhosttyInitialSizeApplied", true);
        window()->setProperty("qhosttyDefaultSize", size);
        window()->resize(size);
      }
      return true;
    }
    case GHOSTTY_ACTION_MOUSE_SHAPE:
      setMouseShape(action.action.mouse_shape);
      return true;
    case GHOSTTY_ACTION_MOUSE_VISIBILITY:
      setMouseVisible(action.action.mouse_visibility == GHOSTTY_MOUSE_VISIBLE);
      return true;
    case GHOSTTY_ACTION_RING_BELL:
      ringBell();
      return true;
    case GHOSTTY_ACTION_OPEN_URL: {
      const auto& url = action.action.open_url;
      return QDesktopServices::openUrl(
          QUrl(QString::fromUtf8(url.url, static_cast<qsizetype>(url.len))));
    }
    case GHOSTTY_ACTION_EXPORT_TERMINAL_IO: {
      const auto& data = action.action.export_terminal_io;
      if (data.contents == nullptr) {
        return false;
      }
      const QString path = QFileDialog::getSaveFileName(
          this, tr("Export terminal data"), QStringLiteral("terminal.txt"));
      if (path.isEmpty()) {
        return true;
      }
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
        return true;
      }
      file.write(data.contents, static_cast<qint64>(data.len));
      if (!file.commit()) {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
      }
      return true;
    }
    case GHOSTTY_ACTION_COPY_TITLE_TO_CLIPBOARD:
      QGuiApplication::clipboard()->setText(m_title);
      return true;
    case GHOSTTY_ACTION_MOUSE_OVER_LINK: {
      const auto& link = action.action.mouse_over_link;
      setToolTip(
          link.url == nullptr
              ? QString()
              : QString::fromUtf8(link.url, static_cast<qsizetype>(link.len)));
      return true;
    }
    case GHOSTTY_ACTION_RENDERER_HEALTH:
      if (action.action.renderer_health == GHOSTTY_RENDERER_HEALTH_HEALTHY) {
        m_statusOverlay->hide();
      } else {
        m_statusOverlay->setText(
            tr("The terminal renderer stopped unexpectedly."));
        m_statusOverlay->show();
        layoutOverlays();
      }
      return true;
    case GHOSTTY_ACTION_SHOW_CHILD_EXITED:
      m_statusOverlay->setText(tr("The shell exited with status %1.")
                                   .arg(action.action.child_exited.exit_code));
      m_statusOverlay->show();
      layoutOverlays();
      return true;
    case GHOSTTY_ACTION_INSPECTOR: {
      const auto mode = action.action.inspector;
      const bool shouldShow =
          mode == GHOSTTY_INSPECTOR_SHOW ||
          (mode == GHOSTTY_INSPECTOR_TOGGLE &&
           (m_inspectorWindow == nullptr || !m_inspectorWindow->isVisible()));
      if (!shouldShow) {
        if (m_inspectorWindow != nullptr) {
          m_inspectorWindow->hide();
        }
        return true;
      }
      if (m_inspectorWindow == nullptr) {
        m_inspectorWindow = new InspectorWindow(this, window());
      }
      m_inspectorWindow->show();
      m_inspectorWindow->raise();
      m_inspectorWindow->activateWindow();
      return true;
    }
    case GHOSTTY_ACTION_RENDER_INSPECTOR:
      if (m_inspectorWindow == nullptr) {
        return false;
      }
      m_inspectorWindow->requestRender();
      return true;
    case GHOSTTY_ACTION_START_SEARCH:
      showSearch(action.action.start_search.needle);
      return true;
    case GHOSTTY_ACTION_END_SEARCH:
      m_searchFrame->hide();
      updateSplitDimming();
      setFocus(Qt::OtherFocusReason);
      return true;
    case GHOSTTY_ACTION_SEARCH_TOTAL:
      m_searchTotal = action.action.search_total.total;
      updateSearchCount();
      return true;
    case GHOSTTY_ACTION_SEARCH_SELECTED:
      m_searchSelected = action.action.search_selected.selected;
      updateSearchCount();
      return true;
    case GHOSTTY_ACTION_SHOW_ON_SCREEN_KEYBOARD:
      QGuiApplication::inputMethod()->show();
      return true;
    case GHOSTTY_ACTION_CONFIG_CHANGE: {
      if (action.action.config_change.config == nullptr) {
        return false;
      }
      ghostty_config_t applied =
          ghostty_config_clone(action.action.config_change.config);
      if (applied == nullptr) {
        return false;
      }
      if (m_appliedConfig != nullptr) {
        ghostty_config_free(m_appliedConfig);
      }
      m_appliedConfig = applied;

      updateSplitDimming();
      updateDragHandle();
      m_focusFollowsMouse = false;
      ghostty_config_get(m_appliedConfig, &m_focusFollowsMouse,
                         "focus-follows-mouse",
                         std::strlen("focus-follows-mouse"));
      bool progressEnabled = true;
      ghostty_config_get(m_appliedConfig, &progressEnabled, "progress-style",
                         std::strlen("progress-style"));
      if (!progressEnabled) {
        m_progressTimer->stop();
        m_progressBar->hide();
      }
      const char* scrollbarMode = nullptr;
      if (ghostty_config_get(m_appliedConfig,
                             static_cast<void*>(&scrollbarMode), "scrollbar",
                             std::strlen("scrollbar")) &&
          scrollbarMode != nullptr && QByteArray(scrollbarMode) == "never") {
        m_scrollMaximum = 0;
        m_scrollBar->hide();
      }
      layoutOverlays();
      emit configChanged(m_appliedConfig);
      return true;
    }
    case GHOSTTY_ACTION_SCROLLBAR:
      updateScrollbar(action.action.scrollbar);
      return true;
    case GHOSTTY_ACTION_PROGRESS_REPORT:
      updateProgress(action.action.progress_report);
      return true;
    case GHOSTTY_ACTION_READONLY:
      m_readonlyBadge->setVisible(action.action.readonly ==
                                  GHOSTTY_READONLY_ON);
      layoutOverlays();
      return true;
    case GHOSTTY_ACTION_KEY_SEQUENCE:
      if (action.action.key_sequence.active) {
        const ghostty_input_trigger_s trigger =
            action.action.key_sequence.trigger;
        QStringList parts;
        if (trigger.mods & GHOSTTY_MODS_CTRL) {
          parts.append(tr("Ctrl"));
        }
        if (trigger.mods & GHOSTTY_MODS_ALT) {
          parts.append(tr("Alt"));
        }
        if (trigger.mods & GHOSTTY_MODS_SHIFT) {
          parts.append(tr("Shift"));
        }
        if (trigger.mods & GHOSTTY_MODS_SUPER) {
          parts.append(tr("Meta"));
        }
        if (trigger.tag == GHOSTTY_TRIGGER_UNICODE) {
          const char32_t codepoint = static_cast<char32_t>(trigger.key.unicode);
          parts.append(QString::fromUcs4(&codepoint, 1));
        } else if (trigger.tag == GHOSTTY_TRIGGER_PHYSICAL) {
          const auto key = trigger.key.physical;
          if (key >= GHOSTTY_KEY_A && key <= GHOSTTY_KEY_Z) {
            parts.append(
                QChar(QLatin1Char('A').unicode() + key - GHOSTTY_KEY_A));
          } else if (key >= GHOSTTY_KEY_DIGIT_0 && key <= GHOSTTY_KEY_DIGIT_9) {
            parts.append(
                QChar(QLatin1Char('0').unicode() + key - GHOSTTY_KEY_DIGIT_0));
          } else {
            parts.append(tr("Key %1").arg(static_cast<int>(key)));
          }
        }
        m_keySequence.append(parts.join(QLatin1Char('+')));
      } else {
        m_keySequence.clear();
      }
      updateKeyState();
      return true;
    case GHOSTTY_ACTION_KEY_TABLE:
      if (action.action.key_table.tag == GHOSTTY_KEY_TABLE_ACTIVATE) {
        const auto& value = action.action.key_table.value.activate;
        m_keyTables.append(
            QString::fromUtf8(value.name, static_cast<qsizetype>(value.len)));
      } else if (action.action.key_table.tag == GHOSTTY_KEY_TABLE_DEACTIVATE) {
        if (!m_keyTables.isEmpty()) {
          m_keyTables.removeLast();
        }
      } else if (action.action.key_table.tag ==
                 GHOSTTY_KEY_TABLE_DEACTIVATE_ALL) {
        m_keyTables.clear();
      }
      updateKeyState();
      return true;
    case GHOSTTY_ACTION_COMMAND_FINISHED:
      commandFinished(action.action.command_finished);
      return true;
    case GHOSTTY_ACTION_SELECTION_CHANGED:
    case GHOSTTY_ACTION_COLOR_CHANGE:
      return true;
    default:
      return std::nullopt;
  }
}

void TerminalWidget::initializeGL() {
  connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
          &TerminalWidget::cleanupContext, Qt::DirectConnection);

  const bool dmabuf = m_presenter.initialize(context());
  m_contextRealized = true;
  const bool recreated = m_surface != nullptr;
  if (m_surface == nullptr) {
    // Ghostty makes its own context current while it creates the surface.
    doneCurrent();
    createSurface();
    makeCurrent();
  }
  if (m_surface != nullptr) {
    ghostty_surface_report_presentation_health(
        m_surface, dmabuf ? GHOSTTY_RENDERER_HEALTH_HEALTHY
                          : GHOSTTY_RENDERER_HEALTH_UNHEALTHY);
    if (recreated) {
      requestFrame();
    }
  }

  syncSurfaceMetrics();
}

void TerminalWidget::paintGL() {
  if (m_surface == nullptr || !m_contextRealized) {
    return;
  }

  ghostty_frame_s frame{};
  if (ghostty_surface_take_frame(m_surface, &frame) &&
      !m_presenter.update(frame)) {
    qWarning("Cannot import a DMABUF frame, using CPU frames");
    ghostty_surface_report_presentation_health(
        m_surface, GHOSTTY_RENDERER_HEALTH_UNHEALTHY);
    requestFrame();
  }

  if (QAccessible::isActive() && hasFocus() && !m_accessibleTimer->isActive()) {
    m_accessibleTimer->start();
  }

  const qreal scale = devicePixelRatioF();
  m_presenter.paint(defaultFramebufferObject(),
                    QSize(qRound(width() * scale), qRound(height() * scale)));
}

void TerminalWidget::resizeGL(int, int) {
  syncSurfaceMetrics();
}

void TerminalWidget::resizeEvent(QResizeEvent* event) {
  QOpenGLWidget::resizeEvent(event);
  layoutOverlays();
  // Show the overlay after Ghostty has the new size.
  QTimer::singleShot(0, this, &TerminalWidget::showResizeOverlay);
}

void TerminalWidget::showResizeOverlay() {
  if (m_surface == nullptr) {
    return;
  }
  const ghostty_surface_size_s size = ghostty_surface_size(m_surface);
  if (size.columns == 0 || size.rows == 0 ||
      (size.columns == m_gridColumns && size.rows == m_gridRows)) {
    return;
  }
  const bool first = m_gridColumns == 0;
  m_gridColumns = size.columns;
  m_gridRows = size.rows;

  const char* mode = nullptr;
  ghostty_config_get(m_appliedConfig, static_cast<void*>(&mode),
                     "resize-overlay", std::strlen("resize-overlay"));
  const QByteArray overlay = mode == nullptr ? "after-first" : mode;
  if (overlay == "never" || (overlay == "after-first" && first)) {
    return;
  }

  m_resizeOverlay->setText(
      QStringLiteral("%1 x %2").arg(size.columns).arg(size.rows));
  m_resizeOverlay->adjustSize();

  const char* position = nullptr;
  ghostty_config_get(m_appliedConfig, static_cast<void*>(&position),
                     "resize-overlay-position",
                     std::strlen("resize-overlay-position"));
  const QByteArray where = position == nullptr ? "center" : position;
  constexpr int margin = 8;
  const int left = margin;
  const int right = width() - m_resizeOverlay->width() - margin;
  const int centerX = (width() - m_resizeOverlay->width()) / 2;
  const int top = margin;
  const int bottom = height() - m_resizeOverlay->height() - margin;
  const int centerY = (height() - m_resizeOverlay->height()) / 2;
  int x = centerX;
  int y = centerY;
  if (where.startsWith("top")) {
    y = top;
  } else if (where.startsWith("bottom")) {
    y = bottom;
  }
  if (where.endsWith("left")) {
    x = left;
  } else if (where.endsWith("right")) {
    x = right;
  }
  m_resizeOverlay->move(x, y);
  m_resizeOverlay->show();
  m_resizeOverlay->raise();

  uint64_t duration = 750000000;
  ghostty_config_get(m_appliedConfig, &duration, "resize-overlay-duration",
                     std::strlen("resize-overlay-duration"));
  m_resizeOverlayTimer->start(static_cast<int>(
      std::min<uint64_t>(duration / 1000000, std::numeric_limits<int>::max())));
}

void TerminalWidget::keyPressEvent(QKeyEvent* event) {
  clearBell();
  const ghostty_input_action_e action =
      event->isAutoRepeat() ? GHOSTTY_ACTION_REPEAT : GHOSTTY_ACTION_PRESS;
  const bool handled = sendKey(event, action);
  if (handled || event->key() == Qt::Key_Tab ||
      event->key() == Qt::Key_Backtab) {
    event->accept();
    return;
  }
  QOpenGLWidget::keyPressEvent(event);
}

void TerminalWidget::keyReleaseEvent(QKeyEvent* event) {
  if (!ghosttyShouldSendKeyRelease(*event)) {
    event->accept();
    return;
  }
  if (sendKey(event, GHOSTTY_ACTION_RELEASE)) {
    event->accept();
  } else {
    QOpenGLWidget::keyReleaseEvent(event);
  }
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent* event) {
  if (m_surface == nullptr) {
    return;
  }

  const QByteArray preedit = event->preeditString().toUtf8();
  m_composing = !preedit.isEmpty();
  ghostty_surface_preedit(m_surface, preedit.constData(),
                          static_cast<uintptr_t>(preedit.size()));

  const QByteArray commit = event->commitString().toUtf8();
  if (!commit.isEmpty()) {
    ghostty_surface_text(m_surface, commit.constData(),
                         static_cast<uintptr_t>(commit.size()));
  }
  event->accept();
}

QVariant TerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImCursorRectangle && m_surface != nullptr) {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    ghostty_surface_ime_point(m_surface, &x, &y, &width, &height);
    const qreal scale = std::max<qreal>(1.0, devicePixelRatioF());
    return QRectF(x / scale, y / scale, width / scale, height / scale);
  }

  return QOpenGLWidget::inputMethodQuery(query);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* event) {
  if (m_focusFollowsMouse && !hasFocus()) {
    setFocus(Qt::MouseFocusReason);
  }
  sendMousePosition(event->position(), event->modifiers());
  event->accept();
}

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
  const QWidget* focus = QApplication::focusWidget();
  const bool hadFocus =
      hasFocus() || focus == this || (focus != nullptr && isAncestorOf(focus));
  if (event->button() == Qt::LeftButton) {
    m_suppressLeftMouseRelease = !hadFocus;
  } else if (event->button() == Qt::RightButton) {
    m_contextMenuPending = false;
  }
  setFocus(Qt::MouseFocusReason);

  if (event->button() == Qt::LeftButton && !hadFocus) {
    event->accept();
    return;
  }
  if (m_surface != nullptr) {
    sendMousePosition(event->position(), event->modifiers());
    const bool consumed = ghostty_surface_mouse_button(
        m_surface, GHOSTTY_MOUSE_PRESS, mouseButton(event->button()),
        modifiers(event->modifiers()));
    if (event->button() == Qt::RightButton && !consumed) {
      m_contextMenuPosition = event->globalPosition().toPoint();
      m_contextMenuPending = true;
    }
  }
  event->accept();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && m_suppressLeftMouseRelease) {
    m_suppressLeftMouseRelease = false;
    event->accept();
    return;
  }
  if (m_surface != nullptr) {
    sendMousePosition(event->position(), event->modifiers());
    ghostty_surface_mouse_button(m_surface, GHOSTTY_MOUSE_RELEASE,
                                 mouseButton(event->button()),
                                 modifiers(event->modifiers()));
  }
  if (event->button() == Qt::RightButton && m_contextMenuPending) {
    m_contextMenuPending = false;
    m_contextMenu->popup(m_contextMenuPosition);
  }
  event->accept();
}

void TerminalWidget::wheelEvent(QWheelEvent* event) {
  if (m_surface == nullptr) {
    return;
  }

  const QPoint pixel = event->pixelDelta();
  const bool precise = !pixel.isNull();
  const QPointF delta =
      precise ? QPointF(pixel) : QPointF(event->angleDelta()) / 120.0;
  const ghostty_input_scroll_mods_t scrollMods = precise ? 1 : 0;
  ghostty_surface_mouse_scroll(m_surface, delta.x(), delta.y(), scrollMods);
  event->accept();
}

void TerminalWidget::dragEnterEvent(QDragEnterEvent* event) {
  dragMoveEvent(event);
}

void TerminalWidget::dragMoveEvent(QDragMoveEvent* event) {
  const QMimeData& mime = *event->mimeData();
  if (mime.hasFormat(QString::fromLatin1(qhostty::surfaceMimeType))) {
    if (draggedSurface(mime) == nullptr) {
      event->ignore();
      m_dropZone->hide();
      return;
    }
    showDropZone(event->position());
    event->acceptProposedAction();
    return;
  }
  if (mime.hasUrls() || mime.hasText()) {
    event->acceptProposedAction();
  }
}

void TerminalWidget::dragLeaveEvent(QDragLeaveEvent* event) {
  m_dropZone->hide();
  QOpenGLWidget::dragLeaveEvent(event);
}

TerminalWidget* TerminalWidget::draggedSurface(const QMimeData& mime) const {
  auto* source =
      qhostty::dragPointer<TerminalWidget>(mime, qhostty::surfaceMimeType);
  if (source == nullptr || source == this || !m_app->hasSurface(source)) {
    return nullptr;
  }
  if (TerminalTab::tabOf(source) == nullptr ||
      TerminalTab::tabOf(this) == nullptr) {
    return nullptr;
  }
  return source;
}

ghostty_action_split_direction_e TerminalWidget::dropDirection(
    const QPointF& position) const {
  // The nearest edge, relative to the size of the terminal.
  const double x = position.x() / std::max(1, width()) - 0.5;
  const double y = position.y() / std::max(1, height()) - 0.5;
  if (std::abs(x) > std::abs(y)) {
    return x < 0 ? GHOSTTY_SPLIT_DIRECTION_LEFT : GHOSTTY_SPLIT_DIRECTION_RIGHT;
  }
  return y < 0 ? GHOSTTY_SPLIT_DIRECTION_UP : GHOSTTY_SPLIT_DIRECTION_DOWN;
}

void TerminalWidget::showDropZone(const QPointF& position) {
  const int halfWidth = width() / 2;
  const int halfHeight = height() / 2;
  QRect zone;
  switch (dropDirection(position)) {
    case GHOSTTY_SPLIT_DIRECTION_LEFT:
      zone = QRect(0, 0, halfWidth, height());
      break;
    case GHOSTTY_SPLIT_DIRECTION_RIGHT:
      zone = QRect(halfWidth, 0, width() - halfWidth, height());
      break;
    case GHOSTTY_SPLIT_DIRECTION_UP:
      zone = QRect(0, 0, width(), halfHeight);
      break;
    default:
      zone = QRect(0, halfHeight, width(), height() - halfHeight);
      break;
  }
  m_dropZone->setGeometry(zone);
  m_dropZone->show();
  m_dropZone->raise();
}

void TerminalWidget::dropEvent(QDropEvent* event) {
  m_dropZone->hide();
  if (event->mimeData()->hasFormat(
          QString::fromLatin1(qhostty::surfaceMimeType))) {
    TerminalWidget* source = draggedSurface(*event->mimeData());
    TerminalTab* tab = TerminalTab::tabOf(this);
    if (source != nullptr && tab != nullptr &&
        tab->moveTerminal(source, this, dropDirection(event->position()))) {
      event->acceptProposedAction();
    }
    return;
  }

  const QByteArray text = ghosttyDropText(*event->mimeData()).toUtf8();
  if (m_surface == nullptr || text.isEmpty()) {
    return;
  }

  // Paste protection asks for confirmation for unsafe text.
  ghostty_surface_paste_text(m_surface, text.constData(),
                             static_cast<uintptr_t>(text.size()));
  event->acceptProposedAction();
  setFocus(Qt::OtherFocusReason);
}

void TerminalWidget::leaveEvent(QEvent* event) {
  if (m_surface != nullptr && QGuiApplication::mouseButtons() == Qt::NoButton) {
    ghostty_surface_mouse_pos(m_surface, -1, -1,
                              modifiers(QGuiApplication::keyboardModifiers()));
  }
  QOpenGLWidget::leaveEvent(event);
}

void TerminalWidget::focusInEvent(QFocusEvent* event) {
  clearBell();
  m_app->notifications()->dismiss(this);
  m_outputMonitor->reset();
  scheduleSurfaceFocus(true);
  emit focused();
  QOpenGLWidget::focusInEvent(event);
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
  scheduleSurfaceFocus(false);
  QOpenGLWidget::focusOutEvent(event);
}

bool TerminalWidget::focusNextPrevChild(bool) {
  // Tab and Backtab are terminal input, not Qt focus traversal.
  return false;
}

void TerminalWidget::showEvent(QShowEvent* event) {
  if (m_surface != nullptr) {
    ghostty_surface_set_occlusion(m_surface, true);
  }
  QOpenGLWidget::showEvent(event);
}

void TerminalWidget::hideEvent(QHideEvent* event) {
  if (m_surface != nullptr) {
    ghostty_surface_set_occlusion(m_surface, false);
  }
  QOpenGLWidget::hideEvent(event);
}

bool TerminalWidget::event(QEvent* event) {
  if (event->type() == QEvent::DevicePixelRatioChange && m_surface != nullptr) {
    syncSurfaceMetrics();
  }
  return QOpenGLWidget::event(event);
}

bool TerminalWidget::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_dragHandle) {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (event->type() == QEvent::MouseButtonPress &&
        mouse->button() == Qt::LeftButton) {
      m_dragHandleStart = mouse->position().toPoint();
      return true;
    }
    if (event->type() == QEvent::MouseMove &&
        (mouse->buttons() & Qt::LeftButton) &&
        (mouse->position().toPoint() - m_dragHandleStart).manhattanLength() >=
            QApplication::startDragDistance()) {
      emit dragRequested();
      return true;
    }
  }
  if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      runBindingAction(QStringLiteral("end_search"));
      return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      runBindingAction(key->modifiers().testFlag(Qt::ShiftModifier)
                           ? QStringLiteral("navigate_search:previous")
                           : QStringLiteral("navigate_search:next"));
      return true;
    }
  }
  return QOpenGLWidget::eventFilter(watched, event);
}

void TerminalWidget::cleanupContext() {
  if (!m_contextRealized || context() == nullptr) {
    return;
  }

  makeCurrent();
  m_presenter.release();
  m_contextRealized = false;
  doneCurrent();
}

void TerminalWidget::createSurface() {
  m_config.platform_tag = GHOSTTY_PLATFORM_EGL;
  m_config.platform.egl.display = FramePresenter::eglDisplay(context());
  m_config.userdata = this;
  m_config.scale_factor = devicePixelRatioF();
  m_config.working_directory = m_workingDirectoryUtf8.isEmpty()
                                   ? nullptr
                                   : m_workingDirectoryUtf8.constData();
  m_config.command =
      m_commandUtf8.isEmpty() ? nullptr : m_commandUtf8.constData();
  m_config.initial_input =
      m_initialInputUtf8.isEmpty() ? nullptr : m_initialInputUtf8.constData();

  // Qt can change the environment, for example when it activates a window.
  ghostty_sync_environ();
  m_surface = ghostty_surface_new(m_app->handle(), &m_config);
  if (m_surface == nullptr) {
    qCritical("Failed to create Ghostty surface");
    return;
  }
  m_app->applyBackgroundOpacity(this);
}

void TerminalWidget::destroySurface() {
  if (m_contextRealized && context() != nullptr && context()->isValid()) {
    makeCurrent();
    m_presenter.release();
    doneCurrent();
  }
  m_contextRealized = false;

  if (m_surface == nullptr) {
    return;
  }

  ghostty_surface_free(m_surface);
  m_surface = nullptr;
}

void TerminalWidget::requestFrame() {
  // The renderer draws a full frame when the surface becomes visible.
  ghostty_surface_set_occlusion(m_surface, false);
  ghostty_surface_set_occlusion(m_surface, isVisible());
}

void TerminalWidget::syncSurfaceMetrics() {
  if (m_surface == nullptr) {
    return;
  }

  const qreal scale = std::max<qreal>(1.0, devicePixelRatioF());
  ghostty_surface_set_content_scale(m_surface, scale, scale);
  ghostty_surface_set_size(m_surface, qRound(width() * scale),
                           qRound(height() * scale));
}

bool TerminalWidget::sendKey(QKeyEvent* event, ghostty_input_action_e action) {
  if (m_surface == nullptr) {
    return false;
  }

  int modifierBits = modifiers(event->modifiers());
  switch (event->nativeScanCode()) {
    case 0x3e:
      modifierBits |= GHOSTTY_MODS_SHIFT_RIGHT;
      break;
    case 0x69:
      modifierBits |= GHOSTTY_MODS_CTRL_RIGHT;
      break;
    case 0x6c:
      modifierBits |= GHOSTTY_MODS_ALT_RIGHT;
      break;
    case 0x86:
      modifierBits |= GHOSTTY_MODS_SUPER_RIGHT;
      break;
    default:
      break;
  }

  const uint32_t unshifted = ghosttyUnshiftedCodepoint(*event);
  const QByteArray text = ghosttyKeyText(*event, unshifted);

  int consumedBits = GHOSTTY_MODS_NONE;
  const std::u32string codepoints = QString::fromUtf8(text).toStdU32String();
  if (event->modifiers().testFlag(Qt::ShiftModifier) && unshifted != 0 &&
      !codepoints.empty() && codepoints.front() != unshifted) {
    consumedBits |= GHOSTTY_MODS_SHIFT;
  }

  ghostty_input_key_s key{};
  key.action = action;
  key.mods = static_cast<ghostty_input_mods_e>(modifierBits);
  key.consumed_mods = static_cast<ghostty_input_mods_e>(consumedBits);
  key.keycode = event->nativeScanCode();
  key.text = text.isEmpty() ? nullptr : text.constData();
  key.unshifted_codepoint = unshifted;
  key.composing = m_composing;
  key.logical_key = ghosttyLogicalKey(*event);
  return ghostty_surface_key(m_surface, key);
}

void TerminalWidget::sendMousePosition(
    const QPointF& position,
    Qt::KeyboardModifiers keyboardModifiers) {
  if (m_surface != nullptr) {
    const QPointF surfacePosition =
        ghosttySurfaceMousePosition(position, devicePixelRatioF());
    ghostty_surface_mouse_pos(m_surface, surfacePosition.x(),
                              surfacePosition.y(),
                              modifiers(keyboardModifiers));
  }
}

void TerminalWidget::setMouseShape(ghostty_action_mouse_shape_e shape) {
  m_mouseShape = shape;
  applyMouseCursor();
}

void TerminalWidget::setMouseVisible(bool visible) {
  m_mouseVisible = visible;
  applyMouseCursor();
}

void TerminalWidget::applyMouseCursor() {
  if (!m_mouseVisible) {
    setCursor(Qt::BlankCursor);
    return;
  }

  switch (m_mouseShape) {
    case GHOSTTY_MOUSE_SHAPE_HELP:
      setCursor(Qt::WhatsThisCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_POINTER:
    case GHOSTTY_MOUSE_SHAPE_ZOOM_IN:
    case GHOSTTY_MOUSE_SHAPE_ZOOM_OUT:
      setCursor(Qt::PointingHandCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_PROGRESS:
      setCursor(Qt::BusyCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_WAIT:
      setCursor(Qt::WaitCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_CELL:
    case GHOSTTY_MOUSE_SHAPE_CROSSHAIR:
      setCursor(Qt::CrossCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_TEXT:
    case GHOSTTY_MOUSE_SHAPE_VERTICAL_TEXT:
      setCursor(Qt::IBeamCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_ALIAS:
      setCursor(Qt::DragLinkCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_COPY:
      setCursor(Qt::DragCopyCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_MOVE:
    case GHOSTTY_MOUSE_SHAPE_ALL_SCROLL:
      setCursor(Qt::SizeAllCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NOT_ALLOWED:
    case GHOSTTY_MOUSE_SHAPE_NO_DROP:
      setCursor(Qt::ForbiddenCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_GRAB:
      setCursor(Qt::OpenHandCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_GRABBING:
      setCursor(Qt::ClosedHandCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_E_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_W_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_EW_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_COL_RESIZE:
      setCursor(Qt::SizeHorCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_N_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_S_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_NS_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_ROW_RESIZE:
      setCursor(Qt::SizeVerCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NE_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_SW_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_NESW_RESIZE:
      setCursor(Qt::SizeBDiagCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NW_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_SE_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_NWSE_RESIZE:
      setCursor(Qt::SizeFDiagCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_DEFAULT:
    case GHOSTTY_MOUSE_SHAPE_CONTEXT_MENU:
    default:
      setCursor(Qt::ArrowCursor);
      break;
  }
}

void TerminalWidget::scheduleSurfaceFocus(bool focused) {
  m_pendingSurfaceFocus = focused;
  if (m_focusUpdateScheduled) {
    return;
  }
  m_focusUpdateScheduled = true;
  QTimer::singleShot(0, this, [this]() {
    m_focusUpdateScheduled = false;
    if (!m_pendingSurfaceFocus.has_value()) {
      return;
    }
    const bool focused = m_pendingSurfaceFocus.value();
    m_pendingSurfaceFocus.reset();
    if (m_surface != nullptr) {
      ghostty_surface_set_focus(m_surface, focused);
    }
  });
}

bool TerminalWidget::runBindingAction(const QString& action) {
  if (m_surface == nullptr) {
    return false;
  }
  const QByteArray bytes = action.toUtf8();
  return ghostty_surface_binding_action(m_surface, bytes.constData(),
                                        static_cast<uintptr_t>(bytes.size()));
}

void TerminalWidget::showSearch(const char* needle) {
  if (needle != nullptr && *needle != '\0') {
    const QSignalBlocker blocker(m_searchEdit);
    m_searchEdit->setText(QString::fromUtf8(needle));
  }
  m_searchTotal = -1;
  m_searchSelected = -1;
  updateSearchCount();
  m_searchFrame->show();
  updateSplitDimming();
  m_searchFrame->raise();
  layoutOverlays();
  m_searchEdit->setFocus(Qt::ShortcutFocusReason);
  m_searchEdit->selectAll();
}

void TerminalWidget::updateSearchCount() {
  if (m_searchSelected >= 0 && m_searchTotal >= 0) {
    m_searchCount->setText(
        tr("%1/%2").arg(m_searchSelected).arg(m_searchTotal));
  } else if (m_searchTotal >= 0) {
    m_searchCount->setText(tr("–/%1").arg(m_searchTotal));
  } else {
    m_searchCount->clear();
  }
  layoutOverlays();
}

void TerminalWidget::updateProgress(
    const ghostty_action_progress_report_s& progress) {
  bool enabled = true;
  ghostty_config_get(m_appliedConfig, &enabled, "progress-style",
                     std::strlen("progress-style"));
  if (!enabled || progress.state == GHOSTTY_PROGRESS_STATE_REMOVE) {
    m_progressTimer->stop();
    m_progressBar->hide();
    return;
  }

  QPalette palette = this->palette();
  if (progress.state == GHOSTTY_PROGRESS_STATE_ERROR) {
    palette.setColor(QPalette::Highlight, QColor(210, 55, 55));
  } else if (progress.state == GHOSTTY_PROGRESS_STATE_PAUSE) {
    palette.setColor(QPalette::Highlight, QColor(220, 145, 35));
  }
  m_progressBar->setPalette(palette);
  if (progress.state == GHOSTTY_PROGRESS_STATE_INDETERMINATE ||
      (progress.progress < 0 &&
       progress.state != GHOSTTY_PROGRESS_STATE_PAUSE)) {
    m_progressBar->setRange(0, 0);
  } else {
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(progress.progress < 0
                                ? 100
                                : std::clamp<int>(progress.progress, 0, 100));
  }
  m_progressBar->show();
  m_progressBar->raise();
  m_progressTimer->start();
  layoutOverlays();
}

void TerminalWidget::commandFinished(
    const ghostty_action_command_finished_s& command) {
  const bool notifyNext = m_notifyNextCommandAction->isChecked();
  if (notifyNext) {
    m_notifyNextCommandAction->setChecked(false);
  }

  const char* modeValue = nullptr;
  if (!ghostty_config_get(m_appliedConfig, static_cast<void*>(&modeValue),
                          "notify-on-command-finish",
                          std::strlen("notify-on-command-finish")) ||
      modeValue == nullptr) {
    return;
  }
  const QByteArray mode(modeValue);
  const QWidget* focus = QApplication::focusWidget();
  const bool surfaceFocused =
      focus == this || (focus != nullptr && isAncestorOf(focus));
  if (!notifyNext &&
      (mode == "never" || (mode == "unfocused" && surfaceFocused))) {
    return;
  }

  uint64_t threshold = 0;
  ghostty_config_get(m_appliedConfig, &threshold,
                     "notify-on-command-finish-after",
                     std::strlen("notify-on-command-finish-after"));
  if (command.duration <= threshold) {
    return;
  }

  uint32_t configuredActions = 0;
  ghostty_config_get(m_appliedConfig, &configuredActions,
                     "notify-on-command-finish-action",
                     std::strlen("notify-on-command-finish-action"));
  if ((configuredActions & 1U) != 0) {
    ringBell();
  }
  if ((configuredActions & 2U) == 0) {
    return;
  }

  const QString title = command.exit_code < 0    ? tr("Command Finished")
                        : command.exit_code == 0 ? tr("Command Succeeded")
                                                 : tr("Command Failed");
  const double seconds = static_cast<double>(command.duration) / 1.0e9;
  QString elapsed;
  if (seconds >= 3600) {
    elapsed = tr("%1 h").arg(seconds / 3600, 0, 'f', 1);
  } else if (seconds >= 60) {
    elapsed = tr("%1 min").arg(seconds / 60, 0, 'f', 1);
  } else if (seconds >= 1) {
    elapsed = tr("%1 s").arg(seconds, 0, 'f', 1);
  } else {
    elapsed = tr("%1 ms").arg(command.duration / 1000000);
  }
  const QString body = command.exit_code < 0
                           ? tr("Command took %1.").arg(elapsed)
                           : tr("Command took %1 and exited with code %2.")
                                 .arg(elapsed)
                                 .arg(command.exit_code);
  m_app->sendNotification(this, title, body);
}

void TerminalWidget::updateScrollbar(
    const ghostty_action_scrollbar_s& scrollbar) {
  const char* mode = nullptr;
  if (ghostty_config_get(m_appliedConfig, static_cast<void*>(&mode),
                         "scrollbar", std::strlen("scrollbar")) &&
      mode != nullptr && QByteArray(mode) == "never") {
    m_scrollMaximum = 0;
    m_scrollBar->hide();
    layoutOverlays();
    return;
  }

  m_scrollMaximum =
      scrollbar.total > scrollbar.len ? scrollbar.total - scrollbar.len : 0;
  const uint64_t displayMaximum =
      std::min<uint64_t>(m_scrollMaximum, std::numeric_limits<int>::max());
  const int displayValue = m_scrollMaximum == 0
                               ? 0
                               : static_cast<int>(std::llround(
                                     static_cast<double>(scrollbar.offset) /
                                     static_cast<double>(m_scrollMaximum) *
                                     static_cast<double>(displayMaximum)));
  const int pageStep =
      scrollbar.total == 0
          ? 1
          : std::max(1, static_cast<int>(
                            std::llround(static_cast<double>(scrollbar.len) /
                                         static_cast<double>(scrollbar.total) *
                                         static_cast<double>(displayMaximum))));

  const QSignalBlocker blocker(m_scrollBar);
  m_scrollBar->setRange(0, static_cast<int>(displayMaximum));
  m_scrollBar->setPageStep(pageStep);
  m_scrollBar->setValue(displayValue);
  m_scrollBar->setVisible(m_scrollMaximum > 0);
  layoutOverlays();
}

void TerminalWidget::updateKeyState() {
  QStringList state;
  if (!m_keyTables.isEmpty()) {
    state.append(tr("Table: %1").arg(m_keyTables.join(QStringLiteral(", "))));
  }
  if (!m_keySequence.isEmpty()) {
    state.append(tr("Sequence: %1").arg(m_keySequence.join(QLatin1Char(' '))));
  }
  m_keyStateBadge->setText(state.join(QStringLiteral("  ")));
  m_keyStateBadge->setVisible(!state.isEmpty());
  layoutOverlays();
}

void TerminalWidget::ringBell() {
  // Same defaults as Ghostty: attention and title.
  uint32_t features = 0b01100;
  ghostty_config_get(m_appliedConfig, &features, "bell-features",
                     std::strlen("bell-features"));
  constexpr uint32_t system = 1U << 0;
  constexpr uint32_t audio = 1U << 1;
  constexpr uint32_t attention = 1U << 2;
  constexpr uint32_t title = 1U << 3;
  constexpr uint32_t border = 1U << 4;

  if ((features & system) != 0) {
    QApplication::beep();
  }
  if ((features & audio) != 0) {
    ghostty_config_path_s path{};
    double volume = 0.5;
    ghostty_config_get(m_appliedConfig, &volume, "bell-audio-volume",
                       std::strlen("bell-audio-volume"));
    if (ghostty_config_get(m_appliedConfig, &path, "bell-audio-path",
                           std::strlen("bell-audio-path")) &&
        path.path != nullptr && path.path[0] != '\0') {
      m_app->playSound(QString::fromUtf8(path.path), volume);
    }
  }
  if ((features & attention) != 0 && !window()->isActiveWindow()) {
    QApplication::alert(window());
  }
  if ((features & border) != 0) {
    m_bellBorder->show();
    m_bellBorder->raise();
    layoutOverlays();
  }
  if ((features & title) != 0 && !m_bellTitle) {
    m_bellTitle = true;
    emit bellChanged();
  }
}

void TerminalWidget::clearBell() {
  m_bellBorder->hide();
  if (m_bellTitle) {
    m_bellTitle = false;
    emit bellChanged();
  }
}

void TerminalWidget::setSplitDimmed(bool dimmed) {
  m_splitDimmed = dimmed;
  updateSplitDimming();
}

void TerminalWidget::present() {
  auto* owner = qobject_cast<MainWindow*>(window());
  if (owner == nullptr) {
    return;
  }
  ghostty_action_s action{};
  action.tag = GHOSTTY_ACTION_PRESENT_TERMINAL;
  (void)owner->handleAction(this, action);
}

QString TerminalWidget::displayName() const {
  return m_title.isEmpty() ? tr("Terminal") : m_title;
}

void TerminalWidget::alertOutput(const QString& title, const QString& body) {
  if (hasFocus() && window()->isActiveWindow()) {
    m_outputMonitor->reset();
    return;
  }
  m_app->sendNotification(this, title, body);
}

QString TerminalWidget::accessibleText() const {
  if (m_accessibleText.isEmpty()) {
    m_accessibleText = qhostty::viewportText(m_surface);
  }
  return m_accessibleText;
}

std::optional<TerminalWidget::Grid> TerminalWidget::grid() const {
  if (m_surface == nullptr) {
    return std::nullopt;
  }
  const ghostty_surface_size_s size = ghostty_surface_size(m_surface);
  if (size.columns == 0 || size.cell_width_px == 0 ||
      size.cell_height_px == 0) {
    return std::nullopt;
  }
  const double scale = devicePixelRatioF();
  const double cellWidth = size.cell_width_px / scale;
  const double cellHeight = size.cell_height_px / scale;
  // The cursor point is the middle of the cursor cell in x and its bottom
  // in y. This gives the padding while it is smaller than a cell.
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  ghostty_surface_ime_point(m_surface, &x, &y, &width, &height);
  return Grid{
      .cellWidth = cellWidth,
      .cellHeight = cellHeight,
      .left = std::fmod(x - cellWidth / 2, cellWidth),
      .top = std::fmod(y, cellHeight),
      .columns = size.columns,
  };
}

// The viewport text has one line per logical line. A soft-wrapped line
// covers more than one row.
int TerminalWidget::offsetOfCell(int row, int column) const {
  const std::optional<Grid> cells = grid();
  const QString text = accessibleText();
  if (!cells.has_value()) {
    return 0;
  }
  int offset = 0;
  int firstRow = 0;
  for (const QStringView line : QStringView(text).split(u'\n')) {
    const int rows =
        std::max(1, int((line.size() + cells->columns - 1) / cells->columns));
    if (row < firstRow + rows) {
      const int inLine = (row - firstRow) * cells->columns + column;
      return offset + std::clamp(inLine, 0, int(line.size()));
    }
    offset += int(line.size()) + 1;
    firstRow += rows;
  }
  return int(text.size());
}

QPoint TerminalWidget::cellOfOffset(int offset) const {
  const std::optional<Grid> cells = grid();
  if (!cells.has_value()) {
    return {};
  }
  int lineStart = 0;
  int firstRow = 0;
  for (const QStringView line : QStringView(accessibleText()).split(u'\n')) {
    if (offset <= lineStart + line.size()) {
      const int inLine = offset - lineStart;
      return {inLine % cells->columns, firstRow + inLine / cells->columns};
    }
    firstRow +=
        std::max(1, int((line.size() + cells->columns - 1) / cells->columns));
    lineStart += int(line.size()) + 1;
  }
  return {0, firstRow};
}

int TerminalWidget::accessibleCursor() const {
  const std::optional<Grid> cells = grid();
  if (!cells.has_value()) {
    return 0;
  }
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  ghostty_surface_ime_point(m_surface, &x, &y, &width, &height);
  const int column = int((x - cells->left) / cells->cellWidth);
  const int row = int((y - cells->top) / cells->cellHeight) - 1;
  return offsetOfCell(row, column);
}

std::optional<std::pair<int, int>> TerminalWidget::accessibleSelection()
    const {
  if (m_surface == nullptr || !ghostty_surface_has_selection(m_surface)) {
    return std::nullopt;
  }
  ghostty_text_s text{};
  if (!ghostty_surface_read_selection(m_surface, &text)) {
    return std::nullopt;
  }
  const uint32_t start = text.offset_start;
  const uint32_t length = text.offset_len;
  ghostty_surface_free_text(m_surface, &text);

  // The offsets count cells: row * columns + column.
  const std::optional<Grid> cells = grid();
  if (!cells.has_value()) {
    return std::nullopt;
  }
  const auto columns = uint32_t(cells->columns);
  const uint32_t end = start + length;
  return std::pair{offsetOfCell(int(start / columns), int(start % columns)),
                   offsetOfCell(int(end / columns), int(end % columns) + 1)};
}

QRect TerminalWidget::accessibleCharacterRect(int offset) const {
  const std::optional<Grid> cells = grid();
  if (!cells.has_value()) {
    return {};
  }
  const QPoint cell = cellOfOffset(offset);
  return QRectF(cells->left + cell.x() * cells->cellWidth,
                cells->top + cell.y() * cells->cellHeight, cells->cellWidth,
                cells->cellHeight)
      .toAlignedRect();
}

int TerminalWidget::accessibleOffsetAt(const QPoint& position) const {
  const std::optional<Grid> cells = grid();
  if (!cells.has_value()) {
    return -1;
  }
  const int column = int((position.x() - cells->left) / cells->cellWidth);
  const int row = int((position.y() - cells->top) / cells->cellHeight);
  return offsetOfCell(std::max(0, row), std::max(0, column));
}

// Tells screen readers what changed: only the changed middle part of the
// text, so they do not read the whole screen again.
void TerminalWidget::updateAccessibleText() {
  const QString previous = m_accessibleText;
  const QString current = qhostty::viewportText(m_surface);
  m_accessibleText = current;
  if (previous != current && !previous.isEmpty()) {
    qsizetype prefix = 0;
    while (prefix < previous.size() && prefix < current.size() &&
           previous[prefix] == current[prefix]) {
      ++prefix;
    }
    if (prefix > 0 && prefix < current.size() &&
        current[prefix].isLowSurrogate()) {
      --prefix;
    }
    qsizetype suffix = 0;
    while (suffix < previous.size() - prefix &&
           suffix < current.size() - prefix &&
           previous[previous.size() - suffix - 1] ==
               current[current.size() - suffix - 1]) {
      ++suffix;
    }
    if (suffix > 0 && current.size() - suffix > prefix &&
        current[current.size() - suffix].isLowSurrogate()) {
      --suffix;
    }
    QAccessibleTextUpdateEvent event(
        this, int(prefix),
        previous.sliced(prefix, previous.size() - prefix - suffix),
        current.sliced(prefix, current.size() - prefix - suffix));
    event.setCursorPosition(accessibleCursor());
    QAccessible::updateAccessibility(&event);
  }

  const int cursor = accessibleCursor();
  if (cursor != m_accessibleCursor) {
    m_accessibleCursor = cursor;
    QAccessibleTextCursorEvent event(this, cursor);
    QAccessible::updateAccessibility(&event);
  }
}

void TerminalWidget::setSplitCount(int count) {
  m_splitCount = count;
  updateDragHandle();
}

void TerminalWidget::updateDragHandle() {
  const char* mode = nullptr;
  ghostty_config_get(m_appliedConfig, static_cast<void*>(&mode), "drag-handle",
                     std::strlen("drag-handle"));
  const QByteArray value = mode != nullptr ? QByteArray(mode) : "auto";
  const bool visible =
      value == "always" || (value == "auto" && m_splitCount > 1);
  m_dragHandle->setVisible(visible);
  if (visible) {
    m_dragHandle->adjustSize();
    m_dragHandle->move((width() - m_dragHandle->width()) / 2, 0);
    m_dragHandle->raise();
  }
}

void TerminalWidget::updateSplitDimming() {
  double opacity = 0.7;
  ghostty_config_get(m_appliedConfig, &opacity, "unfocused-split-opacity",
                     std::strlen("unfocused-split-opacity"));
  ghostty_config_color_s fill{};
  if (!ghostty_config_get(m_appliedConfig, &fill, "unfocused-split-fill",
                          std::strlen("unfocused-split-fill"))) {
    ghostty_config_get(m_appliedConfig, &fill, "background",
                       std::strlen("background"));
  }

  // As in GTK: not while searching.
  if (!m_splitDimmed || m_searchFrame->isVisible() || opacity >= 1.0) {
    m_dimOverlay->hide();
    return;
  }
  m_dimOverlay->setStyleSheet(
      QStringLiteral(
          "#qhostty-unfocused-split { background: rgba(%1, %2, %3, %4); }")
          .arg(fill.r)
          .arg(fill.g)
          .arg(fill.b)
          .arg(1.0 - opacity));
  m_dimOverlay->setGeometry(rect());
  m_dimOverlay->show();
}

void TerminalWidget::layoutOverlays() {
  m_bellBorder->setGeometry(rect());
  m_dimOverlay->setGeometry(rect());
  if (!m_dragHandle->isHidden()) {
    m_dragHandle->move((width() - m_dragHandle->width()) / 2, 0);
    m_dragHandle->raise();
  }
  const int scrollWidth = m_scrollBar != nullptr && m_scrollBar->isVisible()
                              ? m_scrollBar->sizeHint().width()
                              : 0;
  if (m_progressBar != nullptr) {
    m_progressBar->setGeometry(0, 0, std::max(0, width() - scrollWidth), 4);
  }
  if (m_scrollBar != nullptr) {
    m_scrollBar->setGeometry(std::max(0, width() - scrollWidth), 0, scrollWidth,
                             height());
    m_scrollBar->raise();
  }
  if (m_searchFrame != nullptr) {
    const QSize hint = m_searchFrame->sizeHint().boundedTo(QSize(
        std::max(0, width() - scrollWidth - 16), std::max(0, height() - 16)));
    m_searchFrame->setGeometry(
        std::max(8, width() - scrollWidth - hint.width() - 8), 8, hint.width(),
        hint.height());
  }
  if (m_readonlyBadge != nullptr) {
    const QSize hint = m_readonlyBadge->sizeHint();
    m_readonlyBadge->setGeometry(8, std::max(0, height() - hint.height() - 8),
                                 hint.width(), hint.height());
    m_readonlyBadge->raise();
  }
  if (m_keyStateBadge != nullptr) {
    const QSize hint = m_keyStateBadge->sizeHint();
    m_keyStateBadge->setGeometry(
        std::max(8, width() - scrollWidth - hint.width() - 8),
        std::max(0, height() - hint.height() - 8), hint.width(), hint.height());
    m_keyStateBadge->raise();
  }
  if (m_statusOverlay != nullptr) {
    const int overlayWidth =
        std::min(420, std::max(0, width() - scrollWidth - 32));
    const QSize hint =
        m_statusOverlay->sizeHint().boundedTo(QSize(overlayWidth, height()));
    m_statusOverlay->setGeometry((width() - scrollWidth - overlayWidth) / 2,
                                 (height() - hint.height()) / 2, overlayWidth,
                                 hint.height());
    m_statusOverlay->raise();
  }
}

ghostty_input_mods_e TerminalWidget::modifiers(
    Qt::KeyboardModifiers keyboardModifiers) {
  int result = GHOSTTY_MODS_NONE;
  if (keyboardModifiers.testFlag(Qt::ShiftModifier)) {
    result |= GHOSTTY_MODS_SHIFT;
  }
  if (keyboardModifiers.testFlag(Qt::ControlModifier)) {
    result |= GHOSTTY_MODS_CTRL;
  }
  if (keyboardModifiers.testFlag(Qt::AltModifier)) {
    result |= GHOSTTY_MODS_ALT;
  }
  if (keyboardModifiers.testFlag(Qt::MetaModifier)) {
    result |= GHOSTTY_MODS_SUPER;
  }
  return static_cast<ghostty_input_mods_e>(result);
}

ghostty_input_mouse_button_e TerminalWidget::mouseButton(
    Qt::MouseButton button) {
  switch (button) {
    case Qt::LeftButton:
      return GHOSTTY_MOUSE_LEFT;
    case Qt::RightButton:
      return GHOSTTY_MOUSE_RIGHT;
    case Qt::MiddleButton:
      return GHOSTTY_MOUSE_MIDDLE;
    case Qt::BackButton:
      return GHOSTTY_MOUSE_FOUR;
    case Qt::ForwardButton:
      return GHOSTTY_MOUSE_FIVE;
    case Qt::TaskButton:
      return GHOSTTY_MOUSE_SIX;
    case Qt::ExtraButton4:
      return GHOSTTY_MOUSE_SEVEN;
    case Qt::ExtraButton5:
      return GHOSTTY_MOUSE_EIGHT;
    case Qt::ExtraButton6:
      return GHOSTTY_MOUSE_NINE;
    case Qt::ExtraButton7:
      return GHOSTTY_MOUSE_TEN;
    case Qt::ExtraButton8:
      return GHOSTTY_MOUSE_ELEVEN;
    default:
      return GHOSTTY_MOUSE_UNKNOWN;
  }
}
