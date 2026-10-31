#include "TerminalTab.h"

#include "DragMime.h"
#include "GhosttyApp.h"
#include "SplitAlgorithms.h"
#include "TerminalWidget.h"

#include <QBoxLayout>
#include <QDrag>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QSplitter>

#include <algorithm>
#include <cstring>

TerminalTab::TerminalTab(GhosttyApp* app,
                         QWidget* parent,
                         const ghostty_surface_config_s* baseConfig)
    : QWidget(parent), m_app(app), m_layout(new QVBoxLayout(this)) {
  m_layout->setContentsMargins(0, 0, 0, 0);
  m_layout->setSpacing(0);

  applyConfig(app->config());
  auto* terminal = new TerminalWidget(app, this, baseConfig);
  m_layout->addWidget(terminal);
  connectTerminal(terminal);
  setActive(terminal);
}

QList<TerminalWidget*> TerminalTab::terminals() const {
  return findChildren<TerminalWidget*>(Qt::FindChildrenRecursively);
}

QString TerminalTab::title() const {
  QString result = tr("Terminal");
  if (!m_titleOverride.isEmpty()) {
    result = m_titleOverride;
  } else if (m_active != nullptr && !m_active->title().isEmpty()) {
    result = m_active->title();
  }

  // The bell-features title feature marks the tab until the terminal that
  // rang is focused or gets input.
  for (const TerminalWidget* terminal : terminals()) {
    if (terminal->bellTitle()) {
      return QStringLiteral("\U0001F514 ") + result;
    }
  }
  return result;
}

void TerminalTab::setTitleOverride(const QString& title) {
  m_titleOverride = title;
  emit titleChanged(this->title());
}

bool TerminalTab::present(TerminalWidget* terminal) {
  if (terminal == nullptr || !terminals().contains(terminal)) {
    return false;
  }
  setActive(terminal);
  terminal->setFocus(Qt::OtherFocusReason);
  return true;
}

TerminalWidget* TerminalTab::newSplit(
    TerminalWidget* source,
    ghostty_action_split_direction_e direction) {
  if (source == nullptr) {
    source = m_active;
  }
  if (source == nullptr) {
    return nullptr;
  }

  ghostty_surface_config_s config =
      source->inheritedConfig(GHOSTTY_SURFACE_CONTEXT_SPLIT);
  auto* terminal = new TerminalWidget(m_app, nullptr, &config);
  source->freeInheritedConfig(config);
  connectTerminal(terminal);
  placeTerminal(terminal, source, direction);

  setActive(terminal);
  terminal->setFocus(Qt::ShortcutFocusReason);
  return terminal;
}

void TerminalTab::placeTerminal(TerminalWidget* terminal,
                                TerminalWidget* target,
                                ghostty_action_split_direction_e direction) {
  const bool horizontal = direction == GHOSTTY_SPLIT_DIRECTION_LEFT ||
                          direction == GHOSTTY_SPLIT_DIRECTION_RIGHT;
  const bool before = direction == GHOSTTY_SPLIT_DIRECTION_LEFT ||
                      direction == GHOSTTY_SPLIT_DIRECTION_UP;
  const Qt::Orientation orientation =
      horizontal ? Qt::Horizontal : Qt::Vertical;
  // The new terminal gets half of the space of the target terminal.
  const int available = horizontal ? target->width() : target->height();

  if (auto* parentSplitter = qobject_cast<QSplitter*>(target->parentWidget());
      parentSplitter != nullptr &&
      parentSplitter->orientation() == orientation) {
    const int targetIndex = parentSplitter->indexOf(target);
    const int terminalIndex = before ? targetIndex : targetIndex + 1;
    QList<int> sizes = parentSplitter->sizes();
    parentSplitter->insertWidget(terminalIndex, terminal);
    sizes.insert(terminalIndex, available / 2);
    sizes[before ? targetIndex + 1 : targetIndex] = available - available / 2;
    parentSplitter->setSizes(sizes);
  } else {
    auto* splitter = new QSplitter(orientation);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    if (auto* parentSplitter =
            qobject_cast<QSplitter*>(target->parentWidget())) {
      const int targetIndex = parentSplitter->indexOf(target);
      parentSplitter->replaceWidget(targetIndex, splitter);
    } else {
      m_layout->replaceWidget(target, splitter);
    }

    if (before) {
      splitter->addWidget(terminal);
      splitter->addWidget(target);
    } else {
      splitter->addWidget(target);
      splitter->addWidget(terminal);
    }
    splitter->setSizes({available / 2, available - available / 2});
  }

  terminal->show();
  updateZoomVisibility();
  updateSplitState();
}

