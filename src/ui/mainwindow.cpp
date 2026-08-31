#include "mainwindow.h"

#include "articleview.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStringListModel>
#include <QSystemTrayIcon>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace qmdict {
namespace {

constexpr int kSuggestionLimit = 400;
constexpr int kSearchDelayMs = 120;

// Qt treats a sequence listed twice on one action as ambiguous and then
// triggers neither, so overlapping spellings have to be collapsed.
QList<QKeySequence> distinctShortcuts(std::initializer_list<QKeySequence> candidates)
{
    QList<QKeySequence> result;
    for (const QKeySequence &candidate : candidates) {
        if (!candidate.isEmpty() && !result.contains(candidate))
            result.append(candidate);
    }
    return result;
}

QString formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024);
    return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
}

} // namespace

MainWindow::MainWindow(const QString &cacheDir, QWidget *parent)
    : QMainWindow(parent)
    , m_cacheDir(cacheDir)
{
    setWindowTitle(QStringLiteral("qMdict"));
    buildUi();
    buildActions();
    restoreSettings();
    setupTrayIcon();

    connect(&m_library, &Library::loadingStarted, this, [this](int total) {
        m_status->setText(QStringLiteral("Indexing %1 dictionaries...").arg(total));
    });
    connect(&m_library, &Library::loadingProgress, this,
            [this](int done, int total, const QString &name) {
                m_status->setText(QStringLiteral("Indexing %1/%2: %3").arg(done).arg(total).arg(name));
            });
    connect(&m_library, &Library::loadingFinished, this, [this](int loaded, int failed) {
        for (Dictionary *dictionary : m_library.dictionaries()) {
            if (m_disabledDictionaries.contains(dictionary->path()))
                dictionary->setEnabled(false);
        }
        setStatusSummary();
        if (failed > 0)
            m_status->setText(m_status->text() + QStringLiteral("  (%1 skipped)").arg(failed));
        if (loaded > 0) {
            updateSuggestions();
            m_search->setFocus();
        } else {
            m_article->showMessage(QStringLiteral("No dictionaries found"),
                                   QStringLiteral("Choose a folder that contains .mdx files. "
                                                  "Sub-folders are searched automatically."));
        }
    });

    if (m_folder.isEmpty()) {
        m_article->showMessage(QStringLiteral("Welcome to qMdict"),
                               QStringLiteral("Open a folder of .mdx dictionaries to begin. "
                                              "Sub-folders are scanned recursively."));
    } else {
        openFolder(m_folder);
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Search"));
    m_search->setClearButtonEnabled(true);

    m_wordModel = new QStringListModel(this);
    m_wordList = new QListView(this);
    m_wordList->setModel(m_wordModel);
    m_wordList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_wordList->setUniformItemSizes(true);
    m_wordList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_wordList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(6, 6, 3, 6);
    leftLayout->setSpacing(6);
    leftLayout->addWidget(m_search);
    leftLayout->addWidget(m_wordList, 1);

    m_article = new ArticleView(this);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(left);
    m_splitter->addWidget(m_article);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 740});
    setCentralWidget(m_splitter);

    m_status = new QLabel(this);
    m_memory = new QLabel(this);
    statusBar()->addWidget(m_status, 1);
    statusBar()->addPermanentWidget(m_memory);

    m_baseListPointSize = m_wordList->font().pointSizeF();
    if (m_baseListPointSize <= 0)
        m_baseListPointSize = QApplication::font().pointSizeF();
    if (m_baseListPointSize <= 0)
        m_baseListPointSize = 10.0;

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(kSearchDelayMs);

    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::updateSuggestions);
    connect(m_search, &QLineEdit::textChanged, this, [this]() { m_searchTimer->start(); });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        const QString word = m_search->text().trimmed();
        if (!word.isEmpty())
            navigateTo(word);
    });
    connect(m_wordList->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::showCurrentSelection);
    connect(m_article, &ArticleView::wordActivated, this, [this](const QString &word) {
        syncSearchTo(word);
        navigateTo(word);
    });
    connect(m_article, &ArticleView::externalLinkActivated, this, &MainWindow::openExternal);
    connect(m_article, &ArticleView::wordLookupRequested, this, [this](const QString &word) {
        // Ignore double-clicks on words no dictionary has, so a stray click
        // cannot replace the article with a "not found" page.
        if (!m_library.contains(word)) {
            m_status->setText(QStringLiteral("\"%1\" is not in any dictionary").arg(word));
            return;
        }
        syncSearchTo(word);
        navigateTo(word);
    });

    resize(1040, 700);
}

