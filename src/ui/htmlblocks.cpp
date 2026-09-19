#include "htmlblocks.h"

#include <QList>
#include <QRegularExpression>
#include <QStringList>

namespace qmdict {
namespace htmlblocks {
namespace {

// Elements Qt's rich text engine already knows. Their layout is its business,
// and wrapping them risks changing meaning (an <img> or <td> must stay put).
const QSet<QString> &standardElements()
{
    static const QSet<QString> known = {
        QStringLiteral("a"),       QStringLiteral("address"),  QStringLiteral("b"),
        QStringLiteral("big"),     QStringLiteral("blockquote"), QStringLiteral("body"),
        QStringLiteral("br"),      QStringLiteral("caption"),  QStringLiteral("center"),
        QStringLiteral("cite"),    QStringLiteral("code"),     QStringLiteral("dd"),
        QStringLiteral("dfn"),     QStringLiteral("div"),      QStringLiteral("dl"),
        QStringLiteral("dt"),      QStringLiteral("em"),       QStringLiteral("font"),
        QStringLiteral("form"),    QStringLiteral("h1"),       QStringLiteral("h2"),
        QStringLiteral("h3"),      QStringLiteral("h4"),       QStringLiteral("h5"),
        QStringLiteral("h6"),      QStringLiteral("head"),     QStringLiteral("hr"),
        QStringLiteral("html"),    QStringLiteral("i"),        QStringLiteral("img"),
        QStringLiteral("input"),   QStringLiteral("ins"),      QStringLiteral("kbd"),
        QStringLiteral("li"),      QStringLiteral("link"),     QStringLiteral("meta"),
        QStringLiteral("nobr"),    QStringLiteral("ol"),       QStringLiteral("p"),
        QStringLiteral("pre"),     QStringLiteral("q"),        QStringLiteral("s"),
        QStringLiteral("samp"),    QStringLiteral("script"),   QStringLiteral("small"),
        QStringLiteral("span"),    QStringLiteral("strong"),   QStringLiteral("style"),
        QStringLiteral("sub"),     QStringLiteral("sup"),      QStringLiteral("table"),
        QStringLiteral("tbody"),   QStringLiteral("td"),       QStringLiteral("tfoot"),
        QStringLiteral("th"),      QStringLiteral("thead"),    QStringLiteral("title"),
        QStringLiteral("tr"),      QStringLiteral("tt"),       QStringLiteral("u"),
        QStringLiteral("ul"),      QStringLiteral("var"),
    };
    return known;
}

bool isVoidElement(const QString &name)
{
    static const QSet<QString> voids = {
        QStringLiteral("area"),  QStringLiteral("base"), QStringLiteral("br"),
        QStringLiteral("col"),   QStringLiteral("embed"), QStringLiteral("hr"),
        QStringLiteral("img"),   QStringLiteral("input"), QStringLiteral("link"),
        QStringLiteral("meta"),  QStringLiteral("param"), QStringLiteral("source"),
        QStringLiteral("track"), QStringLiteral("wbr"),
    };
    return voids.contains(name);
}

bool isBlockDisplay(const QString &value)
{
    const QString v = value.trimmed().toLower();
    // inline-block is deliberately absent: it does not start a new line, and
    // wrapping it would break entries apart mid-sentence.
    return v == QLatin1String("block") || v == QLatin1String("list-item") ||
           v == QLatin1String("table") || v == QLatin1String("table-row") ||
           v == QLatin1String("table-caption") || v == QLatin1String("flex") ||
           v == QLatin1String("grid");
}

// Parses one compound such as "unbox", ".big_pic", "symbol[type=key]" or
// "xhtml\:br". Returns false when nothing is left to match on.
bool parseCompound(const QString &text, Selector *selector)
{
    // An escaped colon is part of a namespaced element name, not a pseudo, so
    // it is set aside before looking for one.
    static const QString marker = QStringLiteral("\x01");
    QString compound = text;
    compound.replace(QLatin1String("\\:"), marker);

    if (compound.contains(QLatin1Char(':')))
        return false;

    static const QRegularExpression attributeToken(
        QStringLiteral("\\[\\s*([-_a-zA-Z][-_a-zA-Z0-9]*)\\s*(?:([~|^$*]?)=\\s*(\"[^\"]*\"|'[^']*'|[^\\]]*))?\\]"));
    const QRegularExpressionMatch attribute = attributeToken.match(compound);
    if (attribute.hasMatch()) {
        // Only a plain match is understood; "~=" and friends would need the
        // value split up, and no dictionary stylesheet leans on them.
        if (attribute.captured(2).isEmpty()) {
            selector->attribute = attribute.captured(1).toLower();
            QString value = attribute.captured(3).trimmed();
            if (value.size() >= 2 &&
                (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);
            selector->attributeValue = value;
        }
    }

    const int bracket = compound.indexOf(QLatin1Char('['));
    if (bracket >= 0)
        compound.truncate(bracket);
    compound.replace(marker, QLatin1String(":"));

    static const QRegularExpression classToken(QStringLiteral("\\.([_a-zA-Z][-_a-zA-Z0-9]*)"));
    const QRegularExpressionMatch klass = classToken.match(compound);
    if (klass.hasMatch())
        selector->klass = klass.captured(1);

    static const QRegularExpression elementToken(
        QStringLiteral("^([a-zA-Z][-_a-zA-Z0-9]*(?::[a-zA-Z][-_a-zA-Z0-9]*)?)"));
    const QRegularExpressionMatch element = elementToken.match(compound);
    if (element.hasMatch()) {
        QString name = element.captured(1).toLower();
        const int colon = name.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0)
            name = name.mid(colon + 1); // xhtml:br is just br to Qt
        selector->element = name;
    }

    return !selector->isEmpty();
}

// Splits one alternative of a selector list into its compounds, and reports
// whether the last two were joined by ">". The difference matters: Oxford puts
// the same arrow in front of "x-g-blk > x" and of the recorded-example block
// inside it, and reading one as the other draws the arrow twice.
QStringList splitCompounds(const QString &alternative, bool *parentOnly)
{
    QString flattened = alternative.trimmed();
    flattened.replace(QLatin1Char('+'), QLatin1Char(' '));
    flattened.replace(QLatin1Char('~'), QLatin1Char(' '));
    flattened.replace(QLatin1Char('>'), QLatin1String(" > "));

    QStringList compounds = flattened.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    if (parentOnly)
        *parentOnly = compounds.size() >= 2 &&
                      compounds.at(compounds.size() - 2) == QLatin1String(">");
    compounds.removeAll(QStringLiteral(">"));
    return compounds;
}

// How strongly a selector claims an element, on the same footing CSS uses: a
// class or an attribute test counts for more than an element name.
int weightOf(const Selector &selector)
{
    int weight = 0;
    if (!selector.element.isEmpty())
        weight += 1;
    if (!selector.klass.isEmpty())
        weight += 10;
    if (!selector.attribute.isEmpty())
        weight += 10;
    return weight;
}

// Collects the rules a selector list contributes, keeping the ancestor when
// the stylesheet scoped the rule to one.
void noteAttributeElement(const Selector &selector, QSet<QString> *elements)
{
    if (selector.attribute.isEmpty() || !elements)
        return;
    // A rule with no element name has to be tested against everything.
    elements->insert(selector.element.isEmpty() ? QString() : selector.element);
}

void collectRules(const QString &selector, bool forBlocks, QList<Rule> *rules,
                  QSet<QString> *attributeElements, int order = 0,
                  const QString &boxCss = QString(), QString *extraCss = nullptr,
                  int borderWidth = 0, int cellPadding = 0)
{
    for (const QString &alternative : selector.split(QLatin1Char(','))) {
        bool parentOnly = false;
        const QStringList parts = splitCompounds(alternative, &parentOnly);
        if (parts.isEmpty())
            continue;

        Rule rule;
        rule.ancestorIsParent = parentOnly;
        if (!parseCompound(parts.last(), &rule.target))
            continue;

        // Wrapping an element Qt already lays out gains nothing, and forcing a
        // wrapper around an <img> or a <td> would change what the markup means.
        if (forBlocks && rule.target.klass.isEmpty() &&
            standardElements().contains(rule.target.element))
            continue;

        if (parts.size() >= 2) {
            Selector ancestor;
            if (parseCompound(parts.at(parts.size() - 2), &ancestor)) {
                rule.ancestor = ancestor;
                noteAttributeElement(ancestor, attributeElements);
            }
        }

        // Weighed over the whole selector, not just the two compounds kept for
        // matching: "unbox inlinelist und" has to outweigh "unbox und", and
        // the difference between them is the compound that is dropped.
        for (const QString &compound : parts) {
            Selector counted;
            if (parseCompound(compound, &counted))
                rule.weight += weightOf(counted);
        }
        rule.order = order;

        noteAttributeElement(rule.target, attributeElements);

        if (!boxCss.isEmpty() && extraCss) {
            rule.wrapperClass = QStringLiteral("qmdict-b%1").arg(rules->size());
            rule.borderWidth = borderWidth;
            rule.cellPadding = cellPadding;
            *extraCss += QStringLiteral("%1.%2{%3}\n")
                             .arg(borderWidth > 0 ? QLatin1String("table") : QLatin1String("div"),
                                  rule.wrapperClass, boxCss);
        }

        rules->append(rule);
    }
}


bool isHexDigit(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
           (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
           (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
}

// Decodes the CSS escapes a content string may carry. "\e900" is how these
// stylesheets name a glyph in the icon font they ship.
QString decodeCssEscapes(const QString &text)
{
    if (!text.contains(QLatin1Char('\\')))
        return text;

    QString out;
    out.reserve(text.size());

    int i = 0;
    while (i < text.size()) {
        if (text.at(i) != QLatin1Char('\\') || i + 1 >= text.size()) {
            out += text.at(i++);
            continue;
        }

        ++i;
        QString digits;
        while (digits.size() < 6 && i < text.size() && isHexDigit(text.at(i)))
            digits += text.at(i++);

        if (digits.isEmpty()) {
            out += text.at(i++); // an escaped literal, such as \" or \\
            continue;
        }

        // A single space after the digits closes the escape; it is not content.
        if (i < text.size() && text.at(i) == QLatin1Char(' '))
            ++i;

        bool ok = false;
        const uint code = digits.toUInt(&ok, 16);
        if (ok && code > 0)
            out += QChar(code);
    }
    return out;
}

// Splits a `content` value into its literal strings and attr() references.
// Returns false for anything Qt cannot be given text for: counters, images,
// quotes, and the values that draw nothing.
bool parseContent(const QString &value, QList<ContentPart> *parts)
{
    static const QRegularExpression token(
        QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\"|'((?:[^'\\\\]|\\\\.)*)'|attr\\(\\s*([-_a-zA-Z][-_a-zA-Z0-9]*)\\s*\\)|(\\S+)"));

    auto it = token.globalMatch(value.trimmed());
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (!match.captured(3).isEmpty()) {
            parts->append({QString(), match.captured(3).toLower()});
            continue;
        }
        if (!match.captured(4).isEmpty())
            return false; // counter(), url(), open-quote, none, normal
        const QString literal =
            decodeCssEscapes(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1));
        if (!literal.isEmpty())
            parts->append({literal, QString()});
    }
    return !parts->isEmpty();
}

// The declarations of a pseudo-element rule, minus the ones that describe the
// rule itself rather than how its text looks.
QString declarationsForGenerated(const QString &block)
{
    QStringList keep;
    for (const QString &declaration : block.split(QLatin1Char(';'))) {
        const int colon = declaration.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString property = declaration.left(colon).trimmed().toLower();
        if (property == QLatin1String("content") || property == QLatin1String("visibility") ||
            property == QLatin1String("position") || property == QLatin1String("display"))
            continue;
        keep.append(declaration.trimmed());
    }
    return keep.join(QLatin1Char(';'));
}

// The declarations that draw a box, which Qt acts on only for an element it
// lays out as a block. They move to the wrapper; everything else -- colour,
// font, weight -- stays on the dictionary's own element, where its selectors
// still reach it.
QString boxDeclarations(const QString &block)
{
    static const QSet<QString> moved = {
        QStringLiteral("border"),           QStringLiteral("border-color"),
        QStringLiteral("border-style"),     QStringLiteral("border-width"),
        QStringLiteral("border-top"),       QStringLiteral("border-bottom"),
        QStringLiteral("border-left"),      QStringLiteral("border-right"),
        QStringLiteral("background"),       QStringLiteral("background-color"),
        QStringLiteral("margin"),           QStringLiteral("margin-top"),
        QStringLiteral("margin-bottom"),    QStringLiteral("margin-left"),
        QStringLiteral("margin-right"),     QStringLiteral("padding"),
        QStringLiteral("padding-top"),      QStringLiteral("padding-bottom"),
        QStringLiteral("padding-left"),     QStringLiteral("padding-right"),
        QStringLiteral("text-align"),
    };

    QStringList keep;
    for (const QString &declaration : block.split(QLatin1Char(';'))) {
        const int colon = declaration.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        QString property = declaration.left(colon).trimmed().toLower();
        if (property.startsWith(QLatin1String("border-")) &&
            (property.endsWith(QLatin1String("-color")) ||
             property.endsWith(QLatin1String("-style")) ||
             property.endsWith(QLatin1String("-width"))))
            property = property.left(property.lastIndexOf(QLatin1Char('-')));
        if (!moved.contains(property))
            continue;
        keep.append(declaration.trimmed());
    }
    return keep.join(QLatin1Char(';'));
}

// A CSS length in pixels, for the table attributes Qt takes a number for.
// Anything relative is measured against a typical body size, which is as close
// as an attribute can get.
int lengthInPixels(const QString &value)
{
    static const QRegularExpression number(
        QStringLiteral("(-?[0-9]*\\.?[0-9]+)\\s*(px|pt|em|ex|%)?"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = number.match(value.trimmed());
    if (!match.hasMatch())
        return 0;

    const double amount = match.captured(1).toDouble();
    const QString unit = match.captured(2).toLower();
    if (unit == QLatin1String("em"))
        return qRound(amount * 14.0);
    if (unit == QLatin1String("ex"))
        return qRound(amount * 7.0);
    if (unit == QLatin1String("pt"))
        return qRound(amount * 4.0 / 3.0);
    if (unit == QLatin1String("%"))
        return 0;
    return qRound(amount);
}

// The width of a border drawn on every side, and 0 for anything else: a rule
// that borders one side only cannot be expressed as a table.
int borderWidthOf(const QString &block)
{
    static const QRegularExpression shorthand(
        QStringLiteral("(?:^|;)\\s*border\\s*:\\s*([^;]+)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = shorthand.match(block);
    if (!match.hasMatch())
        return 0;

    const QString value = match.captured(1).simplified();
    if (value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0 ||
        value.startsWith(QLatin1String("0 ")) || value == QLatin1String("0"))
        return 0;

    const int width = lengthInPixels(value);
    return width > 0 ? width : 1; // "border: solid #ccc" is one pixel wide
}

int paddingOf(const QString &block)
{
    static const QRegularExpression padding(
        QStringLiteral("(?:^|;)\\s*padding\\s*:\\s*([^;]+)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = padding.match(block);
    if (!match.hasMatch())
        return 0;
    return qBound(0, lengthInPixels(match.captured(1)), 40);
}

// Reads the :before and :after rules of one selector list.
void collectGenerated(const QString &selector, const QString &block, LayoutRules *rules)
{
    static const QRegularExpression content(
        QStringLiteral("(?:^|;)\\s*content\\s*:([^;]*)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression visible(
        QStringLiteral("(?:^|;)\\s*visibility\\s*:\\s*visible"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pseudo(
        QStringLiteral("::?(before|after)\\s*$"), QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch text = content.match(block);
    if (!text.hasMatch())
        return;

    QList<ContentPart> parts;
    if (!parseContent(text.captured(1), &parts))
        return;

    const QString declarations = declarationsForGenerated(block);

    for (const QString &alternative : selector.split(QLatin1Char(','))) {
        QString flattened = alternative.trimmed();
        const QRegularExpressionMatch tail = pseudo.match(flattened);
        if (!tail.hasMatch())
            continue;
        flattened.truncate(tail.capturedStart());

        bool parentOnly = false;
        const QStringList compounds = splitCompounds(flattened, &parentOnly);
        if (compounds.isEmpty())
            continue;

        Generated generated;
        generated.ancestorIsParent = parentOnly;
        if (!parseCompound(compounds.last(), &generated.target))
            continue;
        if (compounds.size() >= 2)
            parseCompound(compounds.at(compounds.size() - 2), &generated.ancestor);

        generated.after = tail.captured(1).compare(QLatin1String("after"), Qt::CaseInsensitive) == 0;
        generated.whenHidden = visible.match(block).hasMatch();
        generated.content = parts;
        // A rule that only names the text needs no element of its own, and
        // leaving it out keeps thousands of them off the page: every element
        // this adds is one more for Qt to match the whole stylesheet against.
        if (!declarations.isEmpty())
            generated.element = QStringLiteral("qmdict-g%1").arg(rules->generated.size());

        noteAttributeElement(generated.target, &rules->attributeElements);
        noteAttributeElement(generated.ancestor, &rules->attributeElements);
        for (const ContentPart &part : parts) {
            if (!part.attribute.isEmpty())
                rules->attributeElements.insert(generated.target.element);
        }

        if (!generated.element.isEmpty())
            rules->extraCss += generated.element + QLatin1Char('{') + declarations +
                               QLatin1String("}\n");
        rules->generated.append(generated);
    }
}

// Turns off the marker Qt would draw for a list, for the case where the
// stylesheet draws one of its own.
QString withoutListMarker(const QString &tag)
{
    static const QRegularExpression styleAttribute(
        QStringLiteral("\\bstyle\\s*=\\s*([\"'])(.*?)\\1"),
        QRegularExpression::CaseInsensitiveOption);

    const QString property = QStringLiteral("list-style-type:none");
    const QRegularExpressionMatch style = styleAttribute.match(tag);
    if (style.hasMatch()) {
        QString out = tag;
        out.insert(style.capturedEnd(2), QLatin1Char(';') + property);
        return out;
    }

    const int end = tag.endsWith(QLatin1String("/>")) ? tag.size() - 2 : tag.size() - 1;
    QString out = tag;
    out.insert(end, QStringLiteral(" style=\"%1\"").arg(property));
    return out;
}

struct OpenElement
{
    QString name;
    QStringList classes;
    QHash<QString, QString> attributes;
    QString wrapperClose; // empty when the element was not wrapped
    bool hidden = false;
    bool unwrapped = false;
    bool needsContinuation = false;
    bool continuation = false;
    QString closingContent; // an :after rule's text, held until the close tag
    QString firstChildContent; // a list's :before text, held for its first item
};

} // namespace

bool Selector::matches(const QString &name, const QStringList &classes,
                       const QHash<QString, QString> &attributes) const
{
    if (!element.isEmpty() && element != name)
        return false;
    if (!klass.isEmpty() && !classes.contains(klass))
        return false;
    if (!attribute.isEmpty()) {
        const auto found = attributes.constFind(attribute);
        if (found == attributes.constEnd())
            return false;
        if (!attributeValue.isEmpty() && found.value() != attributeValue)
            return false;
    }
    return !isEmpty();
}

bool isNavigableHref(const QString &href)
{
    const QString target = href.trimmed();
    if (target.isEmpty())
        return false;
    if (target.startsWith(QLatin1Char('#')))
        return true;

    static const QRegularExpression scheme(QStringLiteral("^([a-zA-Z][a-zA-Z0-9+.-]*):"));
    const QRegularExpressionMatch match = scheme.match(target);
    if (!match.hasMatch())
        return true; // a bare headword or relative path

    static const QSet<QString> known = {
        QStringLiteral("entry"), QStringLiteral("bword"),  QStringLiteral("sound"),
        QStringLiteral("http"),  QStringLiteral("https"),  QStringLiteral("mailto"),
        QStringLiteral("file"),  QStringLiteral("x-dictionary"),
    };
    return known.contains(match.captured(1).toLower());
}

LayoutRules rulesFromStyleSheet(const QString &css)
{
    LayoutRules rules;
    if (css.isEmpty())
        return rules;

    static const QRegularExpression rule(QStringLiteral("([^{}]+)\\{([^{}]*)\\}"));
    static const QRegularExpression display(
        QStringLiteral("(?:^|;)\\s*display\\s*:\\s*([^;!]+)"),
        QRegularExpression::CaseInsensitiveOption);
    // Anchored after a semicolon or the start so the old IE hacks these
    // stylesheets are full of -- "*visibility", "_visibility" -- are ignored.
    static const QRegularExpression visibility(
        QStringLiteral("(?:^|;)\\s*visibility\\s*:\\s*([^;!]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression comment(QStringLiteral("/\\*.*?\\*/"),
                                            QRegularExpression::DotMatchesEverythingOption);

    QString clean = css;
    clean.remove(comment);

    int order = 0;
    auto it = rule.globalMatch(clean);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        ++order;

        const QString selector = match.captured(1);
        if (selector.contains(QLatin1Char('@'))) // @media and friends
            continue;

        // A pseudo-element styles text the stylesheet draws itself, which has
        // to be put into the markup rather than matched against it.
        if (selector.contains(QLatin1String("before")) || selector.contains(QLatin1String("after")))
            collectGenerated(selector, match.captured(2), &rules);

        const QRegularExpressionMatch shown = display.match(match.captured(2));
        const QRegularExpressionMatch visible = visibility.match(match.captured(2));
        if (!shown.hasMatch() && !visible.hasMatch())
            continue;

        // "visibility: hidden" is how these stylesheets blank out a character
        // they intend to replace with an icon-font glyph: Oxford hides the
        // literal key emoji in front of a headword this way. Qt draws neither
        // the icon nor nothing, so the raw emoji leaks onto the page.
        if (visible.hasMatch() &&
            visible.captured(1).trimmed().compare(QLatin1String("hidden"), Qt::CaseInsensitive) == 0) {
            collectRules(selector, false, &rules.hidden, &rules.attributeElements, order);
            continue;
        }

        if (!shown.hasMatch())
            continue;

        const QString value = shown.captured(1).trimmed().toLower();
        if (isBlockDisplay(value))
            collectRules(selector, true, &rules.blocks, &rules.attributeElements, order,
                         boxDeclarations(match.captured(2)), &rules.extraCss,
                         borderWidthOf(match.captured(2)), paddingOf(match.captured(2)));
        else if (value == QLatin1String("none"))
            collectRules(selector, false, &rules.hidden, &rules.attributeElements, order);
        else
            // Kept only so a later, more specific rule can take an element
            // back out of the blocks, as Oxford does inside an <inlinelist>.
            collectRules(selector, true, &rules.inlines, &rules.attributeElements, order);
    }

    return rules;
}

QString adaptForTextDocument(const QString &html, const LayoutRules &rules)
{
    if (html.isEmpty())
        return html;

    static const QRegularExpression classAttribute(
        QStringLiteral("\\bclass\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hrefAttribute(
        QStringLiteral("\\bhref\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"),
        QRegularExpression::CaseInsensitiveOption);

    // Read by hand rather than by regular expression: this runs on every tag
    // of a long entry, where a regular expression is the whole cost of it.
    auto readAttributes = [](const QString &tag) {
        QHash<QString, QString> attributes;

        int at = 1;
        while (at < tag.size() && !tag.at(at).isSpace())
            ++at; // the element name

        while (at < tag.size()) {
            while (at < tag.size() && tag.at(at).isSpace())
                ++at;

            const int nameAt = at;
            while (at < tag.size() && (tag.at(at).isLetterOrNumber() ||
                                       tag.at(at) == QLatin1Char('-') ||
                                       tag.at(at) == QLatin1Char('_') ||
                                       tag.at(at) == QLatin1Char(':')))
                ++at;
            if (at == nameAt) {
                ++at; // '>' or something unexpected
                continue;
            }

            const QString name = tag.mid(nameAt, at - nameAt).toLower();
            while (at < tag.size() && tag.at(at).isSpace())
                ++at;
            if (at >= tag.size() || tag.at(at) != QLatin1Char('=')) {
                attributes.insert(name, QString()); // valueless, as in <pron gs>
                continue;
            }

            ++at;
            while (at < tag.size() && tag.at(at).isSpace())
                ++at;
            if (at >= tag.size())
                break;

            const QChar quote = tag.at(at);
            int valueAt = at;
            int valueEnd = at;
            if (quote == QLatin1Char('"') || quote == QLatin1Char('\'')) {
                valueAt = ++at;
                while (at < tag.size() && tag.at(at) != quote)
                    ++at;
                valueEnd = at;
                ++at;
            } else {
                while (at < tag.size() && !tag.at(at).isSpace() && tag.at(at) != QLatin1Char('>'))
                    ++at;
                valueEnd = at;
            }
            attributes.insert(name, tag.mid(valueAt, valueEnd - valueAt));
        }

        return attributes;
    };

    auto attributeValue = [](const QRegularExpressionMatch &match) {
        QString value = match.captured(1);
        if (value.isEmpty())
            value = match.captured(2);
        if (value.isEmpty())
            value = match.captured(3);
        return value;
    };

    QString out;
    out.reserve(html.size() + html.size() / 4);

    QList<OpenElement> open;
    int suppressed = 0; // depth of hidden elements currently being skipped

    // Qt can split text across paragraphs when custom inline elements follow
    // a nested block (Oxford's "[<gram>uncountable</gram>]", for example).
    // Give that inline run an explicit container, retaining the custom tags
    // so their colour and font selectors still apply.
    auto beginContinuation = [&]() {
        if (!open.isEmpty() && open.last().needsContinuation) {
            out += QLatin1String("<div>");
            open.last().needsContinuation = false;
            open.last().continuation = true;
        }
    };
    auto endContinuation = [&]() {
        if (!open.isEmpty()) {
            if (open.last().continuation)
                out += QLatin1String("</div>");
            open.last().continuation = false;
            open.last().needsContinuation = false;
        }
    };
    // A break that runs straight into the start or the end of a block draws an
    // empty line: the block was going to begin on a new line regardless. Qt
    // keeps both, so Oxford's "<br><chn>translation</chn>" leaves a blank line
    // between an example and its translation.
    auto dropTrailingBreak = [&]() {
        int end = out.size();
        while (end > 0 && out.at(end - 1).isSpace())
            --end;
        if (end >= 4 && QStringView(out).mid(end - 4, 4).compare(QLatin1String("<br>"),
                                                                Qt::CaseInsensitive) == 0)
            out.truncate(end - 4);
    };

    auto afterBlock = [&]() {
        if (!open.isEmpty())
            open.last().needsContinuation = true;
    };

    // Text a :before or :after rule draws, wrapped in the element its own
    // declarations were moved to.
    auto renderContent = [](const Generated &generated,
                            const QHash<QString, QString> &attributes) {
        QString text;
        for (const ContentPart &part : generated.content) {
            if (part.attribute.isEmpty())
                text += part.text.toHtmlEscaped();
            else
                text += attributes.value(part.attribute).toHtmlEscaped();
        }
        if (text.isEmpty() || generated.element.isEmpty())
            return text;
        return QStringLiteral("<%1>%2</%1>").arg(generated.element, text);
    };

    int i = 0;
    const int n = html.size();

    while (i < n) {
        if (html.at(i) != QLatin1Char('<')) {
            if (suppressed == 0) {
                if (!html.at(i).isSpace())
                    beginContinuation();
                out += html.at(i);
            }
            ++i;
            continue;
        }

        const int close = html.indexOf(QLatin1Char('>'), i);
        if (close < 0) {
            if (suppressed == 0)
                out += html.mid(i);
            break;
        }

        QString tag = html.mid(i, close - i + 1);
        i = close + 1;

        // Comments, doctypes and stray '<' characters.
        if (tag.size() < 3 || (!tag.at(1).isLetter() && tag.at(1) != QLatin1Char('/'))) {
            if (suppressed == 0)
                out += tag;
            continue;
        }

        const bool closing = tag.at(1) == QLatin1Char('/');
        const int nameAt = closing ? 2 : 1;

        int nameEnd = nameAt;
        while (nameEnd < tag.size() && (tag.at(nameEnd).isLetterOrNumber() ||
                                        tag.at(nameEnd) == QLatin1Char('-') ||
                                        tag.at(nameEnd) == QLatin1Char('_') ||
                                        tag.at(nameEnd) == QLatin1Char(':')))
            ++nameEnd;

        QString name = tag.mid(nameAt, nameEnd - nameAt);

        const int colon = name.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0) {
            name = name.mid(colon + 1);
            tag = (closing ? QStringLiteral("</") : QStringLiteral("<")) + name + tag.mid(nameEnd);
        }

        const QString key = name.toLower();

        if (closing) {
            if (isVoidElement(key))
                continue; // </br> and friends carry no meaning

            int match = -1;
            for (int at = open.size() - 1; at >= 0; --at) {
                if (open.at(at).name == key) {
                    match = at;
                    break;
                }
            }

            if (match < 0) {
                if (suppressed == 0)
                    out += tag; // stray close tag, let Qt ignore it
                continue;
            }

            // Close anything the dictionary left open inside this element.
            while (open.size() > match + 1) {
                endContinuation();
                const OpenElement inner = open.takeLast();
                if (inner.hidden) {
                    --suppressed;
                } else if (suppressed == 0) {
                    out += inner.closingContent;
                    if (!inner.unwrapped)
                        out += QStringLiteral("</%1>").arg(inner.name);
                }
                if (!inner.wrapperClose.isEmpty() && suppressed == 0) {
                    dropTrailingBreak();
                    out += inner.wrapperClose;
                    afterBlock();
                }
            }

            endContinuation();
            const OpenElement self = open.takeLast();
            if (self.hidden) {
                --suppressed;
                continue;
            }
            if (suppressed == 0) {
                out += self.closingContent;
                if (!self.unwrapped)
                    out += tag;
                if (!self.wrapperClose.isEmpty()) {
                    dropTrailingBreak();
                    out += self.wrapperClose;
                    afterBlock();
                }
            }
            continue;
        }

        // --- opening tag -------------------------------------------------
        QStringList classes;
        const QRegularExpressionMatch classMatch = classAttribute.match(tag);
        if (classMatch.hasMatch())
            classes = attributeValue(classMatch).split(QLatin1Char(' '), Qt::SkipEmptyParts);

        // Attributes are read only for the elements some rule asks about,
        // which is a handful out of the thousands a long entry contains.
        QHash<QString, QString> attributes;
        if (rules.attributeElements.contains(key) || rules.attributeElements.contains(QString()))
            attributes = readAttributes(tag);

        // A scoped rule only applies inside its ancestor, which is what keeps
        // "top-g xhtml:br" from deleting every line break in the entry.
        auto matchesHere = [&](const Selector &target, const Selector &ancestor,
                               bool ancestorIsParent) {
            if (!target.matches(key, classes, attributes))
                return false;
            if (ancestor.isEmpty())
                return true;
            if (ancestorIsParent) {
                return !open.isEmpty() && ancestor.matches(open.last().name, open.last().classes,
                                                           open.last().attributes);
            }
            for (const OpenElement &enclosing : open) {
                if (ancestor.matches(enclosing.name, enclosing.classes, enclosing.attributes))
                    return true;
            }
            return false;
        };
        auto applies = [&](const QList<Rule> &candidates) {
            for (const Rule &rule : candidates) {
                if (matchesHere(rule.target, rule.ancestor, rule.ancestorIsParent))
                    return true;
            }
            return false;
        };

        const bool hidden = applies(rules.hidden);
        const bool selfClosing = tag.endsWith(QLatin1String("/>"));

        if (hidden) {
            // An element hidden only so its own :after can replace it still
            // draws that text: Oxford blanks the literal key emoji in front of
            // a headword and puts an icon-font key in its place.
            if (suppressed == 0) {
                for (const Generated &generated : rules.generated) {
                    if (!generated.whenHidden ||
                        !matchesHere(generated.target, generated.ancestor,
                                     generated.ancestorIsParent))
                        continue;
                    beginContinuation();
                    out += renderContent(generated, attributes);
                }
            }

            // Void elements have no content to skip and no close tag to wait
            // for, so they simply vanish.
            if (!isVoidElement(key) && !selfClosing) {
                OpenElement element;
                element.name = key;
                element.classes = classes;
                element.attributes = attributes;
                element.hidden = true;
                open.append(element);
                ++suppressed;
            }
            continue;
        }

        if (suppressed > 0) {
            if (!isVoidElement(key) && !selfClosing) {
                OpenElement element;
                element.name = key;
                element.classes = classes;
                element.attributes = attributes;
                open.append(element);
            }
            continue;
        }

        // A list's own numbering waits for the item it belongs in front of.
        QString inherited;
        if (!open.isEmpty() && !open.last().firstChildContent.isEmpty()) {
            inherited = open.last().firstChildContent;
            open.last().firstChildContent.clear();
        }

        // A link that goes nowhere should not look like a link.
        bool unwrap = false;
        if (key == QLatin1String("a")) {
            const QRegularExpressionMatch href = hrefAttribute.match(tag);
            unwrap = !href.hasMatch() || !isNavigableHref(attributeValue(href));
        }

        // One pass over the block rules: whether to wrap, and the boxes the
        // wrapper has to draw. Later rules win in Qt as they do in a browser,
        // so the classes stay in the order the stylesheet gave them.
        QStringList boxClasses;
        int borderWidth = 0;
        int cellPadding = 0;
        int blockWeight = -1;
        int blockOrder = -1;
        for (const Rule &rule : rules.blocks) {
            if (!matchesHere(rule.target, rule.ancestor, rule.ancestorIsParent))
                continue;
            if (rule.weight > blockWeight ||
                (rule.weight == blockWeight && rule.order > blockOrder)) {
                blockWeight = rule.weight;
                blockOrder = rule.order;
            }
            if (rule.wrapperClass.isEmpty())
                continue;
            boxClasses.append(rule.wrapperClass);
            if (rule.borderWidth > 0) {
                borderWidth = rule.borderWidth;
                cellPadding = rule.cellPadding;
            }
        }

        bool wrap = blockWeight >= 0;

        // An element the stylesheet lays out inline further down, or by a
        // selector that says more about it, is not a block after all.
        if (wrap) {
            for (const Rule &rule : rules.inlines) {
                if (!matchesHere(rule.target, rule.ancestor, rule.ancestorIsParent))
                    continue;
                if (rule.weight > blockWeight ||
                    (rule.weight == blockWeight && rule.order > blockOrder)) {
                    wrap = false;
                    boxClasses.clear();
                    borderWidth = 0;
                    break;
                }
            }
        }

        if (wrap)
            endContinuation();
        else
            beginContinuation();

        QString before;
        QString after;
        bool drawsOwnMarker = false;
        for (const Generated &generated : rules.generated) {
            if (!matchesHere(generated.target, generated.ancestor, generated.ancestorIsParent))
                continue;
            const QString text = renderContent(generated, attributes);
            if (text.isEmpty())
                continue;
            if (generated.after)
                after += text;
            else
                before += text;
            drawsOwnMarker = true;
        }

        // A list whose stylesheet numbers the items itself would otherwise
        // carry two numbers, Qt's and the dictionary's. Qt also restarts at
        // one for every list, which is what makes "start" worth honouring.
        if (drawsOwnMarker && (key == QLatin1String("ol") || key == QLatin1String("ul")))
            tag = withoutListMarker(tag);

        if (isVoidElement(key) || selfClosing) {
            out += before;
            out += tag;
            out += inherited;
            out += after;
            continue;
        }

        QString wrapperClose;
        if (wrap) {
            dropTrailingBreak();

            const QString classes =
                boxClasses.isEmpty()
                    ? QString()
                    : QStringLiteral(" class=\"%1\"").arg(boxClasses.join(QLatin1Char(' ')));

            if (borderWidth > 0) {
                // Qt draws a border for a table and for nothing else, and takes
                // its width and padding from attributes rather than from style.
                out += QStringLiteral("<table%1 width=\"100%\" border=\"%2\" cellspacing=\"0\" "
                                      "cellpadding=\"%3\"><tr><td>")
                           .arg(classes)
                           .arg(borderWidth)
                           .arg(cellPadding);
                wrapperClose = QStringLiteral("</td></tr></table>");
            } else {
                out += QStringLiteral("<div%1>").arg(classes);
                wrapperClose = QStringLiteral("</div>");
            }
        }

        // Qt draws no list marker box, so a list's own :before text belongs at
        // the start of its first item rather than between the tags.
        const bool deferBefore =
            !before.isEmpty() && (key == QLatin1String("ol") || key == QLatin1String("ul"));

        if (!unwrap)
            out += tag;
        out += inherited;
        if (!deferBefore)
            out += before;

        OpenElement element;
        element.name = key;
        element.classes = classes;
        element.attributes = attributes;
        element.wrapperClose = wrapperClose;
        element.unwrapped = unwrap;
        element.closingContent = after;
        element.firstChildContent = deferBefore ? before : QString();
        open.append(element);
    }

    // Balance anything the dictionary never closed.
    while (!open.isEmpty()) {
        endContinuation();
        const OpenElement inner = open.takeLast();
        if (inner.hidden) {
            --suppressed;
            continue;
        }
        if (suppressed > 0)
            continue;
        out += inner.closingContent;
        if (!inner.unwrapped)
            out += QStringLiteral("</%1>").arg(inner.name);
        out += inner.wrapperClose;
    }

    return out;
}

} // namespace htmlblocks
} // namespace qmdict
