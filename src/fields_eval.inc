std::string format_of(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    if (!s.empty() && s.front() == '"')
        s.remove_prefix(1);
    if (!s.empty() && s.back() == '"')
        s.remove_suffix(1);
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '"') {
            out.push_back('"');
            ++i;
        } else
            out.push_back(s[i]);
    }
    return out;
}

std::string format_fixed6(double n) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.setf(std::ios::fixed);
    o.precision(6);
    o << n;
    return o.str();
}

#include "fields_picture.inc"

std::optional<std::size_t> field_marker_len(std::string_view s) {
    if (s.size() < 2 || s[0] != '%' || s[1] != '<')
        return std::nullopt;
    std::size_t i = 0, depth = 0;
    while (i + 1 < s.size()) {
        if (s[i] == '%' && s[i + 1] == '<') {
            ++depth;
            i += 2;
        } else if (s[i] == '>' && s[i + 1] == '%') {
            --depth;
            i += 2;
            if (depth == 0)
                return i;
        } else
            ++i;
    }
    return std::nullopt;
}

std::optional<std::size_t> top_level_find(std::string_view s, std::string_view pat) {
    std::size_t depth = 0, i = 0;
    while (i < s.size()) {
        if (i + 1 < s.size() && s[i] == '%' && s[i + 1] == '<') {
            ++depth;
            i += 2;
        } else if (i + 1 < s.size() && s[i] == '>' && s[i + 1] == '%') {
            if (depth)
                --depth;
            i += 2;
        } else {
            if (depth == 0 && i + pat.size() <= s.size() && s.substr(i, pat.size()) == pat)
                return i;
            ++i;
        }
    }
    return std::nullopt;
}

std::string mtext_plain(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        auto c = s[i++];
        if (c == '{' || c == '}')
            continue;
        if (c == '\\') {
            if (i >= s.size())
                break;
            auto n = s[i++];
            if (n == 'P' || n == '~')
                out.push_back(' ');
            else if (n == '\\' || n == '{' || n == '}')
                out.push_back(n);
            else if (std::string("ACcFfHQTWpS").find(n) != std::string::npos) {
                while (i < s.size() && s[i] != ';')
                    ++i;
                if (i < s.size())
                    ++i;
            }
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::optional<double> number_in_text(std::string_view text) {
    auto plain = mtext_plain(text);
    auto s = plain;
    auto t = s.find_first_not_of(" \t");
    if (t != std::string::npos)
        s.erase(0, t);
    auto start = s.find_first_of("0123456789.");
    if (start == std::string::npos)
        return std::nullopt;
    bool negative = start && s.substr(0, start).find_last_not_of(" \t") != std::string::npos &&
                    s[s.substr(0, start).find_last_not_of(" \t")] == '-';
    std::string run;
    for (std::size_t i = start; i < s.size(); ++i) {
        auto c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ',')
            run.push_back(c);
        else
            break;
    }
    while (!run.empty() && (run.back() == '.' || run.back() == ','))
        run.pop_back();
    auto last_dot = run.rfind('.');
    auto last_com = run.rfind(',');
    std::string normalized;
    if (last_dot != std::string::npos && last_com != std::string::npos && last_com > last_dot) {
        for (char c : run)
            if (c != '.')
                normalized.push_back(c == ',' ? '.' : c);
    } else if (last_dot != std::string::npos && last_com != std::string::npos) {
        for (char c : run)
            if (c != ',')
                normalized.push_back(c);
    } else if (last_com != std::string::npos) {
        auto commas = std::count(run.begin(), run.end(), ',');
        if (commas > 1 || run.size() - last_com - 1 == 3) {
            for (char c : run)
                if (c != ',')
                    normalized.push_back(c);
        } else {
            for (char c : run)
                normalized.push_back(c == ',' ? '.' : c);
        }
    } else
        normalized = run;
    try {
        auto v = std::stod(normalized);
        return negative ? -v : v;
    } catch (...) {
        return std::nullopt;
    }
}

std::string text_case_pic(std::string s, std::string_view pic) { return text_case(std::move(s), pic); }

enum class ExprError { Invalid, Unresolved };
enum class NumKind { Int, Real };
struct Num {
    NumKind kind = NumKind::Int;
    std::int64_t i = 0;
    double r = 0;
    double real() const { return kind == NumKind::Int ? static_cast<double>(i) : r; }
    static Num from_int(std::int64_t n) { return {NumKind::Int, n, static_cast<double>(n)}; }
    static Num from_real(double x) { return {NumKind::Real, 0, x}; }
};
using ExprResult = std::pair<bool, Num>; // first false => error stored in thread
thread_local ExprError g_expr_error = ExprError::Invalid;

ExprResult int_checked(std::optional<std::int64_t> n) {
    if (!n || *n < std::numeric_limits<std::int32_t>::min() || *n > std::numeric_limits<std::int32_t>::max()) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
    return {true, Num::from_int(*n)};
}

std::vector<const FieldView *> child_fields(const std::vector<FieldView> &fields, std::uint64_t parent) {
    std::vector<const FieldView *> kids;
    if (!parent)
        return kids;
    for (const auto &f : fields)
        if (f.owner == parent)
            kids.push_back(&f);
    std::sort(kids.begin(), kids.end(), [](const FieldView *a, const FieldView *b) { return a->handle < b->handle; });
    return kids;
}

const DxfTable *table_for_host(const DxfDocument &doc, std::uint64_t host) {
    const DxfTable *found = nullptr;
    std::size_t count = 0;
    const DxfTable *only = nullptr;
    doc.for_each<DxfTable>([&](const DxfTable &t) {
        ++count;
        only = &t;
        if (handle_value(t.handle()) == host)
            found = &t;
        auto def = t.definition();
        auto owner = owner_of(doc, host);
        if (owner && def.block_record == *owner)
            found = &t;
    });
    if (found)
        return found;
    std::ostringstream o;
    o << std::hex << host;
    auto *rec = doc.get_record(o.str());
    if (rec) {
        auto owner = rec->owner_handle();
        bool layout = false;
        doc.for_each<DxfBlockRecord>([&](const DxfBlockRecord &b) {
            if (handle_value(b.handle()) == handle_value(owner)) {
                auto n = b.name();
                std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (n.rfind("*model_space", 0) == 0 || n.rfind("*paper_space", 0) == 0)
                    layout = true;
            }
        });
        if (layout)
            return nullptr;
    }
    return count == 1 ? only : nullptr;
}

enum class CellNumKind { Num, Blank, Unknown };
struct CellNum {
    CellNumKind kind = CellNumKind::Blank;
    Num value;
};

CellNum cell_value(const DxfDocument &doc, const std::vector<FieldView> &fields, const DxfTable &table, std::size_t col,
                   std::size_t row, int depth);

struct ExprParser {
    const DxfDocument *doc = nullptr;
    const std::vector<FieldView> *all = nullptr;
    std::string_view s;
    std::size_t i = 0;
    const std::vector<std::uint64_t> *objects = nullptr;
    const DxfTable *table = nullptr;
    bool host_cells = true;
    bool used_cells = false;
    int depth = 0;
    std::vector<const FieldView *> kids;
    const FieldContext *ctx = nullptr;
    std::uint64_t host = 0;

    std::optional<unsigned char> peek() const {
        if (i >= s.size())
            return std::nullopt;
        return static_cast<unsigned char>(s[i]);
    }
    void skip_ws() {
        while (peek() == ' ')
            ++i;
    }
    bool eat(char c) {
        skip_ws();
        if (peek() == static_cast<unsigned char>(c)) {
            ++i;
            return true;
        }
        return false;
    }
    ExprResult parse_all();
    ExprResult parse_expr();
    ExprResult parse_term();
    ExprResult parse_power();
    ExprResult parse_unary();
    ExprResult parse_primary();
    ExprResult parse_number();
    ExprResult parse_name();
    ExprResult parse_nested_field();
    ExprResult parse_table_ref();
    CellNum cell(std::size_t col, std::size_t row);
    std::pair<bool, std::vector<Num>> parse_args();
};

std::optional<std::pair<std::size_t, std::size_t>> parse_cellref(std::string_view s, std::size_t &i) {
    auto start = i;
    std::size_t col = 0;
    bool has_col = false;
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) {
        col = col * 26 + (std::toupper(static_cast<unsigned char>(s[i])) - 'A' + 1);
        ++i;
        has_col = true;
    }
    std::size_t row = 0;
    bool has_row = false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        row = row * 10 + (s[i] - '0');
        ++i;
        has_row = true;
    }
    if (has_col && has_row && col >= 1 && row >= 1)
        return std::make_pair(col - 1, row - 1);
    i = start;
    return std::nullopt;
}