void MainWindow::buildActions()
{
    auto makeAction = [this](const QString &text, auto slot, const QKeySequence &shortcut = {}) {
        auto *action = new QAction(text, this);
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    auto *openAction =
        makeAction(QStringLiteral("&Open Dictionary Folder..."), &MainWindow::chooseFolder,
                   QKeySequence::Open);
    auto *reloadAction =
        makeAction(QStringLiteral("&Reload"), &MainWindow::reloadLibrary, QKeySequence::Refresh);
    auto *quitAction =
        makeAction(QStringLiteral("&Quit"), &MainWindow::quitApplication, QKeySequence::Quit);

    m_closeToTrayAction = new QAction(QStringLiteral("Close to System &Tray"), this);
    m_closeToTrayAction->setCheckable(true);
    m_closeToTrayAction->setChecked(true);

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(openAction);
    fileMenu->addAction(reloadAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_closeToTrayAction);
    fileMenu->addAction(quitAction);

    m_backAction = makeAction(QStringLiteral("&Back"), &MainWindow::goBack, QKeySequence::Back);
    m_forwardAction =
        makeAction(QStringLiteral("&Forward"), &MainWindow::goForward, QKeySequence::Forward);
    auto *focusAction = makeAction(QStringLiteral("Focus &Search"),
                                   [this]() {
                                       m_search->setFocus();
                                       m_search->selectAll();
                                   },
                                   QKeySequence(QStringLiteral("Ctrl+L")));

    auto *goMenu = menuBar()->addMenu(QStringLiteral("&Go"));
    goMenu->addAction(m_backAction);
    goMenu->addAction(m_forwardAction);
    goMenu->addSeparator();
    goMenu->addAction(focusAction);

    m_article->setNavigationActions(m_backAction, m_forwardAction);

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto *themeMenu = viewMenu->addMenu(QStringLiteral("&Theme"));
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    const QVector<QPair<QString, theme::Mode>> themes = {
        {QStringLiteral("Follow &System"), theme::Mode::System},
        {QStringLiteral("&Light"), theme::Mode::Light},
        {QStringLiteral("&Dark"), theme::Mode::Dark},
    };

    for (const auto &entry : themes) {
        auto *action = new QAction(entry.first, this);
        action->setCheckable(true);
        action->setActionGroup(themeGroup);
        const theme::Mode mode = entry.second;
        action->setData(int(mode));
        connect(action, &QAction::triggered, this, [this, mode]() { applyTheme(mode); });
        themeMenu->addAction(action);
        m_themeActions.append(action);
    }

    auto *biggerAction = makeAction(QStringLiteral("&Increase Font Size"),
                                    [this]() { changeFontSize(1.0); }, QKeySequence::ZoomIn);
    auto *smallerAction = makeAction(QStringLiteral("&Decrease Font Size"),
                                     [this]() { changeFontSize(-1.0); }, QKeySequence::ZoomOut);
    auto *resetFontAction = makeAction(QStringLiteral("&Reset Font Size"),
                                       &MainWindow::resetFontSize,
                                       QKeySequence(QStringLiteral("Ctrl+0")));

    // Ctrl+= and Ctrl+Plus are the same physical key on different layouts and
    // QKeySequence::ZoomIn covers only one, so all the spellings are offered.
    // They must be de-duplicated first: the standard keys already expand to
    // Ctrl++ and Ctrl+-, and listing a sequence twice on one action makes Qt
    // call it ambiguous and fire nothing, which is why Ctrl+- never worked.
    biggerAction->setShortcuts(distinctShortcuts({QKeySequence::ZoomIn,
                                                  QKeySequence(QStringLiteral("Ctrl+=")),
                                                  QKeySequence(QStringLiteral("Ctrl++"))}));
    smallerAction->setShortcuts(distinctShortcuts({QKeySequence::ZoomOut,
                                                   QKeySequence(QStringLiteral("Ctrl+-")),
                                                   QKeySequence(QStringLiteral("Ctrl+_"))}));

    auto *fontMenu = viewMenu->addMenu(QStringLiteral("&Font Size"));
    fontMenu->addAction(biggerAction);
    fontMenu->addAction(smallerAction);
    fontMenu->addAction(resetFontAction);

    m_dictionaryStylesAction = new QAction(QStringLiteral("Use Dictionary St&yles"), this);
    m_dictionaryStylesAction->setCheckable(true);
    m_dictionaryStylesAction->setChecked(true);
    connect(m_dictionaryStylesAction, &QAction::toggled, this,
            [this](bool on) { m_article->setUseDictionaryStyles(on); });
    viewMenu->addAction(m_dictionaryStylesAction);

    m_menuBarAction = new QAction(QStringLiteral("Show &Menu Bar"), this);
    m_menuBarAction->setCheckable(true);
    m_menuBarAction->setChecked(true);
    m_menuBarAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    connect(m_menuBarAction, &QAction::toggled, this, &MainWindow::setMenuBarVisible);
    viewMenu->addAction(m_menuBarAction);

    // Owned by the window as well, so Ctrl+M still reaches it once the menu
    // bar it lives in is hidden.
    addAction(m_menuBarAction);

    viewMenu->addSeparator();
    viewMenu->addAction(
        makeAction(QStringLiteral("&Dictionaries..."), &MainWindow::showDictionaryManager));

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(makeAction(QStringLiteral("&About qMdict"), &MainWindow::showAbout));

    // A shortcut carried only by a menu action stops working the moment the
    // menu bar is hidden. Giving the window every shortcut-bearing action
    // fixes that once, rather than one action at a time.
    for (QAction *action : findChildren<QAction *>()) {
        if (!action->shortcuts().isEmpty() && !actions().contains(action))
            addAction(action);
    }

    updateHistoryActions();

    // Let Up/Down drive the result list while the cursor stays in the search box.
    m_search->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            QApplication::sendEvent(m_wordList, event);
            return true;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::restoreSettings()
{
    QSettings settings;

    m_folder = settings.value(QStringLiteral("library/folder")).toString();
    m_disabledDictionaries = settings.value(QStringLiteral("library/disabled")).toStringList();
    m_themeMode = theme::fromString(settings.value(QStringLiteral("ui/theme")).toString());

    const bool dictionaryStyles = settings.value(QStringLiteral("ui/dictionaryStyles"), true).toBool();
    m_dictionaryStylesAction->setChecked(dictionaryStyles);
    m_article->setUseDictionaryStyles(dictionaryStyles);

    applyFontPointSize(settings
                           .value(QStringLiteral("ui/fontPointSize"),
                                  ArticleView::kDefaultFontPointSize)
                           .toDouble());

    const bool menuBarVisible = settings.value(QStringLiteral("ui/menuBar"), true).toBool();
    m_menuBarAction->setChecked(menuBarVisible);
    menuBar()->setVisible(menuBarVisible);

    m_closeToTrayAction->setChecked(
        settings.value(QStringLiteral("ui/closeToTray"), true).toBool());

    if (settings.contains(QStringLiteral("ui/geometry")))
        restoreGeometry(settings.value(QStringLiteral("ui/geometry")).toByteArray());
    if (settings.contains(QStringLiteral("ui/splitter")))
        m_splitter->restoreState(settings.value(QStringLiteral("ui/splitter")).toByteArray());

    applyTheme(m_themeMode);
}

void MainWindow::saveSettings()
{
    QSettings settings;

    QStringList disabled;
    for (Dictionary *dictionary : m_library.dictionaries()) {
        if (!dictionary->isEnabled())
            disabled.append(dictionary->path());
    }

    settings.setValue(QStringLiteral("library/folder"), m_folder);
    settings.setValue(QStringLiteral("library/disabled"), disabled);
    settings.setValue(QStringLiteral("ui/theme"), theme::toString(m_themeMode));
    settings.setValue(QStringLiteral("ui/dictionaryStyles"), m_dictionaryStylesAction->isChecked());
    settings.setValue(QStringLiteral("ui/fontPointSize"), m_article->fontPointSize());
    settings.setValue(QStringLiteral("ui/menuBar"), m_menuBarAction->isChecked());
    settings.setValue(QStringLiteral("ui/closeToTray"), m_closeToTrayAction->isChecked());
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/splitter"), m_splitter->saveState());
}

void MainWindow::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
    m_tray->setToolTip(QStringLiteral("qMdict"));

    auto *menu = new QMenu(this);
    auto *show = menu->addAction(QStringLiteral("Show qMdict"));
    connect(show, &QAction::triggered, this, &MainWindow::toggleWindowVisible);
    menu->addSeparator();
    auto *quit = menu->addAction(QStringLiteral("Quit"));
    connect(quit, &QAction::triggered, this, &MainWindow::quitApplication);

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick)
                    toggleWindowVisible();
            });

    m_tray->show();
}

