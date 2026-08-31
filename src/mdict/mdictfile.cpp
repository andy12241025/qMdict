#include "mdictfile.h"

#include "../util/codec.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamReader>

#include <algorithm>

namespace qmdict {
namespace {

constexpr quint32 kCacheMagic = 0x514d4449; // "QMDI"
// Bumped to 2 so caches holding an unfiltered placeholder title are discarded.
constexpr quint32 kCacheVersion = 2;
constexpr qint64 kBlockCacheLimit = 16 * 1024 * 1024;

inline char fold(char c)
{
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

int compareFolded(const char *a, const char *b)
{
    for (;;) {
        const unsigned char ca = static_cast<unsigned char>(fold(*a));
        const unsigned char cb = static_cast<unsigned char>(fold(*b));
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (ca == 0)
            return 0;
        ++a;
        ++b;
    }
}

// Orders `key` against `prefix`, treating any key that starts with the prefix
// as equal to it.
int compareFoldedPrefix(const char *key, const char *prefix, int prefixLen)
{
    for (int i = 0; i < prefixLen; ++i) {
        const unsigned char ca = static_cast<unsigned char>(fold(key[i]));
        const unsigned char cb = static_cast<unsigned char>(fold(prefix[i]));
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (ca == 0)
            return -1; // key ended first, so it sorts before the prefix
    }
    return 0;
}

quint64 readBigEndian(const char *p, int width)
{
    quint64 value = 0;
    for (int i = 0; i < width; ++i)
        value = (value << 8) | static_cast<unsigned char>(p[i]);
    return value;
}

} // namespace

MdictFile::MdictFile() = default;
MdictFile::~MdictFile() = default;

bool MdictFile::readChunk(qint64 size, QByteArray *out)
{
    if (size < 0 || size > m_file.size() - m_file.pos())
        return false;

    *out = m_file.read(size);
    return out->size() == size;
}

QByteArray MdictFile::foldKey(const QByteArray &key)
{
    QByteArray out = key;
    for (int i = 0; i < out.size(); ++i)
        out[i] = fold(out.at(i));
    return out;
}

bool MdictFile::open(const QString &path, const QString &cacheDir, QString *error)
{
    m_path = path;
    m_isMdd = QFileInfo(path).suffix().compare(QLatin1String("mdd"), Qt::CaseInsensitive) == 0;

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open file: %1").arg(m_file.errorString());
        return false;
    }

    const QFileInfo info(path);
    m_sourceSize = info.size();
    m_sourceModified = info.lastModified().toMSecsSinceEpoch();

    const QString cacheFile = cachePathFor(cacheDir);
    if (!cacheFile.isEmpty() && loadIndexCache(cacheFile))
        return true;

    if (!readHeader(error))
        return false;
    if (!readKeyIndex(error))
        return false;
    if (!readRecordIndex(error))
        return false;

    finaliseIndex();

    if (!cacheFile.isEmpty())
        saveIndexCache(cacheFile);

