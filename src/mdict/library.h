// Owns every dictionary found under a root folder and fans queries out to them.
#pragma once

#include "dictionary.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <atomic>
#include <memory>

namespace qmdict {

// Walks a folder tree on a worker thread so the window stays responsive while
// multi-gigabyte dictionaries are indexed.
class LibraryLoader : public QThread
{
    Q_OBJECT

public:
    LibraryLoader(const QString &root, const QString &cacheDir, QObject *parent = nullptr);

    void requestStop() { m_stop.store(true); }

signals:
    void discovered(int total);
    void loaded(qmdict::Dictionary *dictionary, const QString &path, const QString &error);

protected:
    void run() override;

private:
    QString m_root;
    QString m_cacheDir;
    std::atomic<bool> m_stop{false};
};

class Library : public QObject
{
    Q_OBJECT

public:
    struct Match
    {
        Dictionary *dictionary;
        QString html;
    };

    explicit Library(QObject *parent = nullptr);
    ~Library() override;

    void setCacheDirectory(const QString &dir) { m_cacheDir = dir; }
    QString rootFolder() const { return m_root; }

    // Discovers and opens every .mdx under `root`, recursively.
    void loadFolder(const QString &root);
    bool isLoading() const;
    void cancelLoading();

    int count() const { return int(m_dictionaries.size()); }
    Dictionary *at(int index) const;
    QVector<Dictionary *> dictionaries() const;

    qint64 indexMemoryUsage() const;
    int totalEntryCount() const;

    // Headwords from all enabled dictionaries, merged and de-duplicated.
    QStringList suggestions(const QString &prefix, int limit) const;

    // Definitions for `word` from every enabled dictionary that has it.
    QVector<Match> lookup(const QString &word);

    // Whether any enabled dictionary has `word`. Only consults the headword
    // index, so it costs nothing next to building the article.
    bool contains(const QString &word) const;

signals:
    void loadingStarted(int total);
    void loadingProgress(int done, int total, const QString &name);
    void loadingFinished(int loaded, int failed);
    void dictionaryFailed(const QString &path, const QString &error);

private slots:
    void handleDiscovered(int total);
    void handleLoaded(qmdict::Dictionary *dictionary, const QString &path, const QString &error);
    void handleFinished();

private:
    std::vector<std::unique_ptr<Dictionary>> m_dictionaries;
    QString m_root;
    QString m_cacheDir;
    LibraryLoader *m_loader = nullptr;
    int m_expected = 0;
    int m_done = 0;
    int m_failed = 0;
};

} // namespace qmdict
