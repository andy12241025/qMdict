#include "htmlblocks.h"

#include <QRegularExpression>
#include <QStringList>

namespace qmdict {
namespace htmlblocks {
namespace {

bool isBlockDisplay(const QString &value)
{
    const QString v = value.trimmed().toLower();
    // inline-block is deliberately absent: it does not start a new line, and
    // promoting it would break entries apart mid-sentence.
    return v == QLatin1String("block") || v == QLatin1String("list-item") ||
           v == QLatin1String("table") || v == QLatin1String("table-row") ||
           v == QLatin1String("table-caption") || v == QLatin1String("flex") ||
           v == QLatin1String("grid");
}

// The classes a selector actually targets are those on its rightmost
// compound: in ".entry .def" the rule applies to .def.
void collectTargets(const QString &selector, BlockRules *rules)
{
    static const QRegularExpression classToken(QStringLiteral("\\.([_a-zA-Z][-_a-zA-Z0-9]*)"));

    for (const QString &alternative : selector.split(QLatin1Char(','))) {
        QString rightmost = alternative.trimmed();
        rightmost.replace(QLatin1Char('>'), QLatin1Char(' '));
        rightmost.replace(QLatin1Char('+'), QLatin1Char(' '));
        rightmost.replace(QLatin1Char('~'), QLatin1Char(' '));

        const QStringList parts = rightmost.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;
        const QString compound = parts.last();

        auto it = classToken.globalMatch(compound);
        bool sawClass = false;
        while (it.hasNext()) {
            rules->classes.insert(it.next().captured(1));
            sawClass = true;
        }

        if (!sawClass && compound.compare(QLatin1String("span"), Qt::CaseInsensitive) == 0)
            rules->everySpan = true;
    }
}

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

    auto it = rule.globalMatch(css);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QRegularExpressionMatch declaration = display.match(match.captured(2));
        if (!declaration.hasMatch() || !isBlockDisplay(declaration.captured(1)))
            continue;

        QString selector = match.captured(1);
        // Skip at-rules such as @media, whose "selector" is not one.
        if (selector.contains(QLatin1Char('@')))
            continue;
        collectTargets(selector, &rules);
    }

    return rules;
}

QString promoteSpans(const QString &html, const BlockRules &rules)
{
    if (rules.isEmpty() || html.isEmpty())
        return html;

    static const QRegularExpression classAttribute(
        QStringLiteral("\\bclass\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"),
        QRegularExpression::CaseInsensitiveOption);

    QString out;
    out.reserve(html.size() + html.size() / 8);

    // Tracks, for each open <span>, whether it was rewritten, so the matching
    // close tag gets the same treatment.
    QList<bool> openSpans;

    int i = 0;
    const int n = html.size();

    while (i < n) {
        const QChar c = html.at(i);
        if (c != QLatin1Char('<')) {
            out += c;
            ++i;
            continue;
        }

        const int close = html.indexOf(QLatin1Char('>'), i);
        if (close < 0) {
            out += html.mid(i);
            break;
        }

        const QString tag = html.mid(i, close - i + 1);

        // The character after the name must end it, so <spanish> and
        // </spanish> are not mistaken for spans.
        auto nameEndsAt = [&tag](int at) {
            return tag.size() > at && (tag.at(at).isSpace() || tag.at(at) == QLatin1Char('>') ||
                                       tag.at(at) == QLatin1Char('/'));
        };

        const bool isOpen =
            tag.startsWith(QLatin1String("<span"), Qt::CaseInsensitive) && nameEndsAt(5);
        const bool isClose =
            tag.startsWith(QLatin1String("</span"), Qt::CaseInsensitive) && nameEndsAt(6);

        if (isOpen) {
            bool promote = rules.everySpan;
            if (!promote) {
                const QRegularExpressionMatch attribute = classAttribute.match(tag);
                if (attribute.hasMatch()) {
                    QString value = attribute.captured(1);
                    if (value.isEmpty())
                        value = attribute.captured(2);
                    if (value.isEmpty())
                        value = attribute.captured(3);
                    for (const QString &name : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                        if (rules.classes.contains(name)) {
                            promote = true;
                            break;
                        }
                    }
                }
            }

            const bool selfClosing = tag.endsWith(QLatin1String("/>"));
            if (promote)
                out += QLatin1String("<div") + tag.mid(5);
            else
                out += tag;

            if (!selfClosing)
                openSpans.append(promote);
        } else if (isClose) {
            const bool promoted = openSpans.isEmpty() ? false : openSpans.takeLast();
            out += promoted ? QLatin1String("</div>") : QLatin1String("</span>");
        } else {
            out += tag;
        }

        i = close + 1;
    }

    return out;
}

} // namespace htmlblocks
} // namespace qmdict
