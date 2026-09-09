// Fetches a definition for a word no local dictionary has.
//
// Off unless the reader turns it on: every lookup tells a third party which
// word was searched for, which is the one thing the rest of qMdict never does.
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

signals:
    void finished(const QString &word, const QString &html);
    void failed(const QString &word, const QString &reason);

private:
    void remember(const QString &word, const QString &html);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QString m_pending;

    // Words are looked at again constantly -- Back, Forward, the same search
    // twice -- and the answer does not change while the window is open.
    QHash<QString, QString> m_cache;
    QStringList m_cacheOrder;
};

} // namespace qmdict
