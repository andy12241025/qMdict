// Makes dictionary markup renderable by Qt's rich text engine.
//
// Three habits of real dictionaries defeat QTextDocument, which decides layout
// from the element name alone and ignores `display` entirely:
//
//   * Entries are built from custom elements -- <top-g>, <sn-blk>, <x-g-blk>
//     in Oxford -- laid out with "display: block", so a whole entry collapses
//     into one unbroken paragraph.
//
//   * Elements meant to be invisible are marked "display: none". Oxford hides
//     its BrE and NAmE labels this way and colours the pronunciation instead;
//     showing them anyway jams them against the headword.
//
//   * Tag names carry an XML namespace, as in <xhtml:br> and <xhtml:a>, which
//     Qt does not recognise, so line breaks and links are dropped.
//
// Block elements are wrapped in a <div> rather than renamed to one. Wrapping
// gets the line break while leaving the original element in place, which
// matters because these stylesheets select on the element name: renaming
// <top-g> to <div> would fix the layout and lose every colour and font.
#pragma once

#include <QSet>
#include <QString>

namespace qmdict {
namespace htmlblocks {

struct LayoutRules
{
    QSet<QString> blockClasses;
    QSet<QString> blockElements;  // lower-cased
    QSet<QString> hiddenClasses;
    QSet<QString> hiddenElements; // lower-cased

    bool isEmpty() const
    {
        return blockClasses.isEmpty() && blockElements.isEmpty() && hiddenClasses.isEmpty() &&
               hiddenElements.isEmpty();
    }
};

// Reads which classes and elements a stylesheet lays out as blocks, and which
// it hides outright.
LayoutRules rulesFromStyleSheet(const QString &css);

// Whether a link should stay clickable. Dictionaries carry internal links such
// as "help:bre" and "helpp:n" that mean nothing outside their own reader; left
// in place they offer a hand cursor and then look up nonsense.
bool isNavigableHref(const QString &href);

// Strips XML namespace prefixes, drops hidden elements, unwraps links that go
// nowhere, and wraps block-level elements in <div>, keeping tags balanced.
QString adaptForTextDocument(const QString &html, const LayoutRules &rules);

} // namespace htmlblocks
} // namespace qmdict
