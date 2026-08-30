// Reader for a single MDict container (.mdx dictionary text or .mdd resource
// archive), covering format versions 1.2 and 2.0.
//
// Memory strategy: only the headword index lives in RAM. Record data stays on
// disk and is inflated on demand into a small LRU cache, so opening a 1 GB
// dictionary costs tens of megabytes rather than gigabytes.
#pragma once

#include "../util/textdecode.h"

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>
#include <QVector>

#include <cstdint>

namespace qmdict {

class MdictFile
{
public:
    MdictFile();
    ~MdictFile();

    MdictFile(const MdictFile &) = delete;
    MdictFile &operator=(const MdictFile &) = delete;

    // Opens `path` and builds (or reloads from `cacheDir`) the headword index.
    bool open(const QString &path, const QString &cacheDir, QString *error);

    QString path() const { return m_path; }
    QString title() const { return m_title; }
    QString description() const { return m_description; }
    QString styleSheet() const { return m_styleSheet; }
    double formatVersion() const { return m_version; }
    bool isResourceArchive() const { return m_isMdd; }

    int entryCount() const { return m_entries.size(); }

    // Headword at `index` in the file's own record order, as UTF-8.
    QByteArray keyAt(int index) const;

    // Headword at `position` in case-folded sorted order.
    int entryAtSortedPosition(int position) const;

    // Record bytes for `index`; empty if the entry cannot be read.
    QByteArray recordAt(int index);

    // Interprets raw record bytes using the dictionary's declared encoding.
    QString decodeText(const QByteArray &raw) const { return m_decoder.decode(raw); }

    // First sorted position whose headword is >= `foldedKey`.
    int lowerBound(const QByteArray &foldedKey) const;

    // Every entry whose headword matches `key` exactly, ignoring ASCII case.
    QVector<int> findExact(const QByteArray &key) const;

    // Up to `limit` sorted positions whose headword starts with `prefix`.
    QVector<int> findPrefix(const QByteArray &prefix, int limit) const;

    // Approximate resident size of the index, for the status bar.
    qint64 indexMemoryUsage() const;

    // Lowercases ASCII so lookups are case-insensitive without a full
    // Unicode collation pass.
    static QByteArray foldKey(const QByteArray &key);

private:
    struct Entry
    {
        quint64 recordOffset; // byte offset into the concatenated record stream
        quint32 keyOffset;    // byte offset into m_keyBlob
    };

    struct RecordBlock
    {
        quint64 filePos;
        quint64 compressedSize;
        quint64 decompressedSize;
        quint64 decompressedStart;
    };

    struct CachedBlock
    {
        int index;
        QByteArray data;
    };

    // Reads exactly `size` bytes, refusing lengths that a corrupt index could
    // inflate into an enormous allocation.
    bool readChunk(qint64 size, QByteArray *out);

    bool readHeader(QString *error);
    bool readKeyIndex(QString *error);
    bool readRecordIndex(QString *error);
    void finaliseIndex();

    QString cachePathFor(const QString &cacheDir) const;
    bool loadIndexCache(const QString &file);
    bool saveIndexCache(const QString &file) const;

    const char *keyData(quint32 offset) const { return m_keyBlob.constData() + offset; }
    QByteArray blockData(int blockIndex);

    QFile m_file;
    QString m_path;
    QString m_title;
    QString m_description;
    QString m_styleSheet;
    QString m_encodingName;
    TextDecoder m_decoder;

    double m_version = 2.0;
    bool m_isMdd = false;
    int m_encryptFlags = 0;
    qint64 m_keySectionPos = 0;
    qint64 m_sourceSize = 0;
    qint64 m_sourceModified = 0;

    QByteArray m_keyBlob;
    QVector<Entry> m_entries;
    QVector<quint32> m_sortedOrder; // empty when the file is already sorted
    QVector<RecordBlock> m_recordBlocks;
    quint64 m_recordTotalSize = 0;

    QList<CachedBlock> m_blockCache;
    qint64 m_blockCacheBytes = 0;
};

} // namespace qmdict
