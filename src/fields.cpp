// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/fields.rs evaluation (DIESEL, AcVar, _text, AcSm, AcCount, AcExpr, AcObjProp).
#include <libopencad/fields.hpp>
#include <libopencad/count.hpp>
#include <libopencad/dxf_document.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace libopencad {
namespace {

constexpr const char *kNoValue = "----";
constexpr const char *kNotASheet = "####";

std::uint64_t handle_value(std::string_view s) {
    std::uint64_t v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9')
            v += static_cast<unsigned>(c - '0');
        else if (c >= 'A' && c <= 'F')
            v += static_cast<unsigned>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f')
            v += static_cast<unsigned>(c - 'a' + 10);
    }
    return v;
}

bool is_object_type(std::string_view type) {
    return type == "FIELD" || type == "FIELDLIST" || type == "DICTIONARY" || type == "XRECORD" ||
           type == "DICTIONARYVAR" || type == "MATERIAL" || type == "LAYOUT" || type == "PLOTSETTINGS" ||
           type == "SCALE" || type == "GROUP" || type == "MLINESTYLE" || type == "MLEADERSTYLE" ||
           type == "TABLESTYLE" || type == "VISUALSTYLE" || type == "IMAGEDEF";
}

std::optional<std::uint64_t> owner_of(const DxfDocument &doc, std::uint64_t h) {
    std::ostringstream o;
    o << std::hex << h;
    auto *rec = doc.get_record(o.str());
    if (!rec)
        return std::nullopt;
    auto owner = rec->owner_handle();
    if (owner.empty())
        return std::nullopt;
    return handle_value(owner);
}

std::optional<std::uint64_t> host_of(const DxfDocument &doc, std::uint64_t container) {
    auto h = container;
    for (int i = 0; i < 12; ++i) {
        auto o = owner_of(doc, h);
        if (!o)
            return std::nullopt;
        auto next = owner_of(doc, *o);
        if (!next)
            return o;
        std::ostringstream os;
        os << std::hex << *o;
        auto *rec = doc.get_record(os.str());
        if (!rec || !is_object_type(rec->type()))
            return o;
        h = *o;
    }
    return std::nullopt;
}

struct FieldView {
    std::uint64_t handle = 0;
    std::uint64_t owner = 0;
    FieldDefinition def;
};

std::vector<FieldView> all_fields(const DxfDocument &doc) {
    std::vector<FieldView> out;
    doc.for_each<DxfField>([&](const DxfField &f) {
        FieldView v;
        v.handle = handle_value(f.handle());
        v.owner = handle_value(f.owner_handle());
        v.def = f.definition();
        out.push_back(std::move(v));
    });
    return out;
}

const FieldView *container_for_host(const std::vector<FieldView> &fields, const DxfDocument &doc, std::uint64_t host) {
    for (const auto &f : fields)
        if (f.def.evaluator_id == "_text" && host_of(doc, f.handle) == host)
            return &f;
    return nullptr;
}

std::string mtext_escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string nonempty_or_none(std::string_view s) {
    return s.empty() ? std::string(kNoValue) : std::string(s);
}

std::vector<std::string> split_top_commas(std::string_view s) {
    std::vector<std::string> parts;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(') {
            ++depth;
            cur.push_back(c);
        } else if (c == ')') {
            --depth;
            cur.push_back(c);
        } else if (c == ',' && depth == 0) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(cur);
    return parts;
}

std::string bool_str(bool b) { return b ? "1" : "0"; }

std::string num_str(double x) {
    if (std::isfinite(x) && x == std::trunc(x) && std::abs(x) < 1e15)
        return std::to_string(static_cast<long long>(x));
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << x;
    return o.str();
}

std::string diesel_eval(const DxfDocument &doc, std::string_view s, const FieldContext &ctx);