void MainWindow::toggleWindowVisible()
{
    if (isVisible() && !isMinimized()) {
        hide();
        return;
    }

    showNormal();
    raise();
    activateWindow();
    m_search->setFocus();
}

void MainWindow::quitApplication()
{
    m_quitting = true;
    close();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Settings are written on the way to the tray as well, so a later kill or
    // power loss does not lose them.
    saveSettings();

    if (!m_quitting && m_tray && m_closeToTrayAction->isChecked()) {
        hide();
        event->ignore();

        if (!m_trayHintShown) {
            m_trayHintShown = true;
            m_tray->showMessage(QStringLiteral("qMdict is still running"),
                                QStringLiteral("Click the tray icon to bring it back, or use "
                                               "Quit there to close it."),
                                QSystemTrayIcon::Information, 4000);
        }
        return;
    }

    m_library.cancelLoading();
    if (m_tray)
        m_tray->hide();
    QMainWindow::closeEvent(event);

    // Nothing keeps the process alive once the window is really gone, because
    // quitting on the last window is switched off for the tray's sake.
    QCoreApplication::quit();
}

void MainWindow::applyTheme(theme::Mode mode)
{
    m_themeMode = mode;
    theme::apply(mode);

    for (QAction *action : m_themeActions)
        action->setChecked(theme::Mode(action->data().toInt()) == mode);

    m_article->refreshTheme();
}