    return true;
}

bool MdictFile::readHeader(QString *error)
{
    m_file.seek(0);

    char lengthBytes[4];
    if (m_file.read(lengthBytes, 4) != 4) {
        if (error)
            *error = QStringLiteral("file is truncated");
        return false;
    }

    const quint64 headerLength = readBigEndian(lengthBytes, 4);
    if (headerLength == 0 || headerLength > 64 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("not an MDict file (bad header length)");
        return false;
    }

    QByteArray headerBytes;
    if (!readChunk(qint64(headerLength), &headerBytes)) {
        if (error)
            *error = QStringLiteral("file is truncated in the header");
        return false;
    }

    // The header is UTF-16LE and ends with a NUL code unit that is not part of
    // the XML document.
    int textBytes = headerBytes.size();
    if (textBytes >= 2)
        textBytes -= 2;
    const QString headerXml =
        QString::fromUtf16(reinterpret_cast<const char16_t *>(headerBytes.constData()), textBytes / 2);

    QXmlStreamReader xml(headerXml);
    bool found = false;
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement) {
            const QXmlStreamAttributes attrs = xml.attributes();
            m_title = attrs.value(QLatin1String("Title")).toString();
            m_description = attrs.value(QLatin1String("Description")).toString();
            m_styleSheet = attrs.value(QLatin1String("StyleSheet")).toString();
            m_encodingName = attrs.value(QLatin1String("Encoding")).toString();
            m_version = attrs.value(QLatin1String("GeneratedByEngineVersion")).toDouble();
            if (m_version <= 0.0)
                m_version = attrs.value(QLatin1String("RequiredEngineVersion")).toDouble();
            if (m_version <= 0.0)
                m_version = 2.0;

            const QString encrypted = attrs.value(QLatin1String("Encrypted")).toString();
            if (encrypted.isEmpty() || encrypted.compare(QLatin1String("No"), Qt::CaseInsensitive) == 0)
                m_encryptFlags = 0;
            else if (encrypted.compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0)
                m_encryptFlags = 1;
            else
                m_encryptFlags = encrypted.toInt();

            found = true;
            break;
        }
    }

    if (!found) {
        if (error)
            *error = QStringLiteral("not an MDict file (unreadable header)");
        return false;
    }

    if (m_encryptFlags & 0x01) {
        if (error)
            *error = QStringLiteral("dictionary is encrypted and needs a registration key");
        return false;
    }

    // Resource archives always store their paths as UTF-16, whatever the
    // header claims.
    m_decoder = TextDecoder(m_isMdd ? QStringLiteral("UTF-16") : m_encodingName);

    // MdxBuilder leaves its own field labels in the header when the author
    // never filled them in, so "Title (No HTML code allowed)" is a placeholder
    // rather than a name. Fall back to the file name in that case.
    const QString trimmedTitle = m_title.trimmed();
    if (trimmedTitle.isEmpty() ||
        trimmedTitle.contains(QLatin1String("No HTML code allowed"), Qt::CaseInsensitive) ||
        trimmedTitle.compare(QLatin1String("Title"), Qt::CaseInsensitive) == 0) {
        m_title = QFileInfo(m_path).completeBaseName();
    }

    if (m_description.trimmed().contains(QLatin1String("HTML code allowed"), Qt::CaseInsensitive))
        m_description.clear();

    m_keySectionPos = qint64(4 + headerLength + 4);
    return true;
}