std::optional<std::string> getvar_engine(const DxfDocument &, std::string_view name, const FieldContext &ctx) {
    std::string key(name);
    auto t = key.find_first_not_of(" \t");
    if (t != std::string::npos)
        key.erase(0, t);
    if (!key.empty() && key[0] == '*')
        key.erase(key.begin());
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (key == "cdate") {
        auto p = julian_parts(ctx.now_julian());
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04lld%02lld%02lld.%02lld%02lld%02lld",
                      static_cast<long long>(p[0]), static_cast<long long>(p[1]), static_cast<long long>(p[2]),
                      static_cast<long long>(p[3]), static_cast<long long>(p[4]), static_cast<long long>(p[5]));
        return std::string(buf);
    }
    if (key == "date") {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o.setf(std::ios::fixed);
        o.precision(6);
        o << ctx.now_julian() + 0.5;
        return o.str();
    }
    if (key == "loginname")
        return ctx.login();
    return ctx.getvar(name);
}

std::string diesel_macro(const DxfDocument &doc, std::string_view inner, const FieldContext &ctx) {
    auto parts = split_top_commas(inner);
    if (parts.empty())
        return {};
    std::string func = parts[0];
    std::transform(func.begin(), func.end(), func.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto raw = [&](std::size_t i) -> std::string {
        if (i + 1 >= parts.size())
            return {};
        auto a = parts[i + 1];
        auto t = a.find_first_not_of(" \t");
        if (t != std::string::npos)
            a.erase(0, t);
        if (!a.empty() && a.front() == '"' && a.back() == '"')
            a = a.substr(1, a.size() - 2);
        return a;
    };
    auto ev = [&](std::size_t i) { return diesel_eval(doc, raw(i), ctx); };
    if (func == "if") {
        auto cond = ev(0);
        bool take = false;
        try {
            take = std::stod(cond) != 0.0;
        } catch (...) {
            auto t = cond;
            while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
                t.erase(t.begin());
            take = !t.empty();
        }
        if (take)
            return ev(1);
        if (parts.size() > 3)
            return ev(2);
        return {};
    }
    if (func == "nth") {
        auto w = static_cast<std::size_t>(std::stoul(ev(0)));
        if (w + 2 >= parts.size())
            return {};
        return ev(1 + w);
    }
    std::vector<std::string> args;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        args.push_back(ev(i));
    auto a = [&](std::size_t i) { return i < args.size() ? args[i] : std::string{}; };
    auto n = [&](std::size_t i) {
        try {
            return std::stod(a(i));
        } catch (...) {
            return 0.0;
        }
    };
    auto ni = [&](std::size_t i) {
        try {
            return static_cast<long long>(std::stoll(a(i)));
        } catch (...) {
            return 0LL;
        }
    };
    if (func == "getvar")
        return getvar_engine(doc, a(0), ctx).value_or(std::string{});
    if (func == "getenv")
        return ctx.getenv(a(0)).value_or(std::string{});
    if (func == "eval")
        return diesel_eval(doc, a(0), ctx);
    if (func == "substr") {
        std::string s = a(0);
        auto s0 = args.size() > 1 ? static_cast<std::size_t>(std::max(0LL, ni(1) - 1)) : 0;
        auto end = args.size() > 2 ? std::min(s.size(), s0 + static_cast<std::size_t>(ni(2))) : s.size();
        if (s0 >= s.size())
            return {};
        return s.substr(s0, end - s0);
    }
    if (func == "strlen")
        return std::to_string(a(0).size());
    if (func == "upper") {
        auto s = a(0);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return s;
    }
    if (func == "eq")
        return bool_str(a(0) == a(1));
    if (func == "=")
        return bool_str(n(0) == n(1));
    if (func == "!=")
        return bool_str(n(0) != n(1));
    if (func == "<")
        return bool_str(n(0) < n(1));
    if (func == ">")
        return bool_str(n(0) > n(1));
    if (func == "<=")
        return bool_str(n(0) <= n(1));
    if (func == ">=")
        return bool_str(n(0) >= n(1));
    if (func == "+") {
        double s = 0;
        for (std::size_t i = 0; i < args.size(); ++i)
            s += n(i);
        return num_str(s);
    }
    if (func == "*") {
        double s = 1;
        for (std::size_t i = 0; i < args.size(); ++i)
            s *= n(i);
        return num_str(s);
    }
    if (func == "-") {
        double s = n(0);
        for (std::size_t i = 1; i < args.size(); ++i)
            s -= n(i);
        return num_str(s);
    }
    if (func == "/") {
        double s = n(0);
        for (std::size_t i = 1; i < args.size(); ++i)
            if (n(i) != 0.0)
                s /= n(i);
        return num_str(s);
    }
    if (func == "fix")
        return std::to_string(static_cast<long long>(std::trunc(n(0))));
    if (func == "rtos") {
        auto prec = args.size() > 2 ? static_cast<int>(ni(2)) : 4;
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o.setf(std::ios::fixed);
        o.precision(prec);
        o << n(0);
        return o.str();
    }
    if (func == "time")
        return std::to_string(static_cast<long long>(std::llround((ctx.now_julian() - 2440587.5) * 86400.0)));
    std::string joined;
    for (const auto &arg : args)
        joined += arg;
    return joined;
}

std::string diesel_eval(const DxfDocument &doc, std::string_view s, const FieldContext &ctx) {
    std::string out;
    std::string text(s);
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '(') {
            int depth = 0;
            std::size_t j = i + 1;
            for (; j < text.size(); ++j) {
                if (text[j] == '(')
                    ++depth;
                else if (text[j] == ')') {
                    --depth;
                    if (depth == 0)
                        break;
                }
            }
            auto inner = text.substr(i + 2, j - (i + 2));
            out += diesel_macro(doc, inner, ctx);
            i = j + 1;
        } else {
            out.push_back(text[i]);
            ++i;
        }
    }
    return out;
}

