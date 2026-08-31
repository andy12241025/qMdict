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

// The rule applies to the rightmost compound of the selector: in
// "pron-g-blk brelabel" the target is brelabel.
void collectTargets(const QString &selector, QSet<QString> *classes, QSet<QString> *elements)
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

        // ::before and :after style generated content, not the element box.
        // An escaped colon, as in "xhtml\:br", is not a pseudo-element, but
        // treating it as one simply leaves the element alone, which is safe.
        if (compound.contains(QLatin1Char(':')))
            continue;

        const int bracket = compound.indexOf(QLatin1Char('['));
        if (bracket >= 0)
            compound.truncate(bracket);

        auto tokens = classToken.globalMatch(compound);
        while (tokens.hasNext())
            classes->insert(tokens.next().captured(1));

        const QRegularExpressionMatch element = elementToken.match(compound);
        if (element.hasMatch()) {
            const QString name = element.captured(1).toLower();
            if (!standardElements().contains(name))
                elements->insert(name);
        }
    }
}

struct OpenElement
{
    QString name;
    bool wrapped = false;
    bool hidden = false;
    bool unwrapped = false;
};

} // namespace

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
    static const QRegularExpression comment(QStringLiteral("/\\*.*?\\*/"),
                                            QRegularExpression::DotMatchesEverythingOption);

    QString clean = css;
    clean.remove(comment);

    auto it = rule.globalMatch(clean);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QRegularExpressionMatch declaration = display.match(match.captured(2));
        if (!declaration.hasMatch())
            continue;

        const QString selector = match.captured(1);
        if (selector.contains(QLatin1Char('@'))) // @media and friends
            continue;

        const QString value = declaration.captured(1).trimmed().toLower();
        if (isBlockDisplay(value))
            collectTargets(selector, &rules.blockClasses, &rules.blockElements);
        else if (value == QLatin1String("none"))
            collectTargets(selector, &rules.hiddenClasses, &rules.hiddenElements);
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

    int i = 0;
    const int n = html.size();

    while (i < n) {
        if (html.at(i) != QLatin1Char('<')) {
            if (suppressed == 0)
                out += html.at(i);
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
                const OpenElement inner = open.takeLast();
                if (inner.hidden)
                    --suppressed;
                else if (suppressed == 0 && !inner.unwrapped)
                    out += QStringLiteral("</%1>").arg(inner.name);
                if (inner.wrapped && suppressed == 0)
                    out += QStringLiteral("</div>");
            }

            const OpenElement self = open.takeLast();
            if (self.hidden) {
                --suppressed;
                continue;
            }
            if (suppressed == 0) {
                if (!self.unwrapped)
                    out += tag;
                if (self.wrapped)
                    out += QStringLiteral("</div>");
            }
            continue;
        }

        // --- opening tag -------------------------------------------------
        QStringList classes;
        const QRegularExpressionMatch classMatch = classAttribute.match(tag);
        if (classMatch.hasMatch())
            classes = attributeValue(classMatch).split(QLatin1Char(' '), Qt::SkipEmptyParts);

        auto mentions = [&classes](const QSet<QString> &set) {
            for (const QString &candidate : classes) {
                if (set.contains(candidate))
                    return true;
            }
            return false;
        };

        const bool hidden = rules.hiddenElements.contains(key) || mentions(rules.hiddenClasses);
        const bool selfClosing = tag.endsWith(QLatin1String("/>"));

        if (hidden) {
            // Void elements have no content to skip and no close tag to wait
            // for, so they simply vanish.
            if (!isVoidElement(key) && !selfClosing) {
                open.append({key, false, true, false});
                ++suppressed;
            }
            continue;
        }

        if (suppressed > 0) {
            if (!isVoidElement(key) && !selfClosing)
                open.append({key, false, false, false});
            continue;
        }

        // A link that goes nowhere should not look like a link.
        bool unwrap = false;
        if (key == QLatin1String("a")) {
            const QRegularExpressionMatch href = hrefAttribute.match(tag);
            unwrap = !href.hasMatch() || !isNavigableHref(attributeValue(href));
        }

        const bool wrap =
            rules.blockElements.contains(key) || mentions(rules.blockClasses);

        if (isVoidElement(key) || selfClosing) {
            out += tag;
            continue;
        }

        if (wrap)
            out += QLatin1String("<div>");
        if (!unwrap)
            out += tag;
        open.append({key, wrap, false, unwrap});
    }

    // Balance anything the dictionary never closed.
    while (!open.isEmpty()) {
        const OpenElement inner = open.takeLast();
        if (inner.hidden) {
            --suppressed;
            continue;
        }
        if (suppressed > 0)
            continue;
        if (!inner.unwrapped)
            out += QStringLiteral("</%1>").arg(inner.name);
        if (inner.wrapped)
            out += QStringLiteral("</div>");
    }

    return out;
}

} // namespace htmlblocks
} // namespace qmdict
