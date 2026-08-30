// Decoding of the legacy text encodings MDX files declare in their header.
//
// Qt 6 dropped QTextCodec, and its replacement QStringConverter only covers
// Unicode plus Latin-1. Chinese dictionaries are commonly GBK or BIG5, so
// anything outside QStringConverter's set is routed to the platform
// facility: iconv on Unix, MultiByteToWideChar on Windows.
#pragma once

#include <QByteArray>
#include <QString>

namespace qmdict {

class TextDecoder
{
public:
    // `name` is the raw Encoding attribute from the MDX header; an empty or
    // unrecognised value falls back to UTF-8.
    explicit TextDecoder(const QString &name = QString());

    QString decode(const char *data, int size) const;
    QString decode(const QByteArray &data) const { return decode(data.constData(), data.size()); }

    // UTF-8 re-encoding, used when storing headwords in the key blob.
    QByteArray toUtf8(const char *data, int size) const;

    // Bytes per code unit: 2 for UTF-16, otherwise 1. Determines the width of
    // the NUL terminator separating headwords inside a key block.
    int unitSize() const { return m_unitSize; }

    QString name() const { return m_name; }

private:
    enum class Kind { Utf8, Utf16LE, Latin1, Platform };

    QString m_name;
    Kind m_kind = Kind::Utf8;
    int m_unitSize = 1;
    QByteArray m_platformName;
};

} // namespace qmdict
