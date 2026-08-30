#include "audiodecoder.h"

#include "oggstream.h"

#include <QList>
#include <QVector>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include <minimp3.h>

#include <speex/speex.h>
#include <speex/speex_callbacks.h>
#include <speex/speex_header.h>
#include <speex/speex_stereo.h>

#include <cstring>

namespace qmdict {
namespace {

void appendLe16(QByteArray *out, quint16 value)
{
    out->append(char(value & 0xff));
    out->append(char((value >> 8) & 0xff));
}

void appendLe32(QByteArray *out, quint32 value)
{
    out->append(char(value & 0xff));
    out->append(char((value >> 8) & 0xff));
    out->append(char((value >> 16) & 0xff));
    out->append(char((value >> 24) & 0xff));
}

bool looksLikeMp3(const QByteArray &data)
{
    if (data.size() < 4)
        return false;
    if (data.startsWith("ID3"))
        return true;

    // A bare frame header: eleven set sync bits, and neither the layer nor the
    // bitrate index may be the reserved value.
    const quint8 b0 = quint8(data.at(0));
    const quint8 b1 = quint8(data.at(1));
    const quint8 b2 = quint8(data.at(2));
    return b0 == 0xff && (b1 & 0xe0) == 0xe0 && (b1 & 0x06) != 0x00 && (b2 & 0xf0) != 0xf0;
}

QByteArray decodeMp3(const QByteArray &data)
{
    mp3dec_t decoder;
    mp3dec_init(&decoder);

    QByteArray pcm;
    mp3d_sample_t samples[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info;
    std::memset(&info, 0, sizeof(info));

    const quint8 *at = reinterpret_cast<const quint8 *>(data.constData());
    int left = data.size();
    int rate = 0;
    int channels = 0;

    while (left > 0) {
        const int produced = mp3dec_decode_frame(&decoder, at, left, samples, &info);
        if (info.frame_bytes <= 0)
            break;

        at += info.frame_bytes;
        left -= info.frame_bytes;

        if (produced <= 0)
            continue; // header or junk frame

        rate = info.hz;
        channels = info.channels;
        pcm.append(reinterpret_cast<const char *>(samples),
                   produced * info.channels * int(sizeof(mp3d_sample_t)));
    }

    if (pcm.isEmpty() || rate <= 0 || channels <= 0)
        return QByteArray();

    return AudioDecoder::buildWav(pcm, rate, channels);
}

QByteArray decodeSpeex(const QByteArray &data)
{
    QList<QByteArray> packets;
    if (!OggStream::readPackets(data, &packets) || packets.size() < 2)
        return QByteArray();

    // The first packet is the Speex header; the second is the comment block.
    QByteArray first = packets.first();
    SpeexHeader *header =
        speex_packet_to_header(const_cast<char *>(first.data()), first.size());
    if (!header)
        return QByteArray();

    const SpeexMode *mode = speex_lib_get_mode(header->mode);
    if (!mode) {
        speex_header_free(header);
        return QByteArray();
    }

    void *state = speex_decoder_init(mode);
    if (!state) {
        speex_header_free(header);
        return QByteArray();
    }

    int enhance = 1;
    speex_decoder_ctl(state, SPEEX_SET_ENH, &enhance);

    int frameSize = 0;
    speex_decoder_ctl(state, SPEEX_GET_FRAME_SIZE, &frameSize);

    const int channels = header->nb_channels >= 2 ? 2 : 1;
    const int rate = header->rate;
    // Speex tops out at 32 kHz; anything else means a corrupt header, and a
    // silly frame count would otherwise spin for a long time.
    const int framesPerPacket = qBound(1, header->frames_per_packet, 64);

    if (frameSize <= 0 || frameSize > 4096 || rate < 1000 || rate > 48000) {
        speex_decoder_destroy(state);
        speex_header_free(header);
        return QByteArray();
    }

    SpeexBits bits;
    speex_bits_init(&bits);

    SpeexStereoState stereo = SPEEX_STEREO_STATE_INIT;

    QByteArray pcm;
    QVector<spx_int16_t> frame(std::size_t(frameSize) * 2);

    // Packet 0 is the header and packet 1 the comments; audio starts after the
    // extra headers the header block declares.
    const int firstAudio = 2 + qMax(0, header->extra_headers);
    for (int i = firstAudio; i < packets.size(); ++i) {
        const QByteArray &packet = packets.at(i);
        if (packet.isEmpty())
            continue;

        speex_bits_read_from(&bits, const_cast<char *>(packet.constData()), packet.size());

        for (int f = 0; f < framesPerPacket; ++f) {
            if (speex_decode_int(state, &bits, frame.data()) != 0)
                break;
            if (speex_bits_remaining(&bits) < 0)
                break;

            if (channels == 2)
                speex_decode_stereo_int(frame.data(), frameSize, &stereo);

            pcm.append(reinterpret_cast<const char *>(frame.constData()),
                       frameSize * channels * int(sizeof(spx_int16_t)));
        }
    }

    speex_bits_destroy(&bits);
    speex_decoder_destroy(state);
    speex_header_free(header);

    if (pcm.isEmpty())
        return QByteArray();

    return AudioDecoder::buildWav(pcm, rate, channels);
}

} // namespace

AudioDecoder::Format AudioDecoder::detect(const QByteArray &data)
{
    if (data.size() < 12)
        return Format::Unknown;

    if (data.startsWith("RIFF") && std::memcmp(data.constData() + 8, "WAVE", 4) == 0)
        return Format::Wav;

    if (OggStream::looksLikeOgg(data)) {
        // The codec name sits in the first packet, early in the first page.
        const QByteArray head = data.left(128);
        if (head.contains("Speex   "))
            return Format::Speex;
        return Format::OtherOgg;
    }

    if (looksLikeMp3(data))
        return Format::Mp3;

    return Format::Unknown;
}

QString AudioDecoder::formatName(Format format)
{
    switch (format) {
    case Format::Wav:
        return QStringLiteral("WAV");
    case Format::Mp3:
        return QStringLiteral("MP3");
    case Format::Speex:
        return QStringLiteral("Speex");
    case Format::OtherOgg:
        return QStringLiteral("Ogg");
    case Format::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QByteArray AudioDecoder::buildWav(const QByteArray &pcm, int sampleRate, int channels)
{
    // A damaged header can claim any rate at all, and the derived byte rate
    // would overflow before it ever reached the file.
    if (pcm.isEmpty() || sampleRate < 1000 || sampleRate > 384000 || channels < 1 || channels > 8)
        return QByteArray();

    const quint32 byteRate = quint32(sampleRate) * quint32(channels) * 2u;

    QByteArray wav;
    wav.reserve(pcm.size() + 44);
    wav.append("RIFF");
    appendLe32(&wav, quint32(36 + pcm.size()));
    wav.append("WAVEfmt ");
    appendLe32(&wav, 16);                      // PCM chunk size
    appendLe16(&wav, 1);                       // PCM
    appendLe16(&wav, quint16(channels));
    appendLe32(&wav, quint32(sampleRate));
    appendLe32(&wav, byteRate);
    appendLe16(&wav, quint16(channels * 2));   // block align
    appendLe16(&wav, 16);                      // bits per sample
    wav.append("data");
    appendLe32(&wav, quint32(pcm.size()));
    wav.append(pcm);
    return wav;
}

QByteArray AudioDecoder::toWav(const QByteArray &data)
{
    switch (detect(data)) {
    case Format::Wav:
        return data;
    case Format::Mp3:
        return decodeMp3(data);
    case Format::Speex:
        return decodeSpeex(data);
    case Format::OtherOgg:
    case Format::Unknown:
        break;
    }
    return QByteArray();
}

} // namespace qmdict
