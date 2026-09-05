// Self-contained checks for the MDict reader.
//
// There is no redistributable sample dictionary, so the fixtures are written
// here byte by byte in both container versions and read back through the
// public API.

#include "../src/audio/audiodecoder.h"
#include "../src/audio/oggstream.h"
#include "../src/mdict/library.h"
#include "../src/mdict/mdictfile.h"
#include "../src/ui/darkcolours.h"
#include "../src/ui/cssfilter.h"
#include "../src/ui/htmlblocks.h"
#include "../src/util/lzo1x.h"
#include "../src/util/ripemd128.h"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTextDocument>

#include <speex/speex.h>
#include <speex/speex_header.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using qmdict::lzo1xDecompress;
using qmdict::ripemd128;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

void checkEqual(const QByteArray &actual, const QByteArray &expected, const char *what)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n", what,
                     expected.toPercentEncoding().constData(), actual.toPercentEncoding().constData());
    }
}

QByteArray bigEndian(quint64 value, int width)
{
    QByteArray out(width, '\0');
    for (int i = width - 1; i >= 0; --i) {
        out[i] = char(value & 0xff);
        value >>= 8;
    }
    return out;
}

quint32 adler32(const QByteArray &data)
{
    quint32 a = 1;
    quint32 b = 0;
    for (char c : data) {
        a = (a + static_cast<unsigned char>(c)) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// One MDict block: 4-byte compression type, 4-byte Adler-32, then payload.
QByteArray zlibBlock(const QByteArray &raw)
{
    // qCompress emits a 4-byte size prefix ahead of the zlib stream; MDict
    // stores the bare stream.
    const QByteArray compressed = qCompress(raw, 6).mid(4);

    // Compression tag is little-endian; the Adler-32 that follows is not.
    QByteArray block = QByteArrayLiteral("\x02\x00\x00\x00");
    block += bigEndian(adler32(raw), 4);
    block += compressed;
    return block;
}

QByteArray encodeKey(const QString &key, bool utf16)
{
    if (!utf16)
        return key.toUtf8();

    QByteArray out;
    for (QChar c : key) {
        out.append(char(c.unicode() & 0xff));
        out.append(char((c.unicode() >> 8) & 0xff));
    }
    return out;
}

struct FixtureEntry
{
    QString key;
    QByteArray record;
};

struct FixtureOptions
{
    double version = 2.0;
    bool utf16 = false;      // key/record encoding
    bool isMdd = false;      // affects only the declared Encoding attribute
    int keysPerBlock = 2;
    int recordBlockSplit = -1; // byte offset to force a record across two blocks
    int recordChunkSize = -1;  // fixed record block size, as real writers use
    QString title = QStringLiteral("Fixture");
};

// Assembles a complete MDX/MDD container from `entries`, which must already be
// in the order the reader should see them.
QByteArray buildFixture(const std::vector<FixtureEntry> &entries, const FixtureOptions &options)
{
    const bool v2 = options.version >= 2.0;
    const int numWidth = v2 ? 8 : 4;
    const int sizeWidth = v2 ? 2 : 1;
    const int textTerm = v2 ? 1 : 0;
    const int textWidth = options.utf16 ? 2 : 1;

    const QString encoding = options.isMdd ? QString() : (options.utf16 ? QStringLiteral("UTF-16")
                                                                        : QStringLiteral("UTF-8"));
    const QString headerXml =
        QStringLiteral("<Dictionary GeneratedByEngineVersion=\"%1\" RequiredEngineVersion=\"%1\" "
                       "Encrypted=\"No\" Encoding=\"%2\" Format=\"Html\" KeyCaseSensitive=\"No\" "
                       "Title=\"%3\" Description=\"test\"/>")
            .arg(QString::number(options.version, 'f', 1), encoding, options.title);

    QByteArray headerBytes;
    for (QChar c : headerXml) {
        headerBytes.append(char(c.unicode() & 0xff));
        headerBytes.append(char((c.unicode() >> 8) & 0xff));
    }
    headerBytes.append('\0');
    headerBytes.append('\0');

    QByteArray out;
    out += bigEndian(quint64(headerBytes.size()), 4);
    out += headerBytes;
    out += QByteArray(4, '\0'); // header checksum, not verified by the reader

    // --- key blocks -------------------------------------------------------
    QByteArray recordStream;
    std::vector<quint64> recordOffsets;
    for (const FixtureEntry &entry : entries) {
        recordOffsets.push_back(quint64(recordStream.size()));
        recordStream += entry.record;
    }

    QByteArray keyBlockInfo;
    QByteArray keyBlocks;
    std::size_t at = 0;
    while (at < entries.size()) {
        const std::size_t end = qMin(at + std::size_t(options.keysPerBlock), entries.size());

        QByteArray plain;
        for (std::size_t i = at; i < end; ++i) {
            plain += bigEndian(recordOffsets[i], numWidth);
            plain += encodeKey(entries[i].key, options.utf16);
            plain += QByteArray(textWidth, '\0');
        }

        const QByteArray block = zlibBlock(plain);
        const QByteArray head = encodeKey(entries[at].key, options.utf16);
        const QByteArray tail = encodeKey(entries[end - 1].key, options.utf16);

        keyBlockInfo += bigEndian(quint64(end - at), numWidth);
        keyBlockInfo += bigEndian(quint64(head.size() / textWidth), sizeWidth);
        keyBlockInfo += head + QByteArray(textTerm * textWidth, '\0');
        keyBlockInfo += bigEndian(quint64(tail.size() / textWidth), sizeWidth);
        keyBlockInfo += tail + QByteArray(textTerm * textWidth, '\0');
        keyBlockInfo += bigEndian(quint64(block.size()), numWidth);
        keyBlockInfo += bigEndian(quint64(plain.size()), numWidth);

        keyBlocks += block;
        at = end;
    }

    const int keyBlockCount = int((entries.size() + options.keysPerBlock - 1) / options.keysPerBlock);
    const QByteArray encodedInfo = v2 ? zlibBlock(keyBlockInfo) : keyBlockInfo;

    QByteArray numbers;
    numbers += bigEndian(quint64(keyBlockCount), numWidth);
    numbers += bigEndian(quint64(entries.size()), numWidth);
    if (v2)
        numbers += bigEndian(quint64(keyBlockInfo.size()), numWidth);
    numbers += bigEndian(quint64(encodedInfo.size()), numWidth);
    numbers += bigEndian(quint64(keyBlocks.size()), numWidth);

    out += numbers;
    if (v2)
        out += QByteArray(4, '\0'); // checksum over `numbers`
    out += encodedInfo;
    out += keyBlocks;

    // --- record blocks ----------------------------------------------------
    std::vector<QByteArray> chunks;
    if (options.recordChunkSize > 0) {
        for (int offset = 0; offset < recordStream.size(); offset += options.recordChunkSize)
            chunks.push_back(recordStream.mid(offset, options.recordChunkSize));
    } else if (options.recordBlockSplit > 0 && options.recordBlockSplit < recordStream.size()) {
        chunks.push_back(recordStream.left(options.recordBlockSplit));
        chunks.push_back(recordStream.mid(options.recordBlockSplit));
    } else {
        chunks.push_back(recordStream);
    }

    QByteArray recordInfo;
    QByteArray recordBlocks;
    for (const QByteArray &chunk : chunks) {
        const QByteArray block = zlibBlock(chunk);
        recordInfo += bigEndian(quint64(block.size()), numWidth);
        recordInfo += bigEndian(quint64(chunk.size()), numWidth);
        recordBlocks += block;
    }

    out += bigEndian(quint64(chunks.size()), numWidth);
    out += bigEndian(quint64(entries.size()), numWidth);
    out += bigEndian(quint64(recordInfo.size()), numWidth);
    out += bigEndian(quint64(recordBlocks.size()), numWidth);
    out += recordInfo;
    out += recordBlocks;

    return out;
}

bool writeFixture(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(data) == data.size();
}

QByteArray hex(const uint8_t *digest)
{
    return QByteArray(reinterpret_cast<const char *>(digest), 16).toHex();
}

void testRipemd128()
{
    uint8_t digest[16];

    ripemd128("", 0, digest);
    checkEqual(hex(digest), "cdf26213a150dc3ecb610f18f6b38b46", "ripemd128 of empty string");

    ripemd128("a", 1, digest);
    checkEqual(hex(digest), "86be7afa339d0fc7cfc785e72f578d33", "ripemd128 of \"a\"");

    ripemd128("abc", 3, digest);
    checkEqual(hex(digest), "c14a12199c66e4ba84636b0f69144c77", "ripemd128 of \"abc\"");

    ripemd128("message digest", 14, digest);
    checkEqual(hex(digest), "9e327b3d6e523062afc1132d7df9d1b8", "ripemd128 of \"message digest\"");

    const QByteArray alphabet = "abcdefghijklmnopqrstuvwxyz";
    ripemd128(alphabet.constData(), std::size_t(alphabet.size()), digest);
    checkEqual(hex(digest), "fd2aa607f71dc8f510714922b371834e", "ripemd128 of the alphabet");

    // 64 bytes exactly: exercises the two-block padding path.
    const QByteArray sixtyFour(64, 'x');
    ripemd128(sixtyFour.constData(), 64, digest);
    check(hex(digest).size() == 32, "ripemd128 handles a full final block");
}

void testLzo1x()
{
    // Literal run of five bytes followed by the end-of-stream marker.
    const uint8_t literals[] = {0x16, 'h', 'e', 'l', 'l', 'o', 0x11, 0x00, 0x00};
    char out[64];
    std::size_t produced = sizeof(out);
    check(lzo1xDecompress(literals, sizeof(literals), reinterpret_cast<uint8_t *>(out), &produced),
          "lzo1x decodes a literal run");
    checkEqual(QByteArray(out, int(produced)), QByteArrayLiteral("hello"),
               "lzo1x literal run contents");

    // Same, then an M3 match copying five bytes from a distance of five.
    const uint8_t withMatch[] = {0x16, 'a', 'b', 'c', 'd', 'e', 0x23, 0x10, 0x00, 0x11, 0x00, 0x00};
    produced = sizeof(out);
    check(lzo1xDecompress(withMatch, sizeof(withMatch), reinterpret_cast<uint8_t *>(out), &produced),
          "lzo1x decodes a back-reference");
    checkEqual(QByteArray(out, int(produced)), QByteArrayLiteral("abcdeabcde"),
               "lzo1x back-reference contents");

    // Truncated input must be rejected rather than read past the buffer.
    produced = sizeof(out);
    check(!lzo1xDecompress(literals, 3, reinterpret_cast<uint8_t *>(out), &produced),
          "lzo1x rejects truncated input");
}

std::vector<FixtureEntry> sampleEntries()
{
    return {
        {QStringLiteral("apple"), QByteArrayLiteral("<b>apple</b> a round fruit")},
        {QStringLiteral("Apricot"), QByteArrayLiteral("<b>apricot</b> a small fruit")},
        {QStringLiteral("banana"), QByteArrayLiteral("<b>banana</b> a long yellow fruit")},
        {QStringLiteral("blueberry"), QByteArrayLiteral("@@@LINK=banana")},
        {QStringLiteral("cherry"), QByteArrayLiteral("<b>cherry</b> a small stone fruit")},
    };
}

void testDictionaryVersion(const QString &dir, double version, bool splitRecords)
{
    const QString label = QStringLiteral("v%1%2")
                              .arg(version, 0, 'f', 1)
                              .arg(splitRecords ? QStringLiteral(" split") : QString());
    const QByteArray tag = label.toUtf8();

    FixtureOptions options;
    options.version = version;
    options.keysPerBlock = 2;
    if (splitRecords)
        options.recordBlockSplit = 60; // lands inside the "banana" record

    const std::vector<FixtureEntry> entries = sampleEntries();
    const QString path = QDir(dir).filePath(QStringLiteral("fixture-%1%2.mdx")
                                                .arg(version, 0, 'f', 1)
                                                .arg(splitRecords ? QStringLiteral("-split")
                                                                  : QString()));

    check(writeFixture(path, buildFixture(entries, options)), (tag + " fixture written").constData());

    qmdict::MdictFile file;
    QString error;
    if (!file.open(path, dir, &error)) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s could not be opened: %s\n", tag.constData(),
                     error.toUtf8().constData());
        return;
    }

    check(file.entryCount() == int(entries.size()), (tag + " entry count").constData());
    check(file.title() == QLatin1String("Fixture"), (tag + " title from header").constData());

    // Case-insensitive exact lookup.
    const QVector<int> hits = file.findExact(QByteArrayLiteral("APRICOT"));
    check(hits.size() == 1, (tag + " exact lookup ignores case").constData());
    if (!hits.isEmpty())
        checkEqual(file.recordAt(hits.first()), QByteArrayLiteral("<b>apricot</b> a small fruit"),
                   (tag + " record contents").constData());

    // A record that straddles two blocks must be stitched back together.
    const QVector<int> banana = file.findExact(QByteArrayLiteral("banana"));
    check(banana.size() == 1, (tag + " finds banana").constData());
    if (!banana.isEmpty())
        checkEqual(file.recordAt(banana.first()),
                   QByteArrayLiteral("<b>banana</b> a long yellow fruit"),
                   (tag + " record spanning blocks").constData());

    // Last record in the file, whose length comes from the total stream size.
    const QVector<int> cherry = file.findExact(QByteArrayLiteral("cherry"));
    if (!cherry.isEmpty())
        checkEqual(file.recordAt(cherry.first()),
                   QByteArrayLiteral("<b>cherry</b> a small stone fruit"),
                   (tag + " final record").constData());

    // Prefix search spans key blocks and is sorted case-insensitively.
    const QVector<int> prefix = file.findPrefix(QByteArrayLiteral("b"), 10);
    check(prefix.size() == 2, (tag + " prefix search count").constData());
    if (prefix.size() == 2) {
        checkEqual(file.keyAt(prefix.at(0)), QByteArrayLiteral("banana"),
                   (tag + " prefix search order").constData());
        checkEqual(file.keyAt(prefix.at(1)), QByteArrayLiteral("blueberry"),
                   (tag + " prefix search second hit").constData());
    }

    check(file.findExact(QByteArrayLiteral("durian")).isEmpty(),
          (tag + " missing word returns nothing").constData());
    check(file.findPrefix(QByteArrayLiteral("a"), 10).size() == 2,
          (tag + " prefix matches both a-words").constData());
}

