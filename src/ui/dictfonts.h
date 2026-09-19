// Fonts a dictionary stylesheet brings with it.
//
// Dictionaries ship their own faces in an @font-face rule, either as a data:
// URL or as a file inside the .mdd, and then name them in font-family. Qt has
// no @font-face of its own, so the face is registered with the application
// font database instead.
//
// The name the stylesheet uses is rarely the name inside the font file --
// Oxford's icon font calls itself "icomoon" and is referred to as "coresym" --
// so installing one also yields the rename its declarations need.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <functional>

namespace qmdict {
namespace dictfonts {

// Registers the faces `css` embeds or names, fetching the ones it names
// by file through `resource`. Returns the stylesheet name of each installed
// face mapped to the family Qt knows it by, for the names that differ.
//
// Only a face some rule names on its own is installed; see the note in the
// implementation for why a face listed as a fallback is left alone.
QHash<QString, QString> install(const QString &css,
                                const std::function<QByteArray(const QString &)> &resource);

// Rewrites the family names in `css` to the ones Qt knows. Only font
// declarations are touched, so an element that happens to share a font's name
// keeps its rules.
QString applyAliases(const QString &css, const QHash<QString, QString> &aliases);

} // namespace dictfonts
} // namespace qmdict
