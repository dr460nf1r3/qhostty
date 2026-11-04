#include "settings/editors/PaletteEditor.h"

#include <QColorDialog>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QPushButton>

namespace qhostty {
namespace {

QMap<int, QString> parse(const QStringList& values) {
  QMap<int, QString> result;
  for (const QString& entry : values) {
    const qsizetype separator = entry.indexOf(u'=');
    bool valid = false;
    const int index = entry.left(separator).trimmed().toInt(&valid);
    if (separator > 0 && valid) {
      result.insert(index, entry.mid(separator + 1).trimmed());
    }
  }
  return result;
}

}  // namespace

PaletteEditor::PaletteEditor(QWidget* parent) : ValueEditor(parent) {
  auto* grid = new QGridLayout(this);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(4);
  for (int index = 0; index < 16; ++index) {
    auto* button = new QPushButton(QString::number(index), this);
    button->setFixedSize(40, 28);
    button->installEventFilter(this);
    grid->addWidget(button, index / 8, index % 8);
    m_buttons.append(button);
    connect(button, &QPushButton::clicked, this, [this, index]() {
      const QColor current(m_user.value(index, m_reference.value(index)));
      const QColor color = QColorDialog::getColor(
          current, this, tr("Palette color %1").arg(index));
      if (color.isValid()) {
        m_user.insert(index, color.name());
        updateButtons();
        emit changed();
      }
    });
  }
  grid->setColumnStretch(8, 1);
}

bool PaletteEditor::eventFilter(QObject* watched, QEvent* event) {
  // A right click removes the user's color, so the theme's applies again.
  if (event->type() == QEvent::ContextMenu) {
    const int index = int(m_buttons.indexOf(qobject_cast<QPushButton*>(watched)));
    if (index >= 0 && m_user.remove(index) > 0) {
      updateButtons();
      emit changed();
    }
    return true;
  }
  return ValueEditor::eventFilter(watched, event);
}

QStringList PaletteEditor::values() const {
  QStringList result;
  for (auto it = m_user.cbegin(); it != m_user.cend(); ++it) {
    result.append(QStringLiteral("%1=%2").arg(it.key()).arg(it.value()));
  }
  return result;
}

void PaletteEditor::setValues(const QStringList& values) {
  m_user = parse(values);
  updateButtons();
}

void PaletteEditor::setReference(const QStringList& values) {
  m_reference = parse(values);
  updateButtons();
}

void PaletteEditor::updateButtons() {
  for (int index = 0; index < m_buttons.size(); ++index) {
    QPushButton* button = m_buttons.at(index);
    const bool own = m_user.contains(index);
    const QColor color(m_user.value(index, m_reference.value(index)));
    const QColor text = color.lightnessF() > 0.5 ? Qt::black : Qt::white;
    button->setStyleSheet(
        color.isValid()
            ? QStringLiteral("background: %1; color: %2; %3")
                  .arg(color.name(), text.name(),
                       own ? QStringLiteral("font-weight: bold;") : QString())
            : QString());
    button->setToolTip(own ? tr("Your color %1. Right-click to remove it.")
                                 .arg(color.name())
                           : tr("Current color %1").arg(color.name()));
  }
}

}  // namespace qhostty
