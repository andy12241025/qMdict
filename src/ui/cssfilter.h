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

namespace qmdict {
namespace cssfilter {

// Drops comments, at-rules, generated-content rules and any declaration Qt's
// rich text engine ignores, along with rules left empty as a result.
// Depends only on the stylesheet, so the result is worth caching.
QString usable(const QString &css);

// Drops rules whose target element or class does not occur in `html`, since
// they cannot match however hard Qt looks.
QString relevantTo(const QString &css, const QString &html);

} // namespace cssfilter
} // namespace qmdict