void testIndexCache(const QString &dir)
{
    FixtureOptions options;
    const QString path = QDir(dir).filePath(QStringLiteral("cached.mdx"));
    check(writeFixture(path, buildFixture(sampleEntries(), options)), "cache fixture written");

    const QString cacheDir = QDir(dir).filePath(QStringLiteral("idx"));
    QDir().mkpath(cacheDir);

    QString error;
    {
        qmdict::MdictFile first;
        check(first.open(path, cacheDir, &error), "first open builds the index");
    }

    check(!QDir(cacheDir).entryList({QStringLiteral("*.idx")}, QDir::Files).isEmpty(),
          "index cache file is written");

    // The cache must produce an identical index on the next open.
    qmdict::MdictFile second;
    check(second.open(path, cacheDir, &error), "second open reuses the cache");
    check(second.entryCount() == 5, "cached entry count");
    check(second.title() == QLatin1String("Fixture"), "cached title");

    const QVector<int> hits = second.findExact(QByteArrayLiteral("cherry"));
    check(hits.size() == 1, "cached index still resolves lookups");
    if (!hits.isEmpty())
        checkEqual(second.recordAt(hits.first()),
                   QByteArrayLiteral("<b>cherry</b> a small stone fruit"), "cached record contents");
}

void testUnsortedKeys(const QString &dir)
{
    // Real dictionaries are not always stored in the order we search in.
    std::vector<FixtureEntry> entries = {
        {QStringLiteral("zebra"), QByteArrayLiteral("striped horse")},
        {QStringLiteral("ant"), QByteArrayLiteral("small insect")},
        {QStringLiteral("Moose"), QByteArrayLiteral("large deer")},
    };

    const QString path = QDir(dir).filePath(QStringLiteral("unsorted.mdx"));
    FixtureOptions options;
    options.keysPerBlock = 3;
    check(writeFixture(path, buildFixture(entries, options)), "unsorted fixture written");

    qmdict::MdictFile file;
    QString error;
    check(file.open(path, QString(), &error), "unsorted fixture opens");

    checkEqual(file.keyAt(file.entryAtSortedPosition(0)), QByteArrayLiteral("ant"),
               "unsorted keys are re-ordered");
    checkEqual(file.keyAt(file.entryAtSortedPosition(2)), QByteArrayLiteral("zebra"),
               "unsorted keys keep their records");

    const QVector<int> hits = file.findExact(QByteArrayLiteral("moose"));
    check(hits.size() == 1, "lookup works after re-ordering");
    if (!hits.isEmpty())
        checkEqual(file.recordAt(hits.first()), QByteArrayLiteral("large deer"),
                   "record survives re-ordering");
}

