// Rewrites colours authored for a white page so they stay legible on a dark one.
//
// Dictionary stylesheets and inline markup routinely use navy, dark green or
// plain blue for example sentences, which all but disappear against a dark
// background. Rather than discarding the dictionary's styling wholesale, each
// colour is nudged to a readable lightness with its hue intact.
#pragma once

#include <QColor>
#include <QString>

namespace qmdict {
namespace darkcolours {

// `isBackground` inverts the direction: text is lightened, panels are darkened.
QColor remap(const QColor &colour, bool isBackground);

// Rewrites every colour-bearing declaration in a stylesheet.
QString adaptStyleSheet(const QString &css);

// Rewrites style="", color="" and bgcolor="" attribute values in an article.
// Body text is never touched, so prose like "Colour: red apples" is safe.
QString adaptHtml(const QString &html);

} // namespace darkcolours
} // namespace qmdict
