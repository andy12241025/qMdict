#include "cssfilter.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace qmdict {
namespace cssfilter {
namespace {

// Properties QTextDocument honours. Anything else -- float, clear, position,
// transitions -- is parsed and thrown away, so carrying it costs matching time
// for no result.
const QSet<QString> &usableProperties()
{
    static const QSet<QString> properties = {
        QStringLiteral("color"),
        QStringLiteral("background"),
        QStringLiteral("background-color"),
        QStringLiteral("background-image"),
        QStringLiteral("background-repeat"),
        QStringLiteral("font"),
        QStringLiteral("font-family"),
        QStringLiteral("font-size"),
        QStringLiteral("font-style"),
        QStringLiteral("font-weight"),
        QStringLiteral("font-variant"),
        QStringLiteral("text-decoration"),
        QStringLiteral("text-indent"),
        QStringLiteral("text-align"),
        QStringLiteral("text-transform"),
        QStringLiteral("vertical-align"),
        QStringLiteral("white-space"),
        QStringLiteral("list-style"),
        QStringLiteral("list-style-type"),
        QStringLiteral("margin"),
        QStringLiteral("margin-top"),
        QStringLiteral("margin-bottom"),
        QStringLiteral("margin-left"),
        QStringLiteral("margin-right"),
        QStringLiteral("padding"),
        QStringLiteral("padding-top"),
        QStringLiteral("padding-bottom"),
        QStringLiteral("padding-left"),
        QStringLiteral("padding-right"),
        QStringLiteral("border"),
        QStringLiteral("border-color"),
        QStringLiteral("border-style"),
        QStringLiteral("border-width"),
        QStringLiteral("border-top"),
        QStringLiteral("border-bottom"),
        QStringLiteral("border-left"),
        QStringLiteral("border-right"),
        QStringLiteral("border-top-color"),
        QStringLiteral("border-bottom-color"),
        QStringLiteral("border-left-color"),
        QStringLiteral("border-right-color"),
        QStringLiteral("border-top-style"),
        QStringLiteral("border-bottom-style"),
        QStringLiteral("border-left-style"),
        QStringLiteral("border-right-style"),
        QStringLiteral("border-top-width"),
        QStringLiteral("border-bottom-width"),
        QStringLiteral("border-left-width"),
        QStringLiteral("border-right-width"),
        QStringLiteral("width"),
        QStringLiteral("height"),
        QStringLiteral("min-width"),
        QStringLiteral("min-height"),
        QStringLiteral("max-width"),
        QStringLiteral("max-height"),
    };
    return properties;
}

// Margins and padding, which Qt honours only on an element it lays out as a
// block. Everywhere else it parses the value, converts it, and throws it away.
const QSet<QString> &boxProperties()
{
    static const QSet<QString> properties = {
        QStringLiteral("margin"),       QStringLiteral("margin-top"),
        QStringLiteral("margin-bottom"), QStringLiteral("margin-left"),
        QStringLiteral("margin-right"), QStringLiteral("padding"),
        QStringLiteral("padding-top"),  QStringLiteral("padding-bottom"),
        QStringLiteral("padding-left"), QStringLiteral("padding-right"),
    };
    return properties;
}

// The elements Qt's rich text engine gives a block of their own. A dictionary's
// custom elements are not among them, however the stylesheet declares them:
// Qt decides layout from the element name alone.
const QSet<QString> &blockElements()
{
    static const QSet<QString> known = {
        QStringLiteral("address"), QStringLiteral("blockquote"), QStringLiteral("body"),
        QStringLiteral("caption"), QStringLiteral("center"),     QStringLiteral("dd"),
        QStringLiteral("div"),     QStringLiteral("dl"),         QStringLiteral("dt"),
        QStringLiteral("form"),    QStringLiteral("h1"),         QStringLiteral("h2"),
        QStringLiteral("h3"),      QStringLiteral("h4"),         QStringLiteral("h5"),
        QStringLiteral("h6"),      QStringLiteral("hr"),         QStringLiteral("html"),
        QStringLiteral("li"),      QStringLiteral("ol"),         QStringLiteral("p"),
        QStringLiteral("pre"),     QStringLiteral("table"),      QStringLiteral("tbody"),
        QStringLiteral("td"),      QStringLiteral("tfoot"),      QStringLiteral("th"),
        QStringLiteral("thead"),   QStringLiteral("tr"),         QStringLiteral("ul"),
    };
    return known;
}

const QRegularExpression &ruleExpression()
{
    static const QRegularExpression expression(QStringLiteral("([^{}]+)\\{([^{}]*)\\}"));
    return expression;
}

// Whether `selector` can land on an element Qt lays out as a block, and so
// whether its margins and padding are worth carrying. Anything uncertain --
// a class on its own, an unrecognised construct -- counts as yes.
bool canSelectABlock(const QString &selector)
{
    static const QRegularExpression elementToken(QStringLiteral("^([a-zA-Z][-_a-zA-Z0-9]*)"));
    static const QRegularExpression combinator(QStringLiteral("[\\s>+~]"));
    static const QString marker = QStringLiteral("\x01");

    for (const QString &alternative : selector.split(QLatin1Char(','))) {
        // Only the rightmost compound decides what the rule lands on; an
        // ancestor cannot turn an inline element into a block.
        QString compound = alternative.trimmed().section(combinator, -1);

        // An escaped colon is part of a namespaced element name rather than a
        // pseudo-class, and Qt knows such an element by its local name, so
        // xhtml\:table is a table.
        compound.replace(QLatin1String("\\:"), marker);
        const int pseudo = compound.indexOf(QLatin1Char(':'));
        if (pseudo >= 0)
            compound.truncate(pseudo);
        const int bracket = compound.indexOf(QLatin1Char('['));
        if (bracket >= 0)
            compound.truncate(bracket);
        const int prefix = compound.lastIndexOf(marker);
        if (prefix >= 0)
            compound = compound.mid(prefix + marker.size());

        // A rule selecting on a class alone can land on anything, so it counts.
        const QRegularExpressionMatch element = elementToken.match(compound);
        if (!element.hasMatch() || blockElements().contains(element.captured(1).toLower()))
            return true;
    }
    return false;
}

} // namespace

QString usable(const QString &css)
{
    if (css.isEmpty())
        return css;

    QString source = css;
    source.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"),
                                     QRegularExpression::DotMatchesEverythingOption));
    // @font-face, @media, @supports: one level of nesting is enough for these.
    source.remove(QRegularExpression(QStringLiteral("@[a-zA-Z-]+[^{]*\\{(?:[^{}]|\\{[^{}]*\\})*\\}"),
                                     QRegularExpression::DotMatchesEverythingOption));

    QString out;
    out.reserve(source.size());

    auto it = ruleExpression().globalMatch(source);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString selector = match.captured(1).trimmed();
        if (selector.isEmpty())
            continue;

        // Qt has no generated content, so these rules can only cost time.
        if (selector.contains(QLatin1String("::")) ||
            selector.contains(QLatin1String(":before")) ||
            selector.contains(QLatin1String(":after")))
            continue;

        // Box properties on an element Qt lays out inline are the single most
        // expensive thing a dictionary stylesheet can contain: Qt resolves them
        // for every element it matches and then discards the result, which is
        // most of the second a long Oxford entry takes to appear.
        const bool keepBox = canSelectABlock(selector);

        QStringList keep;
        for (const QString &declaration : match.captured(2).split(QLatin1Char(';'))) {
            const int colon = declaration.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            const QString property = declaration.left(colon).trimmed().toLower();
            if (!usableProperties().contains(property))
                continue;
            if (!keepBox && boxProperties().contains(property))
                continue;
            keep.append(declaration.trimmed());
        }

        if (keep.isEmpty())
            continue;

        out += selector + QLatin1Char('{') + keep.join(QLatin1Char(';')) + QLatin1String("}\n");
    }

    return out;
}

