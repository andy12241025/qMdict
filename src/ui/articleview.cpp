#include "articleview.h"

#include "../mdict/dictionary.h"
#include "theme.h"

#include <QImage>
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

QString ArticleView::collectStyles() const
{
    const QString base = theme::articleBaseCss(m_fontPointSize);
    if (!m_useDictionaryStyles)
        return base;

    QString css = base;
    for (const auto &article : m_articles) {
        if (!article.first)
            continue;
        // Resolved from the article's own <link> tags, which is why this uses
        // the raw HTML rather than the sanitised copy.
        const QString embedded = article.first->styleSheetFor(article.second);
        if (!embedded.isEmpty())
            css += QLatin1Char('\n') + theme::adaptStyleSheetForDark(embedded);
    }

    // The theme rules come last so headword colours stay readable even when a
    // dictionary stylesheet assumes a white page.
    return css + QLatin1Char('\n') + base;
}

const htmlblocks::BlockRules &ArticleView::blockRulesFor(Dictionary *dictionary,
                                                         const QString &articleHtml)
{
    const auto cached = m_blockRules.constFind(dictionary);
    if (cached != m_blockRules.constEnd())
        return cached.value();

    return *m_blockRules.insert(
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
        if (m_useDictionaryStyles && article.first)
            html = htmlblocks::promoteSpans(html, blockRulesFor(article.first, article.second));

        body += html;
        body += QLatin1String("\n");
    }

    document()->setDefaultStyleSheet(collectStyles());
    setHtml(QStringLiteral("<body>%1</body>").arg(body));
    verticalScrollBar()->setValue(0);
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
