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

    // The dictionary's own CSS, taken from the first .css found in its .mdd
    // archives (many MDX files reference one with a <link> tag).
    QString embeddedStyleSheet();

private:
    QString decodeRecord(const QByteArray &raw) const;
    QString applyStyleSheet(const QString &text) const;

    std::unique_ptr<MdictFile> m_mdx;
    std::vector<std::unique_ptr<MdictFile>> m_mdd;
    QHash<QString, QPair<QString, QString>> m_styleRules; // id -> (prefix, suffix)
    bool m_enabled = true;
    bool m_cssProbed = false;
    QString m_css;
};

} // namespace qmdict
