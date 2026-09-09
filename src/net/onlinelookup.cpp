#include "onlinelookup.h"

#include "wiktionary.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace qmdict {
namespace {

constexpr int kTimeoutMs = 8000;
constexpr int kCacheLimit = 200;

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

void OnlineLookup::cancel()
{
    m_pending.clear();
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

void OnlineLookup::remember(const QString &word, const QString &html)
{
    if (!m_cache.contains(word))
        m_cacheOrder.append(word);
    m_cache.insert(word, html);

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
        const QString html = cached.value();
        // Queued, so a caller gets the answer the same way whether it came from
        // the network or from here.
        QTimer::singleShot(0, this, [this, trimmed, html]() {
            if (html.isEmpty())
                emit failed(trimmed, QStringLiteral("no definitions were found"));
            else
                emit finished(trimmed, html);
        });
        return;
    }

    QNetworkRequest request(wiktionary::definitionUrl(trimmed));
    request.setRawHeader("User-Agent", userAgent());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTimeoutMs);

    m_pending = trimmed;
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, trimmed]() {
        // Superseded while in flight, and already disconnected and abandoned.
        if (m_reply != reply)
            return;

        m_reply = nullptr;
        m_pending.clear();
        reply->deleteLater();

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();

        if (reply->error() != QNetworkReply::NoError && status != 404) {
            emit failed(trimmed, reply->errorString());
            return;
        }

        if (status == 404) {
            // Remembered as a miss, so the same word does not go out again.
            remember(trimmed, QString());
            emit failed(trimmed, QStringLiteral("no definitions were found"));
            return;
        }

        QString error;
        const QString html = wiktionary::articleFrom(payload, &error);
        if (html.isEmpty()) {
            remember(trimmed, QString());
            emit failed(trimmed, error);
            return;
        }

        remember(trimmed, html);
        emit finished(trimmed, html);
    });
}

} // namespace qmdict
