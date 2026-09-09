#include "onlinelookup.h"

#include "wiktionary.h"
#include "youdao.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace qmdict {
namespace {

// Per source, so a source that is blocked rather than merely slow does not eat
// the whole wait before the next one is tried.
constexpr int kTimeoutMs = 5000;
constexpr int kCacheLimit = 200;

struct Source
{
    QString (*name)();
    QUrl (*url)(const QString &);
    QString (*article)(const QByteArray &, QString *);
};

const QVector<Source> &sources()
{
    static const QVector<Source> known = {
        {&youdao::sourceName, &youdao::definitionUrl, &youdao::articleFrom},
        {&wiktionary::sourceName, &wiktionary::definitionUrl, &wiktionary::articleFrom},
    };
    return known;
}

// Wikimedia asks that clients say who they are and how to reach them, and
// answers anonymous requests less generously.
QByteArray userAgent()
{
    return QStringLiteral("qMdict/%1 (https://github.com/andy12241025/qMdict)")
        .arg(QCoreApplication::applicationVersion())
        .toLatin1();
}

} // namespace

OnlineLookup::OnlineLookup(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

OnlineLookup::~OnlineLookup()
{
    cancel();
}

QStringList OnlineLookup::sourceNames()
{
    QStringList names;
    for (const Source &source : sources())
        names.append(source.name());
    return names;
}

void OnlineLookup::cancel()
{
    m_troubles.clear();
    m_answered = false;

    if (!m_reply)
        return;

    // Detached first, so aborting does not come back as a failure for a word
    // nobody is waiting on any more.
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void OnlineLookup::remember(const QString &word, const Answer &answer)
{
    if (!m_cache.contains(word))
        m_cacheOrder.append(word);
    m_cache.insert(word, answer);

    while (m_cacheOrder.size() > kCacheLimit)
        m_cache.remove(m_cacheOrder.takeFirst());
}

void OnlineLookup::lookUp(const QString &word)
{
    const QString trimmed = word.trimmed();
    if (trimmed.isEmpty())
        return;

    cancel();

    const auto cached = m_cache.constFind(trimmed);
    if (cached != m_cache.constEnd()) {
        const Answer answer = cached.value();
        // Queued, so a caller gets the answer the same way whether it came from
        // the network or from here.
        QTimer::singleShot(0, this, [this, trimmed, answer]() {
            if (answer.html.isEmpty())
                emit failed(trimmed, QStringLiteral("no online entry"));
            else
                emit finished(trimmed, answer.source, answer.html);
        });
        return;
    }

    ask(trimmed, 0);
}

void OnlineLookup::ask(const QString &word, int index)
{
    if (index >= sources().size()) {
        // Every source came up empty, so the word is worth remembering as a
        // miss. One that could not be reached says nothing about the word.
        if (m_answered && m_troubles.isEmpty())
            remember(word, Answer{});

        emit failed(word, m_troubles.isEmpty() ? QStringLiteral("no online entry")
                                               : m_troubles.join(QStringLiteral("; ")));
        return;
    }

    const Source &source = sources().at(index);

    QNetworkRequest request(source.url(word));
    request.setRawHeader("User-Agent", userAgent());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, word, index]() {
        // Superseded while in flight, and already disconnected and abandoned.
        if (m_reply != reply)
            return;

        m_reply = nullptr;
        reply->deleteLater();

        const Source &asked = sources().at(index);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        const QString name = asked.name();

        // A 404 is an answer: this source does not have the word. Anything else
        // going wrong means it could not be asked, which is worth saying.
        if (reply->error() != QNetworkReply::NoError && status != 404) {
            m_troubles.append(QStringLiteral("%1: %2").arg(name, reply->errorString()));
            ask(word, index + 1);
            return;
        }

        m_answered = true;

        if (status == 404) {
            ask(word, index + 1);
            return;
        }

        QString error;
        const QString html = asked.article(payload, &error);
        if (html.isEmpty()) {
            ask(word, index + 1);
            return;
        }

        remember(word, Answer{name, html});
        emit finished(word, name, html);
    });
}

} // namespace qmdict
