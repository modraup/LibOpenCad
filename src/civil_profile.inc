// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/classes/civil/create/profile.rs (layout profiles of
// fixed tangents with free parabolas, circular curves and asymmetric
// parabolas) and the write-path subset of profile::pieces.
constexpr const char *PROFILE_TAIL_BITS =
    "10001111111100001111000000000000000010100001000000010100000001101010100000000110001010100001011111000000100";
constexpr const char *PROFILE_HELPERS[] = {
    "AECC_DB_FLOAT_TANGENT_GRADELEN", "AECC_DB_FIXED_PARABOLA_HLPT", "AECC_DB_FIXED_PARABOLA_HLPTPATSPEAK",
    "AECC_ENTITYCIRCLEINFOS",         "AECC_ENTITYEXTENDPARAM",      "AECC_DB_FIXED_ARC_HLPT",
    "AECC_NOTIFICATIONINFOS"};
enum class ProfileKeyKind { Text, Helper, Byte, Stamp };
struct ProfileKey {
    std::uint32_t key;
    ProfileKeyKind kind;
    const char *text;
    std::uint8_t byte;
};
constexpr ProfileKey PROFILE_KEYS[] = {
    {0x0400FE0B, ProfileKeyKind::Text, "A", 0},
    {0x1200FF73, ProfileKeyKind::Helper, "", 0},
    {0x0400FE08, ProfileKeyKind::Text, "HP", 0},
    {0x1200FF70, ProfileKeyKind::Helper, "", 0},
    {0x0400FE16, ProfileKeyKind::Text, "Overall LP", 0},
    {0x0400FE05, ProfileKeyKind::Text, "EVC", 0},
    {0x0400FE02, ProfileKeyKind::Text, "BVC", 0},
    {0x0300FB05, ProfileKeyKind::Byte, "", 0},
    {0x0400FE0A, ProfileKeyKind::Text, "K", 0},
    {0x1200FF72, ProfileKeyKind::Helper, "", 0},
    {0x0B001081, ProfileKeyKind::Stamp, "", 0},
    {0x0400FE07, ProfileKeyKind::Text, "EVCE", 0},
    {0x0400FE15, ProfileKeyKind::Text, "Overall HP", 0},
    {0x0400FE04, ProfileKeyKind::Text, "BVCE", 0},
    {0x0400FE01, ProfileKeyKind::Text, "BREAK", 0},
    {0x0300FF00, ProfileKeyKind::Byte, "", 1},
    {0x1200FF58, ProfileKeyKind::Helper, "", 0},
    {0x1200FF52, ProfileKeyKind::Helper, "", 0},
    {0x0400FE09, ProfileKeyKind::Text, "LP", 0},
    {0x1200FF71, ProfileKeyKind::Helper, "", 0},
    {0x0400FE06, ProfileKeyKind::Text, "EVCS", 0},
    {0x0400FE03, ProfileKeyKind::Text, "BVCS", 0},
    {0x0400FE00, ProfileKeyKind::Text, "PVI", 0},
    {0x1200FF57, ProfileKeyKind::Helper, "", 0},
};
struct ProfilePart {
    std::int32_t id = 0, previous = 0, next = 0;
    VerticalCurve curve{};
    Vector2 start{}, end{};
    bool crest = false;
    bool tangent = true;
};
std::optional<std::pair<double, double>> vertical_curve_reach(const VerticalCurve &c, double g1, double g2) {
    auto positive = [](double v) -> std::optional<double> {
        if (std::isfinite(v) && v > 0.0)
            return v;
        return std::nullopt;
    };
    if (c.kind == VerticalCurve::Kind::None)
        return std::pair<double, double>{0, 0};
    if (c.kind == VerticalCurve::Kind::Parabola) {
        auto l = positive(c.length);
        if (!l)
            return std::nullopt;
        return std::pair<double, double>{*l / 2.0, *l / 2.0};
    }
    if (c.kind == VerticalCurve::Kind::Asymmetric) {
        auto a = positive(c.before), b = positive(c.after);
        if (!a || !b)
            return std::nullopt;
        return std::pair<double, double>{*a, *b};
    }
    auto r = positive(c.radius);
    if (!r)
        return std::nullopt;
    auto a1 = std::atan(g1), a2 = std::atan(g2);
    auto t = *r * std::tan(std::abs(a2 - a1) / 2.0);
    return std::pair<double, double>{t * std::cos(a1), t * std::cos(a2)};
}
std::vector<ProfilePart> profile_build_parts(const std::vector<Vector2> &pvis, const std::vector<VerticalCurve> &curves) {
    auto n = pvis.size() - 1;
    auto next = static_cast<std::int32_t>(n);
    std::vector<std::optional<std::int32_t>> ids(curves.size());
    for (std::size_t j = 0; j < curves.size(); ++j)
        if (curves[j].kind != VerticalCurve::Kind::None)
            ids[j] = ++next;
    std::vector<ProfilePart> out;
    for (std::size_t i = 0; i < n; ++i) {
        auto id = static_cast<std::int32_t>(i + 1);
        std::int32_t previous = 0, nxt = 0;
        if (i != 0)
            previous = ids[i - 1].value_or(id - 1);
        if (i + 1 != n)
            nxt = ids[i].value_or(id + 1);
        ProfilePart p;
        p.id = id;
        p.previous = previous;
        p.next = nxt;
        p.start = pvis[i];
        p.end = pvis[i + 1];
        out.push_back(p);
    }
    auto grade = [&](std::size_t i) { return (pvis[i + 1].y - pvis[i].y) / (pvis[i + 1].x - pvis[i].x); };
    for (std::size_t j = 0; j < curves.size(); ++j) {
        if (!ids[j])
            continue;
        ProfilePart p;
        p.id = *ids[j];
        p.previous = static_cast<std::int32_t>(j + 1);
        p.next = static_cast<std::int32_t>(j + 2);
        p.tangent = false;
        p.curve = curves[j];
        p.crest = grade(j + 1) < grade(j);
        out.push_back(p);
    }
    return out;
}
std::string profile_bits(unsigned value, int width) {
    std::string s(static_cast<std::size_t>(width), '0');
    for (int i = 0; i < width; ++i)
        if (value & (1u << (width - 1 - i)))
            s[static_cast<std::size_t>(i)] = '1';
    return s;
}
std::string encode_profile_entity(int type, std::uint64_t id, std::uint64_t layer, std::string_view name,
                                  const std::vector<ProfilePart> &parts, const std::array<std::uint64_t, 7> &helpers,
                                  std::uint64_t style, std::uint64_t alignment, const std::array<std::uint8_t, 16> &guid,
                                  const char *page) {
    ArchWriter w;
    w.v = 1032;
    w.page = page;
    auto split = arch_entity_common(w, type, id, layer, {});
    for (auto v : {80, 0, 80, 80, 0, 0x10025F04})
        w.main.bit_long(v);
    write_bit_run(w.main, "010101");
    write_byte(w.main, static_cast<std::uint8_t>(std::size(PROFILE_KEYS)));
    for (auto &key : PROFILE_KEYS) {
        w.main.bit_long(static_cast<std::int32_t>(key.key));
        if (key.kind == ProfileKeyKind::Byte)
            write_byte(w.main, key.byte);
        else if (key.kind == ProfileKeyKind::Stamp)
            for (auto v : {2, 0, 0})
                w.main.bit_long(v);
    }
    bl_byte(w.main, 0);
    write_bit_run(w.main, std::string(63, '0'));
    w.bd(2);
    write_bit_run(w.main, "011011010");
    w.main.bit_long(0);
    w.bd(0);
    w.bd(0);
    w.bd(30);
    for (int i = 0; i < 5; ++i)
        w.bs(0);
    write_bit_run(w.main, "0000001000");
    w.main.bit_long(static_cast<std::int32_t>(parts.size()));
    std::size_t parabolas = 0;
    for (auto &p : parts) {
        int prefix = 2, constraint = 0;
        if (!p.tangent) {
            if (p.curve.kind == VerticalCurve::Kind::Circular) {
                prefix = 4;
                constraint = 9;
            } else if (p.curve.kind == VerticalCurve::Kind::Asymmetric) {
                prefix = 8;
                constraint = 10;
            } else {
                prefix = 6;
                ++parabolas;
            }
        }
        write_bit_run(w.main, profile_bits(static_cast<unsigned>(prefix), 11) + profile_bits(0x08u << 13, 23));
        for (auto v : {constraint, p.id, p.previous, p.next})
            w.main.bit_long(v);
        if (p.tangent) {
            for (auto v : {p.start.x, p.start.y, p.end.x, p.end.y})
                raw_double(w.main, v);
            write_bit_run(w.main, "0000");
        } else if (p.curve.kind == VerticalCurve::Kind::Parabola) {
            w.main.bit_long(1);
            w.bd(p.curve.length);
            w.main.bit_long(p.crest ? 0x1001 : 0x1000);
            w.main.bit_long(0);
            write_bit_run(w.main, "010");
            for (int i = 0; i < 3; ++i)
                w.bd(-1);
        } else if (p.curve.kind == VerticalCurve::Kind::Circular)
            w.bd(p.curve.radius);
        else if (p.curve.kind == VerticalCurve::Kind::Asymmetric) {
            w.bd(p.curve.before);
            w.bd(p.curve.after);
        }
    }
    write_bit_run(w.main, "10");
    for (auto b : guid)
        write_byte(w.main, b);
    write_bit_run(w.main, PROFILE_TAIL_BITS);
    for (auto h : helpers)
        w.href(HARD_OWNER, h);
    w.href(SOFT_POINTER, 0);
    w.href(HARD_POINTER, 0);
    w.href(HARD_POINTER, 0);
    w.href(SOFT_POINTER, 0);
    w.href(HARD_POINTER, style);
    w.href(HARD_POINTER, alignment);
    w.href(SOFT_POINTER, 0);
    w.href(HARD_POINTER, 0);
    w.href(SOFT_POINTER, 0);
    w.href(SOFT_POINTER, 0);
    w.str("AeccImpVAlignment");
    w.str("");
    w.str("");
    for (auto &key : PROFILE_KEYS)
        if (key.kind == ProfileKeyKind::Text)
            w.str(key.text);
    w.str(name);
    for (std::size_t i = 0; i < 4 + parabolas; ++i)
        w.str("");
    return finish_arch(w, split);
}
std::array<std::uint8_t, 16> civil_profile_guid(std::uint64_t handle) {
    std::array<std::uint8_t, 16> id{};
    auto a = handle ^ 0x9E3779B97F4A7C15ull;
    auto b = 116444736000000000ull ^ (handle * 0x100000001B3ull);
    for (int i = 0; i < 8; ++i) {
        id[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(a >> (8 * i));
        id[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>(b >> (8 * i));
    }
    id[7] = static_cast<std::uint8_t>((id[7] & 0x0F) | 0x40);
    id[8] = static_cast<std::uint8_t>((id[8] & 0x3F) | 0x80);
    return id;
}
bool skip_profile_head(DwgBitReader &main) {
    for (auto want : {80, 0, 80, 80, 0})
        if (main.bit_long() != want)
            return false;
    main.bit_long();
    if (main.bits(6) != 0b010101)
        return false;
    auto keys = main.bits(8);
    for (unsigned i = 0; i < keys; ++i)
        if (!skip_civil_key(main))
            return false;
    if (main.bit_long() != 0)
        return false;
    for (int i = 0; i < 63; ++i)
        main.bits(1);
    main.bit_double();
    main.bits(9);
    main.bit_long();
    main.bit_double();
    main.bit_double();
    main.bit_double();
    for (int i = 0; i < 5; ++i)
        main.bit_short();
    main.bits(10);
    return true;
}
std::optional<std::vector<ProfilePart>> read_written_profile_parts(DwgBitReader &main) {
    auto n = main.bit_long();
    if (n < 1 || n > 1000000)
        return std::nullopt;
    std::vector<ProfilePart> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        auto prefix = static_cast<int>(main.bits(11));
        main.bits(23);
        auto constraint = main.bit_long();
        ProfilePart p;
        p.id = main.bit_long();
        p.previous = main.bit_long();
        p.next = main.bit_long();
        if (prefix == 2 && constraint == 0) {
            p.start = {main.raw_double(), main.raw_double()};
            p.end = {main.raw_double(), main.raw_double()};
            main.bits(4);
        } else if (prefix == 6 && constraint == 0) {
            p.tangent = false;
            main.bit_long();
            p.curve = VerticalCurve::parabola(main.bit_double());
            p.crest = main.bit_long() == 0x1001;
            main.bit_long();
            main.bits(3);
            for (int k = 0; k < 3; ++k)
                main.bit_double();
        } else if (prefix == 4 && constraint == 9) {
            p.tangent = false;
            p.curve = VerticalCurve::circular(main.bit_double());
        } else if (prefix == 8 && constraint == 10) {
            p.tangent = false;
            auto before = main.bit_double(), after = main.bit_double();
            p.curve = VerticalCurve::asymmetric(before, after);
        } else
            return std::nullopt;
        out.push_back(p);
    }
    return out;
}
std::optional<std::vector<ProfilePart>> parse_profile_parts(const ArchEntityData &entity) {
    if (entity.dxf_name != "AECC_PROFILE" || !entity.raw_dwg_data || entity.dwg_source_version != DxfVersion::AC1032)
        return std::nullopt;
    try {
        auto rec = unframed_arch_record(*entity.raw_dwg_data, entity.dwg_handle_bits, 1032, entity.handle);
        auto s = open_dynamic(*entity.raw_dwg_data, rec, entity.handle, 1032, nullptr);
        read_entity_properties(s.main, s.refs, entity.handle, s.end, 1032, rec, nullptr, nullptr, nullptr, nullptr);
        if (!skip_profile_head(s.main))
            return std::nullopt;
        return read_written_profile_parts(s.main);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::vector<std::pair<ProfilePiece, std::vector<Vector2>>> profile_solved_pieces(const std::vector<ProfilePart> &parts) {
    std::map<std::int32_t, const ProfilePart *> by_id;
    for (auto &p : parts)
        by_id[p.id] = &p;
    const ProfilePart *start = nullptr;
    for (auto &p : parts)
        if (p.previous == 0)
            start = &p;
    if (!start)
        return {};
    std::vector<const ProfilePart *> run;
    auto *cur = start;
    while (cur) {
        run.push_back(cur);
        cur = cur->next ? by_id[cur->next] : nullptr;
    }
    std::vector<Vector2> pvis;
    std::vector<VerticalCurve> curves;
    for (auto *p : run)
        if (p->tangent) {
            if (pvis.empty())
                pvis.push_back(p->start);
            pvis.push_back(p->end);
        }
    if (pvis.size() < 2)
        return {};
    curves.assign(pvis.size() - 2, VerticalCurve::none());
    std::size_t inner = 0;
    for (auto *p : run)
        if (p->tangent)
            ++inner;
        else if (inner > 0 && inner - 1 < curves.size())
            curves[inner - 1] = p->curve;
    auto grade = [&](std::size_t i) { return (pvis[i + 1].y - pvis[i].y) / (pvis[i + 1].x - pvis[i].x); };
    std::vector<std::pair<double, double>> reach;
    for (std::size_t j = 0; j < curves.size(); ++j) {
        auto r = vertical_curve_reach(curves[j], grade(j), grade(j + 1));
        if (!r)
            return {};
        reach.push_back(*r);
    }
    auto lerp = [](Vector2 a, Vector2 b, double x) {
        auto t = (x - a.x) / (b.x - a.x);
        return Vector2{x, a.y + t * (b.y - a.y)};
    };
    std::vector<std::pair<ProfilePiece, std::vector<Vector2>>> out;
    for (std::size_t i = 0; i + 1 < pvis.size(); ++i) {
        auto before = i == 0 ? 0.0 : reach[i - 1].second;
        auto after = i < reach.size() ? reach[i].first : 0.0;
        auto a = lerp(pvis[i], pvis[i + 1], pvis[i].x + before);
        auto b = lerp(pvis[i], pvis[i + 1], pvis[i + 1].x - after);
        if (b.x > a.x + 1e-9)
            out.push_back({ProfilePiece::Tangent, {a, b}});
        if (i < curves.size() && curves[i].kind != VerticalCurve::Kind::None) {
            auto c0 = lerp(pvis[i], pvis[i + 1], pvis[i + 1].x - reach[i].first);
            auto c1 = lerp(pvis[i + 1], pvis[i + 2], pvis[i + 1].x + reach[i].second);
            ProfilePiece kind = ProfilePiece::Parabola;
            if (curves[i].kind == VerticalCurve::Kind::Circular)
                kind = ProfilePiece::Circular;
            else if (curves[i].kind == VerticalCurve::Kind::Asymmetric)
                kind = ProfilePiece::AsymmetricParabola;
            if (c1.x > c0.x + 1e-9)
                out.push_back({kind, {c0, c1}});
        }
    }
    return out;
}
std::optional<std::string> parse_profile_name(const ArchEntityData &entity) {
    if (entity.dxf_name != "AECC_PROFILE")
        return std::nullopt;
    auto strings = entity.decode().strings;
    for (std::size_t i = 0; i + 1 < strings.size(); ++i)
        if (strings[i] == "PVI")
            return strings[i + 1];
    return std::nullopt;
}
std::vector<std::pair<ProfilePiece, std::vector<Vector2>>> parse_profile_pieces(const ArchEntityData &entity) {
    auto parts = parse_profile_parts(entity);
    if (!parts)
        return {};
    return profile_solved_pieces(*parts);
}
