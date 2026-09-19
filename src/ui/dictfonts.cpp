#include "dictfonts.h"

#include <QFontDatabase>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace qmdict {
namespace dictfonts {
namespace {

const QRegularExpression &fontFaceBlock()
{
    static const QRegularExpression re(QStringLiteral("@font-face\\s*\\{([^{}]*)\\}"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString declaredFamily(const QString &block)
{
    static const QRegularExpression re(QStringLiteral("font-family\\s*:\\s*([^;}]+)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(block);
    if (!match.hasMatch())
        return QString();

    QString name = match.captured(1).trimmed();
    name.remove(QLatin1Char('"'));
    name.remove(QLatin1Char('\''));
    return name.trimmed();
}

QStringList sources(const QString &block)
{
    static const QRegularExpression re(
        QStringLiteral("url\\(\\s*(\"[^\"]*\"|'[^']*'|[^)]*)\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);

    QStringList urls;
    auto it = re.globalMatch(block);
    while (it.hasNext()) {
        QString url = it.next().captured(1).trimmed();
        if (url.size() >= 2 &&
            (url.startsWith(QLatin1Char('"')) || url.startsWith(QLatin1Char('\''))))
            url = url.mid(1, url.size() - 2);
        if (!url.isEmpty())
            urls.append(url);
    }
    return urls;
}

// The bytes of one src entry: a data: URL carries them, anything else names a
// file the dictionary ships.
QByteArray fontData(const QString &url,
                    const std::function<QByteArray(const QString &)> &resource)
{
    if (url.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
        const int base64 = url.indexOf(QLatin1String("base64,"), 0, Qt::CaseInsensitive);
        if (base64 < 0)
            return QByteArray();
        return QByteArray::fromBase64(url.mid(base64 + 7).toLatin1());
    }

    // Some stylesheets give a path, and the .mdd is flat.
    QString name = url;
    const int slash = name.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0)
        name = name.mid(slash + 1);
    // A format hint such as "font.eot?#iefix" is not part of the name.
    const int query = name.indexOf(QLatin1Char('?'));
    if (query >= 0)
        name.truncate(query);

    return resource ? resource(name) : QByteArray();
}

// The first family named by a font or font-family declaration, unquoted.
QString primaryFamily(const QString &value)
{
    QString name = value.section(QLatin1Char(','), 0, 0).trimmed();

    // The `font` shorthand puts style, weight and size in front of the family.
    const int slash = name.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0)
        name = name.mid(name.indexOf(QLatin1Char(' '), slash) + 1);

    name.remove(QLatin1Char('"'));
    name.remove(QLatin1Char('\''));
    const int important = name.indexOf(QLatin1Char('!'));
    if (important >= 0)
        name.truncate(important);
    return name.trimmed();
}

const QRegularExpression &fontDeclaration()
{
    static const QRegularExpression re(QStringLiteral("\\bfont(?:-family)?\\s*:([^;}]*)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Whether some rule asks for `family` by name rather than listing it among the
// families it would settle for.
//
// This is what decides a face is worth installing, and it is the whole reason
// the distinction matters: Qt picks one family for a run of text instead of
// falling back name by name as a browser does. A dictionary's own face sitting
// in the middle of a fallback list would then be used for the entire page --
// and these faces carry a few dozen glyphs in a single weight, which costs the
// entry every bold word in it. A face named on its own is asked for because
// nothing installed can stand in for it, which is how a stylesheet reaches its
// icon glyphs.
bool namedOnItsOwn(const QString &css, const QString &family)
{
    auto it = fontDeclaration().globalMatch(css);
    while (it.hasNext()) {
        if (primaryFamily(it.next().captured(1)).compare(family, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

} // namespace

QHash<QString, QString> install(const QString &css,
                                const std::function<QByteArray(const QString &)> &resource)
{
    QHash<QString, QString> aliases;
    if (css.isEmpty())
        return aliases;

    // A family a stylesheet builds out of several faces -- one per weight, as
    // Oxford does with Optima -- cannot be put back together: Qt knows each
    // file by the family name inside it, and those differ. Installing one of
    // them would render the page in a single weight and lose every bold word,
    // so such a family is left to the platform, which has complete ones.
    QHash<QString, int> facesPerFamily;
    auto faces = fontFaceBlock().globalMatch(css);
    while (faces.hasNext())
        facesPerFamily[declaredFamily(faces.next().captured(1)).toLower()] += 1;

    auto it = fontFaceBlock().globalMatch(css);
    while (it.hasNext()) {
        const QString block = it.next().captured(1);
        const QString declared = declaredFamily(block);
        if (declared.isEmpty() || aliases.contains(declared) || !namedOnItsOwn(css, declared))
            continue;
        if (facesPerFamily.value(declared.toLower()) != 1)
            continue;

        for (const QString &url : sources(block)) {
            const QByteArray data = fontData(url, resource);
            if (data.isEmpty())
                continue;

            const int id = QFontDatabase::addApplicationFontFromData(data);
            if (id < 0)
                continue;

            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (families.isEmpty())
                continue;

            // Only a rename is worth recording; a face that already answers to
            // the name the stylesheet uses needs no help.
            if (families.first().compare(declared, Qt::CaseInsensitive) != 0)
                aliases.insert(declared, families.first());
            break;
        }
    }

    return aliases;
}

QString applyAliases(const QString &css, const QHash<QString, QString> &aliases)
{
    if (css.isEmpty() || aliases.isEmpty())
        return css;

    QString out;
    int cursor = 0;

    auto it = fontDeclaration().globalMatch(css);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString value = match.captured(1);

        // Only the family the declaration asks for by name, for the reason
        // given above: the rest of the list is what it would settle for, and
        // the platform's own fonts settle better.
        const auto alias = aliases.constFind(primaryFamily(value));
        if (alias == aliases.constEnd())
            continue;

        const QRegularExpression name(
            QStringLiteral("(?<![-_a-zA-Z0-9])%1(?![-_a-zA-Z0-9])")
                .arg(QRegularExpression::escape(alias.key())),
            QRegularExpression::CaseInsensitiveOption);

        QString mapped = value;
        mapped.replace(name, alias.value());
        if (mapped == value)
            continue;

        out += css.mid(cursor, match.capturedStart(1) - cursor);
        out += mapped;
        cursor = match.capturedEnd(1);
    }

    if (cursor == 0)
        return css;

    out += css.mid(cursor);
    return out;
}

} // namespace dictfonts
} // namespace qmdict
