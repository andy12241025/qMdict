// Block-level codecs shared by the MDX and MDD readers.
#pragma once

#include <QByteArray>

#include <cstddef>
#include <cstdint>

namespace qmdict {

// Inflates a zlib stream whose decompressed size is known up front. Empty on
// failure or on a size mismatch.
QByteArray inflateZlib(const char *in, std::size_t inLen, std::size_t outLen);

// Decodes one MDict block: an 8-byte header (4-byte compression type,
// 4-byte Adler-32) followed by the payload. `decompressedSize` must be the
// size recorded in the block index. Returns an empty array on failure.
QByteArray decodeBlock(const char *data, std::size_t size, std::size_t decompressedSize);

// Undoes the obfuscation applied to the key-block index of dictionaries built
// with the "encrypted" flag (Encrypted & 0x02). `block` must include the
// 8-byte block header; only the payload after it is modified.
void decryptKeyBlockInfo(char *block, std::size_t size);

} // namespace qmdict