std::string text_case(std::string s, std::string_view pic) {
    auto at = pic.find("%tc");
    int mode = 0;
    if (at != std::string_view::npos) {
        auto rest = pic.substr(at + 3);
        std::string digits;
        for (char c : rest) {
            if (std::isdigit(static_cast<unsigned char>(c)))
                digits.push_back(c);
            else
                break;
        }
        if (!digits.empty())
            mode = std::stoi(digits);
    }
    if (mode == 1) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    } else if (mode == 2) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    } else if (mode == 3) {
        if (!s.empty())
            s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    } else if (mode == 4) {
        bool start = true;
        for (char &c : s) {
            if (start)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            start = std::isspace(static_cast<unsigned char>(c)) != 0;
        }
    }
    return s;
}

std::optional<std::string> eval_acvar(const DxfDocument &doc, std::string_view code, const FieldContext &ctx) {
    std::string body(code);
    auto t = body.find_first_not_of(" \t");
    if (t != std::string::npos)
        body.erase(0, t);
    if (!body.empty() && body[0] == '\\')
        body.erase(body.begin());
    auto ws = body.find_first_of(" \t");
    if (ws == std::string::npos)
        body.clear();
    else {
        body.erase(0, ws);
        t = body.find_first_not_of(" \t");
        if (t != std::string::npos)
            body.erase(0, t);
        else
            body.clear();
    }
    std::string name = body, fmt;
    auto fp = body.find("\\f ");
    if (fp != std::string::npos) {
        name = body.substr(0, fp);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.pop_back();
        fmt = body.substr(fp + 3);
        t = fmt.find_first_not_of(" \t\"");
        if (t != std::string::npos)
            fmt.erase(0, t);
        if (!fmt.empty() && fmt.back() == '"')
            fmt.pop_back();
    }
    auto date_fmt = fmt.empty() ? std::string("%x") : fmt;
    auto date = [&](double jd) { return format_dt_in(julian_parts(jd), date_fmt, ctx.date_locale()); };
    std::optional<std::string> value;
    if (name == "Login")
        value = ctx.login();
    else if (name == "Date")
        value = date(ctx.now_julian());
    else if (name == "PlotDate")
        value = ctx.plotting() ? std::optional<std::string>{date(ctx.now_julian())} : std::nullopt;
    else if (name == "Filename" || name == "FileName") {
        auto path = ctx.getvar("dwgname");
        value = path ? nonempty_or_none(*path) : std::string(kNoValue);
    } else if (name == "Author" || name == "Title" || name == "Subject" || name == "Keywords" || name == "Comments")
        value = ctx.getvar(name).value_or(std::string(kNoValue));
    else
        value = getvar_engine(doc, name, ctx);
    if (!value)
        return std::nullopt;
    return text_case(*value, fmt);
}

