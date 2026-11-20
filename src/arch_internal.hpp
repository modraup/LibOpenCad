// SPDX-License-Identifier: MPL-2.0
// Bit-buffer helpers for Arch proxy profiles and in-place BD edits.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <libopencad/arch.hpp>
#include <libopencad/proxy.hpp>
#include <libopencad/vector.hpp>
namespace libopencad {
inline std::optional<std::vector<Vector3>> polyline_from_type6(const std::vector<std::uint8_t> &data) {
    if (data.size() < 4)
        return std::nullopt;
    std::uint32_t count = 0;
    std::memcpy(&count, data.data(), 4);
    if (count < 2 || count > 100000)
        return std::nullopt;
    const std::size_t needed = 4 + std::size_t(count) * 24;
    if (needed > data.size())
        return std::nullopt;
    std::vector<Vector3> points;
    points.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Vector3 p{};
        std::memcpy(&p.x, data.data() + 4 + i * 24, 8);
        std::memcpy(&p.y, data.data() + 12 + i * 24, 8);
        std::memcpy(&p.z, data.data() + 20 + i * 24, 8);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            return std::nullopt;
        points.push_back(p);
    }
    return points;
}
inline std::vector<std::vector<Vector3>> decode_profiles(const std::vector<std::uint8_t> &graphic_data) {
    if (graphic_data.empty())
        return {};
    try {
        auto graphics = ProxyGraphics::decode(graphic_data);
        std::vector<std::vector<Vector3>> profiles;
        for (const auto &record : graphics.records) {
            if (record.type != 6)
                continue;
            if (auto points = polyline_from_type6(record.data))
                profiles.push_back(std::move(*points));
        }
        return profiles;
    } catch (const std::exception &) {
        return {};
    }
}
inline std::optional<std::uint8_t> read_arch_bits(const std::vector<std::uint8_t> &data, std::size_t at,
                                                  std::size_t count) {
    if (count > 8)
        return std::nullopt;
    std::uint8_t result = 0;
    for (std::size_t bit = at; bit < at + count; ++bit) {
        if (bit / 8 >= data.size())
            return std::nullopt;
        result = static_cast<std::uint8_t>((result << 1) | ((data[bit / 8] >> (7 - bit % 8)) & 1));
    }
    return result;
}
inline bool write_arch_bits(std::vector<std::uint8_t> &data, std::size_t at, std::uint8_t bits, std::size_t count) {
    if (count > 8)
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        auto pos = at + i;
        if (pos / 8 >= data.size())
            return false;
        auto mask = static_cast<std::uint8_t>(1u << (7 - pos % 8));
        if (bits & (1u << (count - 1 - i)))
            data[pos / 8] |= mask;
        else
            data[pos / 8] = static_cast<std::uint8_t>(data[pos / 8] & ~mask);
    }
    return true;
}
inline bool write_bytes_at_bit(std::vector<std::uint8_t> &data, std::int64_t bit_index,
                               const std::vector<std::uint8_t> &bytes) {
    if (bit_index < 0)
        return false;
    auto at = static_cast<std::size_t>(bit_index);
    if (bytes.size() > (SIZE_MAX - at) / 8)
        return false;
    if (at + bytes.size() * 8 > data.size() * 8)
        return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto pos = at + i * 8 + bit;
            auto mask = static_cast<std::uint8_t>(1u << (7 - pos % 8));
            if (bytes[i] & (1u << (7 - bit)))
                data[pos / 8] |= mask;
            else
                data[pos / 8] = static_cast<std::uint8_t>(data[pos / 8] & ~mask);
        }
    }
    return true;
}
inline bool insert_arch_bits(std::vector<std::uint8_t> &data, std::size_t at, const std::uint8_t *bytes,
                             std::size_t n) {
    std::vector<std::uint8_t> bits;
    bits.reserve(data.size() * 8 + n * 8);
    for (auto byte : data)
        for (int shift = 7; shift >= 0; --shift)
            bits.push_back((byte >> shift) & 1);
    if (at > bits.size())
        return false;
    std::vector<std::uint8_t> inserted;
    inserted.reserve(n * 8);
    for (std::size_t i = 0; i < n; ++i)
        for (int shift = 7; shift >= 0; --shift)
            inserted.push_back((bytes[i] >> shift) & 1);
    bits.insert(bits.begin() + static_cast<std::ptrdiff_t>(at), inserted.begin(), inserted.end());
    data.assign((bits.size() + 7) / 8, 0);
    for (std::size_t i = 0; i < bits.size(); ++i)
        if (bits[i])
            data[i / 8] = static_cast<std::uint8_t>(data[i / 8] | (1u << (7 - i % 8)));
    return true;
}
inline std::optional<std::pair<double, std::int64_t>> read_encoded_double(const std::vector<std::uint8_t> &data,
                                                                         std::int64_t bit) {
    if (bit < 0)
        return std::nullopt;
    auto at = static_cast<std::size_t>(bit);
    auto code = read_arch_bits(data, at, 2);
    if (!code)
        return std::nullopt;
    if (*code == 0) {
        std::uint8_t bytes[8]{};
        for (int i = 0; i < 8; ++i) {
            auto value = read_arch_bits(data, at + 2 + static_cast<std::size_t>(i) * 8, 8);
            if (!value)
                return std::nullopt;
            bytes[i] = *value;
        }
        double value = 0;
        std::memcpy(&value, bytes, 8);
        if (!std::isfinite(value))
            return std::nullopt;
        return std::pair<double, std::int64_t>{value, bit + 66};
    }
    if (*code == 1)
        return std::pair<double, std::int64_t>{1.0, bit + 2};
    if (*code == 2)
        return std::pair<double, std::int64_t>{0.0, bit + 2};
    return std::nullopt;
}
inline std::optional<double> read_bit_double_at(const std::vector<std::uint8_t> &data, std::int64_t bit) {
    auto decoded = read_encoded_double(data, bit);
    if (!decoded || decoded->second - bit != 66)
        return std::nullopt;
    return decoded->first;
}
inline void write_double_at_bit(std::vector<std::uint8_t> &data, std::size_t bit_index, double value) {
    std::uint8_t bytes[8];
    std::memcpy(bytes, &value, 8);
    for (int i = 0; i < 8; ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            auto at = bit_index + static_cast<std::size_t>(i) * 8 + static_cast<std::size_t>(bit);
            if (at / 8 >= data.size())
                return;
            auto mask = static_cast<std::uint8_t>(1u << (7 - at % 8));
            if (bytes[i] & (1u << (7 - bit)))
                data[at / 8] |= mask;
            else
                data[at / 8] = static_cast<std::uint8_t>(data[at / 8] & ~mask);
        }
    }
}
inline std::optional<std::int64_t> write_bit_double_at(std::vector<std::uint8_t> &data, std::int64_t bit,
                                                       double value) {
    if (!std::isfinite(value))
        return std::nullopt;
    auto decoded = read_encoded_double(data, bit);
    if (!decoded)
        return std::nullopt;
    auto width = decoded->second - bit;
    if (width == 66) {
        write_double_at_bit(data, static_cast<std::size_t>(bit) + 2, value);
        return 0;
    }
    if (width != 2)
        return std::nullopt;
    if (std::abs(value - decoded->first) <= 1e-12)
        return 0;
    if (value == 0.0 || value == 1.0) {
        if (!write_arch_bits(data, static_cast<std::size_t>(bit), static_cast<std::uint8_t>(value == 0.0 ? 0b10 : 0b01),
                             2))
            return std::nullopt;
        return 0;
    }
    if (!write_arch_bits(data, static_cast<std::size_t>(bit), 0, 2))
        return std::nullopt;
    std::uint8_t bytes[8];
    std::memcpy(bytes, &value, 8);
    if (!insert_arch_bits(data, static_cast<std::size_t>(bit) + 2, bytes, 8))
        return std::nullopt;
    return 64;
}
inline bool write_bit_doubles_at(std::vector<std::uint8_t> &data,
                                 std::vector<std::pair<std::int64_t, double>> edits) {
    std::sort(edits.begin(), edits.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    std::int64_t shift = 0;
    for (auto [bit, value] : edits) {
        auto grown = write_bit_double_at(data, bit + shift, value);
        if (!grown)
            return false;
        shift += *grown;
    }
    return true;
}
} // namespace libopencad