bool MdictFile::readKeyIndex(QString *error)
{
    const bool v2 = m_version >= 2.0;
    const int numWidth = v2 ? 8 : 4;
    const int headerNumbers = v2 ? 5 : 4;

    if (m_keySectionPos >= m_file.size() || !m_file.seek(m_keySectionPos)) {
        if (error)
            *error = QStringLiteral("truncated key section");
        return false;
    }

    QByteArray numbers;
    if (!readChunk(qint64(headerNumbers) * numWidth, &numbers)) {
        if (error)
            *error = QStringLiteral("truncated key section");
        return false;
    }
    if (v2)
        m_file.read(4); // Adler-32 over the numbers above

    int at = 0;
    auto next = [&]() {
        const quint64 v = readBigEndian(numbers.constData() + at, numWidth);
        at += numWidth;
        return v;
    };

    const quint64 keyBlockCount = next();
    next(); // total entry count, recomputed while parsing
    const quint64 infoDecompressedSize = v2 ? next() : 0;
    const quint64 infoSize = next();
    const quint64 keyBlocksSize = next();

    QByteArray info;
    if (!readChunk(qint64(infoSize), &info)) {
        if (error)
            *error = QStringLiteral("truncated key block index");
        return false;
    }

    if (v2) {
        if (m_encryptFlags & 0x02)
            decryptKeyBlockInfo(info.data(), std::size_t(info.size()));
        info = decodeBlock(info.constData(), std::size_t(info.size()), std::size_t(infoDecompressedSize));
        if (info.isEmpty()) {
            if (error)
                *error = QStringLiteral("cannot decode the key block index");
            return false;
        }
    }

    const int textWidth = m_decoder.unitSize();
    const int textTerm = v2 ? 1 : 0;
    const int sizeWidth = v2 ? 2 : 1;

    // Each index entry occupies at least this much, so a count larger than the
    // index can hold means the file is corrupt.
    const int minimumEntrySize = numWidth * 3 + sizeWidth * 2;
    if (keyBlockCount == 0 || keyBlockCount > quint64(info.size() / minimumEntrySize)) {
        if (error)
            *error = QStringLiteral("implausible key block count");
        return false;
    }

    struct BlockSizes
    {
        quint64 compressed;
        quint64 decompressed;
    };
    QVector<BlockSizes> keyBlocks;
    keyBlocks.reserve(int(keyBlockCount));

    int pos = 0;
    for (quint64 b = 0; b < keyBlockCount; ++b) {
        auto need = [&](int bytes) { return pos + bytes <= info.size(); };

        if (!need(numWidth))
            break;
        pos += numWidth; // entries in this block

        if (!need(sizeWidth))
            break;
        const quint64 headSize = readBigEndian(info.constData() + pos, sizeWidth);
        pos += sizeWidth;
        pos += int((headSize + textTerm) * textWidth);
        if (pos < 0 || pos > info.size())
            break;

        if (!need(sizeWidth))
            break;
        const quint64 tailSize = readBigEndian(info.constData() + pos, sizeWidth);
        pos += sizeWidth;
        pos += int((tailSize + textTerm) * textWidth);
        if (pos < 0 || pos > info.size())
            break;

        if (!need(numWidth * 2))
            break;
        BlockSizes sizes;
        sizes.compressed = readBigEndian(info.constData() + pos, numWidth);
        pos += numWidth;
        sizes.decompressed = readBigEndian(info.constData() + pos, numWidth);
        pos += numWidth;
        keyBlocks.append(sizes);
    }

    if (keyBlocks.size() != int(keyBlockCount)) {
        if (error)
            *error = QStringLiteral("malformed key block index");
        return false;
    }

    const qint64 keyBlocksStart = m_file.pos();
    if (keyBlocksSize > quint64(m_file.size() - keyBlocksStart)) {
        if (error)
            *error = QStringLiteral("truncated key blocks");
        return false;
    }

    // A rough reservation keeps the blob from being reallocated on every block.
    m_keyBlob.reserve(int(qMin<quint64>(keyBlocksSize * 2, 256u * 1024u * 1024u)) + 1024);

    for (const BlockSizes &sizes : keyBlocks) {
        QByteArray raw;
        if (!readChunk(qint64(sizes.compressed), &raw)) {
            if (error)
                *error = QStringLiteral("truncated key blocks");
            return false;
        }

        const QByteArray block = decodeBlock(raw.constData(), std::size_t(raw.size()),
                                             std::size_t(sizes.decompressed));
        if (block.isEmpty()) {
            if (error)
                *error = QStringLiteral("cannot decompress a key block");
            return false;
        }

        int p = 0;
        while (p + numWidth <= block.size()) {
            const quint64 recordOffset = readBigEndian(block.constData() + p, numWidth);
            p += numWidth;

            int end = p;
            if (textWidth == 1) {
                while (end < block.size() && block.at(end) != '\0')
                    ++end;
            } else {
                while (end + 1 < block.size() && (block.at(end) != '\0' || block.at(end + 1) != '\0'))
                    end += 2;
            }
            if (end >= block.size())
                break;

            QByteArray key = m_decoder.toUtf8(block.constData() + p, end - p);
            if (m_isMdd)
                key.replace('\\', '/');

            Entry entry{};
            entry.recordOffset = recordOffset;
            entry.keyOffset = quint32(m_keyBlob.size());
            m_entries.append(entry);

            m_keyBlob.append(key);
            m_keyBlob.append('\0');

            p = end + textWidth;
        }
    }

    m_file.seek(keyBlocksStart + qint64(keyBlocksSize));
    m_keyBlob.squeeze();
    m_entries.squeeze();
    return true;
}