bool TerminalTab::closeSurface(TerminalWidget* surface) {
  if (surface == nullptr) {
    return false;
  }

  const QList<TerminalWidget*> all = terminals();
  if (all.size() <= 1) {
    emit closeRequested(this);
    return true;
  }

  auto* splitter = qobject_cast<QSplitter*>(surface->parentWidget());
  surface->setParent(nullptr);
  surface->deleteLater();
  if (splitter != nullptr) {
    collapseSplitter(splitter);
  }

  const QList<TerminalWidget*> remaining = terminals();
  setActive(remaining.isEmpty() ? nullptr : remaining.constFirst());
  updateSplitState();
  if (m_active != nullptr) {
    m_active->setFocus(Qt::OtherFocusReason);
  }
  return true;
}

TerminalTab* TerminalTab::tabOf(const QWidget* widget) {
  for (QWidget* parent = widget != nullptr ? widget->parentWidget() : nullptr;
       parent != nullptr; parent = parent->parentWidget()) {
    if (auto* tab = qobject_cast<TerminalTab*>(parent)) {
      return tab;
    }
  }
  return nullptr;
}

bool TerminalTab::moveTerminal(TerminalWidget* terminal,
                               TerminalWidget* target,
                               ghostty_action_split_direction_e direction) {
  TerminalTab* source = tabOf(terminal);
  if (source == nullptr || terminal == target || tabOf(target) != this ||
      (source == this && terminals().size() < 2)) {
    return false;
  }

  source->releaseTerminal(terminal);
  placeTerminal(terminal, target, direction);
  connectTerminal(terminal);
  setActive(terminal);
  terminal->setFocus(Qt::OtherFocusReason);
  return true;
}

void TerminalTab::releaseTerminal(TerminalWidget* terminal) {
  disconnect(terminal, nullptr, this, nullptr);
  auto* splitter = qobject_cast<QSplitter*>(terminal->parentWidget());
  terminal->setParent(nullptr);
  if (splitter != nullptr) {
    collapseSplitter(splitter);
  }

  m_zoomed = false;
  if (m_active == terminal) {
    const QList<TerminalWidget*> remaining = terminals();
    setActive(remaining.isEmpty() ? nullptr : remaining.constFirst());
  }
  updateZoomVisibility();
  updateSplitState();
}

void TerminalTab::startDrag(TerminalWidget* terminal) {
  auto* mime = new QMimeData();
  qhostty::setDragPointer(*mime, qhostty::surfaceMimeType, terminal);
  auto* drag = new QDrag(this);
  drag->setMimeData(mime);
  const QPixmap preview = terminal->grab();
  if (!preview.isNull()) {
    drag->setPixmap(preview.scaledToWidth(std::min(preview.width(), 240),
                                          Qt::SmoothTransformation));
  }

  drag->exec(Qt::MoveAction);
  // The drop target took the last terminal. The window removes this tab
  // after the drag, because the drag still uses it.
  if (terminals().isEmpty()) {
    emit emptied(this);
  }
}

bool TerminalTab::focusSplit(ghostty_action_goto_split_e direction) {
  const QList<TerminalWidget*> all = terminals();
  if (m_active == nullptr || all.size() < 2) {
    return false;
  }

  const qsizetype currentIndex = all.indexOf(m_active);
  if (direction == GHOSTTY_GOTO_SPLIT_PREVIOUS ||
      direction == GHOSTTY_GOTO_SPLIT_NEXT) {
    const qsizetype delta = direction == GHOSTTY_GOTO_SPLIT_PREVIOUS ? -1 : 1;
    const qsizetype index = (currentIndex + delta + all.size()) % all.size();
    setActive(all.at(index));
    m_active->setFocus(Qt::ShortcutFocusReason);
    return true;
  }

  QList<QPoint> centers;
  centers.reserve(all.size());
  for (TerminalWidget* terminal : all) {
    centers.append(terminal->mapTo(this, terminal->rect().center()));
  }
  const int next = qhostty::directionalNeighbor(
      centers, static_cast<int>(currentIndex), direction);
  if (next < 0) {
    return false;
  }
  setActive(all.at(next));
  m_active->setFocus(Qt::ShortcutFocusReason);
  return true;
}