ExprResult apply_func(std::string_view name, const std::vector<Num> &vals, bool table) {
    std::vector<double> xs;
    for (auto v : vals)
        xs.push_back(v.real());
    auto one = [&]() -> std::optional<double> {
        if (xs.size() == 1)
            return xs[0];
        return std::nullopt;
    };
    if (name == "sum" && table) {
        bool all_int = true;
        for (auto v : vals)
            if (v.kind != NumKind::Int)
                all_int = false;
        if (all_int) {
            long long total = 0;
            for (auto v : vals)
                total += v.i;
            return int_checked(total);
        }
        double s = 0;
        for (auto x : xs)
            s += x;
        return {true, Num::from_real(s)};
    }
    if ((name == "average" || name == "mean") && table && !xs.empty()) {
        double s = 0;
        for (auto x : xs)
            s += x;
        return {true, Num::from_real(s / static_cast<double>(xs.size()))};
    }
    if (name == "count" && table)
        return {true, Num::from_int(static_cast<std::int64_t>(xs.size()))};
    if (name == "min" && table && !xs.empty()) {
        double m = xs[0];
        for (auto x : xs)
            m = std::min(m, x);
        return {true, Num::from_real(m)};
    }
    if (name == "max" && table && !xs.empty()) {
        double m = xs[0];
        for (auto x : xs)
            m = std::max(m, x);
        return {true, Num::from_real(m)};
    }
    if (name == "product" && table) {
        double p = 1;
        for (auto x : xs)
            p *= x;
        return {true, Num::from_real(p)};
    }
    if (name == "abs") {
        auto x = one();
        if (!x) {
            g_expr_error = ExprError::Invalid;
            return {false, {}};
        }
        return {true, Num::from_real(std::abs(*x))};
    }
    if (name == "sqrt") {
        auto x = one();
        if (!x || *x < 0) {
            g_expr_error = ExprError::Invalid;
            return {false, {}};
        }
        return {true, Num::from_real(std::sqrt(*x))};
    }
    if (name == "round") {
        auto x = one();
        if (!x) {
            g_expr_error = ExprError::Invalid;
            return {false, {}};
        }
        return int_checked(static_cast<std::int64_t>(std::round(*x)));
    }
    g_expr_error = ExprError::Invalid;
    return {false, {}};
}

std::string code_evaluator(std::string_view code) {
    auto body = code;
    while (!body.empty() && (body.front() == ' ' || body.front() == '\\'))
        body.remove_prefix(1);
    auto sp = body.find_first_of(" \t");
    return std::string(body.substr(0, sp));
}

ExprResult nested_number(const DxfDocument &doc, const std::vector<FieldView> &fields, const FieldView &field,
                         const FieldContext &ctx, std::uint64_t host);