void testResourceArchive(const QString &dir)
{
    std::vector<FixtureEntry> entries = {
        {QStringLiteral("\\logo.png"), QByteArrayLiteral("\x89PNG-not-really")},
        {QStringLiteral("\\style.css"), QByteArrayLiteral("body { color: red; }")},
    };

    FixtureOptions options;
    options.utf16 = true;
    options.isMdd = true;
    options.keysPerBlock = 1;

    const QString path = QDir(dir).filePath(QStringLiteral("fixture.mdd"));
    check(writeFixture(path, buildFixture(entries, options)), "mdd fixture written");

    qmdict::MdictFile file;
    QString error;
    check(file.open(path, QString(), &error), "mdd fixture opens");
    check(file.isResourceArchive(), "mdd is recognised as a resource archive");

    // Backslashes are normalised to forward slashes when the index is built.
    const QVector<int> hits = file.findExact(QByteArrayLiteral("/style.css"));
    check(hits.size() == 1, "mdd path lookup");
    if (!hits.isEmpty())
        checkEqual(file.recordAt(hits.first()), QByteArrayLiteral("body { color: red; }"),
                   "mdd resource contents");
}

void testDictionaryLayer(const QString &dir)
{
    // An .mdx and an .mdd sharing a base name must be paired automatically.
    const QString base = QDir(dir).filePath(QStringLiteral("paired"));
    check(writeFixture(base + QStringLiteral(".mdx"), buildFixture(sampleEntries(), FixtureOptions())),
          "paired mdx written");

    std::vector<FixtureEntry> resources = {
        {QStringLiteral("\\banana.png"), QByteArrayLiteral("fake-image-bytes")},
        {QStringLiteral("\\fruit.css"), QByteArrayLiteral(".hw { color: green; }")},
    };
    FixtureOptions resourceOptions;
    resourceOptions.utf16 = true;
    resourceOptions.isMdd = true;
    resourceOptions.keysPerBlock = 2;
    check(writeFixture(base + QStringLiteral(".mdd"), buildFixture(resources, resourceOptions)),
          "paired mdd written");

    qmdict::Dictionary dictionary;
    QString error;
    if (!dictionary.load(base + QStringLiteral(".mdx"), QString(), &error)) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: dictionary did not load: %s\n", error.toUtf8().constData());
        return;
    }

    check(dictionary.entryCount() == 5, "dictionary entry count");
    check(dictionary.resourceCount() == 2, "dictionary picked up the .mdd");

    checkEqual(dictionary.definition(QStringLiteral("apple")).toUtf8(),
               QByteArrayLiteral("<b>apple</b> a round fruit"), "definition lookup");

    // "blueberry" is an @@@LINK alias and must resolve to the banana article.
    checkEqual(dictionary.definition(QStringLiteral("blueberry")).toUtf8(),
               QByteArrayLiteral("<b>banana</b> a long yellow fruit"), "@@@LINK redirect");

    check(dictionary.definition(QStringLiteral("durian")).isEmpty(), "missing word yields nothing");
    check(dictionary.contains(QStringLiteral("CHERRY")), "contains() ignores case");

    const QStringList completions = dictionary.completions(QStringLiteral("a"), 10);
    check(completions.size() == 2, "completion count");
    if (completions.size() == 2)
        check(completions.at(0) == QLatin1String("apple") &&
                  completions.at(1) == QLatin1String("Apricot"),
              "completions are sorted case-insensitively");

    // Resources resolve with either slash style and with or without a leading one.
    checkEqual(dictionary.resource(QStringLiteral("banana.png")), QByteArrayLiteral("fake-image-bytes"),
               "resource without a leading slash");
    checkEqual(dictionary.resource(QStringLiteral("\\banana.png")),
               QByteArrayLiteral("fake-image-bytes"), "resource with a backslash");
    checkEqual(dictionary.resource(QStringLiteral("/banana.png?v=2")),
               QByteArrayLiteral("fake-image-bytes"), "resource with a query string");
    check(dictionary.resource(QStringLiteral("missing.png")).isEmpty(), "missing resource");

    // An article that links its stylesheet by name resolves it from the .mdd.
    checkEqual(dictionary
                   .styleSheetFor(QStringLiteral("<link rel=\"stylesheet\" href=\"fruit.css\">"
                                                 "<p>hi</p>"))
                   .toUtf8()
                   .trimmed(),
               QByteArrayLiteral(".hw { color: green; }"), "linked css comes out of the .mdd");

    // An article with no link at all still gets the dictionary's stylesheet.
    checkEqual(dictionary.styleSheetFor(QStringLiteral("<p>hi</p>")).toUtf8().trimmed(),
               QByteArrayLiteral(".hw { color: green; }"), "css falls back when nothing is linked");
}

