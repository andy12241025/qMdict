#include "dictionary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace qmdict {
namespace {

constexpr int kMaxRedirects = 8;

QString normaliseResourceName(const QString &name)
{
    QString path = name;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));

    const int query = path.indexOf(QLatin1Char('?'));
    if (query >= 0)
        path.truncate(query);
    const int fragment = path.indexOf(QLatin1Char('#'));
    if (fragment >= 0)
        path.truncate(fragment);

    if (!path.startsWith(QLatin1Char('/')))
        path.prepend(QLatin1Char('/'));
    return path;
}

} // namespace

Dictionary::Dictionary() = default;
Dictionary::~Dictionary() = default;

bool Dictionary::load(const QString &mdxPath, const QString &cacheDir, QString *error)
{
    auto mdx = std::make_unique<MdictFile>();
    if (!mdx->open(mdxPath, cacheDir, error))
        return false;
    m_mdx = std::move(mdx);

    // StyleSheet is a flat list of triples: id, opening markup, closing markup.
    const QString sheet = m_mdx->styleSheet();
    if (!sheet.isEmpty()) {
        const QStringList lines = sheet.split(QRegularExpression(QStringLiteral("\r\n|\r|\n")));
        for (int i = 0; i + 2 < lines.size(); i += 3)
            m_styleRules.insert(lines.at(i).trimmed(), qMakePair(lines.at(i + 1), lines.at(i + 2)));
    }

    const QFileInfo info(mdxPath);
    const QString base = info.completeBaseName();
    const QDir dir = info.absoluteDir();

    QStringList resourceFiles = dir.entryList({base + QLatin1String(".mdd"),
                                               base + QLatin1String(".*.mdd")},
                                              QDir::Files, QDir::Name);
    for (const QString &file : resourceFiles) {
        auto mdd = std::make_unique<MdictFile>();
        QString mddError;
        if (mdd->open(dir.filePath(file), cacheDir, &mddError))
            m_mdd.push_back(std::move(mdd));
    }

    return true;
}

QString Dictionary::title() const
{
    if (!m_mdx)
        return QString();
    const QString title = m_mdx->title().trimmed();
    return title.isEmpty() ? QFileInfo(m_mdx->path()).completeBaseName() : title;
}

int Dictionary::resourceCount() const
{
    int total = 0;
    for (const auto &mdd : m_mdd)
        total += mdd->entryCount();
    return total;
}

qint64 Dictionary::indexMemoryUsage() const
{
    qint64 total = m_mdx ? m_mdx->indexMemoryUsage() : 0;
    for (const auto &mdd : m_mdd)
        total += mdd->indexMemoryUsage();
    return total;
}

QStringList Dictionary::completions(const QString &prefix, int limit) const
{
    QStringList result;
    if (!m_mdx)
        return result;

    const QVector<int> hits = m_mdx->findPrefix(prefix.toUtf8(), limit);
    result.reserve(hits.size());
    for (int index : hits)
        result.append(QString::fromUtf8(m_mdx->keyAt(index)));
    return result;
}

bool Dictionary::contains(const QString &word) const
{
    return m_mdx && !m_mdx->findExact(word.toUtf8()).isEmpty();
}

QString Dictionary::decodeRecord(const QByteArray &raw) const
{
    QByteArray trimmed = raw;
    while (trimmed.endsWith('\0'))
        trimmed.chop(1);
    return m_mdx->decodeText(trimmed);
}

QString Dictionary::applyStyleSheet(const QString &text) const
{
    if (m_styleRules.isEmpty() || !text.contains(QLatin1Char('`')))
        return text;

    static const QRegularExpression marker(QStringLiteral("`(\\d+)`"));

    QString out;
    out.reserve(text.size() + 64);

    int cursor = 0;
    QString pendingId;
    auto it = marker.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        QString segment = text.mid(cursor, match.capturedStart() - cursor);

        if (pendingId.isEmpty()) {
            out += segment;
        } else {
            const auto rule = m_styleRules.value(pendingId);
            out += rule.first + segment + rule.second;
        }

        pendingId = match.captured(1);
        cursor = match.capturedEnd();
    }

    QString tail = text.mid(cursor);
    if (pendingId.isEmpty()) {
        out += tail;
    } else {
        const auto rule = m_styleRules.value(pendingId);
        out += rule.first + tail + rule.second;
    }

    return out;
}

