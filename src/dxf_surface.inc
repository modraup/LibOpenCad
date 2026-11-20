// SPDX-License-Identifier: MPL-2.0
// acadrust entities/surface.rs and DXF surface reader/writer mappings.
namespace {
const char *surface_subclass(ModelerKind kind) {
    switch (kind) {
    case ModelerKind::ExtrudedSurface:
        return "AcDbExtrudedSurface";
    case ModelerKind::LoftedSurface:
        return "AcDbLoftedSurface";
    case ModelerKind::RevolvedSurface:
        return "AcDbRevolvedSurface";
    case ModelerKind::SweptSurface:
        return "AcDbSweptSurface";
    case ModelerKind::NurbSurface:
        return "AcDbNurbSurface";
    default:
        return "";
    }
}
template <class A> void surface_options(A &a, SurfaceSweepOptions &d) {
    a(42, d.draft_angle);
    a(43, d.draft_start_distance);
    a(44, d.draft_end_distance);
    a(45, d.twist_angle);
    a(48, d.scale_factor);
    a(49, d.align_angle);
    a(46, d.sweep_entity_transform);
    a(47, d.path_entity_transform);
    a(290, d.is_solid);
    a(70, d.sweep_alignment_flags);
    a(71, d.path_flags);
    a(292, d.align_start);
    a(293, d.bank);
    a(294, d.base_point_set);
    a(295, d.sweep_entity_transform_computed);
    a(296, d.path_entity_transform_computed);
    a(11, d.reference_vector);
}
template <class A> void surface_fields(A &a, SurfaceDefinition &d) {
    std::visit(
        [&](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ExtrudedSurfaceDefinition>) {
                a(10, v.sweep_vector);
                a(40, v.sweep_transform);
                surface_options(a, v.options);
            } else if constexpr (std::is_same_v<T, LoftedSurfaceDefinition>) {
                a(40, v.loft_transform);
                a(70, v.plane_normal_lofting_type);
                a(41, v.start_draft_angle);
                a(42, v.end_draft_angle);
                a(43, v.start_draft_magnitude);
                a(44, v.end_draft_magnitude);
                a(290, v.arc_length_parameterization);
                a(291, v.no_twist);
                a(292, v.align_direction);
                a(293, v.simple_surfaces);
                a(294, v.closed_surfaces);
                a(295, v.solid);
                a(296, v.ruled_surface);
                a(297, v.virtual_guide);
            } else if constexpr (std::is_same_v<T, RevolvedSurfaceDefinition>) {
                a(10, v.axis_point);
                a(11, v.axis_vector);
                a(40, v.revolve_angle);
                a(41, v.start_angle);
                a(42, v.entity_transform);
                a(43, v.draft_angle);
                a(44, v.draft_start_distance);
                a(45, v.draft_end_distance);
                a(46, v.twist_angle);
                a(290, v.solid);
                a(291, v.close_to_axis);
            } else if constexpr (std::is_same_v<T, SweptSurfaceDefinition>) {
                a(40, v.sweep_transform);
                a(41, v.path_transform);
                surface_options(a, v.options);
            } else if constexpr (std::is_same_v<T, NurbSurfaceDefinition>) {
                a(170, v.short_170);
                a(290, v.cv_hull_display);
                a(10, v.u_vector1);
                a(11, v.v_vector1);
                a(12, v.u_vector2);
                a(13, v.v_vector2);
            }
        },
        d);
}
// Profiles precede construction options and may themselves use 70 or 1/3.
// Only the options region is editable; the leading loft matrix is separate.
std::vector<bool> surface_private_mask(const std::vector<DxfRawField> &fields) {
    std::vector<bool> mask;
    unsigned depth = 0;
    for (const auto &f : fields) {
        bool hidden = depth != 0 || f.code == 102;
        mask.push_back(hidden);
        if (f.code == 102) {
            if (f.value == "}") {
                if (depth)
                    --depth;
            } else if (!f.value.empty() && f.value.front() == '{')
                ++depth;
        }
    }
    return mask;
}
std::size_t surface_options_start(ModelerKind k, const std::vector<DxfRawField> &fields) {
    auto private_fields = surface_private_mask(fields);
    if (k == ModelerKind::NurbSurface)
        return 0;
    if (k == ModelerKind::LoftedSurface) {
        for (std::size_t i = fields.size(); i > 0; --i)
            if (!private_fields[i - 1] && fields[i - 1].code == 70)
                return i - 1;
        return fields.size();
    }
    const int first = k == ModelerKind::SweptSurface ? 40 : 10;
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (!private_fields[i] && fields[i].code == first)
            return i;
    return fields.size();
}
struct SurfaceReadFields {
    const std::vector<DxfRawField> &fields;
    std::size_t begin;
    bool loft;
    const std::string *last(int code) const {
        const std::string *result = nullptr;
        for (std::size_t i = begin; i < fields.size(); ++i)
            if (fields[i].code == code)
                result = &fields[i].value;
        return result;
    }
    static double number(const std::string &s) {
        std::istringstream input(s);
        input.imbue(std::locale::classic());
        double value = 0;
        if (!(input >> value) || !(input >> std::ws).eof() || !std::isfinite(value))
            throw std::invalid_argument("Invalid surface numeric field");
        return value;
    }
    void operator()(int c, double &v) {
        if (auto s = last(c))
            v = number(*s);
    }
    void operator()(int c, bool &v) {
        if (auto s = last(c))
            v = number(*s) != 0;
    }
    void operator()(int c, std::int16_t &v) {
        if (auto s = last(c)) {
            double n = number(*s);
            if (std::trunc(n) != n || n < INT16_MIN || n > INT16_MAX)
                throw std::invalid_argument("Invalid surface short field");
            v = std::int16_t(n);
        }
    }
    void operator()(int c, Vector3 &v) {
        (*this)(c, v.x);
        (*this)(c + 10, v.y);
        (*this)(c + 20, v.z);
    }
    void operator()(int c, SurfaceMatrix &v) {
        std::vector<double> entries;
        for (std::size_t i = (loft && c == 40 ? 0 : begin); i < fields.size(); ++i)
            if (fields[i].code == c)
                entries.push_back(number(fields[i].value));
        if (entries.empty())
            return;
        if (entries.size() != 16)
            throw std::invalid_argument("Surface matrix requires 16 entries");
        for (std::size_t i = 0; i < 16; ++i)
            v[i] = entries[(i % 4) * 4 + i / 4];
    }
};
struct SurfaceWriteFields {
    std::vector<DxfRawField> fields;
    void operator()(int c, double &v) {
        if (!std::isfinite(v))
            throw std::invalid_argument("Non-finite surface parameter");
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.precision(17);
        out << v;
        fields.push_back({c, out.str()});
    }
    void operator()(int c, bool &v) { fields.push_back({c, v ? "1" : "0"}); }
    void operator()(int c, std::int16_t &v) { fields.push_back({c, std::to_string(v)}); }
    void operator()(int c, Vector3 &v) {
        (*this)(c, v.x);
        (*this)(c + 10, v.y);
        (*this)(c + 20, v.z);
    }
    void operator()(int c, SurfaceMatrix &v) {
        for (std::size_t i = 0; i < 16; ++i)
            (*this)(c, v[(i % 4) * 4 + i / 4]);
    }
};
SurfaceDefinition surface_default(ModelerKind k) {
    switch (k) {
    case ModelerKind::ExtrudedSurface:
        return ExtrudedSurfaceDefinition{};
    case ModelerKind::LoftedSurface:
        return LoftedSurfaceDefinition{};
    case ModelerKind::RevolvedSurface:
        return RevolvedSurfaceDefinition{};
    case ModelerKind::SweptSurface:
        return SweptSurfaceDefinition{};
    case ModelerKind::NurbSurface:
        return NurbSurfaceDefinition{};
    default:
        return std::monostate{};
    }
}
} // namespace
SurfaceDefinition DxfModelerEntity::surface_definition() const {
    if (kind() < ModelerKind::Surface)
        throw std::invalid_argument("Entity is not a surface");
    auto result = surface_default(kind());
    if (std::holds_alternative<std::monostate>(result))
        return result;
    auto all = subclass_fields(surface_subclass(kind()));
    auto mask = surface_private_mask(all);
    std::vector<DxfRawField> fields;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (!mask[i])
            fields.push_back(all[i]);
    SurfaceReadFields reader{fields, surface_options_start(kind(), fields),
                             kind() == ModelerKind::LoftedSurface};
    surface_fields(reader, result);
    return result;
}
void DxfModelerEntity::set_surface_definition(const SurfaceDefinition &definition) {
    if (kind() < ModelerKind::Surface || definition.index() != surface_default(kind()).index())
        throw std::invalid_argument("Surface definition kind mismatch");
    if (std::holds_alternative<std::monostate>(definition))
        return;
    auto value = definition;
    SurfaceWriteFields writer;
    surface_fields(writer, value);
    const auto name = surface_subclass(kind());
    const bool present = std::any_of(pairs_.begin(), pairs_.end(),
                                     [&](const auto &p) { return p.code == 100 && p.value == name; });
    auto original = present ? subclass_fields(name) : std::vector<DxfRawField>{};
    auto begin = surface_options_start(kind(), original);
    std::vector<DxfRawField> prefix, unknown;
    auto private_fields = surface_private_mask(original);
    for (std::size_t i = 0; i < original.size(); ++i) {
        const auto &f = original[i];
        bool controlled = !private_fields[i] && std::any_of(writer.fields.begin(), writer.fields.end(),
                                                            [&](const auto &w) { return f.code == w.code; });
        if (i < begin && !(kind() == ModelerKind::LoftedSurface && !private_fields[i] && f.code == 40))
            prefix.push_back(f);
        else if (!controlled)
            unknown.push_back(f);
    }
    if (original.empty()) {
        if (kind() == ModelerKind::ExtrudedSurface || kind() == ModelerKind::RevolvedSurface)
            prefix = {{90, "0"}, {90, "0"}};
        if (kind() == ModelerKind::SweptSurface)
            prefix = {{90, "0"}, {90, "0"}, {91, "0"}, {90, "0"}};
    }
    std::vector<DxfRawField> output;
    if (kind() == ModelerKind::LoftedSurface) {
        for (const auto &f : writer.fields)
            if (f.code == 40)
                output.push_back(f);
    }
    output.insert(output.end(), prefix.begin(), prefix.end());
    for (const auto &f : writer.fields)
        if (kind() != ModelerKind::LoftedSurface || f.code != 40)
            output.push_back(f);
    output.insert(output.end(), unknown.begin(), unknown.end());
    auto copy = *this;
    if (std::none_of(copy.pairs_.begin(), copy.pairs_.end(),
                     [&](const auto &p) { return p.code == 100 && p.value == name; }))
        copy.pairs_.push_back({100, name});
    copy.set_subclass_fields(name, output);
    *this = std::move(copy);
}
namespace {
detail::SurfaceProfileLayout surface_profile_layout(ModelerKind k) {
    using L = detail::SurfaceProfileLayout;
    switch (k) {
    case ModelerKind::ExtrudedSurface:
        return L::Extruded;
    case ModelerKind::LoftedSurface:
        return L::Lofted;
    case ModelerKind::RevolvedSurface:
        return L::Revolved;
    case ModelerKind::SweptSurface:
        return L::Swept;
    default:
        throw std::invalid_argument("Surface kind has no embedded construction profiles");
    }
}
std::size_t surface_profile_begin(ModelerKind k, const std::vector<DxfRawField> &f, std::size_t end) {
    if (k != ModelerKind::LoftedSurface)
        return 0;
    std::size_t n = 0;
    while (n < end && f[n].code == 40)
        ++n;
    if (n != 0 && n != 16)
        throw std::invalid_argument("Incomplete loft placement matrix");
    return n;
}
} // namespace
SurfaceProfiles DxfModelerEntity::surface_profiles() const {
    auto layout = surface_profile_layout(kind());
    auto all = subclass_fields(surface_subclass(kind()));
    auto mask = surface_private_mask(all);
    std::vector<DxfRawField> fields;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (!mask[i])
            fields.push_back(all[i]);
    auto end = surface_options_start(kind(), fields);
    auto begin = surface_profile_begin(kind(), fields, end);
    return detail::decode_surface_profiles(layout, {fields.begin() + begin, fields.begin() + end}, version_);
}
void DxfModelerEntity::set_surface_profiles(const SurfaceProfiles &profiles) {
    auto layout = surface_profile_layout(kind());
    auto name = surface_subclass(kind());
    auto fields = subclass_fields(name);
    auto end = surface_options_start(kind(), fields);
    auto mask = surface_private_mask(fields);
    for (std::size_t i = 0; i < end; ++i)
        if (mask[i])
            throw std::invalid_argument("Private data inside surface profiles");
    auto begin = surface_profile_begin(kind(), fields, end);
    // Validate source framing before replacing anything: malformed records remain recoverable raw data.
    (void)detail::decode_surface_profiles(layout, {fields.begin() + begin, fields.begin() + end}, version_);
    auto encoded = detail::encode_surface_profiles(layout, profiles, version_);
    std::vector<DxfRawField> output(fields.begin(), fields.begin() + begin);
    output.insert(output.end(), encoded.begin(), encoded.end());
    output.insert(output.end(), fields.begin() + end, fields.end());
    auto copy = *this;
    copy.set_subclass_fields(name, output);
    *this = std::move(copy);
}

