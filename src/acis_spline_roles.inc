// SPDX-License-Identifier: MPL-2.0
// acadrust SabWriter::encode_spline_numeric_roles and its bounded field scans.
namespace {
using RoleTokens = std::vector<SatToken>;
std::string_view role_name(const RoleTokens &t, std::size_t i) {
    return i < t.size() ? t[i].as_ident().value_or("") : "";
}
bool role_logical(const RoleTokens &t, std::size_t i) {
    return i < t.size() && (std::holds_alternative<bool>(t[i].value) || role_name(t, i) == "I" ||
                            role_name(t, i) == "F" || role_name(t, i) == "T");
}
std::optional<std::size_t> role_count(const RoleTokens &t, std::size_t i) {
    if (i >= t.size())
        return {};
    auto n = t[i].as_integer();
    if (!n || *n < 0 || std::uint64_t(*n) > std::numeric_limits<std::size_t>::max())
        return {};
    return std::size_t(*n);
}
std::vector<std::size_t> discontinuities(const RoleTokens &t, std::size_t start, std::size_t groups) {
    std::vector<std::size_t> out;
    auto pos = start;
    for (std::size_t g = 0; g < groups; ++g) {
        auto n = role_count(t, pos);
        if (!n || pos >= t.size() || *n > t.size() - pos - 1)
            return {};
        for (std::size_t i = 0; i < *n; ++i) {
            if (!t[pos + 1 + i].as_float())
                return {};
            out.push_back(pos + 1 + i);
        }
        pos += 1 + *n;
    }
    return out;
}
std::optional<std::vector<std::size_t>> nurbs_fields(const RoleTokens &t, std::size_t start,
                                                     std::size_t dimensions, bool surface, bool fit) {
    bool rational = role_name(t, start) == "nurbs";
    auto du = role_count(t, start + 1);
    if (!du)
        return {};
    std::vector<std::size_t> degrees{*du}, counts;
    std::size_t pos;
    if (surface) {
        auto dv = role_count(t, start + 2), nu = role_count(t, start + 7 + rational),
             nv = role_count(t, start + 8 + rational);
        if (!dv || !nu || !nv)
            return {};
        degrees.push_back(*dv);
        counts = {*nu, *nv};
        pos = start + 9 + rational;
    } else {
        auto n = role_count(t, start + 3);
        if (!n)
            return {};
        counts = {*n};
        pos = start + 4;
    }
    std::vector<std::size_t> doubles;
    std::size_t controls = 1;
    for (std::size_t axis = 0; axis < degrees.size(); ++axis) {
        auto count = counts[axis];
        if (pos > t.size() || count < 2 || count > (t.size() - pos) / 2)
            return {};
        std::size_t total = 0;
        for (std::size_t i = 0; i < count; ++i) {
            auto mult = role_count(t, pos + 1);
            if (!t[pos].as_float() || !mult || *mult > std::numeric_limits<std::size_t>::max() - total)
                return {};
            total += *mult;
            doubles.push_back(pos);
            pos += 2;
        }
        if (total == std::numeric_limits<std::size_t>::max() || total + 1 < degrees[axis])
            return {};
        auto n = total + 1 - degrees[axis];
        if (n && controls > t.size() / n)
            return {};
        controls *= n;
    }
    auto stride = dimensions + rational;
    if (controls > (t.size() - pos) / stride)
        return {};
    auto size = controls * stride + fit;
    if (size > t.size() - pos)
        return {};
    for (std::size_t i = 0; i < size; ++i) {
        if (!t[pos + i].as_float())
            return {};
        doubles.push_back(pos + i);
    }
    return doubles;
}
RoleTokens spline_numeric_roles(std::string_view type, const RoleTokens &t) {
    auto out = t;
    std::vector<std::size_t> doubles, enums;
    std::vector<std::string_view> subtypes;
    std::vector<std::optional<std::size_t>> ends;
    auto in_curve = [&] { return !subtypes.empty() && suffix(subtypes.back(), "cur"); };
    auto append = [&](const auto &v) { doubles.insert(doubles.end(), v.begin(), v.end()); };
    for (std::size_t i = 0; i < t.size(); ++i) {
        auto name = role_name(t, i);
        if (name == "{") {
            subtypes.push_back(role_name(t, i + 1));
            ends.push_back({});
        } else if (name == "}") {
            if (in_curve() && !ends.empty() && ends.back() && role_logical(t, *ends.back()) &&
                role_logical(t, *ends.back() + 1))
                append(discontinuities(t, *ends.back() + 2, 3));
            if (!subtypes.empty())
                subtypes.pop_back();
            if (!ends.empty())
                ends.pop_back();
        } else if (name == "nullbs" && in_curve()) {
            if (!ends.empty())
                ends.back() = i + 1;
        }
        if (name == "nubs" || name == "nurbs") {
            auto previous = i ? role_name(t, i - 1) : "", parent = i > 1 ? role_name(t, i - 2) : "";
            if (in_curve())
                parent = "exactcur";
            else if (!subtypes.empty() && subtypes.back() == "offsur")
                parent = "exactsur";
            bool pc = in_curve() && previous != "full";
            std::optional<std::pair<std::size_t, bool>> dims;
            if (parent == "exactcur" && previous == "full")
                dims = {{3, false}};
            else if (parent == "exactsur" && previous == "full")
                dims = {{3, true}};
            else if (previous == "exppc" || pc)
                dims = {{2, false}};
            if (dims)
                if (auto fields = nurbs_fields(t, i, dims->first, dims->second, !pc)) {
                    std::optional<std::size_t> end;
                    if (!fields->empty())
                        end = *std::max_element(fields->begin(), fields->end()) + 1;
                    if (pc) {
                        if (!ends.empty())
                            ends.back() = end;
                    } else if (dims->second && end)
                        append(discontinuities(t, *end, 6));
                    append(*fields);
                    if (previous == "full")
                        enums.push_back(i - 1);
                    if (dims->second) {
                        auto closure = i + 3 + (name == "nurbs");
                        for (auto j = closure; j < closure + 4 && j < t.size(); ++j)
                            enums.push_back(j);
                    } else if (i + 2 < t.size())
                        enums.push_back(i + 2);
                }
        }
        bool logical_false =
            name == "F" || (std::get_if<bool>(&t[i].value) && !*std::get_if<bool>(&t[i].value));
        auto sub = subtypes.empty() ? "" : subtypes.back();
        if (logical_false && i + 1 < t.size() &&
            (subtypes.empty() || sub == "exactcur" || sub == "exact_int_cur" || sub == "exactsur" ||
             sub == "offsur" || sub == "exppc" || in_curve()))
            doubles.push_back(i + 1);
        if (name == "UNEXTENDED" && !subtypes.empty())
            enums.push_back(i);
        bool analytic = name == "plane" || name == "cone" || name == "sphere" || name == "torus";
        if (type == "pcurve" && sub == "exppc" && analytic) {
            for (auto j = i + 1; j < t.size() && role_name(t, j) != "}"; ++j)
                doubles.push_back(j);
        }
        if (type == "intcurve-curve" && in_curve() && analytic) {
            for (auto j = i + 1; j < t.size(); ++j) {
                auto n = role_name(t, j);
                if (n == "plane" || n == "cone" || n == "sphere" || n == "torus" || n == "spline" ||
                    n == "nullbs" || n == "nubs" || n == "nurbs" || n == "{" || n == "}")
                    break;
                doubles.push_back(j);
            }
        }
    }
    if (type == "pcurve" && t.size() >= 2) {
        doubles.push_back(t.size() - 2);
        doubles.push_back(t.size() - 1);
    }
    for (auto i : doubles)
        if (auto v = std::get_if<std::int64_t>(&out[i].value))
            out[i] = SatToken(double(*v));
    for (auto i : enums)
        if (auto v = std::get_if<SatIdent>(&out[i].value)) {
            auto n = v->value;
            if (n == "full" || n == "open" || n == "closed" || n == "periodic" || n == "none" ||
                n == "UNEXTENDED")
                out[i] = SatToken(SatEnum{n});
        }
    return out;
}
} // namespace