void MainWindow::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose a folder containing .mdx dictionaries"),
        m_folder.isEmpty() ? QDir::homePath() : m_folder);
    if (!folder.isEmpty())
        openFolder(folder);
}

void MainWindow::openFolder(const QString &folder)
{
    if (!QFileInfo(folder).isDir()) {
        m_article->showMessage(QStringLiteral("Folder not available"), folder);
        return;
    }

    m_folder = folder;
    m_wordModel->setStringList({});
    m_library.setCacheDirectory(m_cacheDir);
    m_library.loadFolder(folder);
    setWindowTitle(QStringLiteral("qMdict - %1").arg(QDir(folder).dirName()));
}

void MainWindow::reloadLibrary()
{
    if (!m_folder.isEmpty())
        openFolder(m_folder);
}

void MainWindow::updateSuggestions()
{
    const QString prefix = m_search->text().trimmed();
    if (prefix.isEmpty()) {
        m_wordModel->setStringList({});
        return;
    }

    m_wordModel->setStringList(m_library.suggestions(prefix, kSuggestionLimit));
    if (m_wordModel->rowCount() > 0)
        m_wordList->setCurrentIndex(m_wordModel->index(0, 0));
}

void MainWindow::showCurrentSelection()
{
    if (m_suppressSelectionNavigation)
        return;

    const QModelIndex index = m_wordList->currentIndex();
    if (index.isValid())
        navigateTo(index.data(Qt::DisplayRole).toString());
}

