#include "textdecode.h"

#include <QStringDecoder>

#include <cstddef>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#else
#  include <errno.h>
#  include <iconv.h>
#endif

namespace qmdict {
namespace {

QString normalise(const QString &name)
{
    QString s = name.trimmed().toUpper();
    s.remove(QLatin1Char('-'));
    s.remove(QLatin1Char('_'));
    s.remove(QLatin1Char(' '));
    return s;
}

#ifdef Q_OS_WIN
unsigned int codePageFor(const QString &normalised)
{
    if (normalised == QLatin1String("GBK") || normalised == QLatin1String("GB2312") ||
        normalised == QLatin1String("GB18030") || normalised == QLatin1String("CP936"))
        return 936;
    if (normalised == QLatin1String("BIG5") || normalised == QLatin1String("BIG5HKSCS") ||
        normalised == QLatin1String("CP950"))
        return 950;
    if (normalised == QLatin1String("SHIFTJIS") || normalised == QLatin1String("SJIS") ||
        normalised == QLatin1String("CP932"))
        return 932;
    if (normalised == QLatin1String("EUCKR") || normalised == QLatin1String("CP949"))
        return 949;
    if (normalised == QLatin1String("KOI8R"))
        return 20866;
    if (normalised.startsWith(QLatin1String("WINDOWS")))
        return normalised.mid(7).toUInt();
    return 0;
}
#else
QByteArray iconvNameFor(const QString &normalised)
{
    if (normalised == QLatin1String("GBK") || normalised == QLatin1String("GB2312") ||
        normalised == QLatin1String("CP936"))
        return QByteArrayLiteral("GBK");
    if (normalised == QLatin1String("GB18030"))
        return QByteArrayLiteral("GB18030");
    if (normalised == QLatin1String("BIG5") || normalised == QLatin1String("CP950"))
        return QByteArrayLiteral("BIG5");
    if (normalised == QLatin1String("BIG5HKSCS"))
        return QByteArrayLiteral("BIG5-HKSCS");
    if (normalised == QLatin1String("SHIFTJIS") || normalised == QLatin1String("SJIS") ||
        normalised == QLatin1String("CP932"))
        return QByteArrayLiteral("SHIFT-JIS");
    if (normalised == QLatin1String("EUCJP"))
        return QByteArrayLiteral("EUC-JP");
    if (normalised == QLatin1String("EUCKR") || normalised == QLatin1String("CP949"))
        return QByteArrayLiteral("EUC-KR");
    if (normalised == QLatin1String("KOI8R"))
        return QByteArrayLiteral("KOI8-R");
    if (normalised.startsWith(QLatin1String("WINDOWS")))
        return QByteArrayLiteral("CP") + normalised.mid(7).toLatin1();
    return QByteArray();
}
#endif

} // namespace

TextDecoder::TextDecoder(const QString &name)
    : m_name(name)
{
    const QString n = normalise(name);

    if (n.isEmpty() || n == QLatin1String("UTF8")) {
        m_kind = Kind::Utf8;
    } else if (n == QLatin1String("UTF16") || n == QLatin1String("UTF16LE") ||
               n == QLatin1String("UNICODE") || n == QLatin1String("UCS2")) {
        m_kind = Kind::Utf16LE;
        m_unitSize = 2;
    } else if (n == QLatin1String("ISO88591") || n == QLatin1String("LATIN1") ||
               n == QLatin1String("ASCII") || n == QLatin1String("USASCII")) {
        m_kind = Kind::Latin1;
    } else {
#ifdef Q_OS_WIN
        if (codePageFor(n) != 0) {
            m_kind = Kind::Platform;
            m_platformName = n.toLatin1();
        }
#else
        const QByteArray iconvName = iconvNameFor(n);
        if (!iconvName.isEmpty()) {
            m_kind = Kind::Platform;
            m_platformName = iconvName;
        }
#endif
    }
}

QString TextDecoder::decode(const char *data, int size) const
{
    if (size <= 0)
        return QString();

    switch (m_kind) {
    case Kind::Utf8:
        return QString::fromUtf8(data, size);
    case Kind::Utf16LE:
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(data), size / 2);
    case Kind::Latin1:
        return QString::fromLatin1(data, size);
    case Kind::Platform:
        break;
    }

#ifdef Q_OS_WIN
    const unsigned int codePage = codePageFor(QString::fromLatin1(m_platformName));
    const int needed = MultiByteToWideChar(codePage, 0, data, size, nullptr, 0);
    if (needed <= 0)
        return QString::fromLatin1(data, size);
    QString out(needed, Qt::Uninitialized);
    MultiByteToWideChar(codePage, 0, data, size, reinterpret_cast<wchar_t *>(out.data()), needed);
    return out;
#else
    const iconv_t cd = iconv_open("UTF-8", m_platformName.constData());
    if (cd == reinterpret_cast<iconv_t>(-1))
        return QString::fromLatin1(data, size);

    QByteArray out;
    out.resize(size * 4 + 8);
    char *inBuf = const_cast<char *>(data);
    std::size_t inLeft = std::size_t(size);
    char *outBuf = out.data();
    std::size_t outLeft = std::size_t(out.size());

    while (inLeft > 0) {
        const std::size_t rc = iconv(cd, &inBuf, &inLeft, &outBuf, &outLeft);
        if (rc != std::size_t(-1))
            break;
        if (errno == E2BIG) {
            const std::size_t used = std::size_t(outBuf - out.data());
            out.resize(out.size() * 2);
            outBuf = out.data() + used;
            outLeft = std::size_t(out.size()) - used;
            continue;
        }
        // Skip the offending byte so a single bad sequence cannot lose the
        // rest of the string.
        if (outLeft == 0)
            break;
        *outBuf++ = '?';
        --outLeft;
        ++inBuf;
        --inLeft;
    }

    iconv_close(cd);
    out.truncate(int(outBuf - out.data()));
    return QString::fromUtf8(out);
#endif
}

QByteArray TextDecoder::toUtf8(const char *data, int size) const
{
    if (m_kind == Kind::Utf8)
        return QByteArray(data, size);
    return decode(data, size).toUtf8();
}

} // namespace qmdict
