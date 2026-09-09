// Definition pane.
//
// QTextBrowser is used instead of a web engine: it starts instantly, costs a
// few megabytes rather than a few hundred, and renders the HTML subset that
// dictionary articles actually use. Images and stylesheets are served straight
// out of the .mdd archives through loadResource().
#pragma once

#include "cssfilter.h"
#include "htmlblocks.h"

#include <QHash>
#include <QTextBrowser>
#include <QVector>

namespace qmdict {

class Dictionary;

class ArticleView : public QTextBrowser
{
    Q_OBJECT

public:
    explicit ArticleView(QWidget *parent = nullptr);

    // One rendered source. `dictionary` is null for an article that came from
    // somewhere else, which brings no stylesheet and no resources of its own,
    // and is named by `source` instead.
    struct Article
    {
        Dictionary *dictionary = nullptr;
        QString source;
        QString html;
    };

    // Renders `word` from the supplied sources, in the order given.
    void showArticles(const QString &word, const QVector<Article> &articles);
    void showMessage(const QString &title, const QString &body);

    void setUseDictionaryStyles(bool enabled);
    bool usesDictionaryStyles() const { return m_useDictionaryStyles; }

    // Body text size in points; other sizes in the base stylesheet scale with it.
    void setFontPointSize(qreal points);
    qreal fontPointSize() const { return m_fontPointSize; }

    static constexpr qreal kMinFontPointSize = 6.0;
    static constexpr qreal kMaxFontPointSize = 36.0;
    static constexpr qreal kDefaultFontPointSize = 10.5;

    void refreshTheme();

    // Offered at the top of the context menu, so navigation stays reachable
    // when the menu bar is hidden.
    void setNavigationActions(QAction *back, QAction *forward);

    // Offered at the bottom of the context menu, next to where the recent list
    // is read.
    void setClearHistoryAction(QAction *action);

    // Builds everything a dictionary's first article would otherwise build on
    // the spot: its filtered stylesheet, its layout rules, and the fonts that
    // stylesheet asks the platform for.
    void warmUp(Dictionary *dictionary, const QString &articleHtml);

    // Drops every reference to the open dictionaries, which the caches and the
    // article on screen both hold. Must be called before they are destroyed.
    void forgetDictionaries();

signals:
    void wordActivated(const QString &word);

    // A word the reader double-clicked. Unlike a link, this is a guess, so the
    // window only navigates when some dictionary actually has the word.
    void wordLookupRequested(const QString &word);

    void externalLinkActivated(const QUrl &url);

protected:
    QVariant loadResource(int type, const QUrl &name) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void rebuild();
    QString collectStyles();
    static QString sanitise(const QString &html);

    // Filtering 24 KB of stylesheet is not free, and the result never changes
    // while the dictionary is open.
    const QString &usableStyleSheetFor(Dictionary *dictionary, const QString &articleHtml);

    // Parsing a dictionary's stylesheet is not free, and it never changes
    // while the dictionary is open.
    const htmlblocks::LayoutRules &layoutRulesFor(Dictionary *dictionary, const QString &articleHtml);

    QString m_word;
    QVector<Article> m_articles;
    bool m_useDictionaryStyles = true;
    qreal m_fontPointSize = kDefaultFontPointSize;
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_clearHistoryAction = nullptr;
    QHash<Dictionary *, htmlblocks::LayoutRules> m_layoutRules;
    QHash<Dictionary *, QString> m_usableStyles;
};

} // namespace qmdict
