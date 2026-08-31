// Makes dictionary markup renderable by Qt's rich text engine.
//
// Two habits of real dictionaries defeat QTextDocument:
//
//   * Entries are built from custom elements -- <top-g>, <sn-blk>, <x-g-blk>
//     in Oxford, styled <span>s elsewhere -- and laid out with
//     "display: block" in the dictionary's stylesheet. QTextDocument decides
//     block versus inline from the element name alone and ignores `display`
//     entirely, so a whole entry collapses into one unbroken paragraph.
//
//   * Tag names carry an XML namespace, as in <xhtml:br> and <xhtml:a>, which
//     Qt does not recognise, so line breaks and links are silently dropped.
//
// The block elements are therefore wrapped in a <div> rather than renamed to
// one. Wrapping gets the line break while leaving the original element in
// place, which matters because these stylesheets select on the element name:
// renaming <top-g> to <div> would make the entry break correctly and lose
// every colour and font it had.
#pragma once

#include <QSet>
#include <QString>

namespace qmdict {
namespace htmlblocks {

struct BlockRules
{
    QSet<QString> classes;  // class names laid out as blocks
    QSet<QString> elements; // element names laid out as blocks, lower-cased

    bool isEmpty() const { return classes.isEmpty() && elements.isEmpty(); }
};

// Reads which classes and elements a stylesheet gives a block-forming display.
BlockRules rulesFromStyleSheet(const QString &css);

// Strips XML namespace prefixes from tag names and wraps block-level elements
// in <div>, keeping the tags balanced.
QString adaptForTextDocument(const QString &html, const BlockRules &rules);

} // namespace htmlblocks
} // namespace qmdict