ExprResult ExprParser::parse_all() {
    auto v = parse_expr();
    if (!v.first)
        return v;
    skip_ws();
    if (i == s.size())
        return v;
    g_expr_error = ExprError::Invalid;
    return {false, {}};
}
ExprResult ExprParser::parse_expr() {
    auto v = parse_term();
    if (!v.first)
        return v;
    for (;;) {
        if (eat('+')) {
            auto r = parse_term();
            if (!r.first)
                return r;
            if (v.second.kind == NumKind::Int && r.second.kind == NumKind::Int) {
                auto n = int_checked(v.second.i + r.second.i);
                if (!n.first)
                    return n;
                v = n;
            } else
                v = {true, Num::from_real(v.second.real() + r.second.real())};
        } else if (eat('-')) {
            auto r = parse_term();
            if (!r.first)
                return r;
            if (v.second.kind == NumKind::Int && r.second.kind == NumKind::Int) {
                auto n = int_checked(v.second.i - r.second.i);
                if (!n.first)
                    return n;
                v = n;
            } else
                v = {true, Num::from_real(v.second.real() - r.second.real())};
        } else
            return v;
    }
}
ExprResult ExprParser::parse_term() {
    auto v = parse_power();
    if (!v.first)
        return v;
    for (;;) {
        if (eat('*')) {
            auto r = parse_power();
            if (!r.first)
                return r;
            if (v.second.kind == NumKind::Int && r.second.kind == NumKind::Int) {
                auto n = int_checked(v.second.i * r.second.i);
                if (!n.first)
                    return n;
                v = n;
            } else
                v = {true, Num::from_real(v.second.real() * r.second.real())};
        } else if (eat('/')) {
            auto r = parse_power();
            if (!r.first)
                return r;
            auto d = r.second.real();
            if (d == 0.0) {
                g_expr_error = ExprError::Invalid;
                return {false, {}};
            }
            v = {true, Num::from_real(v.second.real() / d)};
        } else
            return v;
    }
}
ExprResult ExprParser::parse_power() {
    auto v = parse_unary();
    if (!v.first)
        return v;
    while (eat('^')) {
        auto e = parse_unary();
        if (!e.first)
            return e;
        v = {true, Num::from_real(std::pow(v.second.real(), e.second.real()))};
    }
    return v;
}
ExprResult ExprParser::parse_unary() {
    if (eat('-')) {
        auto v = parse_primary();
        if (!v.first)
            return v;
        if (v.second.kind == NumKind::Int)
            return {true, Num::from_int(-v.second.i)};
        return {true, Num::from_real(-v.second.real())};
    }
    eat('+');
    return parse_primary();
}
ExprResult ExprParser::parse_primary() {
    skip_ws();
    auto c = peek();
    if (!c) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
    if (*c == '(') {
        ++i;
        auto v = parse_expr();
        if (!v.first)
            return v;
        if (!eat(')')) {
            g_expr_error = ExprError::Invalid;
            return {false, {}};
        }
        return v;
    }
    if (std::isdigit(*c) || *c == '.')
        return parse_number();
    if (std::isalpha(*c))
        return parse_name();
    if (*c == '%')
        return parse_nested_field();
    g_expr_error = ExprError::Invalid;
    return {false, {}};
}
ExprResult ExprParser::parse_nested_field() {
    auto rest = s.substr(i);
    auto len = field_marker_len(rest);
    if (!len) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
    auto inner = rest.substr(2, *len - 4);
    while (!inner.empty() && (inner.front() == ' ' || inner.back() == ' ')) {
        if (inner.front() == ' ')
            inner.remove_prefix(1);
        if (!inner.empty() && inner.back() == ' ')
            inner.remove_suffix(1);
    }
    i += *len;
    if (!ctx) {
        g_expr_error = ExprError::Unresolved;
        return {false, {}};
    }
    const char *idx = "\\_FldIdx ";
    if (inner.size() >= std::strlen(idx) && inner.substr(0, std::strlen(idx)) == idx) {
        try {
            auto n = static_cast<std::size_t>(std::stoul(std::string(inner.substr(std::strlen(idx)))));
            if (n >= kids.size()) {
                g_expr_error = ExprError::Unresolved;
                return {false, {}};
            }
            return nested_number(*doc, *all, *kids[n], *ctx, host);
        } catch (...) {
            g_expr_error = ExprError::Invalid;
            return {false, {}};
        }
    }
    if (inner.empty() || inner.front() != '\\' || depth >= 8) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
    FieldView fake;
    fake.def.evaluator_id = code_evaluator(inner);
    fake.def.code = std::string(inner);
    if (objects)
        fake.def.references = *objects;
    return nested_number(*doc, *all, fake, *ctx, host);
}
ExprResult ExprParser::parse_number() {
    auto start = i;
    bool real = false;
    while (auto c = peek()) {
        if (std::isdigit(*c)) {
        } else if (*c == '.')
            real = true;
        else if (*c == 'e' || *c == 'E') {
            real = true;
            if (i + 1 < s.size() && (s[i + 1] == '+' || s[i + 1] == '-'))
                ++i;
        } else
            break;
        ++i;
    }
    auto text = std::string(s.substr(start, i - start));
    try {
        if (real)
            return {true, Num::from_real(std::stod(text))};
        return int_checked(std::stoll(text));
    } catch (...) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
}
ExprResult ExprParser::parse_name() {
    auto start = i;
    while (auto c = peek()) {
        if (!std::isalpha(*c))
            break;
        ++i;
    }
    std::string name(s.substr(start, i - start));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (peek() == '(') {
        ++i;
        if (name == "table")
            return parse_table_ref();
        auto args = parse_args();
        if (!args.first)
            return {false, {}};
        if (!eat(')')) {
            g_expr_error = ExprError::Invalid;
            return {false, {}};
        }
        return apply_func(name, args.second, table != nullptr);
    }
    if (name == "pi")
        return {true, Num::from_real(std::acos(-1.0))};
    i = start;
    auto cellref = parse_cellref(s, i);
    if (!cellref) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
    auto cv = cell(cellref->first, cellref->second);
    if (cv.kind == CellNumKind::Num)
        return {true, cv.value};
    g_expr_error = cv.kind == CellNumKind::Blank ? ExprError::Invalid : ExprError::Unresolved;
    return {false, {}};
}
ExprResult ExprParser::parse_table_ref() {
    skip_ws();
    auto rest = s.substr(i);
    auto end = rest.find(">%");
    if (end == std::string_view::npos) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
    auto marker = rest.substr(0, end);
    std::uint64_t handle = 0;
    const char *idx = "%<\\_ObjIdx ";
    const char *id = "%<\\_ObjId ";
    try {
        if (marker.size() >= std::strlen(idx) && marker.substr(0, std::strlen(idx)) == idx) {
            auto n = static_cast<std::size_t>(std::stoul(std::string(marker.substr(std::strlen(idx)))));
            if (!objects || n >= objects->size()) {
                g_expr_error = ExprError::Unresolved;
                return {false, {}};
            }
            handle = (*objects)[n];
        } else if (marker.size() >= std::strlen(id) && marker.substr(0, std::strlen(id)) == id) {
            handle = std::stoull(std::string(marker.substr(std::strlen(id))));
        } else {
            g_expr_error = ExprError::Invalid;
            return {false, {}};
        }
    } catch (...) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
    i += end + 2;
    if (!eat(')') || !eat('.')) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
    const DxfTable *found = nullptr;
    doc->for_each<DxfTable>([&](const DxfTable &t) {
        if (handle_value(t.handle()) == handle)
            found = &t;
    });
    if (!found) {
        g_expr_error = ExprError::Unresolved;
        return {false, {}};
    }
    auto saved_table = table;
    auto saved_host = host_cells;
    table = found;
    host_cells = false;
    ExprResult r;
    auto word = s.substr(i, 9);
    std::string lower(word);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "evaluate(") {
        i += 9;
        r = parse_expr();
        if (r.first && !eat(')')) {
            g_expr_error = ExprError::Invalid;
            r = {false, {}};
        }
    } else
        r = parse_name();
    table = saved_table;
    host_cells = saved_host;
    return r;
}
CellNum ExprParser::cell(std::size_t col, std::size_t row) {
    if (!table) {
        g_expr_error = ExprError::Unresolved;
        return {CellNumKind::Unknown, {}};
    }
    if (host_cells)
        used_cells = true;
    return cell_value(*doc, *all, *table, col, row, depth);
}
std::pair<bool, std::vector<Num>> ExprParser::parse_args() {
    std::vector<Num> vals;
    for (;;) {
        skip_ws();
        auto save = i;
        auto c1 = parse_cellref(s, i);
        if (c1 && eat(':')) {
            skip_ws();
            auto c2 = parse_cellref(s, i);
            if (!c2) {
                g_expr_error = ExprError::Invalid;
                return {false, {}};
            }
            auto r1 = std::min(c1->second, c2->second), r2 = std::max(c1->second, c2->second);
            auto col1 = std::min(c1->first, c2->first), col2 = std::max(c1->first, c2->first);
            for (auto r = r1; r <= r2; ++r)
                for (auto c = col1; c <= col2; ++c) {
                    auto cv = cell(c, r);
                    if (cv.kind == CellNumKind::Num)
                        vals.push_back(cv.value);
                    else if (cv.kind == CellNumKind::Unknown) {
                        g_expr_error = ExprError::Unresolved;
                        return {false, {}};
                    }
                }
        } else {
            i = save;
            auto v = parse_expr();
            if (!v.first)
                return {false, {}};
            vals.push_back(v.second);
        }
        if (!eat(','))
            return {true, vals};
    }
}