QString Dictionary::definition(const QString &word)
{
    if (!m_mdx)
        return QString();

    QString current = word;
    for (int hop = 0; hop < kMaxRedirects; ++hop) {
        const QVector<int> hits = m_mdx->findExact(current.toUtf8());
        if (hits.isEmpty())
            return QString();

        QString combined;
        QString redirect;

        for (int index : hits) {
            const QString text = decodeRecord(m_mdx->recordAt(index));
            if (text.startsWith(QLatin1String("@@@LINK="))) {
                if (redirect.isEmpty())
                    redirect = text.mid(8).trimmed();
                continue;
            }
            if (text.trimmed().isEmpty())
                continue;
            if (!combined.isEmpty())
                combined += QStringLiteral("\n<hr class=\"qmdict-sep\">\n");
            combined += applyStyleSheet(text);
        }

        if (!combined.isEmpty())
            return combined;
        if (redirect.isEmpty() || redirect == current)
            return QString();
        current = redirect;
    }

    return QString();
}

QByteArray Dictionary::resource(const QString &name)
{
    if (m_mdd.empty())
        return QByteArray();

    const QString normalised = normaliseResourceName(name);
    const QByteArray key = normalised.toUtf8();

    for (const auto &mdd : m_mdd) {
        QVector<int> hits = mdd->findExact(key);
        if (hits.isEmpty()) {
            // Some archives store paths without the leading separator.
            hits = mdd->findExact(key.mid(1));
        }
        if (!hits.isEmpty())
            return mdd->recordAt(hits.first());
    }

    return QByteArray();
}

QString Dictionary::loadStyleSheet(const QString &name)
{
    const auto cached = m_styleSheets.constFind(name);
    if (cached != m_styleSheets.constEnd())
        return cached.value();

    // Only the file name is used, so a dictionary cannot reach outside its own
    // folder with an href like "../../secrets.css".
    const QString fileName = QFileInfo(normaliseResourceName(name)).fileName();
    QString css;

    const QByteArray packaged = resource(fileName);
    if (!packaged.isEmpty()) {
        css = QString::fromUtf8(packaged);
    } else if (m_mdx) {
        // Plenty of dictionaries ship the stylesheet loose beside the .mdx
        // rather than packing it into the .mdd.
        const QDir dir = QFileInfo(m_mdx->path()).absoluteDir();
        QFile file(dir.filePath(fileName));
        if (file.exists() && file.open(QIODevice::ReadOnly))
            css = QString::fromUtf8(file.readAll());
    }

    m_styleSheets.insert(name, css);
    return css;
}

QString Dictionary::fallbackStyleSheet()
{
    if (m_fallbackProbed)
        return m_fallback;
    m_fallbackProbed = true;

    if (m_mdx) {
        // A .css sitting next to the dictionary, preferring one that shares
        // its base name.
        const QFileInfo info(m_mdx->path());
        const QDir dir = info.absoluteDir();
        QStringList candidates = dir.entryList({QStringLiteral("*.css")}, QDir::Files, QDir::Name);

        const QString preferred = info.completeBaseName() + QLatin1String(".css");
        candidates.removeAll(preferred);
        candidates.prepend(preferred);

        for (const QString &candidate : candidates) {
            QFile file(dir.filePath(candidate));
            if (file.exists() && file.open(QIODevice::ReadOnly)) {
                m_fallback = QString::fromUtf8(file.readAll());
                if (!m_fallback.isEmpty())
                    return m_fallback;
            }
        }
    }

    for (const auto &mdd : m_mdd) {
        const int count = mdd->entryCount();
        for (int position = 0; position < count; ++position) {
            const int index = mdd->entryAtSortedPosition(position);
            if (!QString::fromUtf8(mdd->keyAt(index))
                     .endsWith(QLatin1String(".css"), Qt::CaseInsensitive))
                continue;
            const QByteArray data = mdd->recordAt(index);
            if (!data.isEmpty()) {
                m_fallback = QString::fromUtf8(data);
                return m_fallback;
            }
        }
    }

    return m_fallback;
}

QString Dictionary::styleSheetFor(const QString &articleHtml)
{
    static const QRegularExpression linkTag(
        QStringLiteral("<link\\b[^>]*\\bhref\\s*=\\s*[\"']?([^\"'>\\s]+)"),
        QRegularExpression::CaseInsensitiveOption);

    QString css;
    QSet<QString> seen;

    auto it = linkTag.globalMatch(articleHtml);
    while (it.hasNext()) {
        const QString href = it.next().captured(1);
        if (!href.endsWith(QLatin1String(".css"), Qt::CaseInsensitive) || seen.contains(href))
            continue;
        seen.insert(href);

        const QString resolved = loadStyleSheet(href);
        if (!resolved.isEmpty())
            css += resolved + QLatin1Char('\n');
    }

    // Articles that reference no stylesheet, or whose stylesheet is missing,
    // still usually have one shipped with the dictionary.
    if (css.isEmpty())
        return fallbackStyleSheet();

    return css;
}

} // namespace qmdict
