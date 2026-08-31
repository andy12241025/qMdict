// One logical dictionary: an .mdx of definitions plus any .mdd archives that
// ship its images, stylesheets and audio.
#pragma once

#include "mdictfile.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace qmdict {

class Dictionary
{
public:
    Dictionary();
    ~Dictionary();

    Dictionary(const Dictionary &) = delete;
    Dictionary &operator=(const Dictionary &) = delete;

    // Opens `mdxPath` and every .mdd sharing its base name (foo.mdd,
    // foo.1.mdd, foo.2.mdd, ...).
    bool load(const QString &mdxPath, const QString &cacheDir, QString *error);

    QString title() const;
    QString path() const { return m_mdx ? m_mdx->path() : QString(); }
    QString description() const { return m_mdx ? m_mdx->description() : QString(); }
    int entryCount() const { return m_mdx ? m_mdx->entryCount() : 0; }
    int resourceCount() const;
    qint64 indexMemoryUsage() const;

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    // Headwords starting with `prefix`, in sorted order.
    QStringList completions(const QString &prefix, int limit) const;

    bool contains(const QString &word) const;

    // Definition HTML for `word`, following @@@LINK redirects and applying the
    // header stylesheet. Empty when the word is absent.
    QString definition(const QString &word);

    // Resource bytes from the .mdd archives; `name` may use either slash style
    // and may omit the leading separator.
    QByteArray resource(const QString &name);

    // The dictionary's own CSS for `articleHtml`, resolved from the
    // <link rel="stylesheet"> tags the article carries. Dictionaries ship this
    // either inside their .mdd or as a plain file beside the .mdx, and getting
    // it wrong is what turns a formatted entry into a wall of text.
    QString styleSheetFor(const QString &articleHtml);

private:
    QString decodeRecord(const QByteArray &raw) const;
    QString applyStyleSheet(const QString &text) const;

    QString loadStyleSheet(const QString &name);
    QString fallbackStyleSheet();

    std::unique_ptr<MdictFile> m_mdx;
    std::vector<std::unique_ptr<MdictFile>> m_mdd;
    QHash<QString, QPair<QString, QString>> m_styleRules; // id -> (prefix, suffix)
    bool m_enabled = true;
    QHash<QString, QString> m_styleSheets; // href -> css, empty when unresolved
    bool m_fallbackProbed = false;
    QString m_fallback;
};

} // namespace qmdict
