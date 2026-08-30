#include "theme.h"

#include "darkcolours.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace qmdict {
namespace theme {
namespace {

bool g_dark = false;

bool systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    const QPalette palette = QGuiApplication::palette();
    return palette.color(QPalette::WindowText).lightness() > palette.color(QPalette::Window).lightness();
#endif
}

QPalette darkPalette()
{
    const QColor window(0x1e, 0x1f, 0x22);
    const QColor base(0x18, 0x19, 0x1b);
    const QColor alternate(0x24, 0x25, 0x29);
    const QColor text(0xe4, 0xe4, 0xe7);
    const QColor disabled(0x6b, 0x6d, 0x76);
    const QColor highlight(0x3d, 0x7e, 0xff);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, alternate);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    p.setColor(QPalette::Link, QColor(0x6c, 0xb6, 0xff));
    p.setColor(QPalette::LinkVisited, QColor(0xb3, 0x92, 0xf0));
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, disabled);

    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, alternate);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return p;
}

QPalette lightPalette()
{
    const QColor window(0xf5, 0xf5, 0xf7);
    const QColor base(0xff, 0xff, 0xff);
    const QColor text(0x1a, 0x1a, 0x1c);
    const QColor disabled(0x9a, 0x9a, 0xa0);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0xec, 0xec, 0xef));
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(0x0b, 0x57, 0xd0));
    p.setColor(QPalette::LinkVisited, QColor(0x6b, 0x3f, 0xa0));
    p.setColor(QPalette::Highlight, QColor(0x2d, 0x6c, 0xdf));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, disabled);

    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    return p;
}

} // namespace

void apply(Mode mode)
{
    const bool dark = (mode == Mode::Dark) || (mode == Mode::System && systemPrefersDark());
    g_dark = dark;

    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QApplication::setPalette(dark ? darkPalette() : lightPalette());
}

bool isDark()
{
    return g_dark;
}

Mode fromString(const QString &value)
{
    if (value.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0)
        return Mode::Dark;
    if (value.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0)
        return Mode::Light;
    return Mode::System;
}

QString toString(Mode mode)
{
    switch (mode) {
    case Mode::Dark:
        return QStringLiteral("dark");
    case Mode::Light:
        return QStringLiteral("light");
    case Mode::System:
        break;
    }
    return QStringLiteral("system");
}

QString articleBaseCss(qreal pointSize)
{
    const QString foreground = g_dark ? QStringLiteral("#e4e4e7") : QStringLiteral("#1a1a1c");
    const QString muted = g_dark ? QStringLiteral("#8b8d98") : QStringLiteral("#6b6b73");
    const QString link = g_dark ? QStringLiteral("#6cb6ff") : QStringLiteral("#0b57d0");
    const QString rule = g_dark ? QStringLiteral("#3a3b40") : QStringLiteral("#d8d8de");
    const QString chip = g_dark ? QStringLiteral("#26272c") : QStringLiteral("#ececf0");

    return QStringLiteral(
               "body { color: %1; font-size: %6pt; }\n"
               "a { color: %2; text-decoration: none; }\n"
               "hr.qmdict-sep { border: 0; height: 1px; background: %3; }\n"
               ".qmdict-source { color: %4; background: %5; font-size: %7pt;\n"
               "  padding: 2px 6px; margin: 0 0 6px 0; }\n"
               ".qmdict-empty { color: %4; font-style: italic; }\n")
        .arg(foreground, link, rule, muted, chip)
        .arg(pointSize, 0, 'f', 1)
        .arg(pointSize * 0.82, 0, 'f', 1);
}

QString adaptStyleSheetForDark(const QString &css)
{
    if (!g_dark)
        return css;
    return darkcolours::adaptStyleSheet(css);
}

QString adaptHtmlForDark(const QString &html)
{
    if (!g_dark)
        return html;
    return darkcolours::adaptHtml(html);
}

} // namespace theme
} // namespace qmdict
