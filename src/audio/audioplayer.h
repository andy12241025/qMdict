// Plays a pronunciation clip without involving the desktop's file associations.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace qmdict {

class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    // Decodes `data` and starts playback. Returns false, with `error` set, if
    // the clip is in a format qMdict cannot decode.
    bool play(const QByteArray &data, QString *error = nullptr);

private:
    void cleanupTemporaryFile();

    // Windows plays straight from memory, so the buffer has to outlive the
    // call that starts playback.
    QByteArray m_buffer;
    QString m_temporaryFile;
};

} // namespace qmdict
