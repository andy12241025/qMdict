// Light/dark theming that looks the same on Windows and Linux.
//
// Qt's native styles only follow the desktop's dark mode on some platforms, so
// qMdict switches to the Fusion style and installs its own palette. That keeps
// the two platforms identical and avoids depending on a desktop portal.
#pragma once

#include <QString>

namespace qmdict {
namespace theme {

enum class Mode {
    System,
    Light,
    Dark,
};

// Installs the palette and style for `mode` on the running application.
void apply(Mode mode);

// Whether the currently applied mode resolves to dark.
bool isDark();

Mode fromString(const QString &value);
QString toString(Mode mode);

// Base CSS handed to the article view, matching the active palette.
// `pointSize` sets the body text size.
QString articleBaseCss(qreal pointSize);

// Rewrites colours in a dictionary's own stylesheet so they stay legible on a
// dark background. Dictionaries are written for a white page, so a blue
// example sentence or a pale highlight box becomes unreadable otherwise.
QString adaptStyleSheetForDark(const QString &css);

// The same, but for an article's inline style attributes and legacy
// <font color> markup. Only attribute values are touched, never body text.
QString adaptHtmlForDark(const QString &html);

} // namespace theme
} // namespace qmdict
