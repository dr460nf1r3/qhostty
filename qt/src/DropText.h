#pragma once

#include <QString>

class QMimeData;

// Returns the text to paste for dropped data. Local files become
// shell-escaped paths separated by spaces.
QString ghosttyDropText(const QMimeData& mime);

// Escapes the same characters as Ghostty's ShellEscapeWriter.
QString ghosttyShellEscape(const QString& value);
