#include "ripemd128.h"

#include <cstring>

namespace qmdict {
namespace {

inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

inline uint32_t f1(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
inline uint32_t f2(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
inline uint32_t f3(uint32_t x, uint32_t y, uint32_t z) { return (x | ~y) ^ z; }
inline uint32_t f4(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }

#define FF(a, b, c, d, x, s)  { (a) += f1((b), (c), (d)) + (x);              (a) = rol((a), (s)); }
#define GG(a, b, c, d, x, s)  { (a) += f2((b), (c), (d)) + (x) + 0x5a827999u; (a) = rol((a), (s)); }
#define HH(a, b, c, d, x, s)  { (a) += f3((b), (c), (d)) + (x) + 0x6ed9eba1u; (a) = rol((a), (s)); }
#define II(a, b, c, d, x, s)  { (a) += f4((b), (c), (d)) + (x) + 0x8f1bbcdcu; (a) = rol((a), (s)); }
#define FFF(a, b, c, d, x, s) { (a) += f1((b), (c), (d)) + (x);              (a) = rol((a), (s)); }
#define GGG(a, b, c, d, x, s) { (a) += f2((b), (c), (d)) + (x) + 0x6d703ef3u; (a) = rol((a), (s)); }
#define HHH(a, b, c, d, x, s) { (a) += f3((b), (c), (d)) + (x) + 0x5c4dd124u; (a) = rol((a), (s)); }
#define III(a, b, c, d, x, s) { (a) += f4((b), (c), (d)) + (x) + 0x50a28be6u; (a) = rol((a), (s)); }

void compress(uint32_t state[4], const uint32_t x[16])
{
    uint32_t aa = state[0], bb = state[1], cc = state[2], dd = state[3];
    uint32_t aaa = state[0], bbb = state[1], ccc = state[2], ddd = state[3];

    FF(aa, bb, cc, dd, x[0], 11);  FF(dd, aa, bb, cc, x[1], 14);
    FF(cc, dd, aa, bb, x[2], 15);  FF(bb, cc, dd, aa, x[3], 12);
    FF(aa, bb, cc, dd, x[4], 5);   FF(dd, aa, bb, cc, x[5], 8);
    FF(cc, dd, aa, bb, x[6], 7);   FF(bb, cc, dd, aa, x[7], 9);
    FF(aa, bb, cc, dd, x[8], 11);  FF(dd, aa, bb, cc, x[9], 13);
    FF(cc, dd, aa, bb, x[10], 14); FF(bb, cc, dd, aa, x[11], 15);
    FF(aa, bb, cc, dd, x[12], 6);  FF(dd, aa, bb, cc, x[13], 7);
    FF(cc, dd, aa, bb, x[14], 9);  FF(bb, cc, dd, aa, x[15], 8);

    GG(aa, bb, cc, dd, x[7], 7);   GG(dd, aa, bb, cc, x[4], 6);
    GG(cc, dd, aa, bb, x[13], 8);  GG(bb, cc, dd, aa, x[1], 13);
    GG(aa, bb, cc, dd, x[10], 11); GG(dd, aa, bb, cc, x[6], 9);
    GG(cc, dd, aa, bb, x[15], 7);  GG(bb, cc, dd, aa, x[3], 15);
    GG(aa, bb, cc, dd, x[12], 7);  GG(dd, aa, bb, cc, x[0], 12);
    GG(cc, dd, aa, bb, x[9], 15);  GG(bb, cc, dd, aa, x[5], 9);
    GG(aa, bb, cc, dd, x[2], 11);  GG(dd, aa, bb, cc, x[14], 7);
    GG(cc, dd, aa, bb, x[11], 13); GG(bb, cc, dd, aa, x[8], 12);

    HH(aa, bb, cc, dd, x[3], 11);  HH(dd, aa, bb, cc, x[10], 13);
    HH(cc, dd, aa, bb, x[14], 6);  HH(bb, cc, dd, aa, x[4], 7);
    HH(aa, bb, cc, dd, x[9], 14);  HH(dd, aa, bb, cc, x[15], 9);
    HH(cc, dd, aa, bb, x[8], 13);  HH(bb, cc, dd, aa, x[1], 15);
    HH(aa, bb, cc, dd, x[2], 14);  HH(dd, aa, bb, cc, x[7], 8);
    HH(cc, dd, aa, bb, x[0], 13);  HH(bb, cc, dd, aa, x[6], 6);
    HH(aa, bb, cc, dd, x[13], 5);  HH(dd, aa, bb, cc, x[11], 12);
    HH(cc, dd, aa, bb, x[5], 7);   HH(bb, cc, dd, aa, x[12], 5);

    II(aa, bb, cc, dd, x[1], 11);  II(dd, aa, bb, cc, x[9], 12);
    II(cc, dd, aa, bb, x[11], 14); II(bb, cc, dd, aa, x[10], 15);
    II(aa, bb, cc, dd, x[0], 14);  II(dd, aa, bb, cc, x[8], 15);
    II(cc, dd, aa, bb, x[12], 9);  II(bb, cc, dd, aa, x[4], 8);
    II(aa, bb, cc, dd, x[13], 9);  II(dd, aa, bb, cc, x[3], 14);
    II(cc, dd, aa, bb, x[7], 5);   II(bb, cc, dd, aa, x[15], 6);
    II(aa, bb, cc, dd, x[14], 8);  II(dd, aa, bb, cc, x[5], 6);
    II(cc, dd, aa, bb, x[6], 5);   II(bb, cc, dd, aa, x[2], 12);

    III(aaa, bbb, ccc, ddd, x[5], 8);   III(ddd, aaa, bbb, ccc, x[14], 9);
    III(ccc, ddd, aaa, bbb, x[7], 9);   III(bbb, ccc, ddd, aaa, x[0], 11);
    III(aaa, bbb, ccc, ddd, x[9], 13);  III(ddd, aaa, bbb, ccc, x[2], 15);
    III(ccc, ddd, aaa, bbb, x[11], 15); III(bbb, ccc, ddd, aaa, x[4], 5);
    III(aaa, bbb, ccc, ddd, x[13], 7);  III(ddd, aaa, bbb, ccc, x[6], 7);
    III(ccc, ddd, aaa, bbb, x[15], 8);  III(bbb, ccc, ddd, aaa, x[8], 11);
    III(aaa, bbb, ccc, ddd, x[1], 14);  III(ddd, aaa, bbb, ccc, x[10], 14);
    III(ccc, ddd, aaa, bbb, x[3], 12);  III(bbb, ccc, ddd, aaa, x[12], 6);

    HHH(aaa, bbb, ccc, ddd, x[6], 9);   HHH(ddd, aaa, bbb, ccc, x[11], 13);
    HHH(ccc, ddd, aaa, bbb, x[3], 15);  HHH(bbb, ccc, ddd, aaa, x[7], 7);
    HHH(aaa, bbb, ccc, ddd, x[0], 12);  HHH(ddd, aaa, bbb, ccc, x[13], 8);
    HHH(ccc, ddd, aaa, bbb, x[5], 9);   HHH(bbb, ccc, ddd, aaa, x[10], 11);
    HHH(aaa, bbb, ccc, ddd, x[14], 7);  HHH(ddd, aaa, bbb, ccc, x[15], 7);
    HHH(ccc, ddd, aaa, bbb, x[8], 12);  HHH(bbb, ccc, ddd, aaa, x[12], 7);
    HHH(aaa, bbb, ccc, ddd, x[4], 6);   HHH(ddd, aaa, bbb, ccc, x[9], 15);
    HHH(ccc, ddd, aaa, bbb, x[1], 13);  HHH(bbb, ccc, ddd, aaa, x[2], 11);

    GGG(aaa, bbb, ccc, ddd, x[15], 9);  GGG(ddd, aaa, bbb, ccc, x[5], 7);
    GGG(ccc, ddd, aaa, bbb, x[1], 15);  GGG(bbb, ccc, ddd, aaa, x[3], 11);
    GGG(aaa, bbb, ccc, ddd, x[7], 8);   GGG(ddd, aaa, bbb, ccc, x[14], 6);
    GGG(ccc, ddd, aaa, bbb, x[6], 6);   GGG(bbb, ccc, ddd, aaa, x[9], 14);
    GGG(aaa, bbb, ccc, ddd, x[11], 12); GGG(ddd, aaa, bbb, ccc, x[8], 13);
    GGG(ccc, ddd, aaa, bbb, x[12], 5);  GGG(bbb, ccc, ddd, aaa, x[2], 14);
    GGG(aaa, bbb, ccc, ddd, x[10], 13); GGG(ddd, aaa, bbb, ccc, x[0], 13);
    GGG(ccc, ddd, aaa, bbb, x[4], 7);   GGG(bbb, ccc, ddd, aaa, x[13], 5);

    FFF(aaa, bbb, ccc, ddd, x[8], 15);  FFF(ddd, aaa, bbb, ccc, x[6], 5);
    FFF(ccc, ddd, aaa, bbb, x[4], 8);   FFF(bbb, ccc, ddd, aaa, x[1], 11);
    FFF(aaa, bbb, ccc, ddd, x[3], 14);  FFF(ddd, aaa, bbb, ccc, x[11], 14);
    FFF(ccc, ddd, aaa, bbb, x[15], 6);  FFF(bbb, ccc, ddd, aaa, x[0], 14);
    FFF(aaa, bbb, ccc, ddd, x[5], 6);   FFF(ddd, aaa, bbb, ccc, x[12], 9);
    FFF(ccc, ddd, aaa, bbb, x[2], 12);  FFF(bbb, ccc, ddd, aaa, x[13], 9);
    FFF(aaa, bbb, ccc, ddd, x[9], 12);  FFF(ddd, aaa, bbb, ccc, x[7], 5);
    FFF(ccc, ddd, aaa, bbb, x[10], 15); FFF(bbb, ccc, ddd, aaa, x[14], 8);

    ddd += cc + state[1];
    state[1] = state[2] + dd + aaa;
    state[2] = state[3] + aa + bbb;
    state[3] = state[0] + bb + ccc;
    state[0] = ddd;
}

inline uint32_t le32(const uint8_t *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

} // namespace

void ripemd128(const void *data, std::size_t length, uint8_t out[16])
{
    uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint32_t block[16];

    std::size_t remaining = length;
    while (remaining >= 64) {
        for (int i = 0; i < 16; ++i)
            block[i] = le32(p + i * 4);
        compress(state, block);
        p += 64;
        remaining -= 64;
    }

    uint8_t tail[128];
    std::memset(tail, 0, sizeof(tail));
    std::memcpy(tail, p, remaining);
    tail[remaining] = 0x80;

    // The 64-bit length in bits goes in the last 8 bytes; a second block is
    // needed when the 0x80 marker leaves no room for it.
    const std::size_t tailBlocks = (remaining < 56) ? 1 : 2;
    const uint64_t bits = uint64_t(length) * 8;
    uint8_t *lengthField = tail + tailBlocks * 64 - 8;
    for (int i = 0; i < 8; ++i)
        lengthField[i] = uint8_t((bits >> (8 * i)) & 0xff);

    for (std::size_t b = 0; b < tailBlocks; ++b) {
        for (int i = 0; i < 16; ++i)
            block[i] = le32(tail + b * 64 + i * 4);
        compress(state, block);
    }

    for (int i = 0; i < 4; ++i) {
        out[i * 4 + 0] = uint8_t(state[i] & 0xff);
        out[i * 4 + 1] = uint8_t((state[i] >> 8) & 0xff);
        out[i * 4 + 2] = uint8_t((state[i] >> 16) & 0xff);
        out[i * 4 + 3] = uint8_t((state[i] >> 24) & 0xff);
    }
}

} // namespace qmdict
