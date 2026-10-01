#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

inline uint32_t pyroWaveReadWord(const uint8_t* data)
{
    return uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16 | uint32_t(data[3]) << 24;
}

inline unsigned pyroWaveMaxBlocks(unsigned width, unsigned height, bool chroma444)
{
    // Parentheses prevent Windows min/max macros from expanding this call.
    width = (std::max)(128u, (width + 31) & ~31u);
    height = (std::max)(128u, (height + 31) & ~31u);
    unsigned blocks = 0;
    // Matches the pinned codec's five wavelet decomposition levels.
    for (unsigned level = 0; level < 5; level++) {
        unsigned w = width >> (level + 1), h = height >> (level + 1);
        blocks += ((w + 31) / 32) * ((h + 31) / 32) * (level == 4 ? 4 : 3) *
                  (level == 0 && !chroma444 ? 1 : 3);
    }
    return blocks;
}

// One independently decodable wire-v1 frame. The native library also accepts
// incremental packets/sequence changes, so validate this stricter integration
// contract before sending records to it. Scratch capacity is reused per frame.
inline bool pyroWaveValidateFrame(const uint8_t* data, size_t size, int width, int height,
                                 bool chroma444, bool hdr, std::vector<uint8_t>& seen)
{
    if (!data || size < 8 || size > 3u * 1024 * 1024 || (size & 3) ||
        width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
        (!chroma444 && ((width | height) & 1))) return false;
    const auto a = pyroWaveReadWord(data), b = pyroWaveReadWord(data + 4);
    if (!(a >> 31) || ((b >> 24) & 3) || int((a & 0x3fff) + 1) != width ||
        int(((a >> 14) & 0x3fff) + 1) != height || bool(b & (1u << 26)) != chroma444 ||
        ((b >> 27) & 7) != (hdr ? 7u : 0u)) return false;
    const unsigned maximum = pyroWaveMaxBlocks(width, height, chroma444);
    const unsigned announced = b & 0xffffff;
    if (announced > maximum) return false;
    seen.assign(maximum, 0);
    unsigned records = 0;
    for (size_t offset = 8; offset < size;) {
        if (size - offset < 8) return false;
        const auto h0 = pyroWaveReadWord(data + offset), h1 = pyroWaveReadWord(data + offset + 4);
        const size_t bytes = ((h0 >> 16) & 0xfff) * 4;
        const unsigned index = h1 >> 8;
        if ((h0 >> 31) || ((h0 >> 28) & 7) != ((a >> 28) & 7) ||
            bytes < 8 || bytes > size - offset || index >= maximum || seen[index] ||
            ++records > announced) return false;
        seen[index] = 1;
        offset += bytes;
    }
    return records == announced;
}

// Convert the negotiated record transport to the codec's contiguous block
// stream in place. Native frames never use this adapter. Revision 186f0393
// leaves color metadata unset; record sessions carry those axes in SDP.
inline bool pyroWaveUnpackRecords(uint8_t* data, size_t& size, bool hdr)
{
    if (!data || size < 8 || size > 3u * 1024 * 1024 || size % 4) return false;
    size_t read = 0, write = 0;
    while (read < size) {
        if (size - read < 8) return false;
        const uint32_t a = pyroWaveReadWord(data + read);
        const uint32_t b = pyroWaveReadWord(data + read + 4);
        size_t bytes;
        if (a == UINT32_MAX) {
            if (read == 0 || b > (size - read - 8) / 4) return false;
            bytes = 8 + size_t(b) * 4;
            for (size_t at = read + 8; at < read + bytes; at += 4)
                if (pyroWaveReadWord(data + at) != 0) return false;
            read += bytes; continue;
        }
        if (read == 0) {
            if (!(a >> 31) || ((b >> 24) & 3)) return false;
            bytes = 8;
        } else {
            if (a >> 31) return false;
            bytes = size_t((a >> 16) & 0xfff) * 4;
            if (bytes < 8 || bytes > size - read) return false;
        }
        if (write != read) std::memmove(data + write, data + read, bytes);
        write += bytes; read += bytes;
    }
    // Older codec omits color bits, but still encodes correctly transformed
    // samples. Accept only zero/default metadata or the negotiated matrix.
    uint32_t b = pyroWaveReadWord(data + 4);
    const unsigned color = (b >> 27) & 7;
    if (color != 0 && color != (hdr ? 7u : 0u)) return false;
    b = (b & ~(7u << 27)) | (hdr ? 7u << 27 : 0u);
    for (unsigned i = 0; i < 4; ++i) data[4 + i] = uint8_t(b >> (i * 8));
    size = write;
    return true;
}