CellNum cell_value(const DxfDocument &doc, const std::vector<FieldView> &fields, const DxfTable &table, std::size_t col,
                   std::size_t row, int depth) {
    auto def = table.definition();
    if (row >= def.cells.size() || col >= def.cells[row].size())
        return {CellNumKind::Blank, {}};
    const auto &cell = def.cells[row][col];
    if (cell.field) {
        const FieldView *f = nullptr;
        for (const auto &x : fields)
            if (x.handle == cell.field)
                f = &x;
        if (!f)
            return {CellNumKind::Unknown, {}};
        if (f->def.evaluator_id == "_text" && f->def.code.find("%<\\_FldIdx 0>%") != std::string::npos) {
            auto kids = child_fields(fields, f->handle);
            if (kids.empty())
                return {CellNumKind::Unknown, {}};
            f = kids.front();
        }
        if (f->def.evaluator_id != "AcExpr" || depth >= 8)
            return {CellNumKind::Unknown, {}};
        auto code = f->def.code;
        auto t = code.find_first_not_of(" \t");
        if (t != std::string::npos)
            code.erase(0, t);
        const char *prefix = "\\AcExpr";
        if (code.rfind(prefix, 0) == 0)
            code.erase(0, std::strlen(prefix));
        auto fp = top_level_find(code, "\\f ");
        auto expr = fp ? code.substr(0, *fp) : code;
        t = expr.find_first_not_of(" \t");
        if (t != std::string::npos)
            expr.erase(0, t);
        auto e = expr.find_last_not_of(" \t");
        if (e != std::string::npos)
            expr.erase(e + 1);
        ExprParser p;
        p.doc = &doc;
        p.all = &fields;
        p.s = expr;
        p.objects = &f->def.references;
        p.table = &table;
        p.host_cells = false;
        p.depth = depth + 1;
        p.kids = child_fields(fields, f->handle);
        auto v = p.parse_all();
        if (v.first)
            return {CellNumKind::Num, v.second};
        return g_expr_error == ExprError::Invalid ? CellNum{CellNumKind::Blank, {}} : CellNum{CellNumKind::Unknown, {}};
    }
    if (!cell.value)
        return {CellNumKind::Blank, {}};
    auto cv = *cell.value;
    if (cv.type == 1)
        return {CellNumKind::Num, Num::from_int(static_cast<std::int64_t>(cv.number))};
    if (cv.type == 2)
        return {CellNumKind::Num, Num::from_real(cv.number)};
    auto s = !cv.formatted.empty() ? cv.formatted : cv.text;
    auto lead = s.find_first_not_of(" \t");
    if (lead != std::string::npos)
        s.erase(0, lead);
    try {
        std::size_t idx = 0;
        auto n = std::stoi(s, &idx);
        if (idx == s.size())
            return {CellNumKind::Num, Num::from_int(n)};
    } catch (...) {
    }
    try {
        return {CellNumKind::Num, Num::from_real(std::stod(s))};
    } catch (...) {
        return {CellNumKind::Blank, {}};
    }
}