// The failure this reproduces: a dictionary that ships its stylesheet as a
// loose file beside the .mdx instead of inside the .mdd. Without it every
// entry renders as one unbroken paragraph.
void testLooseStyleSheet(const QString &dir)
{
    const QString folder = QDir(dir).filePath(QStringLiteral("loose"));
    QDir().mkpath(folder);

    const QString base = QDir(folder).filePath(QStringLiteral("O2"));
    check(writeFixture(base + QStringLiteral(".mdx"), buildFixture(sampleEntries(), FixtureOptions())),
          "loose-css fixture written");

    const QByteArray css = "span.def { display: block; }\nspan.x { display: block; }\n";
    QFile sheet(base + QStringLiteral(".css"));
    check(sheet.open(QIODevice::WriteOnly), "loose css written");
    sheet.write(css);
    sheet.close();

    qmdict::Dictionary dictionary;
    QString error;
    check(dictionary.load(base + QStringLiteral(".mdx"), QString(), &error), "loose-css dict loads");

    const QString linked = dictionary.styleSheetFor(
        QStringLiteral("<link rel=\"stylesheet\" type=\"text/css\" href=\"O2.css\"><span "
                       "class=\"def\">a</span><span class=\"def\">b</span>"));
    checkEqual(linked.toUtf8().trimmed(), css.trimmed(), "css beside the .mdx is found by href");

    // Even when the article forgets the link, or names a file that is absent.
    check(dictionary.styleSheetFor(QStringLiteral("<p>no link here</p>")).contains(
              QLatin1String("display: block")),
          "css beside the .mdx is found without a link");
    check(dictionary.styleSheetFor(QStringLiteral("<link href=\"missing.css\">")).contains(
              QLatin1String("display: block")),
          "a missing stylesheet falls back rather than yielding nothing");

    // An href must not be able to escape the dictionary's own folder.
    QFile outside(QDir(dir).filePath(QStringLiteral("outside.css")));
    outside.open(QIODevice::WriteOnly);
    outside.write("body { color: red; }");
    outside.close();
    check(!dictionary.styleSheetFor(QStringLiteral("<link href=\"../outside.css\">"))
               .contains(QLatin1String("color: red")),
          "a stylesheet href cannot escape the dictionary folder");
}

// MdxBuilder leaves its field labels in the header when the author never
// filled them in, which showed up in the UI as "Title (No HTML code allowed)".
void testPlaceholderTitle(const QString &dir)
{
    FixtureOptions options;
    options.title = QStringLiteral("Title (No HTML code allowed)");

    const QString path = QDir(dir).filePath(QStringLiteral("Collins.mdx"));
    check(writeFixture(path, buildFixture(sampleEntries(), options)), "placeholder-title fixture");

    qmdict::MdictFile file;
    QString error;
    check(file.open(path, QString(), &error), "placeholder-title fixture opens");
    checkEqual(file.title().toUtf8(), QByteArrayLiteral("Collins"),
               "a placeholder title falls back to the file name");

    // A real title must still survive untouched.
    FixtureOptions named;
    named.title = QStringLiteral("Oxford Advanced Learner's");
    const QString other = QDir(dir).filePath(QStringLiteral("O2.mdx"));
    check(writeFixture(other, buildFixture(sampleEntries(), named)), "named fixture");

    qmdict::MdictFile second;
    check(second.open(other, QString(), &error), "named fixture opens");
    checkEqual(second.title().toUtf8(), QByteArrayLiteral("Oxford Advanced Learner's"),
               "a real title is kept");
}

void testLibraryScan(const QString &dir)
{
    // Dictionaries must be discovered in nested sub-folders.
    const QString nested = QDir(dir).filePath(QStringLiteral("tree/en/sub"));
    QDir().mkpath(nested);

    check(writeFixture(QDir(nested).filePath(QStringLiteral("nested.mdx")),
                       buildFixture(sampleEntries(), FixtureOptions())),
          "nested fixture written");

    qmdict::Library library;
    library.loadFolder(QDir(dir).filePath(QStringLiteral("tree")));

    // The loader runs on its own thread; wait for it without an event loop.
    for (int i = 0; i < 200 && library.isLoading(); ++i)
        QThread::msleep(10);
    QCoreApplication::processEvents();

    check(library.count() == 1, "library found the nested dictionary");
    if (library.count() != 1)
        return;

    check(library.totalEntryCount() == 5, "library entry total");

    const QStringList suggestions = library.suggestions(QStringLiteral("b"), 10);
    check(suggestions.size() == 2, "library suggestions");

    const QVector<qmdict::Library::Match> matches = library.lookup(QStringLiteral("cherry"));
    check(matches.size() == 1, "library lookup");
    if (!matches.isEmpty())
        checkEqual(matches.first().html.toUtf8(),
                   QByteArrayLiteral("<b>cherry</b> a small stone fruit"), "library lookup html");

    library.at(0)->setEnabled(false);
    check(library.lookup(QStringLiteral("cherry")).isEmpty(), "disabled dictionaries are skipped");
}

// --- audio ----------------------------------------------------------------

// Ogg's own CRC: polynomial 0x04c11db7, no input or output reflection.
quint32 oggCrc(const QByteArray &page)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 r = i << 24;
            for (int k = 0; k < 8; ++k)
                r = (r & 0x80000000u) ? ((r << 1) ^ 0x04c11db7u) : (r << 1);
            table[i] = r;
        }
        ready = true;
    }

    quint32 crc = 0;
    for (char c : page)
        crc = (crc << 8) ^ table[((crc >> 24) & 0xff) ^ quint8(c)];
    return crc;
}

