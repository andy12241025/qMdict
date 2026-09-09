#include "youdao.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringList>

namespace qmdict {
namespace youdao {
namespace {

constexpr int kMaxCollinsSenses = 12;
constexpr int kMaxPhrases = 6;
constexpr int kMaxSentencePairs = 3;

QString encoded(const QString &word)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(word.trimmed()));
}

QString unescaped(const QString &text)
{
    QString out = text;
    out.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    out.replace(QLatin1String("&quot;"), QLatin1String("\""));
    out.replace(QLatin1String("&#39;"), QLatin1String("'"));
    out.replace(QLatin1String("&apos;"), QLatin1String("'"));
    out.replace(QLatin1String("&lt;"), QLatin1String("<"));
    out.replace(QLatin1String("&gt;"), QLatin1String(">"));
    // Last, so an escaped ampersand cannot revive the ones above.
    out.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return out;
}

// Reduces a field to the text inside it. Only a tag whose name starts with an
// ASCII letter is markup: Youdao writes its register labels the same way,
// <俚> for slang and <美> for American usage, and those the reader is meant to
// see. Everything is escaped again on the way into the article.
QString plain(const QString &fragment)
{
    static const QRegularExpression dropped(
        QStringLiteral("<(style|script)\\b[^>]*>.*?</\\1\\s*>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tag(QStringLiteral("</?[A-Za-z][^>]*>"));

    QString text = fragment;
    text.remove(dropped);
    text.remove(tag);
    return unescaped(text).simplified();
}

// Youdao wraps a translation as {"l": {"i": "..."}}, where "i" is a string in
// some places and a list of them in others.
QString textFrom(const QJsonValue &node)
{
    QJsonValue value = node;
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.contains(QLatin1String("l")))
            value = object.value(QLatin1String("l"));
        if (value.isObject())
            value = value.toObject().value(QLatin1String("i"));
    }

    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue &item : value.toArray()) {
            const QString text = plain(item.toString());
            if (!text.isEmpty())
                parts.append(text);
        }
        return parts.join(QLatin1Char(' '));
    }
    return plain(value.toString());
}

QString section(const QString &title, const QStringList &items, bool numbered)
{
    if (items.isEmpty())
        return QString();

    const QLatin1String list(numbered ? "ol" : "ul");
    return QStringLiteral("<h3>%1</h3>\n<%2>\n%3\n</%2>\n")
        .arg(title.toHtmlEscaped(), list, items.join(QLatin1Char('\n')));
}

} // namespace

QUrl definitionUrl(const QString &word)
{
    return QUrl(QStringLiteral("https://dict.youdao.com/jsonapi?q=%1").arg(encoded(word)));
}

QUrl pageUrl(const QString &word)
{
    return QUrl(QStringLiteral("https://dict.youdao.com/result?word=%1&lang=en").arg(encoded(word)));
}

