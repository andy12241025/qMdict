// RIPEMD-128 message digest.
//
// MDict uses this to derive the key that obfuscates the key-block index of
// "encrypted" dictionaries (the Encrypted & 0x02 flag).
#pragma once

#include <cstddef>
#include <cstdint>

namespace qmdict {

// Writes the 16-byte digest of `data` into `out`.
void ripemd128(const void *data, std::size_t length, uint8_t out[16]);

} // namespace qmdict