void MainWindow::navigateTo(const QString &word)
{
    const QString trimmed = word.trimmed();
    if (trimmed.isEmpty())
        return;

    if (!m_navigating) {
        if (m_historyPosition >= 0 && m_historyPosition < m_history.size() &&
            m_history.at(m_historyPosition) == trimmed) {
            display(trimmed);
            return;
        }
        m_history = m_history.mid(0, m_historyPosition + 1);
        m_history.append(trimmed);
        m_historyPosition = m_history.size() - 1;
        updateHistoryActions();
    }

    display(trimmed);
}

void MainWindow::display(const QString &word)
{
    const QVector<Library::Match> matches = m_library.lookup(word);

    QVector<QPair<Dictionary *, QString>> articles;
    articles.reserve(matches.size());
    for (const Library::Match &match : matches)
        articles.append({match.dictionary, match.html});

    m_article->showArticles(word, articles);

    if (matches.isEmpty())
        m_status->setText(QStringLiteral("\"%1\" not found").arg(word));
    else
        m_status->setText(QStringLiteral("\"%1\" - %2 dictionar%3")
                              .arg(word)
                              .arg(matches.size())
                              .arg(matches.size() == 1 ? QStringLiteral("y") : QStringLiteral("ies")));
}

void MainWindow::goBack()
{
    if (m_historyPosition <= 0)
        return;

    --m_historyPosition;
    m_navigating = true;
    navigateTo(m_history.at(m_historyPosition));
    m_navigating = false;
    updateHistoryActions();
}

void MainWindow::goForward()
{
    if (m_historyPosition + 1 >= m_history.size())
        return;

    ++m_historyPosition;
    m_navigating = true;
    navigateTo(m_history.at(m_historyPosition));
    m_navigating = false;
    updateHistoryActions();
}

void MainWindow::syncSearchTo(const QString &word)
{
    const QSignalBlocker blocker(m_search);
    m_search->setText(word);

    m_suppressSelectionNavigation = true;
    updateSuggestions();
    m_suppressSelectionNavigation = false;
}

void MainWindow::applyFontPointSize(qreal points)
{
    m_article->setFontPointSize(points);

    if (m_baseListPointSize <= 0)
        return;

    // The list follows the article proportionally, so it keeps the platform's
    // natural UI size until the reader actually changes the setting.
    const qreal scale = m_article->fontPointSize() / ArticleView::kDefaultFontPointSize;

    QFont listFont = m_wordList->font();
    listFont.setPointSizeF(m_baseListPointSize * scale);
    m_wordList->setFont(listFont);
    m_search->setFont(listFont);
}

void MainWindow::changeFontSize(qreal delta)
{
    applyFontPointSize(m_article->fontPointSize() + delta);
    m_status->setText(QStringLiteral("Font size %1 pt").arg(m_article->fontPointSize(), 0, 'f', 1));
}

void MainWindow::resetFontSize()
{
    applyFontPointSize(ArticleView::kDefaultFontPointSize);
    m_status->setText(QStringLiteral("Font size reset"));
}

