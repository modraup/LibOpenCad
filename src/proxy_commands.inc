// SPDX-License-Identifier: MPL-2.0
// Ported from acadrust entities/proxy_graphics/records.rs.
namespace {
struct ProxyInput {
    const std::vector<std::uint8_t> &bytes;
    std::size_t at = 0;
    std::uint32_t w() {
        auto v = word(bytes, at);
        at += 4;
        return v;
    }
    double r() {
        auto v = number(bytes, at);
        at += 8;
        if (!std::isfinite(v))
            throw std::invalid_argument("Nonfinite proxy command value");
        return v;
    }
    Point3 p() { return {r(), r(), r()}; }
    bool b() {
        auto v = w();
        if (v > 1)
            throw std::invalid_argument("Invalid proxy boolean");
        return v != 0;
    }
    std::uint8_t byte() {
        if (at >= bytes.size())
            throw std::invalid_argument("Truncated proxy string");
        return bytes[at++];
    }
    void align() {
        while (at % 4)
            byte();
    }
    std::vector<std::uint8_t> raw_text() {
        std::vector<std::uint8_t> result;
        for (auto v = byte(); v; v = byte())
            result.push_back(v);
        align();
        return result;
    }
    std::string text() {
        std::u32string result;
        auto unit = [&]() {
            auto lo = byte();
            return std::uint32_t(lo) | (std::uint32_t(byte()) << 8);
        };
        for (auto c = unit(); c; c = unit()) {
            if (c >= 0xd800 && c <= 0xdbff) {
                auto low = unit();
                if (low < 0xdc00 || low > 0xdfff)
                    throw std::invalid_argument("Invalid proxy surrogate");
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
            } else if (c >= 0xdc00 && c <= 0xdfff)
                throw std::invalid_argument("Invalid proxy surrogate");
            result.push_back(c);
        }
        align();
        return detail::utf8_string(result);
    }
};
std::size_t proxy_utf16_length(const std::string &value) {
    std::size_t size = 0;
    for (auto c : detail::utf8_scalars(value))
        size += c >= 0x10000 ? 2 : 1;
    return size;
}
void proxy_text_mode(std::int32_t length, std::int32_t mode) {
    if (length < -1 || (mode != 0 && mode != 1))
        throw std::invalid_argument("Invalid proxy text mode/length");
}
struct ProxyOutput {
    std::vector<std::uint8_t> bytes;
    void w(std::uint32_t v) { put_word(bytes, v); }
    void r(double v) {
        if (!std::isfinite(v))
            throw std::invalid_argument("Nonfinite proxy command value");
        number(bytes, v);
    }
    void p(Point3 v) {
        r(v.x);
        r(v.y);
        r(v.z);
    }
    void align() {
        while (bytes.size() % 4)
            bytes.push_back(0);
    }
    void raw_text(const std::vector<std::uint8_t> &v) {
        if (std::find(v.begin(), v.end(), 0) != v.end())
            throw std::invalid_argument("NUL in proxy byte string");
        bytes.insert(bytes.end(), v.begin(), v.end());
        bytes.push_back(0);
        align();
    }
    void text(const std::string &v) {
        auto unit = [&](std::uint32_t c) {
            bytes.push_back(c & 255);
            bytes.push_back(c >> 8);
        };
        for (auto c : detail::utf8_scalars(v)) {
            if (!c)
                throw std::invalid_argument("NUL in proxy string");
            if (c <= 65535)
                unit(c);
            else {
                c -= 0x10000;
                unit(0xd800 + (c >> 10));
                unit(0xdc00 + (c & 1023));
            }
        }
        unit(0);
        align();
    }
};
constexpr std::uint32_t proxy_control_codes[] = {14, 16, 18, 19, 20, 22, 23, 24, 25, 31, 51};
} // namespace
std::optional<ProxyCommand> ProxyGraphicRecord::command() const {
    if (type == 51 && data != std::vector<std::uint8_t>({0, 0, 0, 0}))
        return {};
    ProxyInput in{data};
    std::optional<ProxyCommand> out;
    switch (type) {
    case 1:
        out = ProxyExtents{{in.p(), in.p()}};
        break;
    case 2: {
        ProxyCircle v;
        v.center = in.p();
        v.radius = in.r();
        v.normal = in.p();
        out = v;
        break;
    }
    case 3:
        out = ProxyCircle3{{in.p(), in.p(), in.p()}};
        break;
    case 4: {
        ProxyArc v;
        v.center = in.p();
        v.radius = in.r();
        v.normal = in.p();
        v.start_direction = in.p();
        v.sweep = in.r();
        v.arc_type = static_cast<std::int32_t>(in.w());
        out = v;
        break;
    }
    case 5: {
        ProxyArc3 v;
        v.points = {in.p(), in.p(), in.p()};
        v.arc_type = static_cast<std::int32_t>(in.w());
        out = v;
        break;
    }
    case 6:
    case 7:
    case 32: {
        auto count = in.w();
        if (count > (data.size() - 4) / 24)
            throw std::invalid_argument("Untrusted proxy point count");
        ProxyPolyline v;
        v.kind = type == 6   ? ProxyPolyline::Kind::Polyline
                 : type == 7 ? ProxyPolyline::Kind::Polygon
                             : ProxyPolyline::Kind::WithNormal;
        for (std::uint32_t n = 0; n < count; ++n)
            v.vertices.push_back(in.p());
        if (type == 32)
            v.normal = in.p();
        out = v;
        break;
    }
    case 29: {
        ProxyPushTransform v;
        for (auto &x : v.values)
            x = in.r();
        out = v;
        break;
    }
    case 10:
    case 11: {
        ProxyLegacyText v;
        v.position = in.p();
        v.normal = in.p();
        v.direction = in.p();
        if (type == 10) {
            v.height = in.r();
            v.width_factor = in.r();
            v.oblique_angle = in.r();
            v.text = in.raw_text();
        } else {
            v.text = in.raw_text();
            ProxyLegacyStyle s;
            s.declared_length = static_cast<std::int32_t>(in.w());
            s.processing_mode = static_cast<std::int32_t>(in.w());
            proxy_text_mode(s.declared_length, s.processing_mode);
            v.height = in.r();
            v.width_factor = in.r();
            v.oblique_angle = in.r();
            s.tracking = in.r();
            for (auto &b : s.flags)
                b = in.b();
            s.font = in.raw_text();
            s.big_font = in.raw_text();
            v.styling = s;
        }
        out = v;
        break;
    }
    case 38: {
        ProxyStyledUnicodeText v;
        v.position = in.p();
        v.normal = in.p();
        v.direction = in.p();
        v.text = in.text();
        v.declared_length = static_cast<std::int32_t>(in.w());
        v.processing_mode = static_cast<std::int32_t>(in.w());
        proxy_text_mode(v.declared_length, v.processing_mode);
        if (v.declared_length >= 0 && std::size_t(v.declared_length) != proxy_utf16_length(v.text))
            throw std::invalid_argument("Proxy UTF-16 length mismatch");
        v.height = in.r();
        v.width_factor = in.r();
        v.oblique_angle = in.r();
        v.tracking = in.r();
        for (auto &b : v.flags)
            b = in.b();
        v.bold = in.b();
        v.italic = in.b();
        v.charset = in.w();
        v.pitch_family = in.w();
        v.family = in.text();
        v.font = in.text();
        v.big_font = in.text();
        out = v;
        break;
    }
    default: {
        auto code = std::find(std::begin(proxy_control_codes), std::end(proxy_control_codes), type);
        if (code == std::end(proxy_control_codes))
            return {};
        ProxyControl v;
        v.kind = static_cast<ProxyControl::Kind>(code - std::begin(proxy_control_codes));
        if (type == 24 || type == 25)
            v.number = in.r();
        else if (type != 31)
            v.word = in.w();
        if (type == 14 && v.word > 256)
            throw std::invalid_argument("Invalid proxy ACI");
        if (type == 20) {
            if (v.word != 1 && v.word != 2)
                throw std::invalid_argument("Invalid proxy fill flag");
            v.word = v.word == 1;
        }
        if (type == 51 && v.word != 0)
            return {};
        out = v;
        break;
    }
    }
    if (in.at != data.size())
        throw std::invalid_argument("Trailing proxy command data");
    return out;
}
ProxyGraphicRecord ProxyGraphicRecord::from_command(const ProxyCommand &command) {
    ProxyGraphicRecord record;
    ProxyOutput out;
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ProxyExtents>) {
                record.type = 1;
                for (auto p : v.points)
                    out.p(p);
            } else if constexpr (std::is_same_v<T, ProxyCircle>) {
                record.type = 2;
                out.p(v.center);
                out.r(v.radius);
                out.p(v.normal);
            } else if constexpr (std::is_same_v<T, ProxyCircle3>) {
                record.type = 3;
                for (auto p : v.points)
                    out.p(p);
            } else if constexpr (std::is_same_v<T, ProxyArc>) {
                record.type = 4;
                out.p(v.center);
                out.r(v.radius);
                out.p(v.normal);
                out.p(v.start_direction);
                out.r(v.sweep);
                out.w(v.arc_type);
            } else if constexpr (std::is_same_v<T, ProxyArc3>) {
                record.type = 5;
                for (auto p : v.points)
                    out.p(p);
                out.w(v.arc_type);
            } else if constexpr (std::is_same_v<T, ProxyPolyline>) {
                record.type = v.kind == ProxyPolyline::Kind::Polyline  ? 6
                              : v.kind == ProxyPolyline::Kind::Polygon ? 7
                                                                       : 32;
                if (v.vertices.size() > UINT32_MAX)
                    throw std::length_error("Too many proxy points");
                out.w(v.vertices.size());
                for (auto p : v.vertices)
                    out.p(p);
                if (record.type == 32)
                    out.p(v.normal);
            } else if constexpr (std::is_same_v<T, ProxyPushTransform>) {
                record.type = 29;
                for (auto x : v.values)
                    out.r(x);
            } else if constexpr (std::is_same_v<T, ProxyControl>) {
                auto n = static_cast<std::size_t>(v.kind);
                if (n >= std::size(proxy_control_codes))
                    throw std::invalid_argument("Unknown proxy control");
                record.type = proxy_control_codes[n];
                if (record.type == 14 && v.word > 256)
                    throw std::invalid_argument("Invalid proxy ACI");
                if (record.type == 20 && v.word > 1)
                    throw std::invalid_argument("Invalid proxy fill");
                if (record.type == 24 || record.type == 25)
                    out.r(v.number);
                else if (record.type == 20)
                    out.w(v.word ? 1 : 2);
                else if (record.type == 51) {
                    if (v.word)
                        throw std::invalid_argument("Invalid compatibility command");
                    out.w(0);
                } else if (record.type != 31)
                    out.w(v.word);
            } else if constexpr (std::is_same_v<T, ProxyLegacyText>) {
                record.type = v.styling ? 11 : 10;
                out.p(v.position);
                out.p(v.normal);
                out.p(v.direction);
                if (v.styling) {
                    const auto &s = *v.styling;
                    proxy_text_mode(s.declared_length, s.processing_mode);
                    out.raw_text(v.text);
                    out.w(s.declared_length);
                    out.w(s.processing_mode);
                    out.r(v.height);
                    out.r(v.width_factor);
                    out.r(v.oblique_angle);
                    out.r(s.tracking);
                    for (bool b : s.flags)
                        out.w(b);
                    out.raw_text(s.font);
                    out.raw_text(s.big_font);
                } else {
                    out.r(v.height);
                    out.r(v.width_factor);
                    out.r(v.oblique_angle);
                    out.raw_text(v.text);
                }
            } else if constexpr (std::is_same_v<T, ProxyStyledUnicodeText>) {
                record.type = 38;
                proxy_text_mode(v.declared_length, v.processing_mode);
                if (v.declared_length >= 0 && std::size_t(v.declared_length) != proxy_utf16_length(v.text))
                    throw std::invalid_argument("Proxy UTF-16 length mismatch");
                out.p(v.position);
                out.p(v.normal);
                out.p(v.direction);
                out.text(v.text);
                out.w(v.declared_length);
                out.w(v.processing_mode);
                out.r(v.height);
                out.r(v.width_factor);
                out.r(v.oblique_angle);
                out.r(v.tracking);
                for (bool b : v.flags)
                    out.w(b);
                out.w(v.bold);
                out.w(v.italic);
                out.w(v.charset);
                out.w(v.pitch_family);
                out.text(v.family);
                out.text(v.font);
                out.text(v.big_font);
            }
        },
        command);
    record.data = std::move(out.bytes);
    return record;
}
