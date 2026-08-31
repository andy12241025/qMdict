#pragma once

#include "../audio/audioplayer.h"
#include "../mdict/library.h"
#include "theme.h"

#include <QMainWindow>
#include <QStringList>
#include <QVector>

class QAction;
class QLabel;
class QLineEdit;
class QListView;
class QSplitter;
class QStringListModel;
class QTimer;

namespace qmdict {

class ArticleView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &cacheDir, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens `folder` and indexes every dictionary below it.
    void openFolder(const QString &folder);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void chooseFolder();
    void reloadLibrary();
    void updateSuggestions();
    void showCurrentSelection();
    void navigateTo(const QString &word);
    void goBack();
    void goForward();
    void showDictionaryManager();
    void showAbout();
    void openExternal(const QUrl &url);
    void changeFontSize(qreal delta);
    void resetFontSize();
    void setMenuBarVisible(bool visible);

private:
    void buildUi();
    void buildActions();

    // Points the search box and result list at `word` without triggering a
    // second navigation from the list selection.
    void syncSearchTo(const QString &word);

    // Applies `points` to the article and scales the result list to match.
    void applyFontPointSize(qreal points);
    void restoreSettings();
    void saveSettings();
    void applyTheme(theme::Mode mode);
    void setStatusSummary();
    void updateHistoryActions();
    void display(const QString &word);

    Library m_library;
    AudioPlayer m_audio;

    QSplitter *m_splitter = nullptr;
    QLineEdit *m_search = nullptr;
    QListView *m_wordList = nullptr;
    QStringListModel *m_wordModel = nullptr;
    ArticleView *m_article = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_memory = nullptr;
    QTimer *m_searchTimer = nullptr;

    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_dictionaryStylesAction = nullptr;
    QAction *m_menuBarAction = nullptr;
    QVector<QAction *> m_themeActions;

    QStringList m_history;
    int m_historyPosition = -1;
    bool m_navigating = false;
    bool m_suppressSelectionNavigation = false;

    // The list's natural size at startup, so it scales with the article
    // instead of jumping to the article's point size.
    qreal m_baseListPointSize = 0;

    theme::Mode m_themeMode = theme::Mode::System;
    QString m_folder;
    QString m_cacheDir;
    QStringList m_disabledDictionaries;
};

} // namespace qmdict
