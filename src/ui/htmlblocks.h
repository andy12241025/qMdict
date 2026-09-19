// Makes dictionary markup renderable by Qt's rich text engine.
//
// Four habits of real dictionaries defeat QTextDocument, which decides layout
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
//   * Part of the text is not in the markup at all but in :before and :after
//     rules -- Oxford numbers its senses and draws its example arrows this way
//     -- and part of the layout is a border, which Qt draws for a table and
//     for nothing else. Both are put into the markup here, and the rules that
//     styled them follow in LayoutRules::extraCss.
//
// Block elements are wrapped in a <div>, or in a table where a border has to
// be drawn, rather than renamed. Wrapping gets the line break while leaving
// the original element in place, which matters because these stylesheets
// select on the element name: renaming <top-g> to <div> would fix the layout
// and lose every colour and font.
#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace qmdict {
namespace htmlblocks {

// One compound selector: an element name, a class, an attribute test, or a
// combination of them.
struct Selector
{
    QString element; // lower-cased; empty matches any element
    QString klass;   // empty imposes no class requirement
    QString attribute;      // lower-cased; empty imposes no attribute requirement
    QString attributeValue; // empty means the attribute need only be present

    bool isEmpty() const
    {
        return element.isEmpty() && klass.isEmpty() && attribute.isEmpty();
    }
    bool matches(const QString &name, const QStringList &classes,
                 const QHash<QString, QString> &attributes) const;
};

// A rule and, when the stylesheet gave one, the ancestor it is scoped to.
// The scope matters: Oxford hides <br> inside <top-g> and `und` inside
// `unbox`, and applying either everywhere would delete wanted content.
struct Rule
{
    Selector target;
    Selector ancestor;
    bool ancestorIsParent = false; // the stylesheet wrote ">" rather than a space

    // Enough of the cascade to settle a contradiction. Oxford lays out <und>
    // as a block and then puts it back inline inside an <inlinelist>, and
    // taking the first answer breaks a list of synonyms over five lines.
    int weight = 0; // specificity
    int order = 0;  // position in the stylesheet, which breaks a tie

    // For a block rule that also draws a box: the class given to the wrapper
    // this element goes in, which `extraCss` carries the box rules for. Qt
    // honours a background or a margin only on an element it lays out as a
    // block, and a dictionary's own elements are never that.
    QString wrapperClass;

    // A border is narrower still: Qt draws one for a table and nothing else,
    // so a bordered block is wrapped in a single-celled table, whose width and
    // padding are attributes rather than style.
    int borderWidth = 0;
    int cellPadding = 0;
};

// One piece of a `content` value: either literal text or, when `attribute` is
// set, the value the element carries for that attribute.
struct ContentPart
{
    QString text;
    QString attribute;
};

// Text a stylesheet draws with a :before or :after rule. Qt has no generated
// content of its own, so the text is inserted into the markup instead, wrapped
// in an element of its own -- `element` -- which `extraCss` then styles. Oxford
// numbers its senses this way, and without it a whole entry reads "1." over
// and over.
struct Generated
{
    Selector target;
    Selector ancestor;
    bool ancestorIsParent = false;
    bool after = false;       // :after rather than :before
    bool whenHidden = false;  // the rule makes itself visible inside a hidden element
    QString element;          // the wrapper this content is emitted in
    QList<ContentPart> content;
};

struct LayoutRules
{
    QList<Rule> blocks;

    // Rules that lay an element out inline, kept only to overrule a block rule
    // that a more specific selector has superseded.
    QList<Rule> inlines;

    QList<Rule> hidden;
    QList<Generated> generated;

    // Rules for the elements this rendering inserts -- the wrappers named in
    // `generated` and the ones in Rule::wrapperClass -- to be appended to the
    // stylesheet the article is rendered with.
    QString extraCss;

    // Elements some rule tests an attribute of, or reads one from. Every other
    // tag can skip attribute parsing, which is most of them.
    QSet<QString> attributeElements;

    bool isEmpty() const
    {
        return blocks.isEmpty() && hidden.isEmpty() && generated.isEmpty();
    }
};

// Reads which elements a stylesheet lays out as blocks, which it hides, and
// what text it draws around them.
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
