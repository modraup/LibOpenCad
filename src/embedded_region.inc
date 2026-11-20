// SPDX-License-Identifier: MPL-2.0
// Inline COMMON_3DSOLID layout from acadrust embedded_entity/object_reader/writer.
std::string region_sat(std::string_view text) {
    return detail::dwg_sat_transport(text, false);
}
ProfileAcis read_acis_payload(Read &r, int version) {
    ProfileAcis v;
    if (version == 1) {
        std::string encrypted;
        for (;;) {
            auto n = r.n(8);
            if (!n)
                break;
            for (std::size_t i = 0; i < n; ++i)
                encrypted += char(r.r.bits(8));
        }
        v.sat = region_sat(detail::dwg_sat_cipher(encrypted));
    } else if (version == 2) {
        v.is_binary = true;
        auto start = r.r.position();
        std::vector<std::uint8_t> probe;
        auto available = (r.limit - start) / 8;
        probe.reserve(available);
        for (std::size_t i = 0; i < available; ++i)
            probe.push_back(r.r.bits(8));
        auto parsed = acis::SabReader::read_with_consumed(probe);
        if (!parsed.second || parsed.second > probe.size())
            throw std::invalid_argument("Invalid inline SAB length");
        probe.resize(parsed.second);
        v.sab = std::move(probe);
        r.r.seek(start + parsed.second * 8);
    } else
        throw std::invalid_argument("Unsupported inline ACIS version");
    return v;
}
void write_acis_payload(Write &w, const ProfileAcis &v, bool extra = false) {
    if (v.is_binary && !v.sab.empty()) {
        w.w.bit_short(2);
        auto bytes = v.sab;
        // Match upstream's canonical product normalization; foreign SAB stays untouched.
        if (!extra && bytes.size() >= 41 && bytes[31] == 7 && bytes[32] == 8 &&
            std::string(bytes.begin() + 33, bytes.begin() + 41) == "acadrust")
            bytes = acis::SabWriter::write(acis::SabReader::read(bytes));
        auto used = acis::SabReader::read_with_consumed(bytes).second;
        if (used != bytes.size())
            throw std::invalid_argument("Trailing inline SAB bytes");
        for (auto b : bytes)
            w.w.bits(8, b);
    } else {
        w.w.bit_short(1);
        std::string text = v.sat;
        if (text.empty() && !v.sab.empty())
            text = acis::SabReader::read(v.sab).to_sat_string();
        text = detail::dwg_sat_cipher(detail::dwg_sat_transport(text, !extra));
        for (std::size_t i = 0; i < text.size(); i += 4096) {
            auto n = std::min<std::size_t>(4096, text.size() - i);
            w.w.bit_long(count(n));
            for (std::size_t j = 0; j < n; ++j)
                w.w.bits(8, static_cast<unsigned char>(text[i + j]));
        }
        w.w.bit_long(0);
    }
}
ProfileWire read_wire(Read &r, bool wide = false) {
    ProfileWire v;
    auto type = r.r.bits(8);
    v.type = type <= 4 ? type : 0;
    v.selection_marker = r.r.bit_long();
    auto color = wide ? r.r.bit_long() : r.r.bit_short();
    v.color = color == 256 ? 256 : static_cast<std::uint8_t>(color);
    v.acis_index = r.r.bit_long();
    auto n = r.n(6);
    for (std::size_t i = 0; i < n; ++i)
        v.points.push_back(r.p());
    v.has_transform = r.b();
    if (v.has_transform) {
        v.x_axis = r.p();
        v.y_axis = r.p();
        v.z_axis = r.p();
        v.translation = r.p();
        v.scale = r.d();
        v.rotation = r.b();
        v.reflection = r.b();
        v.shear = r.b();
    }
    return v;
}
void write_wire(Write &w, const ProfileWire &v) {
    if (v.type > 4 || v.color < 0 || v.color > 256)
        throw std::invalid_argument("Invalid inline wire type/color");
    w.w.bits(8, v.type);
    w.w.bit_long(v.selection_marker);
    w.w.bit_short(static_cast<std::int16_t>(v.color));
    w.w.bit_long(v.acis_index);
    w.w.bit_long(count(v.points.size()));
    for (auto p : v.points)
        w.p(p);
    w.b(v.has_transform);
    if (v.has_transform) {
        w.p(v.x_axis);
        w.p(v.y_axis);
        w.p(v.z_axis);
        w.p(v.translation);
        w.d(v.scale);
        w.b(v.rotation);
        w.b(v.reflection);
        w.b(v.shear);
    }
}
void read_region_frame(Read &r, ProfileRegion &v, DxfVersion version, bool wide = false) {
    v.point_present = r.b();
    if (v.point_present)
        v.reference_point = r.p();
    v.isolines = r.r.bit_long();
    v.isoline_present = r.b();
    if (v.isoline_present) {
        auto n = r.n();
        for (std::size_t i = 0; i < n; ++i)
            v.wires.push_back(read_wire(r, wide));
    }
    auto n = r.n();
    for (std::size_t i = 0; i < n; ++i) {
        ProfileSilhouette s;
        if (version >= DxfVersion::AC1024) {
            auto bytes = unsigned(r.r.bits(3));
            s.viewport_id = r.r.little_endian(bytes);
        } else {
            s.viewport_id = static_cast<std::uint64_t>(std::int64_t(r.r.bit_long()));
        }
        s.target = r.p();
        s.direction = r.p();
        s.up = r.p();
        s.perspective = r.b();
        s.has_wires = r.b();
        if (s.has_wires) {
            auto m = r.n();
            for (std::size_t j = 0; j < m; ++j)
                s.wires.push_back(read_wire(r, wide));
        }
        v.silhouettes.push_back(std::move(s));
    }
}
ProfileRegion read_region(Read &r, DxfVersion version, RegionDatabase *db = nullptr) {
    ProfileRegion v;
    bool ds = db && db->modeler.properties.has_ds_data;
    bool empty = db && !db->force_inline && version >= DxfVersion::AC1027 ? !ds : r.b();
    int acis_version = ds && version >= DxfVersion::AC1027 ? 2 : 0;
    if (!empty && !ds) {
        (void)r.b();
        acis_version = r.r.bit_short();
        if (db)
            db->modeler.payload_begin_bit = r.r.position();
        v.geometry = read_acis_payload(r, acis_version);
        if (db)
            db->modeler.payload_end_bit = r.r.position();
    }
    if (ds)
        v.geometry.is_binary = true;
    if (db) {
        db->modeler.acis_empty = empty;
        db->modeler.acis_version = static_cast<std::int16_t>(acis_version);
    }
    if(db && db->force_inline && r.r.position()==r.limit)return v;
    v.wireframe_present = r.b();
    if (v.wireframe_present) {
        if (db && version >= DxfVersion::AC1018) {
            auto checkpoint = r;
            auto original = v;
            try {
                read_region_frame(r, v, version);
            } catch (const std::invalid_argument &) {
                r = checkpoint;
                v = original;
                read_region_frame(r, v, version, true);
            }
        } else
            read_region_frame(r, v, version);
    }
    bool extra_gate =
        !db || v.wireframe_present ||
        (version < DxfVersion::AC1027 && (version >= DxfVersion::AC1021 || r.r.position() < r.limit));
    if (extra_gate) {
        bool no_extra = r.b();
        if (db)
            db->modeler.acis_empty_bit = no_extra;
        if (!no_extra) {
            auto checkpoint = r;
            if (!db) {
                (void)r.b();
                v.extra = read_acis_payload(r, r.r.bit_short());
            } else if (db->allow_extra && (!db->modeler.surface || version >= DxfVersion::AC1027) &&
                       r.limit - r.r.position() >= 11) {
                try {
                    (void)r.b();
                    auto ver = r.r.bit_short();
                    auto data = read_acis_payload(r, ver);
                    if (data.sat.empty() && data.sab.empty())
                        r = checkpoint;
                    else
                        v.extra = std::move(data);
                } catch (const std::exception &) {
                    r = checkpoint;
                }
            }
        }
    }
    if (db) {
        db->modeler.materials_begin_bit = r.r.position();
        db->modeler.material_handles_begin = db->handles.position();
    }
    if (version >= DxfVersion::AC1021) {
        if (acis_version > 1) {
            auto n = r.n(db ? 4 : 12);
            for (std::size_t i = 0; i < n; ++i) {
                ProfileMaterial m;
                m.array_index = r.r.bit_long();
                m.absolute_reference = r.r.bit_long();
                m.handle = db ? db->handles.handle(db->self).value : r.r.handle().value;
                v.materials.push_back(m);
            }
        } else
            (void)r.r.bit_long();
    }
    if (db) {
        db->modeler.materials_end_bit = r.r.position();
        db->modeler.material_handles_end = db->handles.position();
    }
    if (version >= DxfVersion::AC1027) {
        auto &rev = v.revision;
        rev.has_guid = r.b();
        rev.major = static_cast<std::uint32_t>(r.r.bit_long());
        rev.minor1 = r.r.bit_short();
        rev.minor2 = r.r.bit_short();
        for (auto &b : rev.bytes)
            b = r.r.bits(8);
        rev.end_marker = static_cast<std::uint32_t>(r.r.bit_long());
    }
    if (db)
        return v;
    if (r.limit - r.r.position() > 7)
        throw std::invalid_argument("Unrecognized inline REGION tail");
    while (r.r.position() < r.limit)
        if (r.b())
            throw std::invalid_argument("Nonzero inline REGION padding");
    return v;
}
void write_region(Write &w, const ProfileRegion &v, DxfVersion version) {
    if (version < DxfVersion::AC1021)
        throw std::invalid_argument("Typed inline REGION authoring requires R2007+");
    if (v.extra && v.extra->sat.empty() && v.extra->sab.empty())
        throw std::invalid_argument("Empty secondary ACIS stream");
    auto data = v.geometry;
    if (version >= DxfVersion::AC1018 && !data.is_binary && !data.sat.empty()) {
        // Only unparseable legacy SAT stays in version 1, as in upstream.
        std::optional<acis::SatDocument> doc;
        try {
            doc = acis::SatDocument::parse(data.sat);
        } catch (const std::exception &) {
        }
        if (doc) {
            data.sab = acis::SabWriter::write(*doc);
            data.is_binary = true;
        }
    }
    auto present = !data.sat.empty() || !data.sab.empty();
    w.b(!present);
    if (present) {
        w.b(!data.is_binary);
        write_acis_payload(w, data);
    }
    auto frame = v.wireframe_present || (present && version < DxfVersion::AC1027) ||
                 v.reference_point != Vector3{} || v.isolines || !v.wires.empty() || !v.silhouettes.empty();
    w.b(frame);
    if (frame) {
        auto point = v.point_present || v.reference_point != Vector3{};
        w.b(point);
        if (point)
            w.p(v.reference_point);
        w.w.bit_long(v.isolines);
        auto isoline = v.isoline_present || !v.wires.empty();
        w.b(isoline);
        if (isoline) {
            w.w.bit_long(count(v.wires.size()));
            for (auto &wire : v.wires)
                write_wire(w, wire);
        }
        w.w.bit_long(count(v.silhouettes.size()));
        for (auto &s : v.silhouettes) {
            if (version >= DxfVersion::AC1024) {
                unsigned bytes = 0;
                auto n = s.viewport_id;
                while (n) {
                    ++bytes;
                    n >>= 8;
                }
                if (bytes > 7)
                    throw std::invalid_argument("Inline silhouette BLL exceeds upstream's 56-bit range");
                w.w.bits(3, bytes);
                w.w.little_endian(bytes, s.viewport_id);
            } else
                w.w.bit_long(static_cast<std::int32_t>(s.viewport_id));
            w.p(s.target);
            w.p(s.direction);
            w.p(s.up);
            w.b(s.perspective);
            auto wires = s.has_wires || !s.wires.empty();
            w.b(wires);
            if (wires) {
                w.w.bit_long(count(s.wires.size()));
                for (auto &wire : s.wires)
                    write_wire(w, wire);
            }
        }
    }
    w.b(!v.extra);
    if (v.extra) {
        w.b(false);
        write_acis_payload(w, *v.extra, true);
    }
    if (version >= DxfVersion::AC1021) {
        if (present && data.is_binary && !data.sab.empty()) {
            w.w.bit_long(count(v.materials.size()));
            for (auto &m : v.materials) {
                w.w.bit_long(m.array_index);
                w.w.bit_long(m.absolute_reference);
                w.w.absolute_handle(5, m.handle);
            }
        } else
            w.w.bit_long(0);
    }
    if (version >= DxfVersion::AC1027) {
        auto &rev = v.revision;
        w.b(rev.has_guid);
        w.w.bit_long(static_cast<std::int32_t>(rev.major));
        w.w.bit_short(rev.minor1);
        w.w.bit_short(rev.minor2);
        for (auto b : rev.bytes)
            w.w.bits(8, b);
        w.w.bit_long(static_cast<std::int32_t>(rev.end_marker));
    }
}
bool same_region_bits(const EncodedProfile &original, const EncodedProfile &encoded) {
    if (encoded.bit_count > original.bit_count || original.bit_count - encoded.bit_count > 7 ||
        (encoded.bit_count != original.bit_count && original.bit_count % 8))
        return false;
    for (std::size_t i = 0; i < original.bit_count; ++i) {
        auto bit = [&](const std::string &s) {
            return (static_cast<unsigned char>(s[i / 8]) >> (7 - i % 8)) & 1;
        };
        if (i < encoded.bit_count) {
            if (bit(original.bytes) != bit(encoded.bytes))
                return false;
        } else if (bit(original.bytes))
            return false;
    }
    return true;
}
