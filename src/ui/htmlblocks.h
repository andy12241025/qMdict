// Makes dictionary markup renderable by Qt's rich text engine.
//
// Three habits of real dictionaries defeat QTextDocument, which decides layout
// from the element name alone and ignores `display` entirely:
//
//   * Entries are built from custom elements -- <top-g>, <sn-blk>, <x-g-blk>
//     in Oxford -- laid out with "display: block", so a whole entry collapses
//     into one unbroken paragraph.
//
//   * Elements meant to be invisible are marked "display: none" or
//     "visibility: hidden". Oxford hides its BrE and NAmE labels this way and
//     colours the pronunciations instead, blanks the literal key emoji before
//     a headword, and suppresses the line break between two pronunciations so
//     they share a line.
//
//   * Tag names carry an XML namespace, as in <xhtml:br> and <xhtml:a>, which
//     Qt does not recognise, so line breaks and links are dropped.
//
// Block elements are wrapped in a <div> rather than renamed to one. Wrapping
// gets the line break while leaving the original element in place, which
// matters because these stylesheets select on the element name: renaming
// <top-g> to <div> would fix the layout and lose every colour and font.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace qmdict {
namespace htmlblocks {

// One compound selector: an element name, a class, or both.
struct Selector
{
    QString element; // lower-cased; empty matches any element
    QString klass;   // empty imposes no class requirement

    bool isEmpty() const { return element.isEmpty() && klass.isEmpty(); }
    bool matches(const QString &name, const QStringList &classes) const;
};

// A rule and, when the stylesheet gave one, the ancestor it is scoped to.
// The scope matters: Oxford hides <br> inside <top-g> and `und` inside
// `unbox`, and applying either everywhere would delete wanted content.
struct Rule
{
    Selector target;
    Selector ancestor;
};

struct LayoutRules
{
    QList<Rule> blocks;
    QList<Rule> hidden;

    bool isEmpty() const { return blocks.isEmpty() && hidden.isEmpty(); }
};

// Reads which elements a stylesheet lays out as blocks and which it hides.
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
