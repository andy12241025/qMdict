// Minimal Ogg container demuxer.
//
// Pronunciation clips in MDict dictionaries are usually Ogg-encapsulated Speex
// (.spx). Only packet extraction is needed, which is a small enough job that
// vendoring libogg as well would not pay for itself.
#pragma once

#include <QByteArray>
#include <QList>

namespace qmdict {

class OggStream
{
public:
    // Splits `data` into the packets of its first logical bitstream. Returns
    // false if the data is not Ogg or is too damaged to walk.
    static bool readPackets(const QByteArray &data, QList<QByteArray> *packets);

    static bool looksLikeOgg(const QByteArray &data);
};

} // namespace qmdict