bool MdictFile::readRecordIndex(QString *error)
{
    const bool v2 = m_version >= 2.0;
    const int numWidth = v2 ? 8 : 4;

    QByteArray numbers;
    if (!readChunk(qint64(4) * numWidth, &numbers)) {
        if (error)
            *error = QStringLiteral("truncated record section");
        return false;
    }

    int at = 0;
    auto next = [&]() {
        const quint64 v = readBigEndian(numbers.constData() + at, numWidth);
        at += numWidth;
        return v;
    };

    const quint64 blockCount = next();
    next(); // entry count
    const quint64 infoSize = next();
    next(); // total compressed record size

    QByteArray info;
    if (!readChunk(qint64(infoSize), &info)) {
        if (error)
            *error = QStringLiteral("truncated record block index");
        return false;
    }

    if (blockCount > quint64(info.size() / (numWidth * 2))) {
        if (error)
            *error = QStringLiteral("implausible record block count");
        return false;
    }

    qint64 filePos = m_file.pos();
    quint64 decompressedStart = 0;

    m_recordBlocks.reserve(int(blockCount));
    int pos = 0;
    for (quint64 b = 0; b < blockCount; ++b) {
        if (pos + numWidth * 2 > info.size())
            break;

        RecordBlock block{};
        block.filePos = quint64(filePos);
        block.compressedSize = readBigEndian(info.constData() + pos, numWidth);
        pos += numWidth;
        block.decompressedSize = readBigEndian(info.constData() + pos, numWidth);
        pos += numWidth;
        block.decompressedStart = decompressedStart;

        filePos += qint64(block.compressedSize);
        decompressedStart += block.decompressedSize;
        m_recordBlocks.append(block);
    }

    if (m_recordBlocks.size() != int(blockCount)) {
        if (error)
            *error = QStringLiteral("malformed record block index");
        return false;
    }

    m_recordTotalSize = decompressedStart;
    return true;
}

void MdictFile::finaliseIndex()
{
    const int n = m_entries.size();

    bool alreadySorted = true;
    for (int i = 1; i < n; ++i) {
        if (compareFolded(keyData(m_entries.at(i - 1).keyOffset), keyData(m_entries.at(i).keyOffset)) > 0) {
            alreadySorted = false;
            break;
        }
    }

    if (alreadySorted) {
        m_sortedOrder.clear();
        m_sortedOrder.squeeze();
        return;
    }

    m_sortedOrder.resize(n);
    for (int i = 0; i < n; ++i)
        m_sortedOrder[i] = quint32(i);

    const char *blob = m_keyBlob.constData();
    const Entry *entries = m_entries.constData();
    std::stable_sort(m_sortedOrder.begin(), m_sortedOrder.end(), [blob, entries](quint32 a, quint32 b) {
        return compareFolded(blob + entries[a].keyOffset, blob + entries[b].keyOffset) < 0;
    });
}

QByteArray MdictFile::keyAt(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return QByteArray();
    return QByteArray(keyData(m_entries.at(index).keyOffset));
}

int MdictFile::entryAtSortedPosition(int position) const
{
    if (position < 0 || position >= m_entries.size())
        return -1;
    return m_sortedOrder.isEmpty() ? position : int(m_sortedOrder.at(position));
}

