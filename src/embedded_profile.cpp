// SPDX-License-Identifier: MPL-2.0
// Port of acadrust io/dwg/embedded_entity.rs and its bounded curve readers.
#include "libopencad/embedded_profile.hpp"
#include "libopencad/dwg_r2000.hpp"
#include "libopencad/transform.hpp"
#include "libopencad/acis.hpp"
#include "dwg_modeler_internal.hpp"
#include "dwg_sat_internal.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace libopencad {
namespace {
void version_check(DxfVersion v) {
    if (v < DxfVersion::AC1012 || dxf_version_string(v) == "UNKNOWN")
        throw std::invalid_argument("Embedded profiles require a known R13+ version");
}
std::int32_t count(std::size_t n) {
    if (n > 1000000)
        throw std::length_error("Embedded profile count exceeds limit");
    return static_cast<std::int32_t>(n);
}
struct Read {
    DwgBitReader r;
    std::size_t limit;
    Read(const EncodedProfile &e) : r(e.bytes, e.bit_count), limit(e.bit_count) {}
    Read(const DwgBitReader &source, std::size_t end) : r(source), limit(end) {}
    bool b() { return r.bits(1) != 0; }
    double d() {
        auto v = r.bit_double();
        if (!std::isfinite(v))
            throw std::invalid_argument("Nonfinite profile coordinate");
        return v;
    }
    double raw() {
        auto v = r.raw_double();
        if (!std::isfinite(v))
            throw std::invalid_argument("Nonfinite profile coordinate");
        return v;
    }
    Vector3 p() {
        double x = d(), y = d(), z = d();
        return {x, y, z};
    }
    std::size_t n(std::size_t minbits = 2) {
        auto v = r.bit_long();
        if (v < 0 || v > 1000000 || std::size_t(v) > (limit - r.position()) / minbits)
            throw std::invalid_argument("Invalid profile count");
        return std::size_t(v);
    }
};
struct Write {
    DwgBitWriter w;
    void b(bool v) { w.bits(1, v); }
    void d(double v) {
        if (!std::isfinite(v))
            throw std::invalid_argument("Nonfinite profile coordinate");
        w.bit_double(v);
    }
    void raw(double v) {
        if (!std::isfinite(v))
            throw std::invalid_argument("Nonfinite profile coordinate");
        std::uint64_t bits;
        std::memcpy(&bits, &v, 8);
        w.little_endian(8, bits);
    }
    void p(Vector3 v) {
        d(v.x);
        d(v.y);
        d(v.z);
    }
};
struct RegionDatabase {
    DwgBitReader& handles;
    std::uint64_t self;
    DwgModeler& modeler;
    bool force_inline=false, allow_extra=true;
};
#include "embedded_region.inc"
#include "dwg_surface.inc"
#include "dwg_history_parameters.inc"
ProfilePolyline read_poly(Read &r) {
    ProfilePolyline v;
    auto f = r.r.bit_short();
    if (f & 4)
        v.constant_width = r.d();
    if (f & 8)
        v.elevation = r.d();
    if (f & 2)
        v.thickness = r.d();
    if (f & 1)
        v.normal = r.p();
    auto n = r.n(128), nb = (f & 16) ? r.n() : 0, ni = (f & 1024) ? r.n() : 0, nw = (f & 32) ? r.n(4) : 0;
    if (nb > n || ni > n || nw > n)
        throw std::invalid_argument("Profile vertex array exceeds vertex count");
    v.vertices.resize(n);
    for (auto &p : v.vertices) {
        p.location.x = r.raw();
        p.location.y = r.raw();
    }
    for (std::size_t i = 0; i < nb; ++i)
        v.vertices[i].bulge = r.d();
    for (std::size_t i = 0; i < ni; ++i)
        v.vertices[i].id = r.r.bit_long();
    for (std::size_t i = 0; i < nw; ++i) {
        v.vertices[i].start_width = r.d();
        v.vertices[i].end_width = r.d();
    }
    v.closed = f & 512;
    v.plinegen = f & 256;
    return v;
}
void write_poly(Write &w, const ProfilePolyline &v, DxfVersion version) {
    bool bulges = false, widths = false, ids = false;
    for (auto &p : v.vertices) {
        bulges |= p.bulge != 0;
        widths |= p.start_width != 0 || p.end_width != 0;
        ids |= p.id != 0;
    }
    ids &= version >= DxfVersion::AC1024;
    auto f = (v.normal != Vector3{0, 0, 1} ? 1 : 0) | (v.thickness != 0 ? 2 : 0) |
             (v.constant_width != 0 ? 4 : 0) | (v.elevation != 0 ? 8 : 0) | (bulges ? 16 : 0) |
             (widths ? 32 : 0) | (v.plinegen ? 256 : 0) | (v.closed ? 512 : 0) | (ids ? 1024 : 0);
    w.w.bit_short(f);
    if (f & 4)
        w.d(v.constant_width);
    if (f & 8)
        w.d(v.elevation);
    if (f & 2)
        w.d(v.thickness);
    if (f & 1)
        w.p(v.normal);
    auto n = count(v.vertices.size());
    w.w.bit_long(n);
    if (bulges)
        w.w.bit_long(n);
    if (ids)
        w.w.bit_long(n);
    if (widths)
        w.w.bit_long(n);
    for (auto &p : v.vertices) {
        w.raw(p.location.x);
        w.raw(p.location.y);
    }
    if (bulges)
        for (auto &p : v.vertices)
            w.d(p.bulge);
    if (ids)
        for (auto &p : v.vertices)
            w.w.bit_long(p.id);
    if (widths)
        for (auto &p : v.vertices) {
            w.d(p.start_width);
            w.d(p.end_width);
        }
}
ProfileSpline read_spline(Read &r, DxfVersion version) {
    ProfileSpline v;
    v.knot_tolerance = v.control_tolerance = v.fit_tolerance = 0;
    auto scenario = r.r.bit_long();
    if (version >= DxfVersion::AC1027) {
        v.dwg_flags = r.r.bit_long();
        v.knot_parameterization = r.r.bit_long();
        scenario = (v.knot_parameterization == 15 || !(v.dwg_flags & 8)) ? 1 : 2;
    }
    v.degree = r.r.bit_long();
    v.closed = v.dwg_flags & 4;
    v.cv_frame_visible = v.dwg_flags & 2;
    if (scenario == 1) {
        v.rational = r.b();
        v.closed = r.b();
        v.periodic = r.b();
        v.knot_tolerance = r.d();
        v.control_tolerance = r.d();
        auto nk = r.n(), nc = r.n(6);
        auto weighted = r.b();
        for (std::size_t i = 0; i < nk; ++i)
            v.knots.push_back(r.d());
        for (std::size_t i = 0; i < nc; ++i) {
            v.control_points.push_back(r.p());
            if (weighted)
                v.weights.push_back(r.d());
        }
    } else {
        v.fit_tolerance = r.d();
        v.begin_tangent = r.p();
        v.end_tangent = r.p();
        auto n = r.n(6);
        for (std::size_t i = 0; i < n; ++i)
            v.fit_points.push_back(r.p());
        if (n >= 3 && v.fit_points.front() == v.fit_points.back())
            v.closed = true;
    }
    return v;
}
void write_spline(Write &w, const ProfileSpline &v, DxfVersion version) {
    auto scenario =
        !v.fit_points.empty() && (version < DxfVersion::AC1027 || v.knot_parameterization != 15) ? 2 : 1;
    w.w.bit_long(scenario);
    if (version >= DxfVersion::AC1027) {
        auto f = v.dwg_flags;
        f = v.cv_frame_visible ? f | 2 : f & ~2;
        f = v.closed ? f | 4 : f & ~4;
        f = scenario == 2 ? f | 9 : f & ~8;
        w.w.bit_long(f);
        w.w.bit_long(v.knot_parameterization);
    }
    auto degree = v.degree;
    auto knots = v.knots;
    if (scenario == 1 && knots.empty() && v.control_points.size() >= 2) {
        auto n = count(v.control_points.size());
        degree = std::clamp(degree, 1, n - 1);
        auto internal = n - degree - 1;
        knots.assign(degree + 1, 0);
        for (int i = 1; i <= internal; ++i)
            knots.push_back(double(i) / (internal + 1));
        knots.insert(knots.end(), degree + 1, 1);
    }
    w.w.bit_long(degree);
    if (scenario == 1) {
        w.b(v.rational);
        w.b(v.closed);
        w.b(v.periodic);
        w.d(v.knot_tolerance);
        w.d(v.control_tolerance);
        w.w.bit_long(count(knots.size()));
        w.w.bit_long(count(v.control_points.size()));
        w.b(!v.weights.empty());
        for (auto k : knots)
            w.d(k);
        for (std::size_t i = 0; i < v.control_points.size(); ++i) {
            w.p(v.control_points[i]);
            if (!v.weights.empty())
                w.d(i < v.weights.size() ? v.weights[i] : 1);
        }
    } else {
        w.d(v.fit_tolerance);
        w.p(v.begin_tangent);
        w.p(v.end_tangent);
        w.w.bit_long(count(v.fit_points.size()));
        for (auto p : v.fit_points)
            w.p(p);
    }
}
} // namespace
std::optional<EmbeddedProfile> decode_embedded_profile(const EncodedProfile &e, DxfVersion version) {
    version_check(version);
    if (e.bit_count / 8 > e.bytes.size() || (e.bit_count / 8 == e.bytes.size() && e.bit_count % 8))
        throw std::invalid_argument("Profile bit count exceeds bytes");
    if (!e.type_code || !e.bit_count)
        return std::nullopt;
    auto opaque = [&]() -> EmbeddedProfile {
        return ProfileOpaque{e.type_code, e.bit_count, e.bytes.substr(0, (e.bit_count + 7) / 8), version};
    };
    try {
        Read r(e);
        switch (e.type_code) {
        case 37: {
            auto v = read_region(r, version);
            auto encoded = encode_embedded_profile(v, version);
            return same_region_bits(e, encoded) ? EmbeddedProfile{std::move(v)} : opaque();
        }
        case 27: {
            ProfilePoint v;
            v.location = r.p();
            v.thickness = r.d();
            v.normal = r.p();
            v.x_axis_angle = r.d();
            return v;
        }
        case 19: {
            ProfileLine v;
            v.start = r.p();
            v.end = r.p();
            v.normal = r.p();
            return v;
        }
        case 17:
        case 18: {
            ProfileArc v;
            auto world = r.p();
            v.radius = r.d();
            v.normal = r.p();
            v.center = Matrix3::arbitrary_axis(v.normal).transpose() * world;
            if (e.type_code == 18)
                return static_cast<ProfileCircle>(v);
            v.start_angle = r.d();
            v.end_angle = r.d();
            return v;
        }
        case 35: {
            ProfileEllipse v;
            v.center = r.p();
            v.normal = r.p();
            v.major_axis = r.p();
            v.ratio = r.d();
            v.start_parameter = r.d();
            v.end_parameter = r.d();
            return v;
        }
        case 36:
            return read_spline(r, version);
        case 77:
            return read_poly(r);
        case 40:
        case 41: {
            ProfileXLine v;
            v.base_point = r.p();
            v.direction = r.p().normalize();
            if (e.type_code == 40)
                return static_cast<ProfileRay>(v);
            return v;
        }
        default:
            return opaque();
        }
    } catch (const std::exception &) {
        return opaque();
    }
}
EncodedProfile encode_embedded_profile(const EmbeddedProfile &entity, DxfVersion version) {
    version_check(version);
    if (auto p = std::get_if<ProfileOpaque>(&entity)) {
        if (p->source_version != version)
            throw std::invalid_argument("Cannot transcode opaque profile");
        if (!p->type_code || !p->bit_count || p->bit_count / 8 > p->bytes.size() ||
            (p->bit_count / 8 == p->bytes.size() && p->bit_count % 8))
            throw std::invalid_argument("Invalid opaque profile length/type");
        return {p->type_code, p->bit_count, p->bytes.substr(0, (p->bit_count + 7) / 8)};
    }
    Write w;
    int type = 0;
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ProfileRegion>) {
                type = 37;
                write_region(w, v, version);
            } else if constexpr (std::is_same_v<T, ProfilePoint>) {
                type = 27;
                w.p(v.location);
                w.d(v.thickness);
                w.p(v.normal);
                w.d(v.x_axis_angle);
            } else if constexpr (std::is_same_v<T, ProfileLine>) {
                type = 19;
                w.p(v.start);
                w.p(v.end);
                w.p(v.normal);
            } else if constexpr (std::is_same_v<T, ProfileArc> || std::is_same_v<T, ProfileCircle>) {
                type = std::is_same_v<T, ProfileArc> ? 17 : 18;
                w.p(Matrix3::arbitrary_axis(v.normal) * v.center);
                w.d(v.radius);
                w.p(v.normal);
                if constexpr (std::is_same_v<T, ProfileArc>) {
                    w.d(v.start_angle);
                    w.d(v.end_angle);
                }
            } else if constexpr (std::is_same_v<T, ProfileEllipse>) {
                type = 35;
                w.p(v.center);
                w.p(v.normal);
                w.p(v.major_axis);
                w.d(v.ratio);
                w.d(v.start_parameter);
                w.d(v.end_parameter);
            } else if constexpr (std::is_same_v<T, ProfileSpline>) {
                type = 36;
                write_spline(w, v, version);
            } else if constexpr (std::is_same_v<T, ProfilePolyline>) {
                type = 77;
                write_poly(w, v, version);
            } else if constexpr (std::is_same_v<T, ProfileRay> || std::is_same_v<T, ProfileXLine>) {
                type = std::is_same_v<T, ProfileRay> ? 40 : 41;
                w.p(v.base_point);
                w.p(v.direction);
            } else if constexpr (std::is_same_v<T, ProfileBody>)
                throw std::invalid_argument("SAT body requires DXF history framing");
        },
        entity);
    return {type, w.w.position(), w.w.bytes()};
}
DwgBitWriter detail::encode_dwg_history_parameters(const DwgHistoryRecord &r, DxfVersion v) {
    Write w;
    write_history_parameters(w, r, v);
    return w.w;
}
void detail::read_dwg_history_parameters(DwgBitReader &main, DwgBitReader &refs, std::size_t end,
                                         DxfVersion v, DwgHistoryRecord &record) {
    Read r(main, end);
    read_history_parameters(r, refs, v, record);
    main = r.r;
}
void detail::read_dwg_surface_body(DwgBitReader &main, std::size_t end, DxfVersion version, DwgModeler &m) {
    Read r(main, end);
    if (!m.data.extra) {
        auto checkpoint = r;
        try {
            (void)r.b();
            auto extra = read_acis_payload(r, r.r.bit_short());
            auto peek = r;
            auto u = peek.r.bit_short(), v = peek.r.bit_short();
            if (u >= 0 && u <= 512 && v >= 0 && v <= 512 && peek.r.position() <= end &&
                (!extra.sat.empty() || !extra.sab.empty()))
                m.data.extra = std::move(extra);
            else
                r = checkpoint;
        } catch (const std::exception &) {
            r = checkpoint;
        }
    }
    read_surface_parameters(r, version, *m.surface);
    main = r.r;
}
DwgBitWriter detail::encode_dwg_surface(const DwgSurface &s, DxfVersion v) {
    Write w;
    write_surface_parameters(w, v, s);
    return w.w;
}
void detail::read_dwg_modeler_body(DwgBitReader &main, DwgBitReader &handles, std::size_t main_end,
                                   std::uint64_t handle, DxfVersion version, DwgModeler &modeler) {
    Read input(main, main_end);
    RegionDatabase db{handles, handle, modeler};
    modeler.data = read_region(input, version, &db);
    main = input.r;
}
} // namespace libopencad
