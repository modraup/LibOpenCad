// SPDX-License-Identifier: MPL-2.0
#include "dxf_code_page.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <libopencad/proxy.hpp>
#include <limits>
#include <stdexcept>
namespace libopencad {
namespace {
std::uint32_t word(const std::vector<std::uint8_t> &d, std::size_t at) {
    if (at > d.size() || d.size() - at < 4)
        throw std::invalid_argument("Truncated proxy graphics word");
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i)
        n |= std::uint32_t(d[at + i]) << (i * 8);
    return n;
}
void put_word(std::vector<std::uint8_t> &d, std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
        d.push_back(n >> (i * 8));
}
double number(const std::vector<std::uint8_t> &d, std::size_t at) {
    if (at > d.size() || d.size() - at < 8)
        throw std::invalid_argument("Truncated proxy graphics number");
    std::uint64_t n = 0;
    for (int i = 0; i < 8; ++i)
        n |= std::uint64_t(d[at + i]) << (i * 8);
    double value;
    std::memcpy(&value, &n, 8);
    return value;
}
void number(std::vector<std::uint8_t> &d, double v) {
    std::uint64_t n;
    std::memcpy(&n, &v, 8);
    for (int i = 0; i < 8; ++i)
        d.push_back(n >> (i * 8));
}
} // namespace
ProxyGraphics ProxyGraphics::decode(const std::vector<std::uint8_t> &data) {
    const auto size = word(data, 0), count = word(data, 4);
    if (size < 8 || size > data.size())
        throw std::invalid_argument("Invalid proxy graphics size");
    ProxyGraphics v;
    std::size_t at = 8;
    for (std::uint32_t n = 0; n < count; ++n) {
        if (at > size || size - at < 8)
            throw std::invalid_argument("Truncated proxy graphics record");
        auto length = word(data, at);
        auto type = word(data, at + 4);
        if (length < 8 || length > size - at)
            throw std::invalid_argument("Invalid proxy graphics record size");
        v.records.push_back({type, {data.begin() + at + 8, data.begin() + at + length}});
        at += length;
    }
    if (at != size)
        throw std::invalid_argument("Proxy graphics record count mismatch");
    return v;
}
std::vector<std::uint8_t> ProxyGraphics::encode() const {
    if (records.size() > UINT32_MAX)
        throw std::length_error("Too many proxy records");
    std::vector<std::uint8_t> out;
    put_word(out, 0);
    put_word(out, records.size());
    for (const auto &r : records) {
        if (r.data.size() > UINT32_MAX - 8 || out.size() > UINT32_MAX - 8 - r.data.size())
            throw std::length_error("Proxy graphics size overflow");
        put_word(out, 8 + r.data.size());
        put_word(out, r.type);
        out.insert(out.end(), r.data.begin(), r.data.end());
    }
    auto size = static_cast<std::uint32_t>(out.size());
    for (int i = 0; i < 4; ++i)
        out[i] = size >> (i * 8);
    return out;
}
ProxyUnicodeText ProxyGraphicRecord::unicode_text() const {
    if (type != 36 || data.size() < 98)
        throw std::invalid_argument("Not a complete Unicode proxy text");
    ProxyUnicodeText v;
    auto point = [&](std::size_t i) {
        return Point3{number(data, i), number(data, i + 8), number(data, i + 16)};
    };
    v.position = point(0);
    v.normal = point(24);
    v.direction = point(48);
    v.height = number(data, 72);
    v.width_factor = number(data, 80);
    v.oblique_angle = number(data, 88);
    std::u32string text;
    bool terminated = false;
    for (std::size_t at = 96; at + 1 < data.size(); at += 2) {
        std::uint32_t c = data[at] | std::uint32_t(data[at + 1]) << 8;
        if (!c) {
            terminated = true;
            break;
        }
        if (c >= 0xd800 && c <= 0xdbff) {
            if (at + 3 >= data.size())
                throw std::invalid_argument("Truncated UTF-16 surrogate");
            auto lo = data[at + 2] | std::uint32_t(data[at + 3]) << 8;
            if (lo < 0xdc00 || lo > 0xdfff)
                throw std::invalid_argument("Invalid UTF-16 surrogate");
            c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
            at += 2;
        } else if (c >= 0xdc00 && c <= 0xdfff)
            throw std::invalid_argument("Invalid UTF-16 surrogate");
        text.push_back(c);
    }
    if (!terminated)
        throw std::invalid_argument("Unterminated proxy text");
    v.text = detail::utf8_string(text);
    return v;
}
ProxyGraphicRecord ProxyGraphicRecord::text(const ProxyUnicodeText &v) {
    ProxyGraphicRecord r;
    r.type = 36;
    auto point = [&](Point3 p) {
        number(r.data, p.x);
        number(r.data, p.y);
        number(r.data, p.z);
    };
    point(v.position);
    point(v.normal);
    point(v.direction);
    number(r.data, v.height);
    number(r.data, v.width_factor);
    number(r.data, v.oblique_angle);
    auto unit = [&](std::uint32_t c) {
        r.data.push_back(c & 255);
        r.data.push_back(c >> 8);
    };
    for (auto c : detail::utf8_scalars(v.text)) {
        if (!c)
            throw std::invalid_argument("NUL in proxy text");
        if (c <= 0xffff)
            unit(c);
        else {
            c -= 0x10000;
            unit(0xd800 + (c >> 10));
            unit(0xdc00 + (c & 1023));
        }
    }
    unit(0);
    while (r.data.size() % 4)
        r.data.push_back(0);
    return r;
}
#include "proxy_commands.inc"
} // namespace libopencad
