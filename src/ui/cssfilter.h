// Cuts a dictionary stylesheet down to what actually matters.
//
// Qt tests every rule against every element, and dictionary stylesheets are
// written for a browser: font faces, generated content, floats and clears that
// the rich text engine cannot act on. Oxford's is 24 KB, and matching it
// against a long entry such as "run" costs well over a second.
//
// Removing rules that can have no effect is therefore not a micro-optimisation
// but the difference between a snappy lookup and a visible freeze. Nothing
// here changes how a page looks; it only removes work whose result is
// discarded.
#pragma once

#include <QString>
#include <QStringList>

namespace qmdict {
namespace cssfilter {

// Drops comments, at-rules, generated-content rules and any declaration Qt's
// rich text engine ignores, along with rules left empty as a result. Margins
// and padding go too unless the rule can land on an element Qt lays out as a
// block, which is the only place it honours them.
// Depends only on the stylesheet, so the result is worth caching.
QString usable(const QString &css);

// Every font family the stylesheet names. Resolving one costs a trip to the
// platform's font matcher, and doing that up front keeps it out of the first
// lookup.
QStringList fontFamilies(const QString &css);

// Drops rules whose target element or class does not occur in `html`, since
// they cannot match however hard Qt looks.
QString relevantTo(const QString &css, const QString &html);

} // namespace cssfilter
} // namespace qmdict