QStringList fontFamilies(const QString &css)
{
    static const QRegularExpression declaration(
        QStringLiteral("\\bfont(?:-family)?\\s*:([^;}]*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression measurement(QStringLiteral("[0-9/]"));

    QStringList families;
    QSet<QString> seen;

    auto it = declaration.globalMatch(css);
    while (it.hasNext()) {
        QString value = it.next().captured(1);
        value.remove(QLatin1Char('"'));
        value.remove(QLatin1Char('\''));
        const int important = value.indexOf(QLatin1Char('!'));
        if (important >= 0)
            value.truncate(important);

        for (const QString &candidate : value.split(QLatin1Char(','))) {
            // The `font` shorthand puts style, weight and size in front of the
            // family list, so nothing up to the last measurement is a name.
            QStringList words = candidate.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (int i = words.size() - 1; i >= 0; --i) {
                if (words.at(i).contains(measurement)) {
                    words = words.mid(i + 1);
                    break;
                }
            }

            const QString family = words.join(QLatin1Char(' '));
            if (family.isEmpty() || seen.contains(family))
                continue;
            seen.insert(family);
            families.append(family);
        }
    }
    return families;
}

QString relevantTo(const QString &css, const QString &html)
{
    if (css.isEmpty() || html.isEmpty())
        return css;

    QSet<QString> elements;
    QSet<QString> classes;

    static const QRegularExpression tagName(QStringLiteral("<\\s*([a-zA-Z][-_a-zA-Z0-9:]*)"));
    auto tags = tagName.globalMatch(html);
    while (tags.hasNext()) {
        QString name = tags.next().captured(1).toLower();
        const int colon = name.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0)
            name = name.mid(colon + 1);
        elements.insert(name);
    }

    static const QRegularExpression classAttribute(
        QStringLiteral("\\bclass\\s*=\\s*[\"']([^\"']*)"), QRegularExpression::CaseInsensitiveOption);
    auto attributes = classAttribute.globalMatch(html);
    while (attributes.hasNext()) {
        const QStringList names =
            attributes.next().captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &name : names)
            classes.insert(name);
    }

    // Wrappers this rendering adds itself.
    elements.insert(QStringLiteral("div"));
    elements.insert(QStringLiteral("body"));

    static const QRegularExpression classToken(QStringLiteral("\\.([_a-zA-Z][-_a-zA-Z0-9]*)"));
    static const QRegularExpression elementToken(QStringLiteral("^([a-zA-Z][-_a-zA-Z0-9]*)"));

    QString out;
    out.reserve(css.size());

    auto it = ruleExpression().globalMatch(css);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();

        bool reachable = false;
        for (const QString &alternative : match.captured(1).split(QLatin1Char(','))) {
            QString flattened = alternative.trimmed();
            flattened.replace(QLatin1Char('>'), QLatin1Char(' '));
            flattened.replace(QLatin1Char('+'), QLatin1Char(' '));
            flattened.replace(QLatin1Char('~'), QLatin1Char(' '));

            const QStringList parts = flattened.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.isEmpty())
                continue;

            // Only the rightmost compound has to exist: if the rule's target is
            // absent, no ancestor can bring it into play.
            QString compound = parts.last();
            const int bracket = compound.indexOf(QLatin1Char('['));
            if (bracket >= 0)
                compound.truncate(bracket);
            const int pseudo = compound.indexOf(QLatin1Char(':'));
            if (pseudo >= 0)
                compound.truncate(pseudo);

            if (compound.isEmpty() || compound == QLatin1String("*")) {
                reachable = true;
                break;
            }

            bool possible = true;
            bool sawClass = false;
            auto tokens = classToken.globalMatch(compound);
            while (tokens.hasNext()) {
                sawClass = true;
                if (!classes.contains(tokens.next().captured(1)))
                    possible = false;
            }

            const QRegularExpressionMatch element = elementToken.match(compound);
            if (element.hasMatch()) {
                if (!elements.contains(element.captured(1).toLower()))
                    possible = false;
            } else if (!sawClass) {
                possible = false; // an id or something else we cannot reason about
            }

            if (possible) {
                reachable = true;
                break;
            }
        }

        if (reachable)
            out += match.captured(0) + QLatin1Char('\n');
    }

    return out;
}

} // namespace cssfilter
} // namespace qmdict
