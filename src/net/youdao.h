// Turns Youdao's dictionary endpoint into an article the reader can show.
//
// Youdao defines English words in Chinese, which is what most of the offline
// dictionaries here do too, so it is asked first and Wiktionary covers what it
// does not have. Like the Wiktionary reader, this half does no networking, so
// what a reply turns into can be checked against saved payloads.
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace qmdict {
namespace youdao {

QUrl definitionUrl(const QString &word);

// The page a reader would open for `word`.
QUrl pageUrl(const QString &word);

// The name shown above the article.
QString sourceName();

// Article HTML built from a reply. Returns empty, and sets `error`, when the
// reply carries no entry for the word -- which Youdao signals by answering
// with everything except a definition rather than with an error.
QString articleFrom(const QByteArray &json, QString *error = nullptr);

} // namespace youdao
} // namespace qmdict
