#include "oggstream.h"

#include <cstring>

namespace qmdict {
namespace {

constexpr int kHeaderSize = 27; // up to and including the segment count

quint32 readLe32(const char *p)
{
    return quint32(quint8(p[0])) | (quint32(quint8(p[1])) << 8) |
           (quint32(quint8(p[2])) << 16) | (quint32(quint8(p[3])) << 24);
}

} // namespace

bool OggStream::looksLikeOgg(const QByteArray &data)
{
    return data.size() >= 4 && data.startsWith("OggS");
}

bool OggStream::readPackets(const QByteArray &data, QList<QByteArray> *packets)
{
    if (!looksLikeOgg(data))
        return false;

    packets->clear();

    quint32 serial = 0;
    bool serialKnown = false;
    QByteArray pending;
    int pos = 0;

    while (pos + kHeaderSize <= data.size()) {
        if (std::memcmp(data.constData() + pos, "OggS", 4) != 0)
            return !packets->isEmpty();

        const char *header = data.constData() + pos;
        const quint8 headerType = quint8(header[5]);
        const quint32 pageSerial = readLe32(header + 14);
        const int segmentCount = quint8(header[26]);

        const int tableAt = pos + kHeaderSize;
        if (tableAt + segmentCount > data.size())
            break;

        int payloadSize = 0;
        for (int i = 0; i < segmentCount; ++i)
            payloadSize += quint8(data.at(tableAt + i));

        const int payloadAt = tableAt + segmentCount;
        if (payloadAt + payloadSize > data.size())
            break;

        // Ignore any bitstream multiplexed after the first one.
        if (!serialKnown) {
            serial = pageSerial;
            serialKnown = true;
        }

        if (pageSerial == serial) {
            // A page that does not continue the previous packet invalidates
            // whatever partial packet we were holding.
            if ((headerType & 0x01) == 0)
                pending.clear();

            int at = payloadAt;
            for (int i = 0; i < segmentCount; ++i) {
                const int length = quint8(data.at(tableAt + i));
                pending.append(data.constData() + at, length);
                at += length;

                // Any segment shorter than 255 bytes terminates the packet.
                if (length < 255) {
                    packets->append(pending);
                    pending.clear();
                }
            }
        }

        pos = payloadAt + payloadSize;
    }

    return !packets->isEmpty();
}

} // namespace qmdict