QByteArray littleEndian(quint64 value, int width)
{
    QByteArray out(width, '\0');
    for (int i = 0; i < width; ++i) {
        out[i] = char(value & 0xff);
        value >>= 8;
    }
    return out;
}

// Builds one Ogg page carrying `packets` whole.
QByteArray oggPage(const QList<QByteArray> &packets, quint32 serial, quint32 sequence,
                   quint8 headerType, quint64 granule)
{
    QByteArray segments;
    QByteArray payload;
    for (const QByteArray &packet : packets) {
        int left = packet.size();
        while (left >= 255) {
            segments.append(char(255));
            left -= 255;
        }
        segments.append(char(left));
        payload += packet;
    }

    QByteArray page = QByteArrayLiteral("OggS");
    page += char(0);            // version
    page += char(headerType);
    page += littleEndian(granule, 8);
    page += littleEndian(serial, 4);
    page += littleEndian(sequence, 4);
    page += littleEndian(0, 4); // CRC placeholder
    page += char(segments.size());
    page += segments;
    page += payload;

    const quint32 crc = oggCrc(page);
    const QByteArray encoded = littleEndian(crc, 4);
    for (int i = 0; i < 4; ++i)
        page[22 + i] = encoded.at(i);
    return page;
}

void testOggDemuxer()
{
    const QByteArray small = QByteArrayLiteral("first");
    const QByteArray large(700, 'x'); // spans three segments
    QByteArray stream = oggPage({small, large}, 42, 0, 0x02, 0);
    stream += oggPage({QByteArrayLiteral("third")}, 42, 1, 0x00, 100);

    QList<QByteArray> packets;
    check(qmdict::OggStream::readPackets(stream, &packets), "ogg stream parses");
    check(packets.size() == 3, "ogg packet count");
    if (packets.size() == 3) {
        checkEqual(packets.at(0), small, "ogg short packet");
        checkEqual(packets.at(1), large, "ogg multi-segment packet");
        checkEqual(packets.at(2), QByteArrayLiteral("third"), "ogg packet from the second page");
    }

    QList<QByteArray> none;
    check(!qmdict::OggStream::readPackets(QByteArrayLiteral("not ogg at all"), &none),
          "non-ogg input is rejected");
    check(!qmdict::OggStream::readPackets(stream.left(20), &none), "truncated ogg is rejected");
}

void testWavPassthroughAndDetection()
{
    QByteArray pcm;
    for (int i = 0; i < 800; ++i) {
        const qint16 sample = qint16(6000 * std::sin(i * 0.2));
        pcm.append(char(sample & 0xff));
        pcm.append(char((sample >> 8) & 0xff));
    }

    const QByteArray wav = qmdict::AudioDecoder::buildWav(pcm, 8000, 1);
    check(wav.startsWith("RIFF") && wav.mid(8, 4) == "WAVE", "buildWav emits a RIFF/WAVE header");
    check(wav.size() == pcm.size() + 44, "buildWav header size");
    check(qmdict::AudioDecoder::detect(wav) == qmdict::AudioDecoder::Format::Wav,
          "wav is detected");
    checkEqual(qmdict::AudioDecoder::toWav(wav), wav, "wav passes through untouched");

    check(qmdict::AudioDecoder::detect(QByteArrayLiteral("\xff\xfb\x90\x00 some mp3 frame")) ==
              qmdict::AudioDecoder::Format::Mp3,
          "mp3 frame header is detected");
    check(qmdict::AudioDecoder::detect(QByteArrayLiteral("ID3\x04\x00\x00\x00\x00\x00\x00rest")) ==
              qmdict::AudioDecoder::Format::Mp3,
          "id3-tagged mp3 is detected");
    check(qmdict::AudioDecoder::detect(QByteArrayLiteral("random junk not audio at all")) ==
              qmdict::AudioDecoder::Format::Unknown,
          "junk is not mistaken for audio");
    check(qmdict::AudioDecoder::toWav(QByteArrayLiteral("random junk not audio")).isEmpty(),
          "junk decodes to nothing");
}

// Encodes a tone with the vendored Speex encoder and wraps it in Ogg, which is
// exactly what a dictionary's .spx clip looks like.
QByteArray buildSpeexClip(int rate, int toneHz, double seconds, int *frameSizeOut)
{
    const SpeexMode *mode = speex_lib_get_mode(SPEEX_MODEID_NB);
    void *state = speex_encoder_init(mode);
    if (!state)
        return QByteArray();

    int frameSize = 0;
    speex_encoder_ctl(state, SPEEX_GET_FRAME_SIZE, &frameSize);
    int quality = 8;
    speex_encoder_ctl(state, SPEEX_SET_QUALITY, &quality);
    int rateCopy = rate;
    speex_encoder_ctl(state, SPEEX_SET_SAMPLING_RATE, &rateCopy);
    *frameSizeOut = frameSize;

    // M_PI is not in standard C++ and MSVC only defines it with
    // _USE_MATH_DEFINES, so spell the constant out.
    constexpr double kPi = 3.14159265358979323846;

    const int total = int(rate * seconds);
    QVector<spx_int16_t> input(total);
    for (int i = 0; i < total; ++i)
        input[i] = spx_int16_t(8000 * std::sin(2.0 * kPi * toneHz * i / rate));

    SpeexBits bits;
    speex_bits_init(&bits);

    QList<QByteArray> audioPackets;
    for (int at = 0; at + frameSize <= total; at += frameSize) {
        speex_bits_reset(&bits);
        speex_encode_int(state, input.data() + at, &bits);
        QByteArray packet(speex_bits_nbytes(&bits), Qt::Uninitialized);
        const int written = speex_bits_write(&bits, packet.data(), packet.size());
        packet.resize(written);
        audioPackets.append(packet);
    }

    speex_bits_destroy(&bits);
    speex_encoder_destroy(state);

    SpeexHeader header;
    speex_init_header(&header, rate, 1, mode);
    header.frames_per_packet = 1;
    int headerSize = 0;
    char *headerBytes = speex_header_to_packet(&header, &headerSize);
    const QByteArray headerPacket(headerBytes, headerSize);
    speex_header_free(headerBytes);

    const QByteArray comment = QByteArrayLiteral("\x08\x00\x00\x00qMdicttest\x00\x00\x00\x00");

    QByteArray stream = oggPage({headerPacket}, 7, 0, 0x02, 0);
    stream += oggPage({comment}, 7, 1, 0x00, 0);

    quint32 sequence = 2;
    quint64 granule = 0;
    for (const QByteArray &packet : audioPackets) {
        granule += quint64(frameSize);
        stream += oggPage({packet}, 7, sequence++, 0x00, granule);
    }
    return stream;
}

