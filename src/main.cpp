#include "ui/mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QSettings>

#ifndef QMDICT_VERSION
#  define QMDICT_VERSION "0.0.0-dev"
#endif
#include <QStandardPaths>

namespace {

// In portable mode the index cache and settings live inside the unzipped
// folder so it stays self-contained and leaves nothing behind on the machine.
// The released layout puts the executable in bin/, one level below data/.
QString portableRoot()
{
    const QDir appDir(QCoreApplication::applicationDirPath());

    // The Linux bundle puts the binary in bin/ with data/ beside it; the
    // Windows one keeps both at the top level.
    if (appDir.dirName().compare(QLatin1String("bin"), Qt::CaseInsensitive) == 0)
        return QDir::cleanPath(appDir.filePath(QStringLiteral("../data")));

    return appDir.filePath(QStringLiteral("data"));
}

QString resolveCacheDirectory(bool portable)
{
    const QString dir = portable
                            ? portableRoot() + QStringLiteral("/cache")
                            : QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                                  QStringLiteral("/index");

    if (!QDir().mkpath(dir))
        return QString();
    return dir;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qMdict"));
    QCoreApplication::setOrganizationName(QStringLiteral("qMdict"));
    QCoreApplication::setApplicationVersion(QStringLiteral(QMDICT_VERSION));

    // The window hides to the tray instead of closing, so the process must
    // not exit with it. MainWindow quits explicitly when it really closes.
    QApplication::setQuitOnLastWindowClosed(false);

    // Every size is registered so window decorations, the task switcher and
    // the taskbar each pick the one they need instead of rescaling.
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/qmdict-%1.png").arg(size));
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Offline MDict (.mdx/.mdd) dictionary reader"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portableOption(
        QStringList{QStringLiteral("p"), QStringLiteral("portable")},
        QStringLiteral("Keep settings and the index cache beside the executable."));
    parser.addOption(portableOption);

    parser.addPositionalArgument(QStringLiteral("folder"),
                                 QStringLiteral("Folder of dictionaries to open."));
    parser.process(app);

    const bool portable = parser.isSet(portableOption);
    if (portable) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, portableRoot());
    }

    qmdict::MainWindow window(resolveCacheDirectory(portable));
    window.show();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty())
        window.openFolder(positional.first());

    return app.exec();
}
