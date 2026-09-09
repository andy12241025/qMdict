#include "wiktionary.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>

namespace qmdict {
namespace wiktionary {
namespace {

// A word like "run" comes back with seventy-odd senses in one part of speech.
// That is worth showing; a payload an order of magnitude beyond it is not.
constexpr int kMaxDefinitions = 120;

QString encoded(const QString &word)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(word.trimmed()));
}

// Each part of speech opens with a summary sense followed by a nested list
// repeating the senses that also arrive as entries of their own. Keeping the
// summary and dropping the nested copy is what makes the result read as a list.
QString withoutNestedList(const QString &definition)
{
    const qsizetype nested = definition.indexOf(QLatin1String("<ol"), 0, Qt::CaseInsensitive);
    return nested < 0 ? definition : definition.left(nested);
}

// Category markers and the like, which carry no text and would otherwise reach
// the article as stray markup.
QString withoutMetadata(const QString &html)
{
    static const QRegularExpression linkTag(QStringLiteral("<link\\b[^>]*>"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression styleTag(
        QStringLiteral("<style\\b[^>]*>.*?</style\\s*>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    QString out = html;
    out.remove(linkTag);
    out.remove(styleTag);
    return out;
}

// Wiktionary links point at other headwords, so they become lookups rather than
// trips to a browser. Links into the wiki's own namespaces -- glossaries,
// categories, appendices -- are no such thing, and are reduced to their text.
QString rewriteLinks(const QString &html)
{
    static const QRegularExpression namespaced(
        QStringLiteral("<a\\b[^>]*href\\s*=\\s*\"/wiki/[^\"]*:[^\"]*\"[^>]*>(.*?)</a\\s*>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression wikiLink(
        QStringLiteral("href\\s*=\\s*\"/wiki/([^\"#]+)(?:#[^\"]*)?\""),
        QRegularExpression::CaseInsensitiveOption);

    QString out = html;
    out.replace(namespaced, QStringLiteral("\\1"));

    QString rewritten;
    rewritten.reserve(out.size());
    qsizetype at = 0;
    auto it = wikiLink.globalMatch(out);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        rewritten += out.mid(at, match.capturedStart() - at);
        const QString target = QUrl::fromPercentEncoding(match.captured(1).toUtf8());
        rewritten += QStringLiteral("href=\"entry://%1\"").arg(encoded(target));
        at = match.capturedEnd();
    }
    rewritten += out.mid(at);
    return rewritten;
}

QString tidy(const QString &html)
{
    return rewriteLinks(withoutMetadata(html)).trimmed();
}

bool hasText(const QString &html)
{
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    QString text = html;
    text.remove(tag);
    return !text.trimmed().isEmpty();
}

struct Section
{
    QString language;
    QStringList parts; // rendered HTML, one block per part of speech
};

} // namespace

QUrl definitionUrl(const QString &word)
{
    return QUrl(QStringLiteral("https://en.wiktionary.org/api/rest_v1/page/definition/%1")
                    .arg(encoded(word)));
}

QUrl pageUrl(const QString &word)
{
    return QUrl(QStringLiteral("https://en.wiktionary.org/wiki/%1").arg(encoded(word)));
}

QString sourceName()
{
    return QStringLiteral("Wiktionary");
}

QString articleFrom(const QByteArray &json, QString *error)
{
    const auto fail = [error](const QString &reason) {
        if (error)
            *error = reason;
        return QString();
    };

    QJsonParseError parsed{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parsed);
    if (parsed.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("the reply could not be read"));

    const QJsonObject root = document.object();

    // An error is an object too, told apart by carrying a status where the
    // definitions document has nothing but language keys.
    if (root.value(QLatin1String("status")).isDouble()) {
        const QString detail = root.value(QLatin1String("detail")).toString();
        return fail(detail.isEmpty() ? QStringLiteral("no definitions were returned") : detail);
    }

    // Grouped by the language each entry declares rather than by the key it
    // arrived under: a page for an English spelling also carries the
    // Translingual and borrowed senses, and they read better apart.
    // English leads because it is what a reader of this dictionary wants first.
    QMap<QString, Section> sections;
    int rendered = 0;

    for (auto language = root.constBegin(); language != root.constEnd(); ++language) {
        const QJsonArray entries = language.value().toArray();
        for (const QJsonValue &value : entries) {
            const QJsonObject entry = value.toObject();
            const QString name =
                entry.value(QLatin1String("language")).toString(language.key());
            const QString partOfSpeech =
                entry.value(QLatin1String("partOfSpeech")).toString();

            QStringList senses;
            const QJsonArray definitions = entry.value(QLatin1String("definitions")).toArray();
            for (const QJsonValue &item : definitions) {
                if (rendered >= kMaxDefinitions)
                    break;

                const QJsonObject definition = item.toObject();
                const QString sense =
                    tidy(withoutNestedList(definition.value(QLatin1String("definition")).toString()));
                if (!hasText(sense))
                    continue;

                QString rendering = QStringLiteral("<li>") + sense;
                const QJsonArray examples = definition.value(QLatin1String("examples")).toArray();
                for (const QJsonValue &example : examples) {
                    const QString text = tidy(example.toString());
                    if (hasText(text))
                        rendering += QStringLiteral("<br><i>%1</i>").arg(text);
                }
                rendering += QStringLiteral("</li>");

                senses.append(rendering);
                ++rendered;
            }

            if (senses.isEmpty())
                continue;

            Section &section = sections[name.isEmpty() ? language.key() : name];
            section.language = name.isEmpty() ? language.key() : name;
            QString block;
            if (!partOfSpeech.isEmpty())
                block += QStringLiteral("<p><b>%1</b></p>\n").arg(partOfSpeech.toHtmlEscaped());
            block += QStringLiteral("<ol>\n%1\n</ol>").arg(senses.join(QLatin1Char('\n')));
            section.parts.append(block);
        }
    }

    if (sections.isEmpty())
        return fail(QStringLiteral("no definitions were returned"));

    QStringList order = sections.keys();
    const QString english = QStringLiteral("English");
    if (order.removeAll(english) > 0)
        order.prepend(english);

    QString body;
    for (const QString &name : std::as_const(order)) {
        const Section &section = sections.value(name);
        body += QStringLiteral("<h3>%1</h3>\n").arg(section.language.toHtmlEscaped());
        body += section.parts.join(QLatin1Char('\n'));
        body += QLatin1Char('\n');
    }
    return body;
}

} // namespace wiktionary
} // namespace qmdict