void MainWindow::setMenuBarVisible(bool visible)
{
    menuBar()->setVisible(visible);
    if (!visible)
        m_status->setText(QStringLiteral("Menu bar hidden - press Ctrl+M to bring it back"));
}

void MainWindow::updateHistoryActions()
{
    if (m_backAction)
        m_backAction->setEnabled(m_historyPosition > 0);
    if (m_forwardAction)
        m_forwardAction->setEnabled(m_historyPosition + 1 < m_history.size());
}

void MainWindow::setStatusSummary()
{
    const int dictionaries = m_library.count();
    const int entries = m_library.totalEntryCount();

    m_status->setText(QStringLiteral("%1 dictionar%2, %3 headwords")
                          .arg(dictionaries)
                          .arg(dictionaries == 1 ? QStringLiteral("y") : QStringLiteral("ies"))
                          .arg(entries));
    m_memory->setText(QStringLiteral("index %1").arg(formatBytes(m_library.indexMemoryUsage())));
}

void MainWindow::showDictionaryManager()
{
    if (m_library.count() == 0) {
        QMessageBox::information(this, QStringLiteral("Dictionaries"),
                                 QStringLiteral("No dictionaries are loaded yet."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Dictionaries"));
    dialog.resize(560, 420);

    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget(&dialog);

    const QVector<Dictionary *> dictionaries = m_library.dictionaries();
    for (Dictionary *dictionary : dictionaries) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  -  %2 entries").arg(dictionary->title()).arg(dictionary->entryCount()),
            list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(dictionary->isEnabled() ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(dictionary->path());
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(list);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    for (int i = 0; i < dictionaries.size(); ++i)
        dictionaries.at(i)->setEnabled(list->item(i)->checkState() == Qt::Checked);

    updateSuggestions();
    setStatusSummary();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, QStringLiteral("About qMdict"),
        QStringLiteral("<h3>qMdict %1</h3>"
                       "<p>A small, fast offline reader for MDict (.mdx / .mdd) dictionaries.</p>"
                       "<p>Only the headword index is kept in memory; articles are decompressed "
                       "on demand.</p>"
                       "<p>Built with Qt %2. MIT licensed.<br>"
                       "<a href=\"https://github.com/andy12241025/qMdict\">"
                       "github.com/andy12241025/qMdict</a></p>")
            .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

void MainWindow::openExternal(const QUrl &url)
{
    const QString raw = url.toString();
    const bool isSoundScheme = url.scheme().compare(QLatin1String("sound"), Qt::CaseInsensitive) == 0;

    // Some dictionaries link audio with a plain href rather than sound://.
    static const QStringList audioSuffixes = {QStringLiteral(".spx"), QStringLiteral(".mp3"),
                                              QStringLiteral(".wav"), QStringLiteral(".ogg"),
                                              QStringLiteral(".oga")};
    bool looksLikeAudio = isSoundScheme;
    for (const QString &suffix : audioSuffixes) {
        if (raw.endsWith(suffix, Qt::CaseInsensitive))
            looksLikeAudio = true;
    }

    if (!looksLikeAudio) {
        QDesktopServices::openUrl(url);
        return;
    }

    QString name = raw;
    if (isSoundScheme)
        name = raw.mid(QStringLiteral("sound://").size());
    name = QUrl::fromPercentEncoding(name.toUtf8());

    for (Dictionary *dictionary : m_library.dictionaries()) {
        const QByteArray data = dictionary->resource(name);
        if (data.isEmpty())
            continue;

        QString error;
        if (m_audio.play(data, &error))
            m_status->setText(QStringLiteral("Playing %1").arg(QFileInfo(name).fileName()));
        else
            m_status->setText(QStringLiteral("%1: %2").arg(QFileInfo(name).fileName(), error));
        return;
    }

    m_status->setText(QStringLiteral("No audio found for %1").arg(QFileInfo(name).fileName()));
}

} // namespace qmdict