std::optional<std::string> eval_acsm(const DxfDocument &doc, std::string_view code, const FieldContext &ctx) {
    auto parsed = parse_sheet_field_code(code);
    if (!parsed)
        return std::string(kNotASheet);
    auto component = std::get<0>(*parsed);
    auto property = std::get<1>(*parsed);
    auto fmt = std::get<2>(*parsed);
    if (!component.empty() && component[0] == '?')
        return placeholder_type_name(component.substr(1), property);
    if (auto nav = parse_sheet_navigation(component)) {
        auto file = std::get<0>(*nav);
        auto set = std::get<1>(*nav);
        auto comp = std::get<2>(*nav);
        auto key = sheet_path_key(file);
        auto value = ctx.sheet_sets([&](const SheetSetDatabase &db) -> std::optional<std::string> {
            if (!db.path || sheet_path_key(*db.path) != key)
                return std::nullopt;
            return db.navigation_value(set, comp, property);
        });
        if (!value) {
            try {
                auto db = SheetSetDatabase::read(file);
                value = db.navigation_value(set, comp, property);
            } catch (...) {
            }
        }
        if (!value)
            return std::string(kNotASheet);
        if (value->empty())
            return std::string("----");
        return text_case(*value, fmt);
    }
    (void)doc;
    return std::string(kNotASheet);
}

std::optional<std::string> eval_field(const DxfDocument &doc, const std::vector<FieldView> &fields,
                                      const FieldView &field, const FieldContext &ctx, std::uint64_t host);

#include "fields_eval.inc"

std::optional<std::string> eval_template(const DxfDocument &doc, const std::vector<FieldView> &fields,
                                         const FieldView &container, const FieldContext &ctx, std::uint64_t host) {
    std::vector<const FieldView *> children;
    for (const auto &f : fields)
        if (f.owner == container.handle)
            children.push_back(&f);
    std::sort(children.begin(), children.end(),
              [](const FieldView *a, const FieldView *b) { return a->handle < b->handle; });
    bool mtext = false;
    std::ostringstream o;
    o << std::hex << host;
    if (auto *rec = doc.get_record(o.str()))
        mtext = rec->type() == "MTEXT";
    std::string out;
    std::string rest = container.def.code;
    for (;;) {
        auto p = rest.find("%<");
        if (p == std::string::npos) {
            out += rest;
            break;
        }
        out += rest.substr(0, p);
        auto after = rest.substr(p);
        auto end = after.find(">%");
        if (end == std::string::npos) {
            out += after;
            return out;
        }
        auto marker = after.substr(2, end - 2);
        if (marker.find("_FldIdx") == std::string::npos)
            return std::nullopt;
        auto sp = marker.find_last_of(' ');
        if (sp == std::string::npos)
            return std::nullopt;
        auto idx = static_cast<std::size_t>(std::stoul(marker.substr(sp + 1)));
        if (idx >= children.size())
            return std::nullopt;
        auto value = eval_field(doc, fields, *children[idx], ctx, host);
        if (!value)
            return std::nullopt;
        out += mtext ? mtext_escape(*value) : *value;
        rest = after.substr(end + 2);
    }
    return out;
}

