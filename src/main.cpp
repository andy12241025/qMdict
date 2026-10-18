#include "ui/mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
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

    // A second launch hands its folder argument to the running instance and
    // exits, so repeated clicks on the launcher keep a single process.
    const QString serverName =
        QStringLiteral("qMdict-") + QString::fromLocal8Bit(qgetenv("USER")) +
        (portable ? QStringLiteral("-portable-") + QString::number(qHash(portableRoot()))
                  : QString());
    const QStringList positional = parser.positionalArguments();

    QLocalSocket probe;
    probe.connectToServer(serverName);
    if (probe.waitForConnected(500)) {
        probe.write(positional.isEmpty() ? QByteArray("show")
                                         : "open:" + positional.first().toUtf8());
        probe.waitForBytesWritten(500);
        probe.disconnectFromServer();
        return 0;
    }

    qmdict::MainWindow window(resolveCacheDirectory(portable));
    window.show();

    // A stale socket from a crashed run would block listen().
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&]() {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            QObject::connect(client, &QLocalSocket::readyRead, &window, [&window, client]() {
                const QByteArray msg = client->readAll();
                window.showNormal();
                window.raise();
                window.activateWindow();
                if (msg.startsWith("open:"))
                    window.openFolder(QString::fromUtf8(msg.mid(5)));
            });
        }
    });
    server.listen(serverName);

    if (!positional.isEmpty())
        window.openFolder(positional.first());

    return app.exec();
}
