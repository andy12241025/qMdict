#include "htmlblocks.h"

#include <QList>
#include <QRegularExpression>
#include <QStringList>

namespace qmdict {
namespace htmlblocks {
namespace {

// Elements Qt's rich text engine already knows. Their layout is decided by the
// engine, and forcing a wrapper around them risks changing meaning (an <img>
// or a <td> must stay where it is).
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

// Elements that never have a closing tag.
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

QString stripComments(const QString &css)
{
    static const QRegularExpression comment(QStringLiteral("/\\*.*?\\*/"),
                                            QRegularExpression::DotMatchesEverythingOption);
    QString out = css;
    out.remove(comment);
    return out;
}

// The rule applies to the rightmost compound of the selector: in
// "x-g-blk cf-blk" the target is cf-blk.
void collectTargets(const QString &selector, BlockRules *rules)
{
    static const QRegularExpression classToken(QStringLiteral("\\.([_a-zA-Z][-_a-zA-Z0-9]*)"));
    static const QRegularExpression elementToken(QStringLiteral("^([a-zA-Z][-_a-zA-Z0-9]*)"));

    for (const QString &alternative : selector.split(QLatin1Char(','))) {
        QString flattened = alternative.trimmed();
        flattened.replace(QLatin1Char('>'), QLatin1Char(' '));
        flattened.replace(QLatin1Char('+'), QLatin1Char(' '));
        flattened.replace(QLatin1Char('~'), QLatin1Char(' '));

        const QStringList parts = flattened.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;

        QString compound = parts.last();

        // ::before and :after style generated content, not the element box, so
        // they must not make the element itself a block.
        if (compound.contains(QLatin1Char(':')))
            continue;

        // Attribute selectors carry no layout information for us.
        const int bracket = compound.indexOf(QLatin1Char('['));
        if (bracket >= 0)
            compound.truncate(bracket);

        auto classes = classToken.globalMatch(compound);
        while (classes.hasNext())
            rules->classes.insert(classes.next().captured(1));

        const QRegularExpressionMatch element = elementToken.match(compound);
        if (element.hasMatch()) {
            const QString name = element.captured(1).toLower();
            // Standard elements are Qt's business; only custom ones need help.
            if (!standardElements().contains(name))
                rules->elements.insert(name);
        }
    }
}

struct OpenElement
{
    QString name;
    bool wrapped;
};

} // namespace

BlockRules rulesFromStyleSheet(const QString &css)
{
    BlockRules rules;
    if (css.isEmpty())
        return rules;

    static const QRegularExpression rule(QStringLiteral("([^{}]+)\\{([^{}]*)\\}"));
    static const QRegularExpression display(
        QStringLiteral("(?:^|;)\\s*display\\s*:\\s*([^;!]+)"),
        QRegularExpression::CaseInsensitiveOption);

    const QString clean = stripComments(css);

    auto it = rule.globalMatch(clean);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QRegularExpressionMatch declaration = display.match(match.captured(2));
        if (!declaration.hasMatch() || !isBlockDisplay(declaration.captured(1)))
            continue;

        const QString selector = match.captured(1);
        if (selector.contains(QLatin1Char('@'))) // @media and friends
            continue;

        collectTargets(selector, &rules);
    }

    return rules;
}

QString adaptForTextDocument(const QString &html, const BlockRules &rules)
{
    if (html.isEmpty())
        return html;

    static const QRegularExpression classAttribute(
        QStringLiteral("\\bclass\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"),
        QRegularExpression::CaseInsensitiveOption);

    QString out;
    out.reserve(html.size() + html.size() / 4);

    QList<OpenElement> open;

    int i = 0;
    const int n = html.size();

    while (i < n) {
        if (html.at(i) != QLatin1Char('<')) {
            out += html.at(i);
            ++i;
            continue;
        }

        const int close = html.indexOf(QLatin1Char('>'), i);
        if (close < 0) {
            out += html.mid(i);
            break;
        }

        QString tag = html.mid(i, close - i + 1);
        i = close + 1;

        // Anything that is not an element: comments, doctypes, stray '<'.
        if (tag.size() < 3 || (!tag.at(1).isLetter() && tag.at(1) != QLatin1Char('/'))) {
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

        // <xhtml:br> is a line break Qt would otherwise throw away, and
        // <xhtml:a> a link it would not follow.
        const int colon = name.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0) {
            name = name.mid(colon + 1);
            tag = (closing ? QStringLiteral("</") : QStringLiteral("<")) + name +
                  tag.mid(nameEnd);
        }

        const QString key = name.toLower();

        if (closing) {
            if (isVoidElement(key))
                continue; // </br> and friends: meaningless, drop them

            int match = -1;
            for (int at = open.size() - 1; at >= 0; --at) {
                if (open.at(at).name == key) {
                    match = at;
                    break;
                }
            }

            if (match < 0) {
                out += tag; // stray close tag, leave it for Qt to ignore
                continue;
            }

            // Close anything the dictionary left open inside this element.
            while (open.size() > match + 1) {
                const OpenElement inner = open.takeLast();
                out += QStringLiteral("</%1>").arg(inner.name);
                if (inner.wrapped)
                    out += QStringLiteral("</div>");
            }

            const OpenElement self = open.takeLast();
            out += tag;
            if (self.wrapped)
                out += QStringLiteral("</div>");
            continue;
        }

        bool wrap = rules.elements.contains(key);
        if (!wrap && !rules.classes.isEmpty()) {
            const QRegularExpressionMatch attribute = classAttribute.match(tag);
            if (attribute.hasMatch()) {
                QString value = attribute.captured(1);
                if (value.isEmpty())
                    value = attribute.captured(2);
                if (value.isEmpty())
                    value = attribute.captured(3);
                for (const QString &candidate : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                    if (rules.classes.contains(candidate)) {
                        wrap = true;
                        break;
                    }
                }
            }
        }

        // A wrapper around a void element would never be closed.
        const bool selfClosing = tag.endsWith(QLatin1String("/>"));
        if (isVoidElement(key) || selfClosing) {
            out += tag;
            continue;
        }

        if (wrap)
            out += QLatin1String("<div>");
        out += tag;
        open.append({key, wrap});
    }

    // Balance anything the dictionary never closed.
    while (!open.isEmpty()) {
        const OpenElement inner = open.takeLast();
        out += QStringLiteral("</%1>").arg(inner.name);
        if (inner.wrapped)
            out += QStringLiteral("</div>");
    }

    return out;
}

} // namespace htmlblocks
} // namespace qmdict