int MdictFile::lowerBound(const QByteArray &foldedKey) const
{
    int low = 0;
    int high = m_entries.size();
    const char *needle = foldedKey.constData();

    while (low < high) {
        const int mid = low + (high - low) / 2;
        const int entry = entryAtSortedPosition(mid);
        if (compareFolded(keyData(m_entries.at(entry).keyOffset), needle) < 0)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}

QVector<int> MdictFile::findExact(const QByteArray &key) const
{
    QVector<int> result;
    const QByteArray needle = foldKey(key);

    for (int p = lowerBound(needle); p < m_entries.size(); ++p) {
        const int entry = entryAtSortedPosition(p);
        if (compareFolded(keyData(m_entries.at(entry).keyOffset), needle.constData()) != 0)
            break;
        result.append(entry);
    }
    return result;
}

QVector<int> MdictFile::findPrefix(const QByteArray &prefix, int limit) const
{
    QVector<int> result;
    if (prefix.isEmpty() || limit <= 0)
        return result;

    const QByteArray needle = foldKey(prefix);
    const char *raw = needle.constData();
    const int length = needle.size();

    int low = 0;
    int high = m_entries.size();
    while (low < high) {
        const int mid = low + (high - low) / 2;
        const int entry = entryAtSortedPosition(mid);
        if (compareFoldedPrefix(keyData(m_entries.at(entry).keyOffset), raw, length) < 0)
            low = mid + 1;
        else
            high = mid;
    }

    for (int p = low; p < m_entries.size() && result.size() < limit; ++p) {
        const int entry = entryAtSortedPosition(p);
        if (compareFoldedPrefix(keyData(m_entries.at(entry).keyOffset), raw, length) != 0)
            break;
        result.append(entry);
    }
    return result;
}

QByteArray MdictFile::blockData(int blockIndex)
{
    for (int i = 0; i < m_blockCache.size(); ++i) {
        if (m_blockCache.at(i).index == blockIndex) {
            if (i != 0)
                m_blockCache.move(i, 0);
            return m_blockCache.at(0).data;
        }
    }

    const RecordBlock &block = m_recordBlocks.at(blockIndex);
    if (!m_file.isOpen() && !m_file.open(QIODevice::ReadOnly))
        return QByteArray();

    if (!m_file.seek(qint64(block.filePos)))
        return QByteArray();

    QByteArray raw;
    if (!readChunk(qint64(block.compressedSize), &raw))
        return QByteArray();

    QByteArray data = decodeBlock(raw.constData(), std::size_t(raw.size()),
                                  std::size_t(block.decompressedSize));
    if (data.isEmpty())
        return QByteArray();

    m_blockCache.prepend({blockIndex, data});
    m_blockCacheBytes += data.size();
    while (m_blockCache.size() > 1 && m_blockCacheBytes > kBlockCacheLimit) {
        m_blockCacheBytes -= m_blockCache.last().data.size();
        m_blockCache.removeLast();
    }

    return data;
}

QByteArray MdictFile::recordAt(int index)
{
    if (index < 0 || index >= m_entries.size() || m_recordBlocks.isEmpty())
        return QByteArray();

    const quint64 start = m_entries.at(index).recordOffset;
    quint64 end = (index + 1 < m_entries.size()) ? m_entries.at(index + 1).recordOffset
                                                 : m_recordTotalSize;
    if (end <= start)
        end = m_recordTotalSize;
    if (start >= m_recordTotalSize)
        return QByteArray();
    end = qMin(end, m_recordTotalSize);

    // Locate the block containing `start`.
    int low = 0;
    int high = m_recordBlocks.size() - 1;
    while (low < high) {
        const int mid = low + (high - low + 1) / 2;
        if (m_recordBlocks.at(mid).decompressedStart <= start)
            low = mid;
        else
            high = mid - 1;
    }

    QByteArray result;
    for (int b = low; b < m_recordBlocks.size(); ++b) {
        const RecordBlock &block = m_recordBlocks.at(b);
        if (block.decompressedStart >= end)
            break;

        const QByteArray data = blockData(b);
        if (data.isEmpty())
            break;

        const quint64 blockEnd = block.decompressedStart + quint64(data.size());
        const quint64 from = qMax(start, block.decompressedStart);
        const quint64 to = qMin(end, blockEnd);
        if (to <= from)
            break;

        // A record fully inside one block is the common case; avoid the copy.
        if (result.isEmpty() && to == end)
            return data.mid(int(from - block.decompressedStart), int(to - from));

        result.append(data.constData() + (from - block.decompressedStart), int(to - from));
    }

    return result;
}

qint64 MdictFile::indexMemoryUsage() const
{
    return qint64(m_keyBlob.capacity()) + qint64(m_entries.capacity()) * qint64(sizeof(Entry)) +
           qint64(m_sortedOrder.capacity()) * 4 +
           qint64(m_recordBlocks.capacity()) * qint64(sizeof(RecordBlock)) + m_blockCacheBytes;
}

QString MdictFile::cachePathFor(const QString &cacheDir) const
{
    if (cacheDir.isEmpty())
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QFileInfo(m_path).absoluteFilePath().toUtf8());
    const QString name = QString::fromLatin1(hash.result().toHex().left(16));

    return QDir(cacheDir).filePath(name + QLatin1String(".idx"));
}

