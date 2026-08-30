// Turns the audio blobs stored in .mdd archives into plain PCM WAV.
//
// Dictionaries mostly ship Speex (.spx), MP3 or WAV. Windows cannot play Speex
// at all, which is why handing the file to the shell only ever produced an
// "open with" prompt. Decoding here, with two small vendored decoders, keeps
// the download tiny compared with pulling in a full media backend.
#pragma once

#include <QByteArray>
#include <QString>

namespace qmdict {

class AudioDecoder
{
public:
    enum class Format {
        Unknown,
        Wav,
        Mp3,
        Speex,
        OtherOgg, // Vorbis or Opus: recognised, but not decoded here
    };

    // Identifies `data` by content rather than by file extension, which
    // dictionaries are not consistent about.
    static Format detect(const QByteArray &data);

    // Returns a 16-bit PCM WAV rendering of `data`, or an empty array if the
    // format is not one we can decode.
    static QByteArray toWav(const QByteArray &data);

    static QString formatName(Format format);

    // Wraps raw little-endian 16-bit samples in a WAV header.
    static QByteArray buildWav(const QByteArray &pcm, int sampleRate, int channels);
};

} // namespace qmdict
