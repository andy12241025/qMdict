// Minimal LZO1X decompressor (decompression only, clean-room from the
// published LZO1X stream format).
//
// Older MDX files mark blocks with compression type 1, meaning LZO1X. Modern
// ones use zlib (type 2), so this path is rarely exercised but still needed to
// open legacy dictionaries.
#pragma once

#include <cstddef>
#include <cstdint>

namespace qmdict {

// Decompresses `inLen` bytes at `in` into the buffer at `out`, which must be
// able to hold `*outLen` bytes. On success `*outLen` is set to the number of
// bytes actually produced. Returns false on any malformed or truncated input.
bool lzo1xDecompress(const uint8_t *in, std::size_t inLen, uint8_t *out, std::size_t *outLen);

} // namespace qmdict