bool TerminalTab::resizeSplit(const ghostty_action_resize_split_s& resize) {
  if (m_active == nullptr || resize.amount == 0) {
    return false;
  }

  const bool horizontal = resize.direction == GHOSTTY_RESIZE_SPLIT_LEFT ||
                          resize.direction == GHOSTTY_RESIZE_SPLIT_RIGHT;
  const Qt::Orientation orientation =
      horizontal ? Qt::Horizontal : Qt::Vertical;

  QWidget* child = m_active;
  auto* splitter = qobject_cast<QSplitter*>(child->parentWidget());
  while (splitter != nullptr && splitter->orientation() != orientation) {
    child = splitter;
    splitter = qobject_cast<QSplitter*>(splitter->parentWidget());
  }
  if (splitter == nullptr) {
    return false;
  }

  QList<int> sizes = splitter->sizes();
  const int index = splitter->indexOf(child);
  const bool positive = resize.direction == GHOSTTY_RESIZE_SPLIT_RIGHT ||
                        resize.direction == GHOSTTY_RESIZE_SPLIT_DOWN;
  if (!qhostty::resizeAdjacent(sizes, index, positive, resize.amount)) {
    return false;
  }
  splitter->setSizes(sizes);
  return true;
}

void TerminalTab::equalizeSplits() {
  for (QSplitter* splitter :
       findChildren<QSplitter*>(Qt::FindChildrenRecursively)) {
    equalizeSplitter(splitter);
  }
}

void TerminalTab::toggleZoom() {
  if (m_active == nullptr) {
    return;
  }

  m_zoomed = !m_zoomed;
  updateZoomVisibility();
}

bool TerminalTab::canClose() const {
  for (TerminalWidget* terminal : terminals()) {
    if (terminal->surface() != nullptr &&
        ghostty_surface_needs_confirm_quit(terminal->surface())) {
      const auto answer = QMessageBox::question(
          const_cast<TerminalTab*>(this), tr("Close tab?"),
          tr("One or more processes are still running in this tab."),
          QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel);
      return answer == QMessageBox::Close;
    }
  }
  return true;
}

void TerminalTab::connectTerminal(TerminalWidget* terminal) {
  connect(terminal, &TerminalWidget::focused, this,
          [this, terminal]() { setActive(terminal); });
  connect(terminal, &TerminalWidget::titleChanged, this,
          [this, terminal](const QString&) {
            if (terminal == m_active && m_titleOverride.isEmpty()) {
              emit titleChanged(title());
            }
          });
  connect(terminal, &TerminalWidget::bellChanged, this,
          [this]() { emit titleChanged(title()); });
  connect(terminal, &TerminalWidget::tabTitleChanged, this,
          [this](const QString& title) { setTitleOverride(title); });
  connect(terminal, &TerminalWidget::closeRequested, this,
          [this](TerminalWidget* surface) { closeSurface(surface); });
  connect(terminal, &TerminalWidget::configChanged, this,
          &TerminalTab::configChanged);
  connect(terminal, &TerminalWidget::dragRequested, this,
          [this, terminal]() { startDrag(terminal); });
}

void TerminalTab::setActive(TerminalWidget* terminal) {
  if (m_active == terminal) {
    return;
  }
  m_active = terminal;
  updateZoomVisibility();
  updateSplitState();
  emit titleChanged(title());
}

void TerminalTab::updateSplitState() {
  // unfocused-split-opacity dims every split except the active one.
  const QList<TerminalWidget*> all = terminals();
  for (TerminalWidget* terminal : all) {
    terminal->setSplitDimmed(all.size() > 1 && terminal != m_active);
    terminal->setSplitCount(static_cast<int>(all.size()));
  }
}

void TerminalTab::applyConfig(ghostty_config_t config) {
  ghostty_config_color_s color{};
  if (config != nullptr &&
      ghostty_config_get(config, &color, "split-divider-color",
                         std::strlen("split-divider-color"))) {
    setStyleSheet(QStringLiteral("QSplitter::handle { background: rgb(%1, %2, "
                                 "%3); }")
                      .arg(color.r)
                      .arg(color.g)
                      .arg(color.b));
  } else {
    setStyleSheet(QString());
  }
}

void TerminalTab::collapseSplitter(QSplitter* splitter) {
  if (splitter == nullptr || splitter->count() != 1) {
    return;
  }

  QWidget* remaining = splitter->widget(0);
  remaining->setParent(nullptr);
  if (auto* parentSplitter =
          qobject_cast<QSplitter*>(splitter->parentWidget())) {
    const int index = parentSplitter->indexOf(splitter);
    parentSplitter->replaceWidget(index, remaining);
  } else {
    m_layout->replaceWidget(splitter, remaining);
  }
  splitter->deleteLater();
}

void TerminalTab::equalizeSplitter(QSplitter* splitter) {
  if (splitter != nullptr && splitter->count() > 0) {
    splitter->setSizes(QList<int>(splitter->count(), 1));
  }
}

void TerminalTab::updateZoomVisibility() {
  for (TerminalWidget* terminal : terminals()) {
    terminal->setVisible(!m_zoomed || terminal == m_active);
  }
}
