#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace me {

// CRC-32 (IEEE 802.3, the zlib/PNG polynomial). Table-driven; the table is built at compile time.
inline std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    static constexpr auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

}  // namespace me
