#include "codec.h"

#include "lzo1x.h"
#include "ripemd128.h"

#include <cstring>

namespace qmdict {

QByteArray inflateZlib(const char *in, std::size_t inLen, std::size_t outLen)
{
    if (outLen == 0)
        return QByteArray();

    // qUncompress wants the expected size as a big-endian prefix. Using it
    // rather than linking zlib directly keeps the build to Qt alone, which
    // matters most on Windows where there is no system zlib.
    QByteArray framed;
    framed.reserve(int(inLen) + 4);
    framed.append(char((outLen >> 24) & 0xff));
    framed.append(char((outLen >> 16) & 0xff));
    framed.append(char((outLen >> 8) & 0xff));
    framed.append(char(outLen & 0xff));
    framed.append(in, int(inLen));

    QByteArray out = qUncompress(framed);
    if (std::size_t(out.size()) != outLen)
        return QByteArray();
    return out;
}

// MDict blocks are at most a few megabytes. A larger figure means the index is
// corrupt, and honouring it would mean a wild allocation.
constexpr std::size_t kMaxBlockSize = 512u * 1024u * 1024u;

QByteArray decodeBlock(const char *data, std::size_t size, std::size_t decompressedSize)
{
    if (size < 8 || decompressedSize > kMaxBlockSize)
        return QByteArray();

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    const uint32_t type = uint32_t(raw[0]) | (uint32_t(raw[1]) << 8) |
                          (uint32_t(raw[2]) << 16) | (uint32_t(raw[3]) << 24);

    const char *payload = data + 8;
    const std::size_t payloadSize = size - 8;

    switch (type) {
    case 0: // stored
        if (payloadSize < decompressedSize)
            return QByteArray();
        return QByteArray(payload, int(decompressedSize));

    case 1: { // LZO1X
        QByteArray out(int(decompressedSize), Qt::Uninitialized);
        std::size_t produced = decompressedSize;
        if (!lzo1xDecompress(reinterpret_cast<const uint8_t *>(payload), payloadSize,
                             reinterpret_cast<uint8_t *>(out.data()), &produced))
            return QByteArray();
        out.resize(int(produced));
        return out;
    }

    case 2: // zlib
        return inflateZlib(payload, payloadSize, decompressedSize);

    default:
        return QByteArray();
    }
}

void decryptKeyBlockInfo(char *block, std::size_t size)
{
    if (size <= 8)
        return;

    // Key material is the block's own Adler-32 field mixed with a fixed salt.
    uint8_t seed[8];
    std::memcpy(seed, block + 4, 4);
    seed[4] = 0x95;
    seed[5] = 0x36;
    seed[6] = 0x00;
    seed[7] = 0x00;

    uint8_t key[16];
    ripemd128(seed, sizeof(seed), key);

    uint8_t *p = reinterpret_cast<uint8_t *>(block) + 8;
    const std::size_t n = size - 8;
    uint8_t previous = 0x36;
    for (std::size_t i = 0; i < n; ++i) {
        const uint8_t current = p[i];
        uint8_t t = uint8_t((current >> 4) | (current << 4));
        t ^= uint8_t(previous ^ (i & 0xff) ^ key[i % 16]);
        previous = current;
        p[i] = t;
    }
}

} // namespace qmdict