std::optional<std::string> fill_template(std::string_view template_code, const std::vector<std::string> &shown,
                                         bool mtext) {
    std::string out;
    std::string rest(template_code);
    for (;;) {
        auto p = rest.find("%<\\_FldIdx ");
        if (p == std::string::npos) {
            out += rest;
            return out;
        }
        out += rest.substr(0, p);
        auto after = rest.substr(p + 11);
        auto end = after.find(">%");
        if (end == std::string::npos)
            return std::nullopt;
        auto token = after.substr(0, end);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
            token.erase(token.begin());
        std::size_t idx = 0;
        try {
            idx = static_cast<std::size_t>(std::stoul(token));
        } catch (...) {
            return std::nullopt;
        }
        if (idx >= shown.size())
            return std::nullopt;
        out += mtext ? mtext_escape(shown[idx]) : shown[idx];
        rest = after.substr(end + 2);
    }
}

std::string cached_field_value(const FieldDefinition &def) {
    if (!def.value.formatted.empty())
        return def.value.formatted;
    if (!def.value.text.empty())
        return def.value.text;
    return def.value_string;
}

std::optional<std::string> eval_field(const DxfDocument &doc, const std::vector<FieldView> &fields,
                                      const FieldView &field, const FieldContext &ctx, std::uint64_t host) {
    const auto &e = field.def.evaluator_id;
    if (e == "_text")
        return eval_template(doc, fields, field, ctx, host);
    if (e.rfind("AcVar", 0) == 0)
        return eval_acvar(doc, field.def.code, ctx);
    if (e == "AcDiesel") {
        auto expr = field.def.code;
        const char *prefix = "\\AcDiesel ";
        if (expr.rfind(prefix, 0) == 0)
            expr.erase(0, std::strlen(prefix));
        auto t = expr.find_first_not_of(" \t");
        if (t != std::string::npos)
            expr.erase(0, t);
        return diesel_eval(doc, expr, ctx);
    }
    if (e.rfind("AcSm", 0) == 0)
        return eval_acsm(doc, field.def.code, ctx);
    if (e == "AcCount" || e == "AcCount2")
        return evaluate_count(doc, field.def.code);
    if (e == "AcExpr")
        return eval_acexpr(doc, fields, field, ctx, host);
    if (e.rfind("AcObjProp", 0) == 0)
        return eval_acobjprop(doc, field, host);
    return std::nullopt;
}

} // namespace

DateLocale DateLocale::make_default() {
    DateLocale loc;
    const char *months[] = {"January", "February", "March",     "April",   "May",      "June",
                            "July",    "August",   "September", "October", "November", "December"};
    const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    for (int i = 0; i < 12; ++i) {
        loc.months[i] = months[i];
        loc.months_abbr[i] = loc.months[i].substr(0, 3);
    }
    for (int i = 0; i < 7; ++i) {
        loc.days[i] = days[i];
        loc.days_abbr[i] = loc.days[i].substr(0, 3);
    }
    return loc;
}

std::array<std::int64_t, 6> julian_parts(double jd) {
    if (jd <= 0.0)
        return {0, 1, 1, 0, 0, 0};
    auto secs = static_cast<long long>(std::llround((jd - 2440587.5) * 86400.0));
    auto days = secs / 86400;
    auto rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    auto h = rem / 3600, mi = (rem % 3600) / 60, s = rem % 60;
    auto z = days + 719468;
    auto era = (z >= 0 ? z : z - 146096) / 146097;
    auto doe = z - era * 146097;
    auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    auto y = yoe + era * 400;
    auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    auto mp = (5 * doy + 2) / 153;
    auto d = doy - (153 * mp + 2) / 5 + 1;
    auto m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2)
        ++y;
    return {y, m, d, h, mi, s};
}

std::string format_dt(const std::array<std::int64_t, 6> &dt, std::string_view fmt) {
    return format_dt_in(dt, fmt, DateLocale::make_default());
}

