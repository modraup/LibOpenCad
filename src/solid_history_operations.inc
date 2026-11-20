// SPDX-License-Identifier: MPL-2.0
// acadrust document.rs create/append/update_solid_history and DXF history schemas.
namespace {
const char *history_names[] = {"Box",     "Wedge", "Sphere",    "Cylinder", "Cone",
                               "Pyramid", "Torus", "Boolean",   "Brep",     "Fillet",
                               "Chamfer", "Sweep", "Extrusion", "Loft",     "Revolve"};
std::string history_cpp(SolidHistoryKind kind) {
    auto i = std::size_t(kind);
    if (i >= 15)
        throw std::invalid_argument("Unknown history kind");
    return std::string("AcDbSh") + history_names[i];
}
std::string history_dxf(SolidHistoryKind kind) {
    auto s = history_cpp(kind).substr(6);
    for (auto &c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return "ACSH_" + s + "_CLASS";
}
std::string history_scope(SolidHistoryKind k) {
    if (k == SolidHistoryKind::Cone)
        return "AcDbShCylinder";
    if (k == SolidHistoryKind::Sweep || k == SolidHistoryKind::Extrusion)
        return "AcDbShSweepBase";
    return history_cpp(k);
}
std::string history_cipher(std::string_view text) {
    std::string out;
    for (unsigned char c : text) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t' || c >= 128)
            out += char(c);
        else if (c >= '@' && c <= '_')
            out += char(159 - c);
        else
            out += char(c ^ 0x5f);
    }
    return out;
}
std::string history_sat(std::string s) {
    std::istringstream in(s);
    std::string line, out;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("End-of-ACIS-data", 0) == 0 || line.rfind("End-of-ASM-data", 0) == 0)
            break;
        out += line;
        out += '\n';
    }
    return trim(out).empty() ? std::string{} : out;
}
std::vector<std::string> history_wrap_sat(const std::string &line) {
    std::vector<std::string> pieces;
    std::size_t start = 0, index = 0;
    std::optional<std::size_t> split;
    while (index < line.size()) {
        if (line[index] == '@' && (index == 0 || line[index - 1] == ' ')) {
            std::size_t at = index + 1, count = 0;
            while (at < line.size() && line[at] >= '0' && line[at] <= '9') {
                if (count > line.size())
                    throw std::invalid_argument("Invalid SAT string length");
                count = count * 10 + std::size_t(line[at++] - '0');
            }
            index = std::min(line.size(), at + 1 + count);
            continue;
        }
        if (line[index] == ' ')
            split = index;
        if (index - start >= 240 && split && *split > start) {
            pieces.push_back(line.substr(start, *split - start));
            start = *split;
            split.reset();
        }
        ++index;
    }
    pieces.push_back(line.substr(start));
    return pieces;
}
std::int32_t history_next(std::int32_t n) {
    if (n == INT32_MAX)
        throw std::overflow_error("History id exhausted");
    return n + 1;
}
struct HistoryWrite {
    std::vector<DxfRawField> fields;
    void operator()(int c, double v) {
        if (!std::isfinite(v))
            throw std::invalid_argument("Nonfinite history parameter");
        std::ostringstream s;
        s.imbue(std::locale::classic());
        s.precision(17);
        s << v;
        fields.push_back({c, s.str()});
    }
    template <class T> void operator()(int c, T v) { fields.push_back({c, std::to_string(v)}); }
    void operator()(int c, Vector3 v) {
        (*this)(c, v.x);
        (*this)(c + 10, v.y);
        (*this)(c + 20, v.z);
    }
    void operator()(int c, const SurfaceMatrix &v) {
        for (std::size_t i = 0; i < 16; ++i)
            (*this)(c, v[(i % 4) * 4 + i / 4]);
    }
    template <class T> void list(int count, int code, std::vector<T> &v) {
        if (v.size() > 100000)
            throw std::length_error("History array limit");
        (*this)(count, std::int32_t(v.size()));
        for (auto n : v)
            (*this)(code, n);
    }
};
struct HistoryRead {
    Fields fields;
    void operator()(int c, double &v) {
        if (!fields.all(c).empty())
            v = fields.d(c);
    }
    template <class T> void operator()(int c, T &v) {
        if (fields.all(c).empty())
            return;
        auto n = fields.i(c);
        if constexpr (std::is_same_v<T, bool>)
            v = n != 0;
        else {
            if (n < std::numeric_limits<T>::min() || n > std::numeric_limits<T>::max())
                throw std::invalid_argument("History parameter range");
            v = T(n);
        }
    }
    void operator()(int c, Vector3 &v) {
        (*this)(c, v.x);
        (*this)(c + 10, v.y);
        (*this)(c + 20, v.z);
    }
    void operator()(int c, SurfaceMatrix &v) {
        auto a = fields.all(c);
        if (a.empty())
            return;
        if (a.size() != 16)
            throw std::invalid_argument("History matrix length");
        for (std::size_t i = 0; i < 16; ++i)
            v[(i % 4) * 4 + i / 4] = real(a[i]);
    }
    template <class T> void list(int count, int code, std::vector<T> &v) {
        auto a = fields.all(code);
        if (a.size() > 100000 || fields.i(count) != std::int64_t(a.size()))
            throw std::invalid_argument("History array count");
        for (auto &s : a) {
            if constexpr (std::is_same_v<T, double>)
                v.push_back(real(s));
            else
                v.push_back(integer(s));
        }
    }
};
template <class A> void history_parameters(A &a, SolidHistoryParameters &params) {
    std::visit(
        [&](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, HistoryBox>) {
                a(40, v.length);
                a(41, v.width);
                a(42, v.height);
            } else if constexpr (std::is_same_v<T, HistorySphere>)
                a(40, v.radius);
            else if constexpr (std::is_same_v<T, HistoryCylinder>) {
                a(40, v.height);
                a(41, v.major_radius);
                a(42, v.minor_radius);
                a(43, v.x_radius);
            } else if constexpr (std::is_same_v<T, HistoryCone>) {
                a(40, v.height);
                a(41, v.base_x_radius);
                a(42, v.base_y_radius);
                a(43, v.top_radius);
            } else if constexpr (std::is_same_v<T, HistoryPyramid>) {
                a(40, v.height);
                a(92, v.sides);
                a(41, v.radius);
                a(42, v.top_radius);
            } else if constexpr (std::is_same_v<T, HistoryTorus>) {
                a(40, v.major_radius);
                a(41, v.minor_radius);
            } else if constexpr (std::is_same_v<T, HistoryBoolean>) {
                a(280, v.operation);
                a(92, v.first_operand);
                a(93, v.second_operand);
            } else if constexpr (std::is_same_v<T, HistoryFillet>) {
                a(92, v.method);
                a.list(93, 94, v.edges);
                a.list(95, 41, v.radii);
                a.list(96, 42, v.start_setbacks);
                a.list(97, 43, v.end_setbacks);
            } else if constexpr (std::is_same_v<T, HistoryChamfer>) {
                a(92, v.method);
                a(41, v.base_distance);
                a(42, v.other_distance);
                a.list(93, 94, v.edges);
                a(95, v.base_face);
            } else if constexpr (std::is_same_v<T, HistorySweep>) {
                a(10, v.direction);
                a(42, v.draft_angle);
                a(43, v.start_draft_distance);
                a(44, v.end_draft_distance);
                a(45, v.twist_angle);
                a(48, v.scale_factor);
                a(49, v.align_angle);
                a(46, v.sweep_entity_transform);
                a(47, v.path_entity_transform);
                a(290, v.has_align_start);
                a(70, v.align_option);
                a(71, v.miter_option);
                a(292, v.align_start);
                a(293, v.bank);
                for (int i = 0; i < 3; ++i)
                    a(294 + i, v.flags_294_296[i]);
                a(11, v.reference_point);
            } else if constexpr (std::is_same_v<T, HistoryLoft>) {
                a(70, v.surface_option);
                a(41, v.start_draft_angle);
                a(42, v.end_draft_angle);
                a(43, v.start_magnitude);
                a(44, v.end_magnitude);
                for (int i = 0; i < 8; ++i)
                    a(290 + i, v.flags[i]);
            } else if constexpr (std::is_same_v<T, HistoryRevolve>) {
                a(10, v.axis_point);
                a(11, v.direction);
                a(40, v.revolve_angle);
                a(41, v.start_angle);
                a(43, v.draft_angle);
                a(44, v.field_44);
                a(45, v.field_45);
                a(46, v.twist_angle);
                a(290, v.flag_290);
                a(291, v.close_to_axis);
            }
        },
        params);
}
HistoryProfiles *history_profiles(SolidHistoryParameters &p) {
    return std::visit(
        [](auto &v) -> HistoryProfiles * {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, HistorySweep> || std::is_same_v<T, HistoryLoft> ||
                          std::is_same_v<T, HistoryRevolve>)
                return &v.profiles;
            return nullptr;
        },
        p);
}
// Profile spans are delimited by native type groups and the first following option.
std::pair<std::size_t, std::size_t> history_profile_span(SolidHistoryKind k,
                                                         const std::vector<DxfRawField> &f) {
    std::size_t begin = f.size(), end = f.size();
    int first = 92, last = k == SolidHistoryKind::Loft ? 70 : (k == SolidHistoryKind::Revolve ? 43 : 42);
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (begin == f.size() && f[i].code == first)
            begin = i;
        if (begin != f.size() && f[i].code == last) {
            end = i;
            break;
        }
    }
    return {begin, end};
}
void validate_profile_fields(const HistoryProfiles &p, SolidHistoryKind k, DxfVersion version) {
    if (p.fields.empty())
        return;
    if (p.source_version != dxf_version_string(version))
        throw std::invalid_argument("Encoded history profiles require their source version");
    for (auto &f : p.fields) {
        bool ok =
            f.code == 310 || f.code == 1 || f.code == 3 || f.code == 70 || (f.code >= 90 && f.code <= 99);
        if (!ok)
            throw std::invalid_argument("Invalid embedded history field");
        (void)detail::binary_value_from_text(f.code, f.value);
    }
    auto span = history_profile_span(k, p.fields);
    if (span.first != 0 || span.second != p.fields.size())
        throw std::invalid_argument("Invalid embedded profile boundaries");
}
template <class A> void loft_settings_fields(A &a, HistoryLoftSettings &v) {
    a(70, v.normals);
    a(40, v.start_draft_angle);
    a(41, v.end_draft_angle);
    a(42, v.start_magnitude);
    a(43, v.end_magnitude);
    a(71, v.start_continuity);
    a(72, v.end_continuity);
    a(44, v.start_bulge);
    a(45, v.end_bulge);
    a(290, v.closed);
    a(291, v.surface);
    a(292, v.align_direction);
    a(293, v.periodic);
}
void validate_loft_settings(const HistoryLoftSettings &v) {
    if (v.normals < 0 || v.normals > 6 || v.start_continuity < 0 || v.start_continuity > 1 ||
        v.end_continuity < 0 || v.end_continuity > 1 || v.start_magnitude < 0 || v.end_magnitude < 0 ||
        v.start_bulge < 0 || v.end_bulge < 0)
        throw std::invalid_argument("Invalid loft settings");
    if (v.section_counts.size() > 100000)
        throw std::length_error("Loft section limit");
    for (auto n : v.section_counts)
        if (n <= 0 || n > 100000)
            throw std::invalid_argument("Invalid loft section count");
    for (auto &f : v.path)
        if (f.code != 92 && f.code != 93 && f.code != 310)
            throw std::invalid_argument("Invalid loft metadata path");
}
std::vector<DxfRawField> history_body(SolidHistoryOperation value, DxfVersion version) {
    if (SolidHistoryOperation(value.base.kind).parameters.index() != value.parameters.index())
        throw std::invalid_argument("History kind/parameter mismatch");
    HistoryWrite out;
    auto scope = [&](const std::string &s) { out.fields.push_back({100, s}); };
    auto &b = value.base;
    if (b.value_code == 91 || std::holds_alternative<std::uint64_t>(b.value))
        throw std::invalid_argument("Unsupported history evaluation handle encoding");
    scope("AcDbEvalExpr");
    out(90, b.eval_node_id);
    out(98, b.eval_major);
    out(99, b.eval_minor);
    auto code = b.value_code;
    if (code == 0 && std::holds_alternative<std::monostate>(b.value))
        code = -9999;
    if (code != -9999) {
        out.fields.push_back({1, ""});
        out(70, code);
        bool valid = false;
        std::visit(
            [&](auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, double>) {
                    valid = code == 40;
                    out(140, v);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    valid = code == 1;
                    out.fields.push_back({1, v});
                } else if constexpr (std::is_same_v<T, std::int32_t>) {
                    valid = code == 90;
                    out(90, v);
                } else if constexpr (std::is_same_v<T, std::int16_t>) {
                    valid = code == 70;
                    out(70, v);
                } else if constexpr (std::is_same_v<T, Vector2>) {
                    valid = code == 10 || code == 11;
                    out(code, v.x);
                    out(code + 10, v.y);
                }
            },
            b.value);
        if (!valid)
            throw std::invalid_argument("History evaluation value/type mismatch");
    } else if (!std::holds_alternative<std::monostate>(b.value))
        throw std::invalid_argument("History value without a type");
    scope("AcDbShHistoryNode");
    out(90, b.major);
    out(91, b.minor);
    for (std::size_t i = 0; i < 16; ++i)
        out(40 + int(i), b.transform[(i % 4) * 4 + i / 4] * (i >= 10 ? 180. / 3.14159265358979323846 : 1.));
    out(62, b.color.index().value_or(256));
    if (auto c = b.color.to_true_color_value())
        out(420, std::int32_t(*c));
    out(92, b.step_id);
    out.fields.push_back({347, hex(b.material)});
    auto k = b.kind;
    if (k != SolidHistoryKind::Boolean && k != SolidHistoryKind::Fillet && k != SolidHistoryKind::Chamfer)
        scope("AcDbShPrimitive");
    scope(history_scope(k));
    out(90, value.operation_major);
    out(91, value.operation_minor);
    auto profiles = history_profiles(value.parameters);
    std::vector<DxfRawField> pf;
    if (profiles) {
        validate_profile_fields(*profiles, k, version);
        pf = profiles->fields;
        if (pf.empty()) {
            pf = {{92, "0"}};
            if (k == SolidHistoryKind::Loft)
                pf = {{92, "0"}, {95, "0"}, {98, "0"}};
            else if (k == SolidHistoryKind::Sweep || k == SolidHistoryKind::Extrusion)
                pf.push_back({93, "0"});
        }
    }
    if (auto loft = std::get_if<HistoryLoft>(&value.parameters); loft && loft->settings) {
        auto &s = *loft->settings;
        validate_loft_settings(s);
        loft->start_draft_angle = s.start_draft_angle;
        loft->end_draft_angle = s.end_draft_angle;
        loft->start_magnitude = s.start_magnitude;
        loft->end_magnitude = s.end_magnitude;
        if (!s.path.empty()) {
            // Metadata always uses R2018 bits; regenerate the native path in the
            // document version (notably spline flags and polyline vertex IDs).
            SolidHistoryOperation path_only(SolidHistoryKind::Loft);
            std::get<HistoryLoft>(path_only.parameters).settings = s;
            auto geometry = read_history_profiles(path_only, version);
            if (geometry.metadata_path && !(std::holds_alternative<ProfileOpaque>(*geometry.metadata_path) &&
                                            version != DxfVersion::AC1032)) {
                set_history_profiles(path_only, geometry, version);
                auto &encoded = std::get<HistoryLoft>(path_only.parameters).profiles.fields;
                auto path =
                    std::find_if(encoded.begin(), encoded.end(), [](const auto &f) { return f.code == 98; });
                auto first = std::find_if(pf.begin(), pf.end(), [](const auto &f) { return f.code == 98; });
                pf.erase(first, pf.end());
                pf.insert(pf.end(), path, encoded.end());
            }
        }
    }
    HistoryWrite scalar;
    history_parameters(scalar, value.parameters);
    int before = k == SolidHistoryKind::Loft ? 70 : (k == SolidHistoryKind::Revolve ? 43 : 42);
    bool inserted = false;
    for (auto &f : scalar.fields) {
        if (profiles && !inserted && f.code == before) {
            out.fields.insert(out.fields.end(), pf.begin(), pf.end());
            inserted = true;
        }
        out.fields.push_back(f);
    }
    if (profiles && !inserted)
        out.fields.insert(out.fields.end(), pf.begin(), pf.end());
    if (auto brep = std::get_if<HistoryBrep>(&value.parameters)) {
        out(70, std::int16_t(1));
        auto sat = history_sat(brep->sat);
        if (!sat.empty() && sat.back() != '\n')
            sat += '\n';
        if (!sat.empty())
            sat += "End-of-ACIS-data\n";
        std::istringstream probe(sat);
        std::string probe_line;
        bool wrap = false;
        while (std::getline(probe, probe_line))
            if (probe_line.size() > 255)
                wrap = true;
        std::istringstream lines(sat);
        std::string line;
        bool any = false;
        while (std::getline(lines, line)) {
            for (auto &piece : (wrap ? history_wrap_sat(line) : std::vector<std::string>{line})) {
                auto encoded = history_cipher(piece);
                for (std::size_t at = 0; at < encoded.size();) {
                    auto length = std::min<std::size_t>(2049, encoded.size() - at);
                    while (at + length < encoded.size() &&
                           (static_cast<unsigned char>(encoded[at + length]) & 0xc0) == 0x80)
                        --length;
                    if (!length)
                        throw std::invalid_argument("Invalid SAT UTF-8");
                    out.fields.push_back({at ? 3 : 1, encoded.substr(at, length)});
                    at += length;
                }
                if (encoded.empty())
                    out.fields.push_back({1, ""});
                any = true;
            }
        }
        if (!any)
            out.fields.push_back({1, ""});
    }
    if (k == SolidHistoryKind::Cone || k == SolidHistoryKind::Sweep || k == SolidHistoryKind::Extrusion)
        scope(history_cpp(k));
    return out.fields;
}
} // namespace
SolidHistoryOperation::SolidHistoryOperation(SolidHistoryKind k) {
    base.kind = k;
    base.major = 1;
    base.eval_major = 1;
    base.transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    switch (k) {
    case SolidHistoryKind::Box:
    case SolidHistoryKind::Wedge:
        parameters = HistoryBox{};
        break;
    case SolidHistoryKind::Sphere:
        parameters = HistorySphere{};
        break;
    case SolidHistoryKind::Cylinder:
        parameters = HistoryCylinder{};
        break;
    case SolidHistoryKind::Cone:
        parameters = HistoryCone{};
        break;
    case SolidHistoryKind::Pyramid:
        parameters = HistoryPyramid{};
        break;
    case SolidHistoryKind::Torus:
        parameters = HistoryTorus{};
        break;
    case SolidHistoryKind::Boolean:
        parameters = HistoryBoolean{};
        break;
    case SolidHistoryKind::Brep:
        parameters = HistoryBrep{};
        break;
    case SolidHistoryKind::Fillet:
        parameters = HistoryFillet{};
        break;
    case SolidHistoryKind::Chamfer:
        parameters = HistoryChamfer{};
        break;
    case SolidHistoryKind::Sweep:
    case SolidHistoryKind::Extrusion:
        parameters = HistorySweep{};
        break;
    case SolidHistoryKind::Loft:
        parameters = HistoryLoft{};
        break;
    case SolidHistoryKind::Revolve:
        parameters = HistoryRevolve{};
        break;
    default:
        throw std::invalid_argument("Unknown history kind");
    }
}
std::optional<SolidHistoryOperation> DxfDocument::solid_history_definition(std::uint64_t id) const {
    auto base = solid_history_node(id);
    if (!base)
        return {};
    SolidHistoryOperation result(base->kind);
    result.base = *base;
    auto &r = *SolidHistoryReader(*this).record(id);
    auto scope = history_scope(base->kind);
    if (base->kind == SolidHistoryKind::Cone && !SolidHistoryReader::fields(r, "AcDbShCone").all(40).empty())
        scope = "AcDbShCone";
    auto f = SolidHistoryReader::fields(r, scope);
    auto versions = f.all(90);
    result.operation_major = versions.empty() ? 0 : integer(versions.front());
    result.operation_minor = f.i(91);
    if (auto profiles = history_profiles(result.parameters)) {
        auto [begin, end] = history_profile_span(base->kind, f.values);
        profiles->fields.assign(f.values.begin() + begin, f.values.begin() + end);
        profiles->source_version = dxf_version_string(version_);
        f.values.erase(f.values.begin() + begin, f.values.begin() + end);
    }
    HistoryRead read{f};
    history_parameters(read, result.parameters);
    if (auto brep = std::get_if<HistoryBrep>(&result.parameters)) {
        auto mf = SolidHistoryReader::fields(r, "AcDbModelerGeometry");
        if (!mf.values.empty())
            f = mf;
        std::string text;
        for (auto &x : f.values)
            if (x.code == 1 || x.code == 3) {
                if (x.code == 1 && !text.empty())
                    text += '\n';
                text += x.value;
            }
        if (f.i(70) == 1)
            text = history_cipher(text);
        brep->sat = history_sat(text);
    }
    if (auto loft = std::get_if<HistoryLoft>(&result.parameters)) {
        auto dict = extension_dictionary_handle(hex(id));
        if (!dict.empty()) {
            auto xr =
                SolidHistoryReader(*this).record(handle(dictionary_entry(dict, "CADCODEC_LOFT_OPTIONS_V1")));
            if (xr && xr->type() == "XRECORD") {
                auto f = SolidHistoryReader::fields(*xr, "AcDbXrecord");
                if (f.i(90) == 1) {
                    HistoryLoftSettings s;
                    HistoryRead a{f};
                    loft_settings_fields(a, s);
                    for (auto &x : f.values) {
                        if (x.code == 91)
                            s.section_counts.push_back(integer(x.value));
                        if (x.code == 92 || x.code == 93 || x.code == 310)
                            s.path.push_back(x);
                    }
                    validate_loft_settings(s);
                    loft->settings = std::move(s);
                }
            }
        }
    }
    return result;
}
void DxfDocument::ensure_history_class(std::string_view name, std::string_view cpp) {
    for (const auto &c : classes())
        if (c.name == name) {
            if (c.is_entity || c.cpp_name != cpp)
                throw std::invalid_argument("Conflicting history CLASS");
            return;
        }
    ClassDefinition c;
    c.name = name;
    c.cpp_name = cpp;
    set_class(c);
}
void DxfDocument::write_history_metadata(std::uint64_t owner, std::string_view key,
                                         const std::vector<DxfRawField> *values) {
    auto dictionary = extension_dictionary_handle(hex(owner));
    if (dictionary.empty()) {
        if (!values)
            return;
        dictionary = hex(add_extension_dictionary(hex(owner)));
    }
    auto entry = dictionary_entry(dictionary, key);
    if (!values && entry.empty())
        return;
    auto dict = SolidHistoryReader(*this).record(handle(dictionary));
    if (!dict || dict->type() != "DICTIONARY" || handle(dict->owner_handle()) != owner)
        throw std::invalid_argument("History metadata dictionary must be exclusively owned");
    // A shared extension dictionary must never be rewritten through another owner.
    for (const auto &[id, locations] : handle_index_)
        if (id != owner)
            for (const auto &loc : locations) {
                auto &r = record_at(loc);
                for (auto [i, editable] : reference_positions(r))
                    if (r.pairs_[i].code == 360 && handle(r.pairs_[i].value) == handle(dictionary))
                        throw std::invalid_argument("Shared history metadata dictionary");
            }
    if (!values) {
        if (entry.empty())
            return;
        auto &d = SolidHistoryReader::mutable_record(*this, handle(dictionary));
        auto fields = d.subclass_fields("AcDbDictionary");
        for (std::size_t i = 0; i + 1 < fields.size(); ++i)
            if (fields[i].code == 3 && fields[i].value == key &&
                (fields[i + 1].code == 350 || fields[i + 1].code == 360)) {
                fields.erase(fields.begin() + i, fields.begin() + i + 2);
                break;
            }
        d.set_subclass_fields("AcDbDictionary", fields);
        erase_history_records({handle(entry)});
        return;
    }
    if (entry.empty()) {
        std::vector<SymbolProperty> f;
        for (auto &x : *values)
            f.push_back({x.code, x.value});
        add_xrecord(dictionary, std::string(key), f);
    } else {
        auto &r = SolidHistoryReader::mutable_record(*this, handle(entry));
        if (r.type() != "XRECORD" || handle(r.owner_handle()) != handle(dictionary))
            throw std::invalid_argument("Foreign history metadata record");
        auto f = *values;
        f.insert(f.begin(), {280, "0"});
        r.set_subclass_fields("AcDbXrecord", f);
    }
}
void DxfDocument::write_history_operation(std::uint64_t id, const SolidHistoryOperation &value) {
    if (auto loft = std::get_if<HistoryLoft>(&value.parameters);
        loft && loft->settings && !loft->settings->path.empty() && version_ != DxfVersion::AC1032) {
        SolidHistoryOperation probe(SolidHistoryKind::Loft);
        std::get<HistoryLoft>(probe.parameters).settings = loft->settings;
        auto path = read_history_profiles(probe, version_).metadata_path;
        if (path && std::holds_alternative<ProfileOpaque>(*path)) {
            auto old = solid_history_definition(id);
            auto previous = old ? std::get_if<HistoryLoft>(&old->parameters) : nullptr;
            auto equal = [](const auto &a, const auto &b) {
                return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                    [](const auto &x, const auto &y) { return x.code == y.code && x.value == y.value; });
            };
            if (!previous || !previous->settings || !equal(previous->settings->path, loft->settings->path))
                throw std::invalid_argument("Cannot transcode an opaque loft metadata path");
        }
    }
    auto body = history_body(value, version_); // Validate before changing records or classes.
    if (value.base.material) {
        auto r = SolidHistoryReader(*this).record(value.base.material);
        if (!r || r->type() != "MATERIAL")
            throw std::invalid_argument("Invalid history material");
    }
    ensure_history_class(history_dxf(value.base.kind), history_cpp(value.base.kind));
    auto &r = SolidHistoryReader::mutable_record(*this, id);
    auto begin = std::find_if(r.pairs_.begin(), r.pairs_.end(), [](const auto &p) { return p.code == 100; });
    auto end =
        std::find_if(begin, r.pairs_.end(), [](const auto &p) { return p.code == 1001 || p.code == 101; });
    if (std::any_of(begin, end, [](const auto &p) { return p.code == 102; }))
        throw std::invalid_argument("History operation contains private subclass data");
    std::vector<DxfRecord::Pair> encoded;
    for (auto &f : body)
        encoded.push_back(
            {f.code, detail::is_string_value(f.code)
                         ? r.encode_string(f.value)
                         : (r.binary_ ? detail::binary_value_from_text(f.code, f.value) : f.value)});
    auto offset = std::size_t(begin - r.pairs_.begin());
    r.pairs_.erase(begin, end);
    r.pairs_.insert(r.pairs_.begin() + offset, encoded.begin(), encoded.end());
    retired_registered_classes_.insert(r.type());
    r.pairs_.front().value = history_dxf(value.base.kind);
    std::optional<std::vector<DxfRawField>> metadata;
    if (auto loft = std::get_if<HistoryLoft>(&value.parameters); loft && loft->settings) {
        auto settings = *loft->settings;
        validate_loft_settings(settings);
        HistoryWrite out;
        out(90, 1);
        loft_settings_fields(out, settings);
        for (auto n : settings.section_counts)
            out(91, n);
        out.fields.insert(out.fields.end(), settings.path.begin(), settings.path.end());
        metadata = std::move(out.fields);
    }
    if (std::holds_alternative<HistoryLoft>(value.parameters))
        write_history_metadata(id, "CADCODEC_LOFT_OPTIONS_V1", metadata ? &*metadata : nullptr);
    rebuild_indexes();
}
void DxfDocument::erase_history_records(const std::unordered_set<std::uint64_t> &doomed) {
    for (const auto &v : records_)
        std::visit(
            [&](const DxfRecord &r) {
                if (doomed.count(handle(r.handle())))
                    retired_registered_classes_.insert(r.type());
            },
            v);
    // Refuse external non-reactor references; remove only complete reactor entries.
    auto clean = [&](DxfRecord &r) {
        if (doomed.count(handle(r.handle())))
            return;
        std::set<std::size_t> erased;
        unsigned depth = 0;
        bool opaque = false;
        for (std::size_t i = 0; i < r.pairs_.size(); ++i) {
            const auto &p = r.pairs_[i];
            if (!depth && (p.code == 1001 || p.code == 101 ||
                           (r.type() == "XRECORD" && p.code == 100 && p.value == "AcDbXrecord")))
                opaque = true;
            if (p.code != 102)
                continue;
            if (!opaque && !depth && p.value == "{ACAD_REACTORS") {
                auto end = i + 1;
                while (end < r.pairs_.size() && r.pairs_[end].code == 330)
                    ++end;
                if (end < r.pairs_.size() && r.pairs_[end].code == 102 && r.pairs_[end].value == "}") {
                    std::size_t removed = 0;
                    for (auto j = i + 1; j < end; ++j)
                        if (doomed.count(handle(r.pairs_[j].value))) {
                            erased.insert(j);
                            ++removed;
                        }
                    if (removed && removed == end - i - 1) {
                        erased.insert(i);
                        erased.insert(end);
                    }
                    i = end;
                    continue;
                }
            }
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (depth && p.value == "}")
                --depth;
        }
        for (auto [i, editable] : reference_positions(r))
            if (doomed.count(handle(r.pairs_[i].value)) && !erased.count(i))
                throw std::invalid_argument("History is referenced outside its owned graph");
        for (auto i = erased.rbegin(); i != erased.rend(); ++i)
            r.pairs_.erase(r.pairs_.begin() + *i);
    };
    for (auto &v : records_)
        std::visit(clean, v);
    for (const auto &[id, locations] : handle_index_)
        for (const auto &loc : locations)
            if (loc.vertex || loc.seqend)
                clean(const_cast<DxfRecord &>(record_at(loc)));
    records_.erase(
        std::remove_if(records_.begin(), records_.end(),
                       [&](const auto &v) {
                           return std::visit(
                               [&](const DxfRecord &r) { return doomed.count(handle(r.handle())) != 0; }, v);
                       }),
        records_.end());
    rebuild_indexes();
}
SolidHistoryGraph DxfDocument::create_solid_history(std::uint64_t entity, SolidHistoryOperation operation) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("History authoring requires R2007+");
    auto found = handle_index_.find(entity);
    if (found == handle_index_.end() || found->second.size() != 1 ||
        !std::holds_alternative<DxfModelerEntity>(records_[found->second[0].record]))
        throw std::invalid_argument("History requires a modeler entity");
    auto entity_type = record_at(found->second[0]).type();
    if (entity_type == "REGION" || entity_type == "BODY")
        throw std::invalid_argument("Upstream DXF does not persist REGION/BODY history links");
    validate_history_mutation(entity);
    auto copy = *this;
    if (auto old = copy.solid_history_graph(entity)) {
        std::unordered_set<std::uint64_t> doomed;
        SolidHistoryReader read(copy);
        for (auto r : read.objects())
            if (read.reaches(handle(r->handle()), old->root))
                doomed.insert(handle(r->handle()));
        auto &e = SolidHistoryReader::mutable_record(copy, entity);
        if (!SolidHistoryReader::fields(e, "AcDb3dSolid").values.empty())
            e.set_field("AcDb3dSolid", 350, "0");
        auto dict = copy.extension_dictionary_handle(hex(entity));
        if (!dict.empty()) {
            auto xr = handle(copy.dictionary_entry(dict, "CADCODEC_SURFACE_HISTORY_V1"));
            if (xr)
                SolidHistoryReader::set(copy, xr, "AcDbXrecord", 330, "0");
        }
        copy.erase_history_records(doomed);
    }
    if (operation.base.step_id <= 0)
        operation.base.step_id = 1;
    operation.base.eval_node_id = 1;
    operation.base.parent_id = -1;
    copy.ensure_history_class("ACSH_HISTORY_CLASS", "AcDbShHistory");
    copy.ensure_history_class("ACAD_EVALUATION_GRAPH", "AcDbEvalGraph");
    copy.ensure_history_class(history_dxf(operation.base.kind), history_cpp(operation.base.kind));
    auto r = copy.make_record("ACSH_HISTORY_CLASS", "AcDbShHistory");
    r.set_field("", 330, hex(entity));
    auto root = copy.insert_object(r);
    r = copy.make_record("ACAD_EVALUATION_GRAPH", "AcDbEvalGraph");
    r.set_field("", 330, hex(root));
    auto evaluation = copy.insert_object(r);
    r = copy.make_record(history_dxf(operation.base.kind));
    r.set_field("", 330, hex(evaluation));
    auto node = copy.insert_object(r);
    copy.write_history_operation(node, operation);
    BlockEvaluationGraph eg;
    eg.first_node_id = eg.first_node_id_copy = 1;
    BlockEvaluationNode n;
    n.id = 0;
    n.edge_flags = 32;
    n.next_id = 1;
    n.expression = node;
    n.node_data = {-1, -1, -1, -1};
    eg.nodes.push_back(n);
    SolidHistoryReader::replace_graph(copy, evaluation, eg);
    bool record_history = false;
    if (copy.header_index_) {
        const auto &header = copy.record_at({*copy.header_index_, {}, false});
        bool active = false;
        for (const auto &p : header.pairs_) {
            if (p.code == 9)
                active = p.value == "$SOLIDHIST";
            else if (active && p.code == 280)
                record_history =
                    integer(copy.binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value) != 0;
        }
    }
    SolidHistoryReader::mutable_record(copy, root)
        .set_subclass_fields("AcDbShHistory", {{90, "1"},
                                               {91, "0"},
                                               {360, hex(evaluation)},
                                               {92, "1"},
                                               {280, "0"},
                                               {281, record_history ? "1" : "0"}});
    auto &e = SolidHistoryReader::mutable_record(copy, entity);
    auto type = e.type();
    if (type == "3DSOLID" || type == "REGION" || type == "BODY")
        e.set_field("AcDb3dSolid", 350, hex(root));
    else {
        if (!SolidHistoryReader::fields(e, "AcDb3dSolid").values.empty())
            e.set_field("AcDb3dSolid", 350, "0");
        std::vector<DxfRawField> f{{90, "1"}, {330, hex(root)}};
        copy.write_history_metadata(entity, "CADCODEC_SURFACE_HISTORY_V1", &f);
    }
    copy.rebuild_indexes();
    auto result = copy.solid_history_graph(entity);
    if (!result)
        throw std::invalid_argument("History attachment failed");
    *this = std::move(copy);
    return *result;
}
SolidHistoryGraph DxfDocument::ensure_history_evaluation_graph(std::uint64_t entity) {
    auto original = solid_history_graph(entity);
    if (!original)
        throw std::invalid_argument("Missing history graph");
    auto graph = *original;
    auto chain = solid_history_operations(entity);
    if (!chain)
        throw std::invalid_argument("Missing usable history chain");
    if (!graph.evaluation_graph) {
        if (chain->size() != graph.nodes.size())
            throw std::invalid_argument("Graphless history has missing parent information");
        ensure_history_class("ACAD_EVALUATION_GRAPH", "AcDbEvalGraph");
        auto r = make_record("ACAD_EVALUATION_GRAPH", "AcDbEvalGraph");
        r.set_field("", 330, hex(graph.root));
        auto id = insert_object(r);
        BlockEvaluationGraph eg;
        eg.first_node_id = eg.first_node_id_copy = std::int32_t(chain->size());
        for (std::size_t i = 0; i < chain->size(); ++i) {
            auto node = (*chain)[i];
            auto value = *solid_history_definition(node.handle);
            value.base.eval_node_id = std::int32_t(i) + 1;
            value.base.parent_id = -1;
            write_history_operation(node.handle, value);
            SolidHistoryReader::set(*this, node.handle, "", 330, hex(id));
            BlockEvaluationNode n;
            n.id = std::int32_t(i);
            n.next_id = n.id + 1;
            n.edge_flags = 32;
            n.expression = node.handle;
            n.node_data = {i ? int(i) - 1 : -1, i ? int(i) - 1 : -1, i + 1 < chain->size() ? int(i) : -1,
                           i + 1 < chain->size() ? int(i) : -1};
            eg.nodes.push_back(n);
            if (i) {
                BlockEvaluationEdge e;
                e.id = int(i) - 1;
                e.source_node = int(i) - 1;
                e.destination_node = int(i);
                e.incoming_edge = 1;
                e.outgoing_edges = {-1, -1, -1, -1, -1};
                eg.edges.push_back(e);
            }
        }
        SolidHistoryReader::replace_graph(*this, id, eg);
        auto &root = SolidHistoryReader::mutable_record(*this, graph.root);
        root.set_field("AcDbShHistory", 360, hex(id));
        root.set_field("AcDbShHistory", 92,
                       root.binary_ ? detail::binary_value_from_text(92, std::to_string(chain->size()))
                                    : std::to_string(chain->size()));
        graph = *solid_history_graph(entity);
    }
    return graph;
}
SolidHistoryGraph DxfDocument::append_solid_history(std::uint64_t entity, SolidHistoryOperation operation) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("History authoring requires R2007+");
    validate_history_mutation(entity);
    auto copy = *this;
    auto graph = copy.ensure_history_evaluation_graph(entity);
    auto active = SolidHistoryReader(copy).active(graph);
    if (!active)
        throw std::invalid_argument("Ambiguous active history operation");
    auto eg = *copy.evaluation_graph(*graph.evaluation_graph);
    auto parent = std::find_if(eg.nodes.begin(), eg.nodes.end(),
                               [&](const auto &n) { return n.expression == active->handle; });
    if (parent == eg.nodes.end() || parent->node_data[2] >= 0)
        throw std::invalid_argument("Append requires the latest history result");
    std::int32_t maxnode = 0, maxstep = 0;
    for (auto id : graph.nodes) {
        auto b = *copy.solid_history_node(id);
        maxnode = std::max(maxnode, b.node_id());
        maxstep = std::max({maxstep, b.step_id, b.eval_node_id});
    }
    operation.base.eval_node_id = history_next(maxnode);
    operation.base.step_id = history_next(maxstep);
    operation.base.parent_id = -1;
    std::int32_t maxid = -1, maxedge = -1;
    for (auto &n : eg.nodes)
        maxid = std::max(maxid, n.id);
    for (auto &e : eg.edges)
        maxedge = std::max(maxedge, e.id);
    auto id = history_next(maxid), edge = history_next(maxedge), next = history_next(id);
    auto source = parent->id;
    parent->node_data[2] = parent->node_data[3] = edge;
    copy.ensure_history_class(history_dxf(operation.base.kind), history_cpp(operation.base.kind));
    auto record = copy.make_record(history_dxf(operation.base.kind));
    record.set_field("", 330, hex(*graph.evaluation_graph));
    auto node = copy.insert_object(record);
    copy.write_history_operation(node, operation);
    BlockEvaluationNode n;
    n.id = id;
    n.edge_flags = 32;
    n.next_id = next;
    n.expression = node;
    n.node_data = {edge, edge, -1, -1};
    eg.nodes.push_back(n);
    BlockEvaluationEdge e;
    e.id = edge;
    e.next_id = 0;
    e.incoming_edge = 1;
    e.source_node = source;
    e.destination_node = id;
    e.outgoing_edges = {-1, -1, -1, -1, -1};
    eg.edges.push_back(e);
    eg.first_node_id = eg.first_node_id_copy = next;
    SolidHistoryReader::replace_graph(copy, *graph.evaluation_graph, eg);
    auto &root = SolidHistoryReader::mutable_record(copy, graph.root);
    root.set_field("AcDbShHistory", 92,
                   root.binary_
                       ? detail::binary_value_from_text(92, std::to_string(operation.base.eval_node_id))
                       : std::to_string(operation.base.eval_node_id));
    auto result = *copy.solid_history_graph(entity);
    if (!copy.solid_history_tree(entity))
        throw std::invalid_argument("Invalid appended history");
    *this = std::move(copy);
    return result;
}
SolidHistoryOperation DxfDocument::update_solid_history(std::uint64_t entity,
                                                        SolidHistoryOperation operation) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("History authoring requires R2007+");
    validate_history_mutation(entity);
    auto current = solid_history_operation(entity);
    auto graph = solid_history_graph(entity);
    if (!current || !graph)
        throw std::invalid_argument("Missing history operation");
    auto old = *solid_history_definition(current->handle);
    if (operation.base.step_id <= 0)
        operation.base.step_id = old.base.step_id;
    if (operation.base.eval_node_id <= 0)
        operation.base.eval_node_id = operation.base.step_id;
    auto copy = *this;
    copy.write_history_operation(current->handle, operation);
    auto &root = SolidHistoryReader::mutable_record(copy, graph->root);
    root.set_field("AcDbShHistory", 92,
                   root.binary_ ? detail::binary_value_from_text(92, std::to_string(operation.base.step_id))
                                : std::to_string(operation.base.step_id));
    *this = std::move(copy);
    return old;
}
SolidHistoryOperation DxfDocument::update_solid_history_step(std::uint64_t entity,
                                                             SolidHistoryOperation operation) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("History authoring requires R2007+");
    validate_history_mutation(entity);
    auto tree = solid_history_tree(entity);
    auto graph = solid_history_graph(entity);
    auto id = operation.base.node_id();
    if (!tree || !graph || id <= 0)
        throw std::invalid_argument("Missing active history step");
    bool present = false;
    std::function<void(const SolidHistoryTree &)> find = [&](const auto &t) {
        if (t.operation.node_id() == id)
            present = true;
        for (auto &c : t.operands)
            find(c);
    };
    find(*tree);
    if (!present)
        throw std::invalid_argument("History step is outside the active tree");
    std::optional<SolidHistoryNode> selected;
    for (auto h : graph->nodes) {
        auto n = *solid_history_node(h);
        if (n.node_id() == id) {
            if (selected)
                throw std::invalid_argument("Ambiguous history step");
            selected = n;
        }
    }
    if (!selected)
        throw std::invalid_argument("Missing history step");
    auto old = *solid_history_definition(selected->handle);
    operation.base.step_id = old.base.step_id;
    operation.base.eval_node_id = old.base.eval_node_id;
    operation.base.parent_id = old.base.parent_id;
    auto copy = *this;
    copy.write_history_operation(selected->handle, operation);
    *this = std::move(copy);
    return old;
}
