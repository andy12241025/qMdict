#include "audioplayer.h"

#include "audiodecoder.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#  include <mmsystem.h>
#else
#  include <QProcess>
#endif

namespace qmdict {

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
{
}

AudioPlayer::~AudioPlayer()
{
#ifdef Q_OS_WIN
    // Stop any clip still reading from m_buffer before it is freed.
    PlaySoundW(nullptr, nullptr, 0);
#endif
    cleanupTemporaryFile();
}

void AudioPlayer::cleanupTemporaryFile()
{
    if (m_temporaryFile.isEmpty())
        return;
    QFile::remove(m_temporaryFile);
    m_temporaryFile.clear();
}

bool AudioPlayer::play(const QByteArray &data, QString *error)
{
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("the audio clip is empty");
        return false;
    }

    const AudioDecoder::Format format = AudioDecoder::detect(data);
    const QByteArray wav = AudioDecoder::toWav(data);
    if (wav.isEmpty()) {
        if (error) {
            *error = QStringLiteral("cannot play %1 audio")
                         .arg(AudioDecoder::formatName(format));
        }
        return false;
    }

#ifdef Q_OS_WIN
    PlaySoundW(nullptr, nullptr, 0);
    m_buffer = wav;
    const BOOL started = PlaySoundW(reinterpret_cast<LPCWSTR>(m_buffer.constData()), nullptr,
                                    SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    if (!started && error)
        *error = QStringLiteral("the system rejected the audio clip");
    return started;
#else
    cleanupTemporaryFile();

    const QString path =
        QDir(QDir::tempPath()).filePath(QStringLiteral("qmdict-pronunciation.wav"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("cannot write a temporary audio file");
        return false;
    }
    file.write(wav);
    file.close();
    m_temporaryFile = path;

    // No audio library is linked in; hand the decoded WAV to whichever simple
    // player the desktop provides.
    struct Candidate
    {
        const char *program;
        QStringList arguments;
    };
    const QList<Candidate> candidates = {
        {"pw-play", {}},
        {"paplay", {}},
        {"aplay", {QStringLiteral("-q")}},
        {"ffplay", {QStringLiteral("-nodisp"), QStringLiteral("-autoexit"),
                    QStringLiteral("-loglevel"), QStringLiteral("quiet")}},
        {"play", {QStringLiteral("-q")}},
    };

    for (const Candidate &candidate : candidates) {
        const QString program = QStandardPaths::findExecutable(QString::fromLatin1(candidate.program));
        if (program.isEmpty())
            continue;
        if (QProcess::startDetached(program, candidate.arguments + QStringList{path}))
            return true;
    }

    if (error)
        *error = QStringLiteral("no audio player found (install pipewire, pulseaudio or alsa-utils)");
    return false;
#endif
}

} // namespace qmdict
