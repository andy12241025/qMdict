#include "library.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QMap>

#include <algorithm>

namespace qmdict {

LibraryLoader::LibraryLoader(const QString &root, const QString &cacheDir, QObject *parent)
    : QThread(parent)
    , m_root(root)
    , m_cacheDir(cacheDir)
{
}

void LibraryLoader::run()
{
    QStringList found;
    QDirIterator it(m_root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (m_stop.load())
            return;
        if (path.endsWith(QLatin1String(".mdx"), Qt::CaseInsensitive))
            found.append(path);
    }

    std::sort(found.begin(), found.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).completeBaseName().localeAwareCompare(QFileInfo(b).completeBaseName()) < 0;
    });

    emit discovered(found.size());

    for (const QString &path : std::as_const(found)) {
        if (m_stop.load())
            return;

        auto dictionary = new Dictionary;
        QString error;
        if (dictionary->load(path, m_cacheDir, &error)) {
            emit loaded(dictionary, path, QString());
        } else {
            delete dictionary;
            emit loaded(nullptr, path, error);
        }
    }
}

Library::Library(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<qmdict::Dictionary *>("qmdict::Dictionary*");
}

Library::~Library()
{
    cancelLoading();
}

void Library::loadFolder(const QString &root)
{
    cancelLoading();

    m_dictionaries.clear();
    m_root = root;
    m_expected = 0;
    m_done = 0;
    m_failed = 0;

    m_loader = new LibraryLoader(root, m_cacheDir, this);
    connect(m_loader, &LibraryLoader::discovered, this, &Library::handleDiscovered);
    connect(m_loader, &LibraryLoader::loaded, this, &Library::handleLoaded);
    connect(m_loader, &QThread::finished, this, &Library::handleFinished);
    m_loader->start(QThread::LowPriority);
}

bool Library::isLoading() const
{
    return m_loader != nullptr && m_loader->isRunning();
}

void Library::cancelLoading()
{
    if (!m_loader)
        return;

    m_loader->requestStop();
    m_loader->wait();
    m_loader->deleteLater();
    m_loader = nullptr;
}

void Library::handleDiscovered(int total)
{
    if (sender() != m_loader)
        return;

    m_expected = total;
    emit loadingStarted(total);
}

void Library::handleLoaded(qmdict::Dictionary *dictionary, const QString &path, const QString &error)
{
    // A loader cancelled by a folder switch may still have signals in flight.
    // Their dictionaries have no owner yet, so drop them here.
    if (sender() != m_loader) {
        delete dictionary;
        return;
    }

    ++m_done;

    if (dictionary) {
        m_dictionaries.emplace_back(dictionary);
        emit loadingProgress(m_done, m_expected, dictionary->title());
    } else {
        ++m_failed;
        emit dictionaryFailed(path, error);
        emit loadingProgress(m_done, m_expected, QFileInfo(path).fileName());
    }
}

void Library::handleFinished()
{
    if (sender() != m_loader)
        return;

    m_loader->deleteLater();
    m_loader = nullptr;
    emit loadingFinished(int(m_dictionaries.size()), m_failed);
}

Dictionary *Library::at(int index) const
{
    if (index < 0 || index >= int(m_dictionaries.size()))
        return nullptr;
    return m_dictionaries[std::size_t(index)].get();
}

QVector<Dictionary *> Library::dictionaries() const
{
    QVector<Dictionary *> result;
    result.reserve(int(m_dictionaries.size()));
    for (const auto &dictionary : m_dictionaries)
        result.append(dictionary.get());
    return result;
}

qint64 Library::indexMemoryUsage() const
{
    qint64 total = 0;
    for (const auto &dictionary : m_dictionaries)
        total += dictionary->indexMemoryUsage();
    return total;
}

int Library::totalEntryCount() const
{
    int total = 0;
    for (const auto &dictionary : m_dictionaries)
        total += dictionary->entryCount();
    return total;
}

QStringList Library::suggestions(const QString &prefix, int limit) const
{
    // Keyed by the folded form so the same headword from several dictionaries
    // collapses into one row, while preserving a stable sorted order.
    QMap<QByteArray, QString> merged;

    for (const auto &dictionary : m_dictionaries) {
        if (!dictionary->isEnabled())
            continue;
        const QStringList words = dictionary->completions(prefix, limit);
        for (const QString &word : words) {
            const QByteArray key = MdictFile::foldKey(word.toUtf8());
            if (!merged.contains(key))
                merged.insert(key, word);
        }
    }

    QStringList result;
    result.reserve(qMin(limit, merged.size()));
    for (auto it = merged.constBegin(); it != merged.constEnd() && result.size() < limit; ++it)
        result.append(it.value());
    return result;
}

bool Library::contains(const QString &word) const
{
    if (word.isEmpty())
        return false;

    for (const auto &dictionary : m_dictionaries) {
        if (dictionary->isEnabled() && dictionary->contains(word))
            return true;
    }
    return false;
}

QVector<Library::Match> Library::lookup(const QString &word)
{
    QVector<Match> matches;
    if (word.isEmpty())
        return matches;

    for (const auto &dictionary : m_dictionaries) {
        if (!dictionary->isEnabled())
            continue;
        const QString html = dictionary->definition(word);
        if (!html.isEmpty())
            matches.append({dictionary.get(), html});
    }
    return matches;
}

} // namespace qmdict