std::string format_dt_in(const std::array<std::int64_t, 6> &dt, std::string_view fmt, const DateLocale &loc) {
    std::string picture(fmt);
    if (fmt == "%x")
        picture = loc.short_date;
    else if (fmt == "%#x")
        picture = loc.long_date;
    else if (fmt == "%X" || fmt == "%#X")
        picture = loc.long_time;
    else if (fmt == "%c")
        picture = loc.short_date + " " + loc.long_time;
    else if (fmt == "%#c")
        picture = loc.long_date + " " + loc.long_time;
    auto y = dt[0], mo = dt[1], d = dt[2], h = dt[3], mi = dt[4], s = dt[5];
    auto h12 = h % 12 == 0 ? 12 : h % 12;
    auto yy = mo <= 2 ? y - 1 : y;
    auto mm = mo <= 2 ? mo + 12 : mo;
    auto k = yy % 100;
    auto j = yy / 100;
    auto zeller = (d + (13 * (mm + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    auto wd = static_cast<std::size_t>((zeller + 6) % 7);
    auto month = static_cast<std::size_t>(std::clamp(mo, static_cast<std::int64_t>(1), static_cast<std::int64_t>(12)) - 1);
    std::string out;
    for (std::size_t i = 0; i < picture.size();) {
        char c = picture[i];
        if (c == '\'' || c == '"') {
            auto end = picture.find(c, i + 1);
            if (end == std::string::npos)
                end = picture.size();
            out += picture.substr(i + 1, end - i - 1);
            i = end + 1;
            continue;
        }
        if (c == '\\' && i + 1 < picture.size()) {
            out.push_back(picture[i + 1]);
            i += 2;
            continue;
        }
        std::size_t n = 1;
        while (i + n < picture.size() && picture[i + n] == c)
            ++n;
        auto ampm = h < 12 ? loc.am : loc.pm;
        auto pad2 = [](std::int64_t v) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02lld", static_cast<long long>(v));
            return std::string(buf);
        };
        if (c == 'd') {
            if (n == 1)
                out += std::to_string(d);
            else if (n == 2)
                out += pad2(d);
            else if (n == 3)
                out += loc.days_abbr[wd];
            else
                out += loc.days[wd];
        } else if (c == 'M') {
            if (n == 1)
                out += std::to_string(mo);
            else if (n == 2)
                out += pad2(mo);
            else if (n == 3)
                out += loc.months_abbr[month];
            else
                out += loc.months[month];
        } else if (c == 'y') {
            if (n == 1)
                out += std::to_string(std::llabs(y % 100));
            else if (n == 2)
                out += pad2(std::llabs(y % 100));
            else {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%0*lld", static_cast<int>(n), static_cast<long long>(y));
                out += buf;
            }
        } else if (c == 'h')
            out += n == 1 ? std::to_string(h12) : pad2(h12);
        else if (c == 'H')
            out += n == 1 ? std::to_string(h) : pad2(h);
        else if (c == 'm')
            out += n == 1 ? std::to_string(mi) : pad2(mi);
        else if (c == 's')
            out += n == 1 ? std::to_string(s) : pad2(s);
        else if (c == 't')
            out += n == 1 && !ampm.empty() ? ampm.substr(0, 1) : ampm;
        else
            out += picture.substr(i, n);
        i += n;
    }
    return out;
}

std::optional<std::string> resolve_field(const DxfDocument &doc, std::uint64_t host, const FieldContext &ctx) {
    auto fields = all_fields(doc);
    if (fields.empty())
        return std::nullopt;
    auto *container = container_for_host(fields, doc, host);
    if (!container)
        return std::nullopt;
    return eval_field(doc, fields, *container, ctx, host);
}

std::optional<std::string> resolve_field_handle(const DxfDocument &doc, std::uint64_t field_handle, std::uint64_t host,
                                                const FieldContext &ctx) {
    auto fields = all_fields(doc);
    for (const auto &f : fields)
        if (f.handle == field_handle)
            return eval_field(doc, fields, f, ctx, host);
    return std::nullopt;
}

std::optional<std::string> cell_field_text(const DxfDocument &doc, std::uint64_t field, std::uint64_t table,
                                           const FieldContext &ctx) {
    auto fields = all_fields(doc);
    const FieldView *def = nullptr;
    for (const auto &f : fields)
        if (f.handle == field)
            def = &f;
    if (!def)
        return std::nullopt;
    if (def->def.evaluator_id != "_text") {
        if (def->def.evaluator_id == "AcExpr")
            return resolve_field_handle(doc, field, table, ctx);
        return cached_field_value(def->def);
    }
    bool formula = false;
    for (const auto &f : fields)
        if (f.owner == def->handle && f.def.evaluator_id == "AcExpr")
            formula = true;
    if (formula)
        return resolve_field_handle(doc, field, table, ctx);
    std::vector<const FieldView *> children;
    for (const auto &f : fields)
        if (f.owner == def->handle)
            children.push_back(&f);
    std::sort(children.begin(), children.end(),
              [](const FieldView *a, const FieldView *b) { return a->handle < b->handle; });
    std::vector<std::string> shown;
    for (const auto *child : children)
        shown.push_back(cached_field_value(child->def));
    return fill_template(def->def.code, shown, false);
}

std::string format_with_picture(const DxfDocument &doc, double value, std::string_view picture) {
    return format_number(doc, value, picture);
}

std::string format_point_with_picture(const DxfDocument &doc, Point3 point, std::string_view picture) {
    return format_point(doc, point, picture);
}

namespace {
std::string_view trim_ws(std::string_view s) {
    auto a = s.find_first_not_of(" \t");
    if (a == std::string_view::npos)
        return {};
    auto b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

std::string renumber_field_objects(std::string_view code, const std::vector<std::uint64_t> &from,
                                   std::vector<std::uint64_t> &into) {
    const std::string key = "%<\\_ObjIdx ";
    std::string out;
    std::string rest(code);
    for (;;) {
        auto at = rest.find(key);
        if (at == std::string::npos) {
            out += rest;
            return out;
        }
        auto after = rest.substr(at + key.size());
        auto end = after.find(">%");
        if (end == std::string::npos) {
            out += rest;
            return out;
        }
        out += rest.substr(0, at);
        std::size_t j = 0;
        bool ok = true;
        try {
            j = static_cast<std::size_t>(std::stoul(std::string(trim_ws(after.substr(0, end)))));
        } catch (...) {
            ok = false;
        }
        if (ok && j < from.size()) {
            auto object = from[j];
            auto it = std::find(into.begin(), into.end(), object);
            std::size_t k = it == into.end() ? into.size() : static_cast<std::size_t>(it - into.begin());
            if (it == into.end())
                into.push_back(object);
            out += key;
            out += std::to_string(k);
            out += ">%";
        } else
            out += rest.substr(at, key.size() + end + 2);
        rest = after.substr(end + 2);
    }
}

std::vector<const FieldView *> owned_child_fields(const std::vector<FieldView> &fields, std::uint64_t parent) {
    std::vector<const FieldView *> kids;
    if (!parent)
        return kids;
    for (const auto &f : fields)
        if (f.owner == parent)
            kids.push_back(&f);
    std::sort(kids.begin(), kids.end(),
              [](const FieldView *a, const FieldView *b) { return a->handle < b->handle; });
    return kids;
}

std::optional<InlinedFieldCode> inline_code_at(const std::vector<FieldView> &fields, const FieldView &field, int depth) {
    if (depth >= 16)
        return std::nullopt;
    auto kids = owned_child_fields(fields, field.handle);
    auto objects = field.def.references;
    std::string out;
    std::string rest = field.def.code;
    const std::string key = "%<\\_FldIdx ";
    for (;;) {
        auto at = rest.find(key);
        if (at == std::string::npos) {
            out += rest;
            break;
        }
        auto after = rest.substr(at + key.size());
        auto end = after.find(">%");
        if (end == std::string::npos) {
            out += rest;
            break;
        }
        out += rest.substr(0, at);
        std::size_t n = 0;
        bool ok = true;
        try {
            n = static_cast<std::size_t>(std::stoul(std::string(trim_ws(after.substr(0, end)))));
        } catch (...) {
            ok = false;
        }
        if (ok && n < kids.size()) {
            auto inner = inline_code_at(fields, *kids[n], depth + 1);
            if (!inner)
                return std::nullopt;
            auto numbered = renumber_field_objects(inner->code, inner->objects, objects);
            out += "%<";
            out += numbered;
            out += ">%";
        } else
            out += rest.substr(at, key.size() + end + 2);
        rest = after.substr(end + 2);
    }
    return InlinedFieldCode{std::move(out), std::move(objects)};
}

} // namespace

std::optional<std::string> evaluate_code(const DxfDocument &doc, std::string_view code,
                                         const std::vector<std::uint64_t> &objects, std::optional<std::uint64_t> host,
                                         const FieldContext &ctx) {
    std::string text(code);
    auto t = text.find_first_not_of(" \t");
    if (t != std::string::npos)
        text.erase(0, t);
    if (text.size() >= 2 && text.compare(0, 2, "%<") == 0 && text.size() >= 4 && text.compare(text.size() - 2, 2, ">%") == 0)
        text = text.substr(2, text.size() - 4);
    t = text.find_first_not_of(" \t");
    if (t != std::string::npos)
        text.erase(0, t);
    auto evaluator = text;
    if (!evaluator.empty() && evaluator.front() == '\\')
        evaluator.erase(evaluator.begin());
    auto ws = evaluator.find_first_of(" \t");
    if (ws != std::string::npos)
        evaluator.resize(ws);
    FieldView fake;
    fake.def.evaluator_id = evaluator;
    fake.def.code = text;
    fake.def.references = objects;
    auto fields = all_fields(doc);
    auto shown = eval_field(doc, fields, fake, ctx, host.value_or(0));
    if (!shown && evaluator.rfind("AcVar", 0) == 0) {
        auto rest = text;
        if (!rest.empty() && rest.front() == '\\')
            rest.erase(rest.begin());
        auto sp = rest.find_first_of(" \t");
        auto word = sp == std::string::npos ? std::string{} : rest.substr(sp + 1);
        auto w = word.find_first_not_of(" \t");
        if (w != std::string::npos)
            word.erase(0, w);
        auto we = word.find_first_of(" \t");
        if (we != std::string::npos)
            word.resize(we);
        if (word == "PlotDate")
            return std::string(kNoValue);
    }
    return shown;
}

std::optional<InlinedFieldCode> inline_code(const DxfDocument &doc, std::uint64_t field) {
    auto fields = all_fields(doc);
    for (const auto &f : fields)
        if (f.handle == field)
            return inline_code_at(fields, f, 0);
    return std::nullopt;
}

std::optional<std::vector<std::pair<std::size_t, std::size_t>>> field_spans(const DxfDocument &doc, std::uint64_t host,
                                                                           std::string_view text) {
    auto fields = all_fields(doc);
    auto *container = container_for_host(fields, doc, host);
    if (!container)
        return std::nullopt;
    auto kids = owned_child_fields(fields, container->handle);
    std::vector<std::string> shown;
    shown.reserve(kids.size());
    for (const auto *kid : kids)
        shown.push_back(cached_field_value(kid->def));
    bool mtext = false;
    std::ostringstream os;
    os << std::hex << host;
    if (const auto *rec = doc.get_record(os.str()))
        mtext = rec->type() == "MTEXT";
    std::string out;
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    std::string rest = container->def.code;
    for (;;) {
        auto p = rest.find("%<\\_FldIdx ");
        if (p == std::string::npos) {
            out += rest;
            break;
        }
        out += rest.substr(0, p);
        auto after = rest.substr(p + 11);
        auto end = after.find(">%");
        if (end == std::string::npos)
            return std::nullopt;
        auto token = after.substr(0, end);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
            token.erase(token.begin());
        std::size_t idx = 0;
        try {
            idx = static_cast<std::size_t>(std::stoul(token));
        } catch (...) {
            return std::nullopt;
        }
        if (idx >= shown.size())
            return std::nullopt;
        auto start = out.size();
        out += mtext ? mtext_escape(shown[idx]) : shown[idx];
        spans.emplace_back(start, out.size());
        rest = after.substr(end + 2);
    }
    if (out != text)
        return std::nullopt;
    return spans;
}

} // namespace libopencad