SurfaceProfiles DxfDocument::surface_profiles(std::uint64_t id) const {
    const DxfModelerEntity *entity = nullptr;
    for_each<DxfModelerEntity>([&](const auto &e) {
        if (parse_handle(e.handle()) == id) {
            if (entity)
                throw std::invalid_argument("Duplicate surface handle");
            entity = &e;
        }
    });
    if (!entity)
        throw std::invalid_argument("Surface entity not found");
    auto value = entity->surface_profiles();
    if (entity->kind() != ModelerKind::LoftedSurface || !value.sections.empty() || !value.guides.empty() ||
        value.path)
        return value;
    auto dict = extension_dictionary_handle(entity->handle());
    auto entry = dictionary_entry(dict, "CADCODEC_LOFT_CURVES_V1");
    if (entry.empty())
        return value;
    auto it = handle_index_.find(parse_handle(entry).value_or(0));
    if (it == handle_index_.end() || it->second.size() != 1)
        throw std::invalid_argument("Missing or ambiguous loft fallback");
    auto &record = record_at(it->second.front());
    if (record.type() != "XRECORD")
        throw std::invalid_argument("Invalid loft fallback record");
    auto fields = record.subclass_fields("AcDbXrecord");
    if (!fields.empty() && fields.front().code == 280)
        fields.erase(fields.begin());
    return detail::decode_loft_profile_metadata(fields);
}
void DxfDocument::set_surface_profiles(std::uint64_t id, const SurfaceProfiles &profiles) {
    auto copy = *this;
    bool found = false, loft = false;
    copy.for_each<DxfModelerEntity>([&](auto &e) {
        if (parse_handle(e.handle()) != id)
            return;
        if (found)
            throw std::invalid_argument("Duplicate surface handle");
        found = true;
        loft = e.kind() == ModelerKind::LoftedSurface;
        e.set_surface_profiles(profiles);
    });
    if (!found)
        throw std::invalid_argument("Surface entity not found");
    if (loft) {
        auto dict = copy.extension_dictionary_handle(hex_handle(id));
        auto entry = copy.dictionary_entry(dict, "CADCODEC_LOFT_CURVES_V1");
        if (!entry.empty()) {
            auto entry_id = parse_handle(entry).value_or(0);
            auto it = copy.handle_index_.find(entry_id);
            if (it == copy.handle_index_.end() || it->second.size() != 1)
                throw std::invalid_argument("Missing or ambiguous loft fallback");
            auto &record = copy.record_at(it->second.front());
            auto previous = record.subclass_fields("AcDbXrecord");
            if (!previous.empty() && previous.front().code == 280)
                previous.erase(previous.begin());
            if (previous.empty() || previous.front().code != 90 || trim(previous.front().value) != "1")
                throw std::invalid_argument("Unsupported loft fallback schema");
            for (std::size_t i = 1; i < previous.size(); ++i)
                if (previous[i].code != 70 && previous[i].code != 91 && previous[i].code != 92 &&
                    previous[i].code != 310)
                    throw std::invalid_argument("Unknown loft fallback fields");
            for (auto &f : record.pairs_)
                if (f.code == 102)
                    throw std::invalid_argument("Private loft fallback data");
            std::size_t references = 0;
            for (const auto &[handle, locations] : copy.handle_index_)
                for (auto &location : locations) {
                    auto &r = copy.record_at(location);
                    for (auto [at, editable] : reference_positions(r))
                        if (parse_handle(r.pairs_[at].value) == entry_id) {
                            if (handle != parse_handle(dict) || ++references != 1)
                                throw std::invalid_argument("Shared loft fallback record");
                        }
                }
            if (references != 1)
                throw std::invalid_argument("Unowned loft fallback record");
            auto fields = detail::encode_loft_profile_metadata(profiles);
            copy.write_history_metadata(id, "CADCODEC_LOFT_CURVES_V1", &fields);
        }
    }
    *this = std::move(copy);
}