void testSpeexDecoding()
{
    const int rate = 8000;
    const int toneHz = 440;
    int frameSize = 0;

    const QByteArray clip = buildSpeexClip(rate, toneHz, 0.5, &frameSize);
    check(!clip.isEmpty(), "speex clip was encoded");
    if (clip.isEmpty())
        return;

    check(qmdict::AudioDecoder::detect(clip) == qmdict::AudioDecoder::Format::Speex,
          "speex clip is detected by content");

    const QByteArray wav = qmdict::AudioDecoder::toWav(clip);
    check(!wav.isEmpty(), "speex clip decodes to wav");
    if (wav.isEmpty())
        return;

    check(wav.startsWith("RIFF"), "decoded speex is a wav file");

    const quint32 storedRate = quint32(quint8(wav.at(24))) | (quint32(quint8(wav.at(25))) << 8) |
                               (quint32(quint8(wav.at(26))) << 16) |
                               (quint32(quint8(wav.at(27))) << 24);
    check(storedRate == quint32(rate), "decoded sample rate matches the source");

    const int samples = (wav.size() - 44) / 2;
    check(samples > int(rate * 0.4) && samples <= int(rate * 0.55),
          "decoded length is close to half a second");

    // Speex is lossy, so compare energy and dominant frequency rather than
    // sample values.
    const qint16 *pcm = reinterpret_cast<const qint16 *>(wav.constData() + 44);
    double energy = 0;
    int crossings = 0;
    for (int i = 0; i < samples; ++i) {
        energy += double(pcm[i]) * pcm[i];
        if (i > 0 && ((pcm[i - 1] < 0) != (pcm[i] < 0)))
            ++crossings;
    }
    const double rms = std::sqrt(energy / samples);
    check(rms > 1000.0, "decoded audio carries real signal energy");

    const double measuredHz = (crossings / 2.0) / (double(samples) / rate);
    check(measuredHz > toneHz * 0.8 && measuredHz < toneHz * 1.25,
          "decoded tone is at roughly the encoded pitch");
}

// --- block layout for span-based dictionaries -----------------------------

