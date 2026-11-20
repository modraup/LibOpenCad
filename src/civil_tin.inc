// SPDX-License-Identifier: MPL-2.0
// Port of acadrust classes/civil/create/tin.rs and classes/civil/tin.rs.
void write_raw_i32(DwgBitWriter &w, std::int32_t value) {
    w.little_endian(4, static_cast<std::uint32_t>(value));
}
void write_bytes(DwgBitWriter &w, const std::uint8_t *data, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        write_byte(w, data[i]);
}
std::int32_t raw_i32(DwgBitReader &r) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(r.little_endian(4)));
}
void skip_bits(DwgBitReader &r, std::size_t n) {
    while (n) {
        auto take = static_cast<unsigned>(std::min<std::size_t>(n, 64));
        r.bits(take);
        n -= take;
    }
}
bool tin_in_circle(Vector3 a, Vector3 b, Vector3 c, Vector3 p) {
    auto ax = a.x - p.x, ay = a.y - p.y, bx = b.x - p.x, by = b.y - p.y, cx = c.x - p.x, cy = c.y - p.y;
    auto det = (ax * ax + ay * ay) * (bx * cy - cx * by) - (bx * bx + by * by) * (ax * cy - cx * ay) +
               (cx * cx + cy * cy) * (ax * by - bx * ay);
    return det > 0;
}
struct TinMesh {
    std::vector<Vector3> vertices;
    std::vector<std::array<std::size_t, 3>> triangles;
    std::vector<std::array<std::int32_t, 3>> neighbors() const {
        std::map<std::pair<std::size_t, std::size_t>, std::int32_t> by_edge;
        for (std::size_t t = 0; t < triangles.size(); ++t) {
            auto &tri = triangles[t];
            for (int k = 0; k < 3; ++k)
                by_edge[{tri[static_cast<std::size_t>(k)], tri[static_cast<std::size_t>((k + 1) % 3)]}] =
                    static_cast<std::int32_t>(t);
        }
        std::vector<std::array<std::int32_t, 3>> out;
        for (auto &tri : triangles) {
            std::array<std::int32_t, 3> n{};
            for (int k = 0; k < 3; ++k) {
                auto key = std::pair<std::size_t, std::size_t>{tri[static_cast<std::size_t>((k + 1) % 3)],
                                                              tri[static_cast<std::size_t>(k)]};
                auto it = by_edge.find(key);
                n[static_cast<std::size_t>(k)] = it == by_edge.end() ? -1 : it->second;
            }
            out.push_back(n);
        }
        return out;
    }
};
TinMesh triangulate_tin(const std::vector<Vector3> &points) {
    auto lo = points.front(), hi = points.front();
    for (auto &p : points) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    auto corner = [&](double x, double y) {
        auto near = points.front();
        auto best = (near.x - x) * (near.x - x) + (near.y - y) * (near.y - y);
        for (auto &p : points) {
            auto d = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
            if (d < best) {
                best = d;
                near = p;
            }
        }
        return Vector3{x, y, near.z};
    };
    auto m = TIN_CORNER_MARGIN;
    TinMesh tin;
    tin.vertices = {corner(lo.x - m, lo.y - m), corner(hi.x + m, lo.y - m), corner(hi.x + m, hi.y + m),
                    corner(lo.x - m, hi.y + m)};
    tin.triangles = {{{0, 1, 2}, {0, 2, 3}}};
    for (auto &p : points) {
        auto i = tin.vertices.size();
        tin.vertices.push_back(p);
        std::vector<std::size_t> bad;
        for (std::size_t t = 0; t < tin.triangles.size(); ++t) {
            auto [a, b, c] = tin.triangles[t];
            if (tin_in_circle(tin.vertices[a], tin.vertices[b], tin.vertices[c], p))
                bad.push_back(t);
        }
        std::vector<std::pair<std::size_t, std::size_t>> edges;
        for (auto t : bad) {
            auto [a, b, c] = tin.triangles[t];
            for (auto e : {std::pair<std::size_t, std::size_t>{a, b}, {b, c}, {c, a}}) {
                auto k = edges.size();
                for (std::size_t j = 0; j < edges.size(); ++j)
                    if (edges[j].first == e.second && edges[j].second == e.first) {
                        k = j;
                        break;
                    }
                if (k != edges.size())
                    edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(k));
                else
                    edges.push_back(e);
            }
        }
        for (auto it = bad.rbegin(); it != bad.rend(); ++it) {
            tin.triangles[*it] = tin.triangles.back();
            tin.triangles.pop_back();
        }
        for (auto [a, b] : edges)
            tin.triangles.push_back({a, b, i});
    }
    return tin;
}
void write_guid(DwgBitWriter &w, std::uint64_t seed) {
    auto a = seed ^ 0x9e3779b97f4a7c15ull;
    auto b = civil_file_time() ^ (seed * 0xbf58476d1ce4e5b9ull + 1);
    for (int i = 0; i < 8; ++i)
        write_byte(w, static_cast<std::uint8_t>(a >> (8 * i)));
    for (int i = 0; i < 8; ++i)
        write_byte(w, static_cast<std::uint8_t>(b >> (8 * i)));
}
std::string encode_tin_entity(int type, std::uint64_t id, std::uint64_t layer, std::string_view name,
                              const std::vector<Vector3> &points, const TinMesh &tin, std::uint64_t style,
                              const char *page) {
    ArchWriter w;
    w.v = 1032;
    w.page = page;
    auto split = arch_entity_common(w, type, id, layer, {});
    auto neighbors = tin.neighbors();
    std::vector<std::int32_t> incident(tin.vertices.size(), 0);
    for (std::size_t t = 0; t < tin.triangles.size(); ++t)
        for (auto v : tin.triangles[t])
            incident[v] = static_cast<std::int32_t>(t);
    w.main.bit_long(80);
    write_bit_run(w.main, "11");
    for (auto v : {static_cast<std::int32_t>(0x1554C738), static_cast<std::int32_t>(0x087C1120), 5, 8})
        w.main.bit_long(v);
    w.main.bit_long(static_cast<std::int32_t>(0x03011E29));
    write_byte(w.main, 1);
    w.main.bit_long(static_cast<std::int32_t>(0x0D011E25));
    w.main.bit_long(0);
    w.main.bit_long(static_cast<std::int32_t>(0x02011E26));
    w.bd(std::numeric_limits<double>::max());
    for (auto v : {static_cast<std::int32_t>(0x0B001081), 2, 0, 0, static_cast<std::int32_t>(0x03001093)})
        w.main.bit_long(v);
    write_byte(w.main, 0);
    w.main.bit_long(static_cast<std::int32_t>(0x0201230C));
    w.bd(0);
    w.main.bit_long(static_cast<std::int32_t>(0x02011E27));
    w.bd(std::numeric_limits<double>::max());
    w.main.bit_long(static_cast<std::int32_t>(0x01011E28));
    w.main.bit_long(8);
    bl_byte(w.main, 0);
    write_bit_run(w.main, std::string(121, '0'));
    for (auto v : {0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0})
        w.bd(v);
    write_byte(w.main, 1);
    write_bit_run(w.main, "01000");
    write_bit_run(w.main, "00");
    write_raw_i32(w.main, static_cast<std::int32_t>(0x80020A28u));
    for (auto v : {0, 0, static_cast<std::int32_t>(0xE8A5800Cu), 40, 0})
        w.main.bit_long(v);
    write_bit_run(w.main, "000");
    write_guid(w.main, id ^ static_cast<std::uint64_t>(points.size()));
    w.main.bit_long(0);
    w.main.bit_long(static_cast<std::int32_t>(points.size()));
    for (auto &p : points) {
        w.bd(p.x);
        w.bd(p.y);
        w.bd(p.z);
    }
    w.main.bits(2, 0);
    write_raw_i32(w.main, 0);
    w.main.bit_long(static_cast<std::int32_t>(0xAC150A00u));
    write_bit_run(w.main, "00");
    write_raw_i32(w.main, -1);
    const std::uint8_t stamp[] = {0xA2, 0x04, 2, 0, 0, 0x3C, 0x2A};
    write_bytes(w.main, stamp, sizeof(stamp));
    for (auto c : std::string_view("MMS01"))
        write_byte(w.main, static_cast<std::uint8_t>(c));
    w.main.bit_long(static_cast<std::int32_t>(tin.vertices.size()));
    for (int c = 0; c < 4; ++c)
        w.main.bit_long(c);
    for (std::size_t i = 0; i < tin.vertices.size(); ++i) {
        write_raw_i32(w.main, static_cast<std::int32_t>(i < 4 ? TIN_CORNER : TIN_POINT));
        write_raw_i32(w.main, incident[i]);
        raw_double(w.main, tin.vertices[i].x);
        raw_double(w.main, tin.vertices[i].y);
        raw_double(w.main, tin.vertices[i].z);
    }
    w.main.bit_long(static_cast<std::int32_t>(tin.triangles.size()));
    for (std::size_t t = 0; t < tin.triangles.size(); ++t) {
        for (auto v : tin.triangles[t])
            write_raw_i32(w.main, static_cast<std::int32_t>(v));
        for (auto n : neighbors[t])
            write_raw_i32(w.main, n);
        auto hidden = 0;
        for (auto v : tin.triangles[t])
            if (v < 4)
                hidden = 1;
        write_raw_i32(w.main, hidden);
        write_raw_i32(w.main, -1);
    }
    write_bit_run(w.main, "1010100");
    w.bs(CIVIL_VERSION);
    w.href(SOFT_POINTER, 0);
    w.href(HARD_OWNER, 0);
    w.href(HARD_POINTER, 0);
    w.href(HARD_POINTER, style);
    w.str("");
    w.str("");
    w.str(name);
    w.str("");
    w.str("");
    return finish_arch(w, split);
}
constexpr std::size_t TIN_VERTEX_BITS = 2 * 32 + 3 * 64;
constexpr std::size_t TIN_TRIANGLE_BITS = 8 * 32;
constexpr std::int32_t TIN_CORNER_FLAG = 0x40;
std::optional<TinSurface> read_tin_at(DwgBitReader &bits) {
    try {
        auto count = bits.bit_long();
        if (count < 7 || static_cast<std::size_t>(count) * TIN_VERTEX_BITS > bits.remaining())
            return std::nullopt;
        TinSurface surface;
        for (int i = 0; i < 4; ++i) {
            auto corner = bits.bit_long();
            if (corner < 0 || corner >= count)
                return std::nullopt;
            surface.corners[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(corner);
        }
        auto first = raw_i32(bits);
        if ((first & (TIN_CORNER_FLAG | 0x80)) == 0)
            return std::nullopt;
        bits.seek(bits.position() - 32);
        surface.vertices.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            TinVertex v;
            v.flags = raw_i32(bits);
            v.triangle = raw_i32(bits);
            v.position = {bits.raw_double(), bits.raw_double(), bits.raw_double()};
            if (!(std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z)))
                return std::nullopt;
            surface.vertices.push_back(v);
        }
        for (auto c : surface.corners)
            if ((surface.vertices[c].flags & TIN_CORNER_FLAG) == 0)
                return std::nullopt;
        auto triangle_count = bits.bit_long();
        if (triangle_count <= 0 || static_cast<std::size_t>(triangle_count) * TIN_TRIANGLE_BITS > bits.remaining())
            return std::nullopt;
        surface.triangles.reserve(static_cast<std::size_t>(triangle_count));
        for (int i = 0; i < triangle_count; ++i) {
            TinTriangle tri;
            for (int k = 0; k < 3; ++k) {
                auto v = raw_i32(bits);
                if (v < 0 || v >= count)
                    return std::nullopt;
                tri.vertices[static_cast<std::size_t>(k)] = static_cast<std::uint32_t>(v);
            }
            for (int k = 0; k < 3; ++k) {
                auto n = raw_i32(bits);
                if (n < -1 || static_cast<std::int64_t>(n) >= triangle_count)
                    return std::nullopt;
                tri.neighbors[static_cast<std::size_t>(k)] = n;
            }
            tri.flags = raw_i32(bits);
            raw_i32(bits);
            surface.triangles.push_back(tri);
        }
        return surface;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
bool skip_written_tin_header(DwgBitReader &main) {
    try {
        if (main.bit_long() != 80 || main.bits(2) != 3)
            return false;
        main.bit_long();
        main.bit_long();
        if (main.bit_long() != 5 || main.bit_long() != 8)
            return false;
        if (main.bit_long() != static_cast<std::int32_t>(0x03011E29) || main.bits(8) != 1)
            return false;
        if (main.bit_long() != static_cast<std::int32_t>(0x0D011E25))
            return false;
        main.bit_long();
        if (main.bit_long() != static_cast<std::int32_t>(0x02011E26))
            return false;
        main.bit_double();
        if (main.bit_long() != static_cast<std::int32_t>(0x0B001081))
            return false;
        main.bit_long();
        main.bit_long();
        main.bit_long();
        if (main.bit_long() != static_cast<std::int32_t>(0x03001093))
            return false;
        main.bits(8);
        if (main.bit_long() != static_cast<std::int32_t>(0x0201230C))
            return false;
        main.bit_double();
        if (main.bit_long() != static_cast<std::int32_t>(0x02011E27))
            return false;
        main.bit_double();
        if (main.bit_long() != static_cast<std::int32_t>(0x01011E28))
            return false;
        main.bit_long();
        if (main.bits(2) != 1 || main.bits(8) != 0)
            return false;
        skip_bits(main, 121);
        for (int i = 0; i < 13; ++i)
            main.bit_double();
        if (main.bits(8) != 1)
            return false;
        skip_bits(main, 5);
        skip_bits(main, 2);
        main.little_endian(4);
        for (int i = 0; i < 5; ++i)
            main.bit_long();
        skip_bits(main, 3);
        skip_bits(main, 128);
        if (main.bit_long() != 0)
            return false;
        auto n = main.bit_long();
        if (n < 3 || n > 1000000)
            return false;
        for (int i = 0; i < n * 3; ++i)
            main.bit_double();
        if (main.bits(2) != 0)
            return false;
        main.little_endian(4);
        main.bit_long();
        skip_bits(main, 2);
        main.little_endian(4);
        skip_bits(main, 7 * 8);
        char mms[5];
        for (int i = 0; i < 5; ++i)
            mms[i] = static_cast<char>(main.bits(8));
        return std::string_view(mms, 5) == "MMS01";
    } catch (const std::exception &) {
        return false;
    }
}
std::optional<TinSurface> parse_tin_surface(const ArchEntityData &entity) {
    if (entity.dxf_name != "AECC_TIN_SURFACE" || !entity.raw_dwg_data ||
        entity.dwg_source_version != DxfVersion::AC1032)
        return std::nullopt;
    try {
        auto rec = unframed_arch_record(*entity.raw_dwg_data, entity.dwg_handle_bits, 1032, entity.handle);
        auto s = open_dynamic(*entity.raw_dwg_data, rec, entity.handle, 1032, nullptr);
        read_entity_properties(s.main, s.refs, entity.handle, s.end, 1032, rec, nullptr, nullptr, nullptr, nullptr);
        auto start = s.main.position();
        if (skip_written_tin_header(s.main))
            if (auto tin = read_tin_at(s.main))
                return tin;
        auto end = s.end;
        for (auto pos = start; pos + 40 < end; ++pos) {
            try {
                s.main.seek(pos);
                char mms[5];
                for (int i = 0; i < 5; ++i)
                    mms[i] = static_cast<char>(s.main.bits(8));
                if (std::string_view(mms, 5) != "MMS01")
                    continue;
                if (auto tin = read_tin_at(s.main))
                    return tin;
            } catch (const std::exception &) {
            }
        }
        for (auto pos = start; pos + TIN_VERTEX_BITS * 7 < end; pos += 8) {
            try {
                s.main.seek(pos);
                if (auto tin = read_tin_at(s.main))
                    return tin;
            } catch (const std::exception &) {
            }
        }
        return std::nullopt;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
