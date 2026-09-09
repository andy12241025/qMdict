// Fetches a definition for a word no local dictionary has.
//
// Off unless the reader turns it on: every lookup tells a third party which
// word was searched for, which is the one thing the rest of qMdict never does.
//
// Youdao is asked first, because it defines English words in Chinese as the
// dictionaries here mostly do, and Wiktionary covers what it lacks. A source
// that cannot be reached is not allowed to end the search: the next one is
// tried, and only an empty answer from all of them counts as a miss.
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

namespace qmdict {

class OnlineLookup : public QObject
{
    Q_OBJECT

public:
    explicit OnlineLookup(QObject *parent = nullptr);
    ~OnlineLookup() override;

    // Starts a lookup, replacing any request still in flight. `finished` or
    // `failed` follows for `word` unless a later call supersedes it.
    void lookUp(const QString &word);

    // Abandons the request in flight, if any. No signal follows.
    void cancel();

    // The sources that will be tried, in order, for the status bar.
    static QStringList sourceNames();

signals:
    void finished(const QString &word, const QString &source, const QString &html);
    void failed(const QString &word, const QString &reason);

private:
    struct Answer
    {
        QString source;
        QString html; // empty for a word every source came up empty on
    };

    void ask(const QString &word, int source);
    void remember(const QString &word, const Answer &answer);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;

    // Why the sources tried so far did not answer, so a failure can say whether
    // the word is unknown or the network is.
    QStringList m_troubles;
    bool m_answered = false;

    // Words are looked at again constantly -- Back, Forward, the same search
    // twice -- and the answer does not change while the window is open.
    QHash<QString, Answer> m_cache;
    QStringList m_cacheOrder;
};

} // namespace qmdict
