#include "lzo1x.h"

namespace qmdict {

// The control flow below mirrors the LZO1X stream grammar, which is naturally
// expressed as a state machine with jumps between literal runs and matches.
bool lzo1xDecompress(const uint8_t *in, std::size_t inLen, uint8_t *out, std::size_t *outLen)
{
    const uint8_t *ip = in;
    const uint8_t *const ipEnd = in + inLen;
    uint8_t *op = out;
    uint8_t *const opEnd = out + *outLen;
    const uint8_t *mPos = nullptr;
    std::size_t t = 0;
    std::size_t n = 0;

#define QMDICT_LZO_LITERALS(count)                                     \
    do {                                                               \
        if (std::size_t(opEnd - op) < (count)) return false;           \
        if (std::size_t(ipEnd - ip) < (count) + 3) return false;       \
        n = (count);                                                   \
        do { *op++ = *ip++; } while (--n);                             \
    } while (false)

#define QMDICT_LZO_EXTEND(base)                                        \
    do {                                                               \
        while (ip < ipEnd && *ip == 0) { t += 255; ++ip; }             \
        if (ip >= ipEnd) return false;                                 \
        t += (base) + *ip++;                                           \
    } while (false)

    if (inLen == 0)
        return false;

    if (*ip > 17) {
        t = std::size_t(*ip++) - 17;
        if (t < 4)
            goto match_next;
        QMDICT_LZO_LITERALS(t);
        goto first_literal_run;
    }

next_block:
    if (ip >= ipEnd)
        return false;
    t = *ip++;
    if (t >= 16)
        goto match;
    if (t == 0)
        QMDICT_LZO_EXTEND(15);
    t += 3;
    QMDICT_LZO_LITERALS(t);

first_literal_run:
    if (ip >= ipEnd)
        return false;
    t = *ip++;
    if (t >= 16)
        goto match;
    if (ip >= ipEnd)
        return false;
    mPos = op - (1 + 0x0800) - (t >> 2) - (std::size_t(*ip++) << 2);
    if (mPos < out || opEnd - op < 3)
        return false;
    *op++ = *mPos++;
    *op++ = *mPos++;
    *op++ = *mPos++;
    goto match_done;

match:
    if (t >= 64) {
        if (ip >= ipEnd)
            return false;
        mPos = op - 1 - ((t >> 2) & 7) - (std::size_t(*ip++) << 3);
        t = (t >> 5) - 1;
    } else if (t >= 32) {
        t &= 31;
        if (t == 0)
            QMDICT_LZO_EXTEND(31);
        if (ipEnd - ip < 2)
            return false;
        mPos = op - 1 - ((std::size_t(ip[0]) >> 2) + (std::size_t(ip[1]) << 6));
        ip += 2;
    } else if (t >= 16) {
        mPos = op - ((t & 8) << 11);
        t &= 7;
        if (t == 0)
            QMDICT_LZO_EXTEND(7);
        if (ipEnd - ip < 2)
            return false;
        mPos -= (std::size_t(ip[0]) >> 2) + (std::size_t(ip[1]) << 6);
        ip += 2;
        if (mPos == op)
            goto eof_found;
        mPos -= 0x4000;
    } else {
        if (ip >= ipEnd)
            return false;
        mPos = op - 1 - (t >> 2) - (std::size_t(*ip++) << 2);
        if (mPos < out || opEnd - op < 2)
            return false;
        *op++ = *mPos++;
        *op++ = *mPos++;
        goto match_done;
    }

    if (mPos < out || mPos >= op)
        return false;
    if (std::size_t(opEnd - op) < t + 2)
        return false;
    n = t + 2;
    do { *op++ = *mPos++; } while (--n);

match_done:
    t = std::size_t(ip[-2]) & 3;
    if (t == 0)
        goto next_block;

match_next:
    QMDICT_LZO_LITERALS(t);
    if (ip >= ipEnd)
        return false;
    t = *ip++;
    goto match;

eof_found:
    *outLen = std::size_t(op - out);
    return ip <= ipEnd;

#undef QMDICT_LZO_LITERALS
#undef QMDICT_LZO_EXTEND
}

} // namespace qmdict