QString sourceName()
{
    return QStringLiteral("Youdao 有道");
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
    const QJsonArray entries =
        root.value(QLatin1String("ec")).toObject().value(QLatin1String("word")).toArray();
    const QJsonArray collins = root.value(QLatin1String("collins"))
                                   .toObject()
                                   .value(QLatin1String("collins_entries"))
                                   .toArray();

    // A word Youdao does not have still comes back as a full document, with
    // web guesses and example sentences but no dictionary entry.
    if (entries.isEmpty() && collins.isEmpty())
        return fail(QStringLiteral("no entry"));

    QStringList phonetics;
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QVector<QPair<QString, QLatin1String>> spellings = {
            {QStringLiteral("UK"), QLatin1String("ukphone")},
            {QStringLiteral("US"), QLatin1String("usphone")},
        };
        for (const auto &spelling : spellings) {
            const QString sound = entry.value(spelling.second).toString();
            const QString shown = QStringLiteral("%1 /%2/").arg(spelling.first, sound);
            if (!sound.isEmpty() && !phonetics.contains(shown))
                phonetics.append(shown);
        }
    }
    if (phonetics.isEmpty()) {
        for (const QJsonValue &value : collins) {
            const QString sound = value.toObject().value(QLatin1String("phonetic")).toString();
            if (!sound.isEmpty()) {
                phonetics.append(QStringLiteral("/%1/").arg(sound));
                break;
            }
        }
    }

    int senses = 0;

    QStringList meanings;
    for (const QJsonValue &value : entries) {
        const QJsonArray groups = value.toObject().value(QLatin1String("trs")).toArray();
        for (const QJsonValue &group : groups) {
            const QJsonArray translations =
                group.toObject().value(QLatin1String("tr")).toArray();
            for (const QJsonValue &translation : translations) {
                const QString text = textFrom(translation);
                if (text.isEmpty())
                    continue;
                ++senses;
                meanings.append(QStringLiteral("<li>%1</li>").arg(text.toHtmlEscaped()));
            }
        }
    }

    QStringList collinsSenses;
    for (const QJsonValue &value : collins) {
        const QJsonArray sections = value.toObject()
                                        .value(QLatin1String("entries"))
                                        .toObject()
                                        .value(QLatin1String("entry"))
                                        .toArray();
        for (const QJsonValue &block : sections) {
            const QJsonArray items = block.toObject().value(QLatin1String("tran_entry")).toArray();
            for (const QJsonValue &value : items) {
                const QJsonObject item = value.toObject();
                const QString text = plain(item.value(QLatin1String("tran")).toString());
                if (text.isEmpty() || collinsSenses.size() >= kMaxCollinsSenses)
                    continue;

                const QJsonObject part = item.value(QLatin1String("pos_entry")).toObject();
                QStringList labels;
                const QString pos = part.value(QLatin1String("pos_tips"))
                                        .toString(part.value(QLatin1String("pos")).toString());
                if (!pos.isEmpty())
                    labels.append(pos);
                const QString use = item.value(QLatin1String("registr")).toString();
                if (!use.isEmpty())
                    labels.append(use);

                QString rendering = QStringLiteral("<li>");
                if (!labels.isEmpty())
                    rendering += QStringLiteral("[%1] ").arg(labels.join(QLatin1Char(' ')).toHtmlEscaped());
                rendering += text.toHtmlEscaped();

                const QJsonArray examples = item.value(QLatin1String("exam_sents"))
                                                .toObject()
                                                .value(QLatin1String("sent"))
                                                .toArray();
                if (!examples.isEmpty()) {
                    const QJsonObject example = examples.first().toObject();
                    for (const QLatin1String key : {QLatin1String("eng_sent"), QLatin1String("chn_sent")}) {
                        const QString sentence = plain(example.value(key).toString());
                        if (!sentence.isEmpty())
                            rendering += QStringLiteral("<br><i>%1</i>").arg(sentence.toHtmlEscaped());
                    }
                }

                rendering += QStringLiteral("</li>");
                collinsSenses.append(rendering);
                ++senses;
            }
        }
    }

    QStringList phrases;
    const QJsonArray rawPhrases =
        root.value(QLatin1String("phrs")).toObject().value(QLatin1String("phrs")).toArray();
    for (const QJsonValue &value : rawPhrases) {
        if (phrases.size() >= kMaxPhrases)
            break;
        const QJsonObject phrase = value.toObject().value(QLatin1String("phr")).toObject();
        const QString headword = textFrom(phrase.value(QLatin1String("headword")));
        if (headword.isEmpty())
            continue;

        QStringList glosses;
        for (const QJsonValue &translation : phrase.value(QLatin1String("trs")).toArray()) {
            const QString gloss = textFrom(translation.toObject().value(QLatin1String("tr")));
            if (!gloss.isEmpty())
                glosses.append(gloss);
        }

        QString rendering = QStringLiteral("<li>%1").arg(headword.toHtmlEscaped());
        if (!glosses.isEmpty())
            rendering += QStringLiteral("  %1").arg(glosses.join(QStringLiteral("; ")).toHtmlEscaped());
        phrases.append(rendering + QStringLiteral("</li>"));
    }

    QStringList sentences;
    const QJsonArray pairs = root.value(QLatin1String("blng_sents_part"))
                                 .toObject()
                                 .value(QLatin1String("sentence-pair"))
                                 .toArray();
    for (const QJsonValue &value : pairs) {
        if (sentences.size() >= kMaxSentencePairs)
            break;
        const QJsonObject pair = value.toObject();
        QStringList lines;
        for (const QLatin1String key :
             {QLatin1String("sentence"), QLatin1String("sentence-translation")}) {
            const QString sentence = plain(pair.value(key).toString());
            if (!sentence.isEmpty())
                lines.append(sentence.toHtmlEscaped());
        }
        if (!lines.isEmpty())
            sentences.append(
                QStringLiteral("<li><i>%1</i></li>").arg(lines.join(QStringLiteral("<br>"))));
    }

    if (senses == 0)
        return fail(QStringLiteral("no entry"));

    QString body;
    if (!phonetics.isEmpty())
        body += QStringLiteral("<p>%1</p>\n").arg(phonetics.join(QStringLiteral("&nbsp;&nbsp;")));
    body += section(QStringLiteral("释义"), meanings, true);
    body += section(QStringLiteral("柯林斯"), collinsSenses, true);
    body += section(QStringLiteral("短语"), phrases, false);
    body += section(QStringLiteral("例句"), sentences, false);
    return body;
}

} // namespace youdao
} // namespace qmdict
