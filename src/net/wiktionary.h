// Turns Wiktionary's definition endpoint into an article the reader can show.
//
// This half is deliberately free of any networking, so what the reply is turned
// into can be checked against saved payloads without a connection.
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace qmdict {
namespace wiktionary {

// Where to ask about `word`. The English Wiktionary is used whatever the word
// is, because it defines terms from every language in English.
QUrl definitionUrl(const QString &word);

// The page a reader would open for `word`, offered when a lookup fails.
QUrl pageUrl(const QString &word);

// The name shown above the article.
QString sourceName();

// Article HTML built from a definition reply. Returns empty, and sets `error`
// to something worth putting in the status bar, when the reply carries no
// definitions.
QString articleFrom(const QByteArray &json, QString *error = nullptr);

} // namespace wiktionary
} // namespace qmdict