bool MdictFile::loadIndexCache(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return false;

    QDataStream stream(&in);
    stream.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0;
    quint32 version = 0;
    qint64 size = 0;
    qint64 modified = 0;
    stream >> magic >> version >> size >> modified;
    if (magic != kCacheMagic || version != kCacheVersion || size != m_sourceSize ||
        modified != m_sourceModified)
        return false;

    quint32 entryCount = 0;
    quint32 blockCount = 0;
    bool hasOrder = false;

    stream >> m_title >> m_description >> m_styleSheet >> m_encodingName;
    stream >> m_version >> m_isMdd >> m_recordTotalSize;
    stream >> m_keyBlob;
    stream >> entryCount >> blockCount >> hasOrder;

    if (stream.status() != QDataStream::Ok || entryCount > 200u * 1000u * 1000u)
        return false;

    m_entries.resize(int(entryCount));
    if (entryCount > 0) {
        const int bytes = int(entryCount) * int(sizeof(Entry));
        if (stream.readRawData(reinterpret_cast<char *>(m_entries.data()), bytes) != bytes)
            return false;
    }

    if (hasOrder) {
        m_sortedOrder.resize(int(entryCount));
        const int bytes = int(entryCount) * 4;
        if (bytes > 0 && stream.readRawData(reinterpret_cast<char *>(m_sortedOrder.data()), bytes) != bytes)
            return false;
    } else {
        m_sortedOrder.clear();
    }

    m_recordBlocks.resize(int(blockCount));
    if (blockCount > 0) {
        const int bytes = int(blockCount) * int(sizeof(RecordBlock));
        if (stream.readRawData(reinterpret_cast<char *>(m_recordBlocks.data()), bytes) != bytes)
            return false;
    }

    if (stream.status() != QDataStream::Ok)
        return false;

    m_decoder = TextDecoder(m_isMdd ? QStringLiteral("UTF-16") : m_encodingName);
    return true;
}

bool MdictFile::saveIndexCache(const QString &file) const
{
    QDir().mkpath(QFileInfo(file).absolutePath());

    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;

    QDataStream stream(&out);
    stream.setVersion(QDataStream::Qt_6_0);

    stream << kCacheMagic << kCacheVersion << m_sourceSize << m_sourceModified;
    stream << m_title << m_description << m_styleSheet << m_encodingName;
    stream << m_version << m_isMdd << m_recordTotalSize;
    stream << m_keyBlob;
    stream << quint32(m_entries.size()) << quint32(m_recordBlocks.size())
           << bool(!m_sortedOrder.isEmpty());

    if (!m_entries.isEmpty())
        stream.writeRawData(reinterpret_cast<const char *>(m_entries.constData()),
                            m_entries.size() * int(sizeof(Entry)));
    if (!m_sortedOrder.isEmpty())
        stream.writeRawData(reinterpret_cast<const char *>(m_sortedOrder.constData()),
                            m_sortedOrder.size() * 4);
    if (!m_recordBlocks.isEmpty())
        stream.writeRawData(reinterpret_cast<const char *>(m_recordBlocks.constData()),
                            m_recordBlocks.size() * int(sizeof(RecordBlock)));

    return out.commit();
}

} // namespace qmdict