void testHtmlBlocks()
{
    using namespace qmdict::htmlblocks;

    // Exercised end to end -- parse a stylesheet, then apply it -- because the
    // interesting behaviour lives in how the two fit together.
    auto apply = [](const QString &css, const QString &html) {
        return adaptForTextDocument(html, rulesFromStyleSheet(css));
    };

    // --- block layout ----------------------------------------------------
    // Wrapped, not renamed: the element must survive so element-name CSS
    // selectors keep matching it.
    checkEqual(apply(QStringLiteral("top-g{display:block}"),
                     QStringLiteral("<top-g>a</top-g>")).toUtf8(),
               QByteArrayLiteral("<div><top-g>a</top-g></div>"),
               "a block element is wrapped rather than renamed");
    checkEqual(apply(QStringLiteral(".def{display:block}"),
                     QStringLiteral("<span class=\"def\">a</span>")).toUtf8(),
               QByteArrayLiteral("<div><span class=\"def\">a</span></div>"),
               "a block class is wrapped");
    checkEqual(apply(QStringLiteral("top-g{display:block}"),
                     QStringLiteral("<other>a</other>")).toUtf8(),
               QByteArrayLiteral("<other>a</other>"), "an inline element is untouched");
    checkEqual(apply(QStringLiteral("top-g{display:block}"),
                     QStringLiteral("<top-g eid=\"x\">a</top-g>")).toUtf8(),
               QByteArrayLiteral("<div><top-g eid=\"x\">a</top-g></div>"),
               "attributes are preserved");

    check(apply(QStringLiteral(".c{display:inline}"), QStringLiteral("<span class=\"c\">a</span>"))
              == QStringLiteral("<span class=\"c\">a</span>"),
          "inline elements are left alone");
    check(apply(QStringLiteral(".c{display:inline-block}"),
                QStringLiteral("<span class=\"c\">a</span>")) ==
              QStringLiteral("<span class=\"c\">a</span>"),
          "inline-block does not force a line break");
    check(apply(QStringLiteral("img{display:block}"), QStringLiteral("<img src=\"x\">")) ==
              QStringLiteral("<img src=\"x\">"),
          "standard elements are Qt's business, not ours");
    check(apply(QStringLiteral("h-g:after{display:block}"), QStringLiteral("<h-g>a</h-g>")) ==
              QStringLiteral("<h-g>a</h-g>"),
          "a pseudo-element rule does not promote its element");
    check(apply(QStringLiteral("/* note */\nund{display:block}"), QStringLiteral("<und>a</und>")) ==
              QStringLiteral("<div><und>a</und></div>"),
          "css comments are stripped before parsing");
    check(apply(QStringLiteral("unbox[type=\"colloc\"]{display:block}"),
                QStringLiteral("<unbox>a</unbox>")) == QStringLiteral("<div><unbox>a</unbox></div>"),
          "attribute selectors still yield their element");

    // Nesting and balance.
    checkEqual(apply(QStringLiteral("top-g{display:block}"),
                     QStringLiteral("<top-g>a<b>c</b></top-g>")).toUtf8(),
               QByteArrayLiteral("<div><top-g>a<b>c</b></top-g></div>"),
               "nested standard elements are left alone");
    checkEqual(apply(QStringLiteral("top-g{display:block}"),
                     QStringLiteral("<top-g><top-g>x</top-g></top-g>")).toUtf8(),
               QByteArrayLiteral("<div><top-g><div><top-g>x</top-g></div></top-g></div>"),
               "nested block elements each get a wrapper");
    checkEqual(apply(QStringLiteral("top-g{display:block}"), QStringLiteral("<top-g>unclosed"))
                   .toUtf8(),
               QByteArrayLiteral("<div><top-g>unclosed</top-g></div>"),
               "an unclosed block element is balanced");
    checkEqual(apply(QStringLiteral("top-g{display:block}"),
                     QStringLiteral("<top-g><i>x</top-g>")).toUtf8(),
               QByteArrayLiteral("<div><top-g><i>x</i></top-g></div>"),
               "an unclosed inner element is balanced too");

    // Check Qt's actual paragraphs: the adapted markup used to put the opening
    // bracket on its own line after the nested part-of-speech block.
    const QString grammarCss = QStringLiteral("top-g,pos-g,sn-gs{display:block}");
    const QString grammarHtml = QStringLiteral(
        "<top-g><pos-g>noun</pos-g><gram-g><gram-blk> [<gram>uncountable</gram>] "
        "</gram-blk></gram-g><label-g-blk>(<label-g>specialist</label-g>)</label-g-blk>"
        "<sn-gs>definition</sn-gs></top-g>");
    QTextDocument grammarDocument;
    grammarDocument.setHtml(apply(grammarCss, grammarHtml));
    checkEqual(grammarDocument.toPlainText().toUtf8(),
               QByteArrayLiteral("noun\n[uncountable] (specialist)\ndefinition"),
               "grammar brackets and usage label share a paragraph after a nested block");
    grammarDocument.setHtml(apply(grammarCss, QStringLiteral(
        "<top-g><pos-g>noun</pos-g>[<gram>uncountable</gram>]</top-g>")));
    checkEqual(grammarDocument.toPlainText().toUtf8(), QByteArrayLiteral("noun\n[uncountable]"),
               "bare inline text after a nested block stays together");

    LayoutRules none;
    checkEqual(adaptForTextDocument(QStringLiteral("text</top-g>more"), none).toUtf8(),
               QByteArrayLiteral("text</top-g>more"), "a stray close tag is left alone");
    checkEqual(adaptForTextDocument(QStringLiteral("a < b and c > d"), none).toUtf8(),
               QByteArrayLiteral("a < b and c > d"), "unescaped angle brackets survive");
    checkEqual(adaptForTextDocument(QStringLiteral("<!-- note -->x"), none).toUtf8(),
               QByteArrayLiteral("<!-- note -->x"), "comments pass through");
    checkEqual(adaptForTextDocument(QStringLiteral("<top-g>a</top-g>"), none).toUtf8(),
               QByteArrayLiteral("<top-g>a</top-g>"), "no rules means no wrapping");

    // --- namespaced tags --------------------------------------------------
    checkEqual(adaptForTextDocument(QStringLiteral("a<xhtml:br></xhtml:br>b"), none).toUtf8(),
               QByteArrayLiteral("a<br>b"), "a namespaced line break becomes a real one");
    checkEqual(adaptForTextDocument(QStringLiteral("<xhtml:a href=\"entry://x\">t</xhtml:a>"), none)
                   .toUtf8(),
               QByteArrayLiteral("<a href=\"entry://x\">t</a>"),
               "a namespaced link becomes a real one");

    // --- hidden elements --------------------------------------------------
    // Oxford hides its BrE/NAmE labels and colours the pronunciations instead.
    checkEqual(apply(QStringLiteral("pron-g-blk brelabel{display:none}"),
                     QStringLiteral("<pron-g-blk>a<brelabel>BrE</brelabel> b</pron-g-blk>"))
                   .toUtf8(),
               QByteArrayLiteral("<pron-g-blk>a b</pron-g-blk>"),
               "a hidden element and its content are dropped");
    checkEqual(apply(QStringLiteral("brelabel{display:none}"),
                     QStringLiteral("a<brelabel>BrE</brelabel> b")).toUtf8(),
               QByteArrayLiteral("a b"), "an unscoped hidden element is dropped anywhere");
    checkEqual(apply(QStringLiteral(".gone{display:none}"),
                     QStringLiteral("<span class=\"gone\">x</span>y")).toUtf8(),
               QByteArrayLiteral("y"), "a hidden class is dropped");
    checkEqual(apply(QStringLiteral("brelabel{display:none}"),
                     QStringLiteral("<brelabel><b>x</b><i>y</i></brelabel>z")).toUtf8(),
               QByteArrayLiteral("z"), "nested content inside a hidden element goes too");

    // visibility:hidden is the other way these stylesheets blank something out.
    checkEqual(apply(QStringLiteral("symbol[type=key]{color:red;visibility:hidden}"),
                     QStringLiteral("<symbol type=\"key\">K</symbol>word")).toUtf8(),
               QByteArrayLiteral("word"), "visibility:hidden also removes the element");
    check(apply(QStringLiteral("x{visibility:hidden;*visibility:visible !important}"),
                QStringLiteral("<x>a</x>b")) == QStringLiteral("b"),
          "the real declaration wins over the IE hacks beside it");
    check(apply(QStringLiteral("y{*visibility:hidden}"), QStringLiteral("<y>a</y>")) ==
              QStringLiteral("<y>a</y>"),
          "an IE-hack property alone hides nothing");
    check(apply(QStringLiteral("z{visibility:visible}"), QStringLiteral("<z>a</z>")) ==
              QStringLiteral("<z>a</z>"),
          "visibility:visible hides nothing");

    // --- scoped rules -----------------------------------------------------
    // Oxford suppresses the break between two pronunciations with
    // "top-g xhtml\:br{display:none}". Applying that everywhere would delete
    // every line break in the entry, so the ancestor has to be respected.
    const QString scoped = QStringLiteral("top-g xhtml\\:br{display:none}");
    checkEqual(apply(scoped, QStringLiteral("<top-g>a<xhtml:br></xhtml:br>b</top-g>")).toUtf8(),
               QByteArrayLiteral("<top-g>ab</top-g>"),
               "a break inside the scoped ancestor is removed");
    checkEqual(apply(scoped, QStringLiteral("<other>a<xhtml:br></xhtml:br>b</other>")).toUtf8(),
               QByteArrayLiteral("<other>a<br>b</other>"),
               "the same break outside that ancestor survives");

    const QString scopedClass = QStringLiteral("unbox und{display:none}");
    checkEqual(apply(scopedClass, QStringLiteral("<unbox><und>x</und></unbox>")).toUtf8(),
               QByteArrayLiteral("<unbox></unbox>"), "a scoped element is hidden inside its box");
    checkEqual(apply(scopedClass, QStringLiteral("<und>x</und>")).toUtf8(),
               QByteArrayLiteral("<und>x</und>"), "and kept outside it");

    // --- links that go nowhere -------------------------------------------
    check(!isNavigableHref(QStringLiteral("help:bre")), "help: is not a headword link");
    check(!isNavigableHref(QStringLiteral("helpp:n")), "helpp: is not a headword link");
    check(!isNavigableHref(QStringLiteral("javascript:x()")), "javascript: is refused");
    check(!isNavigableHref(QString()), "an empty href is refused");
    check(isNavigableHref(QStringLiteral("entry://word")), "entry:// is a headword link");
    check(isNavigableHref(QStringLiteral("bword://word")), "bword:// is a headword link");
    check(isNavigableHref(QStringLiteral("sound://a.spx")), "sound:// is kept");
    check(isNavigableHref(QStringLiteral("apple")), "a bare headword is a link");
    check(isNavigableHref(QStringLiteral("#anchor")), "an in-page anchor is a link");
    check(isNavigableHref(QStringLiteral("https://example.org")), "https is a link");

    checkEqual(adaptForTextDocument(QStringLiteral("<a href=\"help:bre\">BrE</a>"), none).toUtf8(),
               QByteArrayLiteral("BrE"), "a dead link is unwrapped, keeping its text");
    checkEqual(adaptForTextDocument(QStringLiteral("<a href=\"entry://x\">x</a>"), none).toUtf8(),
               QByteArrayLiteral("<a href=\"entry://x\">x</a>"), "a real link is kept");
    checkEqual(adaptForTextDocument(QStringLiteral("<a href=\"sound://a.spx\">s</a>"), none).toUtf8(),
               QByteArrayLiteral("<a href=\"sound://a.spx\">s</a>"), "an audio link is kept");
    checkEqual(adaptForTextDocument(QStringLiteral("<a href=\"help:x\"><b>t</b></a>"), none).toUtf8(),
               QByteArrayLiteral("<b>t</b>"), "markup inside a dead link survives");
}

