#include "articleview.h"

#include "../mdict/dictionary.h"
#include "theme.h"

#include <QImage>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QUrl>

namespace qmdict {
namespace {

// Anything the text engine cannot execute or resolve is stripped so it does
// not leak into the rendered page as literal text.
const QRegularExpression &scriptPattern()
{
    static const QRegularExpression re(QStringLiteral("<script\\b[^>]*>.*?</script\\s*>"),
                                       QRegularExpression::CaseInsensitiveOption |
                                           QRegularExpression::DotMatchesEverythingOption);
    return re;
}

const QRegularExpression &linkTagPattern()
{
    static const QRegularExpression re(QStringLiteral("<link\\b[^>]*>"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

ArticleView::ArticleView(QWidget *parent)
    : QTextBrowser(parent)
{
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setFrameShape(QFrame::NoFrame);
    document()->setDocumentMargin(14);

    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        const QString raw = url.toString();
        const QString scheme = url.scheme().toLower();

        if (scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
            scheme == QLatin1String("mailto") || scheme == QLatin1String("file") ||
            scheme == QLatin1String("sound")) {
            emit externalLinkActivated(url);
            return;
        }

        // Internal links such as "help:bre" or "helpp:n" belong to the
        // dictionary's own reader. Following them looks up nonsense.
        if (!htmlblocks::isNavigableHref(raw))
            return;

        // Everything else is a cross-reference to another headword. MDict uses
        // bare hrefs, "entry://word" and "x-dictionary:d:word" interchangeably.
        QString target = raw;
        if (scheme == QLatin1String("entry"))
            target = raw.mid(QStringLiteral("entry://").size());
        else if (scheme == QLatin1String("x-dictionary"))
            target = raw.mid(raw.lastIndexOf(QLatin1Char(':')) + 1);

        if (target.startsWith(QLatin1Char('#')))
            target.remove(0, 1);

        target = QUrl::fromPercentEncoding(target.toUtf8()).trimmed();
        if (!target.isEmpty())
            emit wordActivated(target);
    });
}

void ArticleView::setUseDictionaryStyles(bool enabled)
{
    if (m_useDictionaryStyles == enabled)
        return;
    m_useDictionaryStyles = enabled;
    rebuild();
}

void ArticleView::setFontPointSize(qreal points)
{
    const qreal clamped = qBound(kMinFontPointSize, points, kMaxFontPointSize);
    if (qFuzzyCompare(m_fontPointSize, clamped))
        return;

    m_fontPointSize = clamped;

    // Setting the widget font too keeps relative sizes in dictionary
    // stylesheets (em, %, larger) scaling along with the base size.
    QFont scaled = font();
    scaled.setPointSizeF(clamped);
    setFont(scaled);

    rebuild();
}

void ArticleView::refreshTheme()
{
    rebuild();
}

void ArticleView::showArticles(const QString &word,
                               const QVector<QPair<Dictionary *, QString>> &articles)
{
    m_word = word;
    m_articles = articles;
    rebuild();
}

void ArticleView::showMessage(const QString &title, const QString &body)
{
    m_word.clear();
    m_articles.clear();

    document()->setDefaultStyleSheet(theme::articleBaseCss(m_fontPointSize));
    setHtml(QStringLiteral("<h3>%1</h3><p class=\"qmdict-empty\">%2</p>")
                .arg(title.toHtmlEscaped(), body.toHtmlEscaped()));
}

QString ArticleView::collectStyles()
{
    const QString base = theme::articleBaseCss(m_fontPointSize);
    if (!m_useDictionaryStyles)
        return base;

    QString css = base;
    for (const auto &article : m_articles) {
        if (!article.first)
            continue;

        // Only the rules Qt can act on, and of those only the ones this
        // article can actually match. On a long Oxford entry that removes
        // about a second of selector matching.
        const QString embedded =
            cssfilter::relevantTo(usableStyleSheetFor(article.first, article.second), article.second);
        if (!embedded.isEmpty())
            css += QLatin1Char('\n') + theme::adaptStyleSheetForDark(embedded);
    }

    // The theme rules come last so headword colours stay readable even when a
    // dictionary stylesheet assumes a white page.
    return css + QLatin1Char('\n') + base;
}

const QString &ArticleView::usableStyleSheetFor(Dictionary *dictionary, const QString &articleHtml)
{
    const auto cached = m_usableStyles.constFind(dictionary);
    if (cached != m_usableStyles.constEnd())
        return cached.value();

    return *m_usableStyles.insert(dictionary,
                                  cssfilter::usable(dictionary->styleSheetFor(articleHtml)));
}

const htmlblocks::LayoutRules &ArticleView::layoutRulesFor(Dictionary *dictionary,
                                                         const QString &articleHtml)
{
    const auto cached = m_layoutRules.constFind(dictionary);
    if (cached != m_layoutRules.constEnd())
        return cached.value();

    // Block rules come from the dictionary's own stylesheet, before filtering:
    // `display` is exactly one of the properties the filter strips.
    return *m_layoutRules.insert(
        dictionary, htmlblocks::rulesFromStyleSheet(dictionary->styleSheetFor(articleHtml)));
}

QString ArticleView::sanitise(const QString &html)
{
    QString out = html;
    out.remove(scriptPattern());
    out.remove(linkTagPattern());
    return theme::adaptHtmlForDark(out);
}

void ArticleView::rebuild()
{
    if (m_articles.isEmpty()) {
        if (!m_word.isEmpty())
            showMessage(m_word, QStringLiteral("No dictionary contains this word."));
        return;
    }

    QString body;
    body.reserve(4096);

    for (const auto &article : m_articles) {
        const QString name = article.first ? article.first->title() : QString();
        body += QStringLiteral("<p class=\"qmdict-source\">%1</p>\n").arg(name.toHtmlEscaped());

        QString html = sanitise(article.second);
        if (article.first) {
            // Namespace prefixes are fixed even with dictionary styling off,
            // since <xhtml:br> is a line break in any theme.
            const htmlblocks::LayoutRules rules =
                m_useDictionaryStyles ? layoutRulesFor(article.first, article.second)
                                      : htmlblocks::LayoutRules{};
            html = htmlblocks::adaptForTextDocument(html, rules);
        }

        body += html;
        body += QLatin1String("\n");
    }

    document()->setDefaultStyleSheet(collectStyles());
    setHtml(QStringLiteral("<body>%1</body>").arg(body));
    verticalScrollBar()->setValue(0);
}

void ArticleView::setNavigationActions(QAction *back, QAction *forward)
{
    m_backAction = back;
    m_forwardAction = forward;
}

void ArticleView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu(event->pos());
    if (!menu) {
        QTextBrowser::contextMenuEvent(event);
        return;
    }

    if (m_backAction || m_forwardAction) {
        QAction *first = menu->actions().value(0);
        if (m_forwardAction)
            menu->insertAction(first, m_forwardAction);
        if (m_backAction)
            menu->insertAction(m_forwardAction ? m_forwardAction : first, m_backAction);
        if (first)
            menu->insertSeparator(first);
    }

    // Offer the selected text as a lookup, which is the other thing a reader
    // reaches for after highlighting a word.
    const QString selection = textCursor().selectedText().trimmed();
    if (!selection.isEmpty() && selection.size() <= 64) {
        menu->addSeparator();
        QAction *lookup = menu->addAction(QStringLiteral("Look Up \"%1\"").arg(selection));
        connect(lookup, &QAction::triggered, this,
                [this, selection]() { emit wordLookupRequested(selection); });
    }

    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

void ArticleView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Let the base class run first so it performs its own word selection.
    QTextBrowser::mouseDoubleClickEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    QString word = textCursor().selectedText();
    if (word.isEmpty()) {
        QTextCursor cursor = cursorForPosition(event->position().toPoint());
        cursor.select(QTextCursor::WordUnderCursor);
        word = cursor.selectedText();
    }

    // Selections pick up trailing punctuation and the odd non-breaking space.
    word.replace(QChar(0x2029), QLatin1Char(' '));
    word.replace(QChar(0x00a0), QLatin1Char(' '));
    while (!word.isEmpty() && !word.at(0).isLetterOrNumber())
        word.remove(0, 1);
    while (!word.isEmpty() && !word.at(word.size() - 1).isLetterOrNumber())
        word.chop(1);

    if (!word.isEmpty())
        emit wordLookupRequested(word.trimmed());
}

QVariant ArticleView::loadResource(int type, const QUrl &name)
{
    Q_UNUSED(type);

    const QString path = name.toString();
    if (path.isEmpty())
        return QVariant();

    // Only local dictionary resources are served; remote URLs are ignored so a
    // dictionary cannot make the app phone home.
    const QString scheme = name.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
        scheme == QLatin1String("data"))
        return QVariant();

    for (const auto &article : m_articles) {
        if (!article.first)
            continue;
        const QByteArray data = article.first->resource(path);
        if (data.isEmpty())
            continue;

        if (type == QTextDocument::ImageResource) {
            QImage image;
            if (image.loadFromData(data))
                return image;
        }
        return data;
    }

    return QVariant();
}

} // namespace qmdict
