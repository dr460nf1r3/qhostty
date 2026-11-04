#include "TerminalAccessible.h"

#include "TerminalWidget.h"

#include <QAccessibleWidget>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <tuple>
#include <utility>

namespace qhostty {
namespace {

class TerminalAccessible final : public QAccessibleWidget,
                                 public QAccessibleTextInterface {
 public:
  explicit TerminalAccessible(TerminalWidget* terminal)
      : QAccessibleWidget(terminal, QAccessible::Terminal) {}

  void* interface_cast(QAccessible::InterfaceType type) override {
    if (type == QAccessible::TextInterface) {
      return static_cast<QAccessibleTextInterface*>(this);
    }
    return QAccessibleWidget::interface_cast(type);
  }

  QString text(QAccessible::Text type) const override {
    if (type == QAccessible::Name) {
      return terminal()->title();
    }
    if (type == QAccessible::Value) {
      return terminal()->accessibleText();
    }
    return QAccessibleWidget::text(type);
  }

  QAccessible::State state() const override {
    QAccessible::State result = QAccessibleWidget::state();
    result.multiLine = true;
    result.readOnly = true;
    result.selectableText = true;
    return result;
  }

  // QAccessibleTextInterface

  void selection(int index, int* start, int* end) const override {
    const auto range = terminal()->accessibleSelection();
    if (index == 0 && range.has_value()) {
      *start = range->first;
      *end = range->second;
    } else {
      *start = *end = 0;
    }
  }

  int selectionCount() const override {
    return terminal()->accessibleSelection().has_value() ? 1 : 0;
  }

  // The selection belongs to the terminal and the mouse. Screen readers
  // cannot change it.
  void addSelection(int, int) override {}
  void removeSelection(int) override {}
  void setSelection(int, int, int) override {}

  int cursorPosition() const override {
    return terminal()->accessibleCursor();
  }

  void setCursorPosition(int) override {}

  QString text(int start, int end) const override {
    const QString all = terminal()->accessibleText();
    start = std::clamp(start, 0, int(all.size()));
    end = std::clamp(end, start, int(all.size()));
    return all.mid(start, end - start);
  }

  QString textBeforeOffset(int offset,
                           QAccessible::TextBoundaryType type,
                           int* start,
                           int* end) const override {
    return boundary(offset, type, -1, start, end);
  }

  QString textAfterOffset(int offset,
                          QAccessible::TextBoundaryType type,
                          int* start,
                          int* end) const override {
    return boundary(offset, type, 1, start, end);
  }

  QString textAtOffset(int offset,
                       QAccessible::TextBoundaryType type,
                       int* start,
                       int* end) const override {
    return boundary(offset, type, 0, start, end);
  }

  int characterCount() const override {
    return int(terminal()->accessibleText().size());
  }

  QRect characterRect(int offset) const override {
    const QRect local = terminal()->accessibleCharacterRect(offset);
    return local.isValid()
               ? QRect(terminal()->mapToGlobal(local.topLeft()), local.size())
               : QRect();
  }

  int offsetAtPoint(const QPoint& point) const override {
    return terminal()->accessibleOffsetAt(terminal()->mapFromGlobal(point));
  }

  void scrollToSubstring(int, int) override {}

  QString attributes(int offset, int* start, int* end) const override {
    *start = *end = offset;
    return {};
  }

 private:
  TerminalWidget* terminal() const {
    return static_cast<TerminalWidget*>(widget());
  }

  // The text unit at, before or after the offset. Lines end at newlines;
  // the other units come from QTextBoundaryFinder.
  QString boundary(int offset,
                   QAccessible::TextBoundaryType type,
                   int direction,
                   int* start,
                   int* end) const {
    const QString all = terminal()->accessibleText();
    *start = *end = -1;
    if (all.isEmpty()) {
      return {};
    }
    offset = std::clamp(offset, 0, int(all.size()));

    const auto unitAt = [&](int position) -> std::pair<int, int> {
      if (type == QAccessible::NoBoundary) {
        return {0, int(all.size())};
      }
      if (type == QAccessible::LineBoundary ||
          type == QAccessible::ParagraphBoundary) {
        const int first =
            position == 0 ? 0 : int(all.lastIndexOf(u'\n', position - 1)) + 1;
        int last = int(all.indexOf(u'\n', position));
        last = last < 0 ? int(all.size()) : last + 1;
        return {first, last};
      }
      QTextBoundaryFinder::BoundaryType finderType =
          QTextBoundaryFinder::Grapheme;
      if (type == QAccessible::WordBoundary) {
        finderType = QTextBoundaryFinder::Word;
      } else if (type == QAccessible::SentenceBoundary) {
        finderType = QTextBoundaryFinder::Sentence;
      }
      QTextBoundaryFinder finder(finderType, all);
      finder.setPosition(position);
      const int first = finder.isAtBoundary()
                            ? position
                            : std::max(0, int(finder.toPreviousBoundary()));
      finder.setPosition(position);
      int last = int(finder.toNextBoundary());
      if (last < 0) {
        last = int(all.size());
      }
      return {first, last};
    };

    auto [first, last] = unitAt(std::min(offset, int(all.size()) - 1));
    if (direction < 0) {
      if (first == 0) {
        return {};
      }
      std::tie(first, last) = unitAt(first - 1);
    } else if (direction > 0) {
      if (last >= all.size()) {
        return {};
      }
      std::tie(first, last) = unitAt(last);
    }
    *start = first;
    *end = last;
    return all.mid(first, last - first);
  }
};

}  // namespace

QAccessibleInterface* terminalAccessibleFactory(const QString&,
                                                QObject* object) {
  if (auto* terminal = qobject_cast<TerminalWidget*>(object)) {
    return new TerminalAccessible(terminal);
  }
  return nullptr;
}

}  // namespace qhostty