// --- stylesheet filtering -------------------------------------------------

void testCssFilter()
{
    using namespace qmdict::cssfilter;

    // Declarations Qt cannot act on are dropped, and rules left empty go too.
    check(usable(QStringLiteral("top-g{display:block;clear:left}")).isEmpty(),
          "a rule Qt cannot act on is dropped entirely");
    check(usable(QStringLiteral("d{color:red;float:left}")).contains(QLatin1String("color:red")),
          "a usable declaration is kept");
    check(!usable(QStringLiteral("d{color:red;float:left}")).contains(QLatin1String("float")),
          "an unusable declaration is stripped from a kept rule");

    check(usable(QStringLiteral("@font-face{font-family:'X';src:url(x.ttf)}")).isEmpty(),
          "at-rules are dropped");
    check(usable(QStringLiteral("/* note */ d{color:red}")).contains(QLatin1String("color:red")),
          "comments are stripped");
    check(usable(QStringLiteral("d::before{color:red}")).isEmpty(),
          "generated-content rules are dropped");
    check(usable(QStringLiteral("d:after{color:red}")).isEmpty(),
          "the single-colon spelling is dropped too");

    // Rules that cannot match this article are dropped.
    const QString sheet = QStringLiteral("top-g{color:red}\nabsent-tag{color:blue}\n"
                                         ".here{color:green}\n.gone{color:grey}\n*{color:black}\n");
    const QString html = QStringLiteral("<top-g><span class=\"here\">x</span></top-g>");
    const QString kept = relevantTo(sheet, html);

    check(kept.contains(QLatin1String("top-g")), "a rule for a present element is kept");
    check(!kept.contains(QLatin1String("absent-tag")), "a rule for an absent element is dropped");
    check(kept.contains(QLatin1String(".here")), "a rule for a present class is kept");
    check(!kept.contains(QLatin1String(".gone")), "a rule for an absent class is dropped");
    check(kept.contains(QLatin1String("*")), "the universal selector is always kept");

    // The wrappers this rendering adds must not be filtered away.
    check(relevantTo(QStringLiteral("div{margin:0}"), QStringLiteral("<top-g>x</top-g>"))
              .contains(QLatin1String("div")),
          "rules for the wrapper element survive");

    // Only the rightmost compound has to exist for a rule to be reachable.
    check(relevantTo(QStringLiteral("absent x{color:red}"), QStringLiteral("<x>1</x>"))
              .contains(QLatin1String("color:red")),
          "an absent ancestor does not make a rule unreachable");

    check(relevantTo(QString(), QStringLiteral("<p>x</p>")).isEmpty(), "an empty sheet stays empty");
    checkEqual(relevantTo(QStringLiteral("d{color:red}"), QString()).toUtf8(),
               QByteArrayLiteral("d{color:red}"), "no html means no filtering");
}

// --- dark mode colours ----------------------------------------------------

void testDarkColours()
{
    using namespace qmdict::darkcolours;

    // The reported problem: blue example sentences on a dark page.
    const QColor blue(0, 0, 255);
    const QColor mappedBlue = remap(blue, false);
    check(mappedBlue.lightnessF() > blue.lightnessF(), "blue text is lightened");
    check(qAbs(mappedBlue.hueF() - blue.hueF()) < 0.02f, "lightening preserves hue");

    check(remap(QColor(0x6c, 0xb6, 0xff), false) == QColor(0x6c, 0xb6, 0xff),
          "already-light text is left alone");

    const QColor panel = remap(QColor(Qt::white), true);
    check(panel.lightnessF() < 0.35f, "light panels are darkened");
    check(remap(QColor(0x20, 0x20, 0x20), true) == QColor(0x20, 0x20, 0x20),
          "dark panels are left alone");
    check(remap(QColor(Qt::transparent), false) == QColor(Qt::transparent),
          "transparent is left alone");

    const QString css = adaptStyleSheet(QStringLiteral(".ex { color: blue; font-size: 12px; }"));
    check(!css.contains(QLatin1String("blue")), "stylesheet colour is rewritten");
    check(css.contains(QLatin1String("font-size: 12px")), "other declarations are untouched");

    const QString navy = adaptStyleSheet(QStringLiteral("p { color: #000080 }"));
    check(!navy.contains(QLatin1String("#000080")), "hex colours are rewritten");

    // Non-colour declarations must survive verbatim.
    const QString untouched = QStringLiteral("div { margin: 0 auto; content: 'red'; }");
    checkEqual(adaptStyleSheet(untouched).toUtf8(), untouched.toUtf8(),
               "declarations without colours are unchanged");

    const QString html = adaptHtml(QStringLiteral("<span style=\"color:#000080\">x</span>"));
    check(!html.contains(QLatin1String("#000080")), "inline style colour is rewritten");

    const QString fontTag = adaptHtml(QStringLiteral("<font color=\"blue\">hi</font>"));
    check(!fontTag.contains(QLatin1String("\"blue\"")), "font colour attribute is rewritten");

    // The important safety property: prose must never be rewritten.
    const QString prose =
        QStringLiteral("<p>Colour: red apples are green. See also: blue whales.</p>");
    checkEqual(adaptHtml(prose).toUtf8(), prose.toUtf8(), "body text is never altered");

    const QString mixed =
        QStringLiteral("<p style=\"color:navy\">Note: the sky is blue</p>");
    const QString mappedMixed = adaptHtml(mixed);
    check(!mappedMixed.contains(QLatin1String("navy")), "style attribute in prose tag is rewritten");
    check(mappedMixed.contains(QLatin1String("the sky is blue")), "prose inside the tag survives");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Setting QMDICT_FIXTURE_DIR keeps the generated dictionaries around, which
    // is handy for exercising the GUI by hand.
    QTemporaryDir temporary;
    const QString keep = qEnvironmentVariable("QMDICT_FIXTURE_DIR");
    if (!keep.isEmpty())
        QDir().mkpath(keep);
    else if (!temporary.isValid()) {
        std::fprintf(stderr, "cannot create a temporary directory\n");
        return 1;
    }

    const QString fixtureDir = keep.isEmpty() ? temporary.path() : keep;

    testRipemd128();
    testLzo1x();

    testDictionaryVersion(fixtureDir, 2.0, false);
    testDictionaryVersion(fixtureDir, 2.0, true);
    testDictionaryVersion(fixtureDir, 1.2, false);

    testIndexCache(fixtureDir);
    testUnsortedKeys(fixtureDir);
    testResourceArchive(fixtureDir);
    testDictionaryLayer(fixtureDir);
    testLooseStyleSheet(fixtureDir);
    testPlaceholderTitle(fixtureDir);
    testLibraryScan(fixtureDir);

    testOggDemuxer();
    testWavPassthroughAndDetection();
    testSpeexDecoding();
    testHtmlBlocks();
    testCssFilter();
    testDarkColours();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
