#include "darkcolours.h"

#include <QRegularExpression>

namespace qmdict {
namespace darkcolours {
namespace {

// Text needs to be at least this light to read comfortably on the dark page.
constexpr float kMinimumTextLightness = 0.68f;
// Panels lighter than this would keep their light-page look and swallow text.
constexpr float kMaximumPanelLightness = 0.55f;

bool propertyIsBackground(const QString &property)
{
    return property.compare(QLatin1String("background"), Qt::CaseInsensitive) == 0 ||
           property.compare(QLatin1String("background-color"), Qt::CaseInsensitive) == 0 ||
           property.compare(QLatin1String("bgcolor"), Qt::CaseInsensitive) == 0;
}

bool propertyCarriesColour(const QString &property)
{
    return property.endsWith(QLatin1String("color"), Qt::CaseInsensitive) ||
           property.compare(QLatin1String("background"), Qt::CaseInsensitive) == 0 ||
           property.compare(QLatin1String("border"), Qt::CaseInsensitive) == 0 ||
           property.startsWith(QLatin1String("border-"), Qt::CaseInsensitive) ||
           property.compare(QLatin1String("outline"), Qt::CaseInsensitive) == 0;
}

// Rewrites the colour literals inside a single declaration value.
QString remapValue(const QString &value, bool isBackground)
{
    static const QRegularExpression token(
        QStringLiteral("#[0-9a-fA-F]{3,8}\\b|rgba?\\([^)]*\\)|\\b[a-zA-Z]{3,20}\\b"));

    QString out;
    int cursor = 0;

    auto it = token.globalMatch(value);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();

        const QColor colour(match.captured());
        if (!colour.isValid() || colour.alpha() == 0)
            continue;

        const QColor mapped = remap(colour, isBackground);
        if (mapped == colour)
            continue;

        out += value.mid(cursor, match.capturedStart() - cursor);
        out += mapped.name(QColor::HexRgb);
        cursor = match.capturedEnd();
    }

    if (cursor == 0)
        return value;

    out += value.mid(cursor);
    return out;
}

// Walks "property: value" pairs in a stylesheet or a style attribute.
QString remapDeclarations(const QString &text)
{
    static const QRegularExpression declaration(
        QStringLiteral("([-a-zA-Z]+)\\s*:\\s*([^;{}]+)"));

    QString out;
    int cursor = 0;

    auto it = declaration.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (!propertyCarriesColour(match.captured(1)))
            continue;

        const QString value = match.captured(2);
        const QString mapped = remapValue(value, propertyIsBackground(match.captured(1)));
        if (mapped == value)
            continue;

        out += text.mid(cursor, match.capturedStart(2) - cursor);
        out += mapped;
        cursor = match.capturedEnd(2);
    }

    if (cursor == 0)
        return text;

    out += text.mid(cursor);
    return out;
}

} // namespace

QColor remap(const QColor &colour, bool isBackground)
{
    if (!colour.isValid() || colour.alpha() == 0)
        return colour;

    float h = 0;
    float s = 0;
    float l = 0;
    float a = 0;
    colour.getHslF(&h, &s, &l, &a);

    // Achromatic colours report a hue of -1, which fromHslF rejects.
    const float hue = h < 0.0f ? 0.0f : h;

    if (isBackground) {
        if (l <= kMaximumPanelLightness)
            return colour;
        // Keep a hint of the original hue so highlight boxes stay
        // distinguishable from the page.
        return QColor::fromHslF(hue, qMin(s, 0.25f), 0.19f, a);
    }

    if (l >= kMinimumTextLightness)
        return colour;

    // Near-greys are body text in disguise; give them the normal text colour
    // rather than a washed-out grey.
    if (s < 0.18f)
        return QColor::fromHslF(hue, s, 0.89f, a);

    return QColor::fromHslF(hue, qMin(1.0f, s * 0.95f), kMinimumTextLightness, a);
}

QString adaptStyleSheet(const QString &css)
{
    if (css.isEmpty())
        return css;
    return remapDeclarations(css);
}

QString adaptHtml(const QString &html)
{
    if (html.isEmpty())
        return html;

    static const QRegularExpression styleAttribute(
        QStringLiteral("style\\s*=\\s*([\"'])(.*?)\\1"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression colourAttribute(
        QStringLiteral("\\b(color|bgcolor)\\s*=\\s*([\"']?)(#?\\w+)\\2"),
        QRegularExpression::CaseInsensitiveOption);

    QString out;
    int cursor = 0;

    auto styles = styleAttribute.globalMatch(html);
    while (styles.hasNext()) {
        const QRegularExpressionMatch match = styles.next();
        const QString value = match.captured(2);
        const QString mapped = remapDeclarations(value);
        if (mapped == value)
            continue;

        out += html.mid(cursor, match.capturedStart(2) - cursor);
        out += mapped;
        cursor = match.capturedEnd(2);
    }

    const QString stage = (cursor == 0) ? html : out + html.mid(cursor);

    out.clear();
    cursor = 0;

    auto attributes = colourAttribute.globalMatch(stage);
    while (attributes.hasNext()) {
        const QRegularExpressionMatch match = attributes.next();

        const QColor colour(match.captured(3));
        if (!colour.isValid() || colour.alpha() == 0)
            continue;

        const bool isBackground =
            match.captured(1).compare(QLatin1String("bgcolor"), Qt::CaseInsensitive) == 0;
        const QColor mapped = remap(colour, isBackground);
        if (mapped == colour)
            continue;

        out += stage.mid(cursor, match.capturedStart(3) - cursor);
        out += mapped.name(QColor::HexRgb);
        cursor = match.capturedEnd(3);
    }

    if (cursor == 0)
        return stage;

    out += stage.mid(cursor);
    return out;
}

} // namespace darkcolours
} // namespace qmdict