std::optional<std::pair<std::string, std::string>> objprop_parts(std::string_view code) {
    auto p = code.find(").");
    if (p == std::string_view::npos)
        return std::nullopt;
    auto rest = code.substr(p + 2);
    auto end = rest.find_first_of(" \\");
    auto prop = rest.substr(0, end);
    while (!prop.empty() && (prop.front() == ' ' || prop.back() == ' ')) {
        if (prop.front() == ' ')
            prop.remove_prefix(1);
        if (!prop.empty() && prop.back() == ' ')
            prop.remove_suffix(1);
    }
    if (prop.empty())
        return std::nullopt;
    std::string fmt;
    auto fp = code.find("\\f ");
    if (fp != std::string_view::npos)
        fmt = format_of(code.substr(fp + 3));
    return std::make_pair(std::string(prop), fmt);
}

std::optional<std::uint64_t> objprop_object(const DxfDocument &doc, const FieldView &field, std::uint64_t host) {
    auto code = field.def.code;
    if (code.find("?BlockRefId") != std::string::npos) {
        std::optional<std::uint64_t> found;
        doc.for_each<DxfInsert>([&](const DxfInsert &ins) {
            for (std::size_t i = 0; i < ins.attribute_count(); ++i)
                if (handle_value(ins.attribute(i).handle()) == host)
                    found = handle_value(ins.handle());
        });
        return found;
    }
    auto between = [&](const char *a, const char *b) -> std::optional<std::string> {
        auto s = code.find(a);
        if (s == std::string::npos)
            return std::nullopt;
        s += std::strlen(a);
        auto e = code.find(b, s);
        if (e == std::string::npos)
            return std::nullopt;
        return code.substr(s, e - s);
    };
    if (auto idx = between("_ObjIdx ", ">%")) {
        try {
            auto n = static_cast<std::size_t>(std::stoul(*idx));
            if (n < field.def.references.size())
                return field.def.references[n];
        } catch (...) {
        }
        return std::nullopt;
    }
    if (auto id = between("_ObjId ", ">%")) {
        try {
            return std::stoull(*id);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> named_object_name(const DxfDocument &doc, std::uint64_t h) {
    std::optional<std::string> name;
    auto take = [&](std::string n, std::string_view handle) {
        if (handle_value(handle) == h)
            name = std::move(n);
    };
    doc.for_each<DxfLayer>([&](const DxfLayer &e) { take(e.name(), e.handle()); });
    doc.for_each<DxfLineType>([&](const DxfLineType &e) { take(e.name(), e.handle()); });
    doc.for_each<DxfBlockRecord>([&](const DxfBlockRecord &e) { take(e.name(), e.handle()); });
    doc.for_each<DxfMaterial>([&](const DxfMaterial &e) { take(e.definition().name, e.handle()); });
    return name;
}

enum class PropKind { Num, Angle, Point, Text, Lineweight };
struct PropVal {
    PropKind kind = PropKind::Num;
    double n = 0;
    Vector3 p{};
    std::string text;
    std::int16_t weight = 0;
};

template <class T>
const T *entity_by_handle(const DxfDocument &doc, std::uint64_t handle) {
    const T *found = nullptr;
    doc.for_each<T>([&](const T &e) {
        if (handle_value(e.handle()) == handle)
            found = &e;
    });
    return found;
}

std::string linetype_text(std::string_view name) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.empty() || lower == "bylayer")
        return "ByLayer";
    if (lower == "byblock")
        return "ByBlock";
    return std::string(name);
}

std::string transparency_text(Transparency t) {
    if (t.kind() == Transparency::Kind::ByLayer)
        return "ByLayer";
    if (t.kind() == Transparency::Kind::ByBlock)
        return "ByBlock";
    auto shown = std::round(static_cast<double>(t.alpha()) * 100.0 / 255.0);
    return std::to_string(static_cast<long long>(shown));
}

std::string color_text(Color c) {
    const char *names[] = {"red", "yellow", "green", "cyan", "blue", "magenta", "white"};
    if (c.kind() == Color::Kind::ByLayer || c.kind() == Color::Kind::None)
        return "BYLAYER";
    if (c.kind() == Color::Kind::ByBlock)
        return "BYBLOCK";
    if (c.kind() == Color::Kind::Index) {
        auto i = c.index().value_or(0);
        if (i >= 1 && i <= 7)
            return names[i - 1];
        return std::to_string(i);
    }
    if (auto rgb = c.rgb())
        return std::to_string((*rgb)[0]) + "," + std::to_string((*rgb)[1]) + "," + std::to_string((*rgb)[2]);
    return "BYLAYER";
}

struct InsertUnit {
    const char *name;
    double meters;
};
constexpr InsertUnit kInsertUnits[] = {
    {"Unitless", 1.0},
    {"Inches", 0.0254},
    {"Feet", 0.3048},
    {"Miles", 1609.344},
    {"Millimeters", 0.001},
    {"Centimeters", 0.01},
    {"Meters", 1.0},
    {"Kilometers", 1000.0},
    {"Microinches", 2.54e-8},
    {"Mils", 2.54e-5},
    {"Yards", 0.9144},
    {"Angstroms", 1e-10},
    {"Nanometers", 1e-9},
    {"Microns", 1e-6},
    {"Decimeters", 0.1},
    {"Dekameters", 10.0},
    {"Hectometers", 100.0},
    {"Gigameters", 1e9},
    {"Astronomical", 1.496e11},
    {"Light Years", 9.4605e15},
    {"Parsecs", 3.084123e16},
    {"US Survey Feet", 1200.0 / 3937.0},
    {"US Survey Inch", 100.0 / 3937.0},
    {"US Survey Yard", 3600.0 / 3937.0},
    {"US Survey Mile", 6336000.0 / 3937.0},
};
constexpr std::size_t kInsertUnitsCount = sizeof(kInsertUnits) / sizeof(kInsertUnits[0]);

const DxfBlockRecord *block_record_named(const DxfDocument &doc, std::string_view name) {
    const DxfBlockRecord *found = nullptr;
    doc.for_each<DxfBlockRecord>([&](const DxfBlockRecord &r) {
        if (r.name() == name)
            found = &r;
    });
    return found;
}

std::int16_t insert_block_units(const DxfDocument &doc, const DxfInsert &ins) {
    auto *rec = block_record_named(doc, ins.definition().block_name);
    return rec ? rec->units() : 0;
}

double insert_unit_factor(const DxfDocument &doc, const DxfInsert &ins) {
    auto block = insert_block_units(doc, ins);
    auto drawing = doc.insertion_units();
    if (block > 0 && drawing > 0 && static_cast<std::size_t>(block) < kInsertUnitsCount &&
        static_cast<std::size_t>(drawing) < kInsertUnitsCount)
        return kInsertUnits[static_cast<std::size_t>(block)].meters /
               kInsertUnits[static_cast<std::size_t>(drawing)].meters;
    return 1.0;
}

std::optional<PropVal> object_property(const DxfDocument &doc, std::uint64_t handle, std::string_view prop) {
    const double pi = std::acos(-1.0);
    auto as_point = [](Vector3 v) {
        PropVal p;
        p.kind = PropKind::Point;
        p.p = v;
        return p;
    };
    auto as_num = [](double n) {
        PropVal p;
        p.kind = PropKind::Num;
        p.n = n;
        return p;
    };
    auto as_text = [](std::string s) {
        PropVal p;
        p.kind = PropKind::Text;
        p.text = std::move(s);
        return p;
    };
    if (auto *line = [&]() -> const DxfLine * {
            const DxfLine *p = nullptr;
            doc.for_each<DxfLine>([&](const DxfLine &e) {
                if (handle_value(e.handle()) == handle)
                    p = &e;
            });
            return p;
        }()) {
        if (prop == "StartPoint")
            return as_point(line->start());
        if (prop == "EndPoint")
            return as_point(line->end());
        if (prop == "Center")
            return as_point((line->start() + line->end()) * 0.5);
        if (prop == "Length" || prop == "Perimeter")
            return as_num(line->length());
        if (prop == "Thickness")
            return as_num(line->thickness());
        if (prop == "Layer")
            return as_text(line->layer());
        if (prop == "TrueColor" || prop == "Color")
            return as_text(color_text(line->color()));
        if (prop == "ObjectName")
            return as_text("AcDbLine");
        if (prop == "LinetypeScale")
            return as_num(line->linetype_scale());
        if (prop == "Lineweight") {
            PropVal p;
            p.kind = PropKind::Lineweight;
            p.weight = line->line_weight().value();
            return p;
        }
    }
    if (auto *arc = [&]() -> const DxfArc * {
            const DxfArc *p = nullptr;
            doc.for_each<DxfArc>([&](const DxfArc &e) {
                if (handle_value(e.handle()) == handle)
                    p = &e;
            });
            return p;
        }()) {
        if (prop == "Center")
            return as_point(arc->center());
        if (prop == "Radius")
            return as_num(arc->radius());
        if (prop == "Diameter")
            return as_num(2 * arc->radius());
        if (prop == "Length" || prop == "Perimeter")
            return as_num(arc->arc_length());
        if (prop == "Normal")
            return as_point(arc->normal());
        if (prop == "Thickness")
            return as_num(arc->thickness());
        if (prop == "Layer")
            return as_text(arc->layer());
        if (prop == "ObjectName")
            return as_text("AcDbArc");
    }
    if (auto *circle = [&]() -> const DxfCircle * {
            const DxfCircle *p = nullptr;
            doc.for_each<DxfCircle>([&](const DxfCircle &e) {
                if (handle_value(e.handle()) == handle)
                    p = &e;
            });
            return p;
        }()) {
        if (prop == "Center")
            return as_point(circle->center());
        if (prop == "Radius")
            return as_num(circle->radius());
        if (prop == "Diameter")
            return as_num(2 * circle->radius());
        if (prop == "Circumference")
            return as_num(2 * pi * circle->radius());
        if (prop == "Area")
            return as_num(circle->area());
        if (prop == "Normal")
            return as_point(circle->normal());
        if (prop == "Thickness")
            return as_num(circle->thickness());
        if (prop == "Layer")
            return as_text(circle->layer());
        if (prop == "ObjectName")
            return as_text("AcDbCircle");
        if (prop == "TrueColor" || prop == "Color")
            return as_text(color_text(circle->color()));
        if (prop == "LinetypeScale")
            return as_num(circle->linetype_scale());
    }
    if (auto *el = [&]() -> const DxfEllipse * {
            const DxfEllipse *p = nullptr;
            doc.for_each<DxfEllipse>([&](const DxfEllipse &e) {
                if (handle_value(e.handle()) == handle)
                    p = &e;
            });
            return p;
        }()) {
        if (prop == "Center")
            return as_point(el->center());
        if (prop == "Area" && el->is_full()) {
            auto major = el->major_axis_length();
            return as_num(pi * major * major * el->minor_axis_ratio());
        }
        if (prop == "ObjectName")
            return as_text("AcDbEllipse");
    }
    if (auto *ins = [&]() -> const DxfInsert * {
            const DxfInsert *p = nullptr;
            doc.for_each<DxfInsert>([&](const DxfInsert &e) {
                if (handle_value(e.handle()) == handle)
                    p = &e;
            });
            return p;
        }()) {
        auto d = ins->definition();
        if (prop == "EffectiveName" || prop == "Name")
            return as_text(d.block_name);
        if (prop == "InsertionPoint" || prop == "Position")
            return as_point(d.insertion);
        if (prop == "Normal")
            return as_point(d.normal);
        if (prop == "Rotation") {
            PropVal p;
            p.kind = PropKind::Angle;
            p.n = d.rotation;
            return p;
        }
        if (prop == "ObjectName")
            return as_text("AcDbBlockReference");
        if (prop == "Layer")
            return as_text(ins->layer());
        if (prop == "XScaleFactor" || prop == "YScaleFactor" || prop == "ZScaleFactor" ||
            prop == "XEffectiveScaleFactor" || prop == "YEffectiveScaleFactor" ||
            prop == "ZEffectiveScaleFactor") {
            double s = d.scale.z;
            if (prop.front() == 'X')
                s = d.scale.x;
            else if (prop.front() == 'Y')
                s = d.scale.y;
            auto unit = prop.find("Effective") != std::string_view::npos ? insert_unit_factor(doc, *ins) : 1.0;
            return as_num(s / unit);
        }
        if (prop == "InsUnits") {
            auto u = insert_block_units(doc, *ins);
            if (u < 0 || static_cast<std::size_t>(u) >= kInsertUnitsCount)
                return std::nullopt;
            return as_text(kInsertUnits[static_cast<std::size_t>(u)].name);
        }
        if (prop == "InsUnitsFactor")
            return as_num(insert_unit_factor(doc, *ins));
    }
    if (auto *text = entity_by_handle<DxfText>(doc, handle)) {
        if (prop == "ObjectName")
            return as_text("AcDbText");
        if (prop == "InsertionPoint" || prop == "Position")
            return as_point(text->insertion_point());
        if (prop == "TextString")
            return as_text(text->text());
    }
    if (auto *mtext = entity_by_handle<DxfMText>(doc, handle)) {
        if (prop == "ObjectName")
            return as_text("AcDbMText");
        if (prop == "InsertionPoint" || prop == "Position")
            return as_point(mtext->definition().insertion);
        if (prop == "TextString")
            return as_text(mtext_plain(mtext->text()));
    }
    if (auto *attr = entity_by_handle<DxfAttribute>(doc, handle)) {
        if (prop == "InsertionPoint" || prop == "Position")
            return as_point(attr->insertion_point());
        if (prop == "TextString")
            return as_text(attr->text());
    }
    if (auto *pl = entity_by_handle<DxfLwPolyline>(doc, handle)) {
        (void)pl;
        if (prop == "ObjectName")
            return as_text("AcDbPolyline");
    }
    if (auto *table = entity_by_handle<DxfTable>(doc, handle)) {
        (void)table;
        if (prop == "ObjectName")
            return as_text("AcDbTable");
    }
    if (auto *ent = entity_by_handle<DxfEntity>(doc, handle)) {
        if (prop == "Linetype")
            return as_text(linetype_text(ent->linetype()));
        if (prop == "EntityTransparency")
            return as_text(transparency_text(ent->transparency()));
        if (prop == "LinetypeScale")
            return as_num(ent->linetype_scale());
        if (prop == "TrueColor" || prop == "Color")
            return as_text(color_text(ent->color()));
        if (prop == "Lineweight") {
            PropVal p;
            p.kind = PropKind::Lineweight;
            p.weight = ent->line_weight().value();
            return p;
        }
        if (prop == "PlotStyleName") {
            auto display = ent->display_properties();
            if (display.plot_style_mode == 1)
                return as_text("ByBlock");
            if (display.plot_style_mode == 3) {
                auto n = named_object_name(doc, display.plot_style);
                if (!n)
                    return std::nullopt;
                return as_text(*n);
            }
            return as_text(doc.plot_style_mode() ? "ByColor" : "ByLayer");
        }
        if (prop == "Material") {
            auto display = ent->display_properties();
            if (display.material_flags == 1)
                return as_text("ByBlock");
            if (display.material_flags == 3) {
                auto n = named_object_name(doc, display.material);
                if (!n)
                    return std::nullopt;
                return as_text(*n);
            }
            return as_text("ByLayer");
        }
        if (prop == "Layer")
            return as_text(ent->layer());
    }
    return std::nullopt;
}

Vector3 to_current_ucs(const DxfDocument &doc, std::uint64_t handle, Vector3 p) {
    std::string owner;
    doc.for_each<DxfEntity>([&](const DxfEntity &e) {
        if (handle_value(e.handle()) == handle)
            owner = e.owner_handle();
    });
    bool paper = false;
    doc.for_each<DxfBlockRecord>([&](const DxfBlockRecord &b) {
        if (handle_value(b.handle()) == handle_value(owner) && b.is_paper_space())
            paper = true;
    });
    auto o = paper ? doc.paper_space_ucs_origin() : doc.model_space_ucs_origin();
    auto x = paper ? doc.paper_space_ucs_x_axis() : doc.model_space_ucs_x_axis();
    auto y = paper ? doc.paper_space_ucs_y_axis() : doc.model_space_ucs_y_axis();
    if (x.length() < 1e-12 || y.length() < 1e-12)
        return p;
    x = x.normalize();
    y = y.normalize();
    auto z = x.cross(y);
    auto d = p - o;
    return {d.dot(x), d.dot(y), d.dot(z)};
}

std::optional<PropVal> entity_property(const DxfDocument &doc, const FieldView &field, std::uint64_t handle,
                                       std::string_view prop) {
    auto val = object_property(doc, handle, prop);
    if (!val)
        return std::nullopt;
    const bool in_ucs = field.def.evaluator_id.rfind("AcObjProp.", 0) == 0;
    if (in_ucs && val->kind == PropKind::Point &&
        (prop == "Center" || prop == "StartPoint" || prop == "EndPoint" || prop == "InsertionPoint" ||
         prop == "Position"))
        val->p = to_current_ucs(doc, handle, val->p);
    return val;
}

std::string format_propval(const DxfDocument &doc, const PropVal &val, std::string_view pic) {
    auto empty = pic.find_first_not_of(" \t") == std::string_view::npos;
    if (val.kind == PropKind::Num || val.kind == PropKind::Angle)
        return empty ? format_fixed6(val.n) : format_number(doc, val.n, pic);
    if (val.kind == PropKind::Point) {
        if (empty)
            return format_fixed6(val.p.x) + ", " + format_fixed6(val.p.y) + ", " + format_fixed6(val.p.z);
        return format_point(doc, val.p, pic);
    }
    if (val.kind == PropKind::Text)
        return text_case_pic(val.text, pic);
    if (pic.find("%lw") == std::string_view::npos)
        return std::to_string(val.weight);
    return format_number(doc, static_cast<double>(val.weight), pic);
}

std::optional<std::string> eval_acobjprop(const DxfDocument &doc, const FieldView &field, std::uint64_t host) {
    auto parts = objprop_parts(field.def.code);
    if (!parts)
        return std::nullopt;
    auto handle = objprop_object(doc, field, host);
    if (!handle) {
        if (field.def.code.find("?BlockRefId") != std::string::npos)
            return text_case_pic(parts->first, parts->second);
        return std::nullopt;
    }
    if (parts->first == "Name")
        if (auto n = named_object_name(doc, *handle))
            return text_case_pic(*n, parts->second);
    auto val = entity_property(doc, field, *handle, parts->first);
    if (!val)
        return std::nullopt;
    return format_propval(doc, *val, parts->second);
}

std::optional<double> objprop_number(const DxfDocument &doc, const FieldView &field, std::uint64_t host) {
    auto parts = objprop_parts(field.def.code);
    if (!parts)
        return std::nullopt;
    auto handle = objprop_object(doc, field, host);
    if (!handle)
        return std::nullopt;
    auto val = entity_property(doc, field, *handle, parts->first);
    if (!val)
        return std::nullopt;
    if (val->kind == PropKind::Num)
        return val->n;
    if (val->kind == PropKind::Point)
        return std::nullopt;
    if (val->kind == PropKind::Text)
        return number_in_text(val->text);
    return std::nullopt;
}

ExprResult nested_number(const DxfDocument &doc, const std::vector<FieldView> &fields, const FieldView &field,
                         const FieldContext &ctx, std::uint64_t host) {
    if (field.def.evaluator_id.rfind("AcObjProp", 0) == 0)
        if (auto v = objprop_number(doc, field, host))
            return {true, Num::from_real(*v)};
    thread_local int nesting = 0;
    if (nesting >= 16) {
        g_expr_error = ExprError::Unresolved;
        return {false, {}};
    }
    ++nesting;
    auto text = eval_field(doc, fields, field, ctx, host);
    --nesting;
    if (!text) {
        g_expr_error = ExprError::Unresolved;
        return {false, {}};
    }
    auto n = number_in_text(*text);
    if (!n) {
        g_expr_error = ExprError::Invalid;
        return {false, {}};
    }
    return {true, Num::from_real(*n)};
}

std::optional<std::string> eval_acexpr(const DxfDocument &doc, const std::vector<FieldView> &fields,
                                       const FieldView &field, const FieldContext &ctx, std::uint64_t host) {
    auto code = field.def.code;
    auto t = code.find_first_not_of(" \t");
    if (t != std::string::npos)
        code.erase(0, t);
    const char *prefix = "\\AcExpr";
    if (code.rfind(prefix, 0) == 0)
        code.erase(0, std::strlen(prefix));
    auto fp = top_level_find(code, "\\f ");
    std::string expr = fp ? code.substr(0, *fp) : code;
    std::string fmt = fp ? format_of(code.substr(*fp + 3)) : std::string{};
    t = expr.find_first_not_of(" \t");
    if (t != std::string::npos)
        expr.erase(0, t);
    auto e = expr.find_last_not_of(" \t");
    if (e != std::string::npos)
        expr.erase(e + 1);
    ExprParser p;
    p.doc = &doc;
    p.all = &fields;
    p.s = expr;
    p.objects = &field.def.references;
    p.table = table_for_host(doc, host);
    p.host_cells = true;
    p.kids = child_fields(fields, field.handle);
    p.ctx = &ctx;
    p.host = host;
    auto v = p.parse_all();
    if (!v.first) {
        if (g_expr_error == ExprError::Invalid)
            return std::string("####");
        return std::nullopt;
    }
    if (!fmt.empty())
        return format_number(doc, v.second.real(), fmt);
    if (p.used_cells)
        return num_str(v.second.real());
    if (v.second.kind == NumKind::Int)
        return std::to_string(v.second.i);
    return format_fixed6(v.second.real());
}
