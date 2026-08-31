// Works around a hard limit of Qt's rich text engine.
//
// QTextDocument decides block versus inline layout from the element name
// alone. A `display: block` on a <span> is ignored no matter how it is
// delivered -- default stylesheet, inline style attribute or an embedded
// <style> block. Dictionaries such as Oxford build an entire entry out of
// styled <span>s, so under QTextBrowser they collapse into one unbroken
// paragraph.
//
// The fix is to read the dictionary's own stylesheet, work out which classes
// it makes block-level, and rewrite exactly those spans as <div>s before
// handing the article to the document.
#pragma once

#include <QHash>
#include <QSet>
#include <QString>

namespace qmdict {
namespace htmlblocks {

struct BlockRules
{
    QSet<QString> classes; // class names the stylesheet lays out as blocks
    bool everySpan = false; // a bare "span { display: block }" rule

    bool isEmpty() const { return classes.isEmpty() && !everySpan; }
};

// Collects the class names a stylesheet gives a block-forming display.
BlockRules rulesFromStyleSheet(const QString &css);

// Rewrites <span class="..."> as <div class="..."> where `rules` says the
// class is block-level, keeping the tags balanced.
QString promoteSpans(const QString &html, const BlockRules &rules);

} // namespace htmlblocks
} // namespace qmdict
