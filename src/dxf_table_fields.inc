// SPDX-License-Identifier: MPL-2.0
// rust CadDocument table cell fields, refresh_table_block, prepare_table_recompose_data.
namespace {
constexpr const char *kRecomposeKey = "ACDB_RECOMPOSE_DATA";
constexpr const char *kCellStyleMapKey = "ACAD_ROUNDTRIP_2008_TABLESTYLE_CELLSTYLEMAP";
constexpr const char *kRoundtripKey = "ACAD_XREC_ROUNDTRIP";

std::string table_value_display(const TableValue &v) {
    if (!v.formatted.empty())
        return v.formatted;
    if (!v.text.empty())
        return v.text;
    if (v.type == 1 || v.type == 2) {
        if (std::isfinite(v.number) && v.number == std::trunc(v.number) && std::abs(v.number) < 1e15)
            return std::to_string(static_cast<long long>(v.number));
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o << v.number;
        return o.str();
    }
    return {};
}

std::optional<std::string> table_fill_template(std::string_view template_code,
                                               const std::vector<FieldDefinition> &children) {
    std::vector<std::string> shown;
    shown.reserve(children.size());
    for (const auto &child : children) {
        auto shown_value = table_value_display(child.value);
        shown.push_back(shown_value.empty() ? child.value_string : shown_value);
    }
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
        out += shown[idx];
        rest = after.substr(end + 2);
    }
}

std::string table_key_number(double v) {
    if (v == 0.0)
        v = 0.0;
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::fixed << std::setprecision(9) << (v + 0.0);
    return o.str();
}

int table_attachment(int align) {
    switch (align) {
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 3;
    case 4:
        return 4;
    case 6:
        return 6;
    case 7:
        return 7;
    case 8:
        return 8;
    case 9:
        return 9;
    default:
        return 5;
    }
}

std::string table_color_key(Color c) {
    if (c.kind() == Color::Kind::ByBlock)
        return "ByBlock";
    if (c.kind() == Color::Kind::ByLayer)
        return "ByLayer";
    if (c.kind() == Color::Kind::None)
        return "None";
    if (auto rgb = c.to_true_color_value())
        return "Rgb" + std::to_string(*rgb);
    if (auto i = c.index())
        return "Index" + std::to_string(*i);
    return "Other";
}

bool table_has_class(const DxfDocument &doc, std::string_view name) {
    for (const auto &c : doc.classes())
        if (c.name == name)
            return true;
    return false;
}

void table_ensure_field_classes(DxfDocument &doc) {
    const std::pair<const char *, const char *> classes[] = {{"FIELD", "AcDbField"},
                                                             {"FIELDLIST", "AcDbFieldList"}};
    for (const auto &item : classes) {
        if (table_has_class(doc, item.first))
            continue;
        ClassDefinition c;
        c.name = item.first;
        c.cpp_name = item.second;
        c.proxy_flags = 1152;
        doc.set_class(c);
    }
}

std::optional<TableDefinition> table_definition_of(const DxfDocument &doc, std::uint64_t table) {
    std::optional<TableDefinition> out;
    doc.for_each<DxfTable>([&](const auto &t) {
        if (handle_value(t.handle()) == table)
            out = t.definition();
    });
    return out;
}

bool table_set_definition(DxfDocument &doc, std::uint64_t table, const TableDefinition &v) {
    bool ok = false;
    doc.for_each<DxfTable>([&](auto &t) {
        if (handle_value(t.handle()) == table) {
            t.set_definition(v);
            ok = true;
        }
    });
    return ok;
}

std::vector<std::uint64_t> table_with_child_fields(const DxfDocument &doc, const std::vector<std::uint64_t> &fields) {
    std::vector<std::uint64_t> all = fields;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> owners;
    std::vector<std::pair<std::uint64_t, std::vector<std::uint64_t>>> kids;
    doc.for_each<DxfField>([&](const auto &f) {
        auto h = handle_value(f.handle());
        owners.emplace_back(h, handle_value(f.owner_handle()));
        kids.emplace_back(h, f.definition().children);
    });
    for (std::size_t i = 0; i < all.size(); ++i) {
        auto h = all[i];
        for (const auto &link : owners)
            if (link.second == h && std::find(all.begin(), all.end(), link.first) == all.end())
                all.push_back(link.first);
        for (const auto &item : kids) {
            if (item.first != h)
                continue;
            for (auto child : item.second)
                if (std::find(all.begin(), all.end(), child) == all.end())
                    all.push_back(child);
        }
    }
    return all;
}

std::uint64_t table_ensure_field_list(DxfDocument &doc) {
    auto nod = doc.root_dictionary_handle();
    auto existing = doc.dictionary_entry(nod, "ACAD_FIELDLIST");
    if (!existing.empty())
        return handle_value(existing);
    std::uint64_t found = 0;
    doc.for_each<DxfFieldList>([&](const auto &list) {
        if (!found)
            found = handle_value(list.handle());
    });
    if (found)
        return found;
    FieldListDefinition v;
    return doc.add_field_list(nod, "ACAD_FIELDLIST", v);
}

void table_register_fields(DxfDocument &doc, std::uint64_t list, const std::vector<std::uint64_t> &fields) {
    doc.for_each<DxfFieldList>([&](auto &item) {
        if (handle_value(item.handle()) != list)
            return;
        auto v = item.definition();
        v.fields.insert(v.fields.end(), fields.begin(), fields.end());
        item.set_definition(v);
    });
}

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

std::string_view code_word(std::string_view code, std::size_t n) {
    auto s = trim(code);
    while (!s.empty() && s.front() == '\\')
        s.remove_prefix(1);
    s = trim(s);
    std::size_t i = 0;
    while (!s.empty()) {
        auto e = s.find_first_of(" \t");
        auto word = e == std::string_view::npos ? s : s.substr(0, e);
        if (i == n)
            return word;
        if (e == std::string_view::npos)
            return {};
        s = trim(s.substr(e));
        ++i;
    }
    return {};
}

std::string code_evaluator(std::string_view code) { return std::string(code_word(code, 0)); }

std::string code_format(std::string_view code) {
    auto text = std::string(code);
    auto p = text.find("\\f ");
    if (p == std::string::npos)
        return {};
    auto rest = trim(std::string_view(text).substr(p + 3));
    while (!rest.empty() && rest.front() == '"')
        rest.remove_prefix(1);
    while (!rest.empty() && rest.back() == '"')
        rest.remove_suffix(1);
    return std::string(rest);
}

std::string renumber_objects(std::string_view code, const std::vector<std::uint64_t> &from,
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
            j = static_cast<std::size_t>(std::stoul(std::string(trim(after.substr(0, end)))));
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

std::pair<FieldDefinition, std::vector<FieldDefinition>> text_split_operands(FieldDefinition field) {
    if (field.evaluator_id.empty())
        field.evaluator_id = code_evaluator(field.code);
    if (field.evaluator_id != "AcExpr")
        return {std::move(field), {}};
    std::string code;
    std::vector<FieldDefinition> operands;
    std::string rest = field.code;
    for (;;) {
        auto at = rest.find("%<");
        if (at == std::string::npos)
            break;
        auto len = field_marker_len(std::string_view(rest).substr(at));
        std::optional<std::string> inner;
        if (len) {
            auto raw = trim(std::string_view(rest).substr(at + 2, *len - 4));
            if (!raw.empty() && raw.front() == '\\' && (raw.size() < 2 || raw[1] != '_'))
                inner = std::string(raw);
        }
        if (!inner) {
            code += rest.substr(0, at + 2);
            rest = rest.substr(at + 2);
            continue;
        }
        code += rest.substr(0, at);
        code += "%<\\_FldIdx ";
        code += std::to_string(operands.size());
        code += ">%";
        std::vector<std::uint64_t> objects;
        auto numbered = renumber_objects(*inner, field.references, objects);
        FieldDefinition operand;
        operand.evaluator_id = code_evaluator(numbered);
        operand.code = numbered;
        operand.references = std::move(objects);
        operand.format = code_format(numbered);
        operand.value.flags = 4;
        operand.evaluation_option =
            operand.evaluator_id == "AcVar" && code_word(operand.code, 1) == "Date" ? 32 : 63;
        operands.push_back(std::move(operand));
        rest = rest.substr(at + *len);
    }
    if (operands.empty())
        return {std::move(field), {}};
    code += rest;
    std::vector<std::uint64_t> objects;
    field.code = renumber_objects(code, field.references, objects);
    field.references = std::move(objects);
    return {std::move(field), std::move(operands)};
}

void add_owned_field_tree(DxfDocument &doc, std::uint64_t parent, FieldDefinition src) {
    auto split = text_split_operands(std::move(src));
    auto id = doc.add_field_child(hex_handle(parent), split.first);
    for (auto &op : split.second)
        add_owned_field_tree(doc, id, std::move(op));
}

void collect_owned_field_handles(const DxfDocument &doc, std::uint64_t parent, std::vector<std::uint64_t> &out) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> links;
    doc.for_each<DxfField>([&](const auto &f) {
        links.emplace_back(handle_value(f.handle()), handle_value(f.owner_handle()));
    });
    std::vector<std::uint64_t> pending{parent};
    std::unordered_set<std::uint64_t> seen;
    while (!pending.empty()) {
        auto p = pending.back();
        pending.pop_back();
        for (const auto &link : links) {
            if (link.second != p || seen.count(link.first))
                continue;
            seen.insert(link.first);
            out.push_back(link.first);
            pending.push_back(link.first);
        }
    }
}

EntityOptions table_block_entity_options(std::string block_name, Color color = Color::by_block()) {
    EntityOptions o;
    o.block_name = std::move(block_name);
    o.layer = "0";
    o.color = color;
    o.linetype = "ByBlock";
    o.line_weight = LineWeight::by_block();
    return o;
}

struct TableBlockPlan {
    struct Text {
        std::size_t row = 0, column = 0;
        MTextDefinition mtext;
        Color color = Color::by_block();
        bool field = false;
    };
    struct Line {
        Point3 a{}, b{};
        std::string layer = "0";
        Color color = Color::by_block();
        bool invisible = false;
        LineWeight weight = LineWeight::by_block();
    };
    struct Fill {
        std::vector<Point3> corners;
        Color color = Color::by_block();
    };
    std::vector<Text> texts;
    std::vector<Line> lines;
    std::vector<Fill> fills;
};

TableBlockPlan table_block_plan(const DxfDocument &doc, const TableDefinition &t) {
    TableBlockPlan plan;
    TableStyleDefinition style;
    bool has_style = false;
    if (t.style)
        doc.for_each<DxfTableStyle>([&](const auto &s) {
            if (handle_value(s.handle()) == t.style) {
                style = s.definition();
                has_style = true;
            }
        });
    const double margin = has_style ? style.horizontal_margin : 0.06;
    const double vmargin = has_style ? style.vertical_margin : 0.06;
    const bool title = !t.overrides.title_suppressed && !(has_style && style.title_suppressed);
    const bool header = !t.overrides.header_suppressed && !(has_style && style.header_suppressed);
    auto row_style = [&](std::size_t r) {
        int kind = 2;
        if (title && r == 0)
            kind = 0;
        else if (header && r == static_cast<std::size_t>(title ? 1 : 0))
            kind = 1;
        if (!has_style)
            return std::pair<double, int>{0.18, 5};
        const TableRowStyle *rows[3] = {&style.title, &style.header, &style.data};
        return std::pair<double, int>{rows[kind]->text_height, rows[kind]->alignment};
    };
    const auto nr = t.row_heights.size(), nc = t.column_widths.size();
    std::vector<double> xs{0}, ys{0};
    double ax = 0, ay = 0;
    for (auto w : t.column_widths) {
        ax += w;
        xs.push_back(ax);
    }
    for (auto h : t.row_heights) {
        ay += h;
        ys.push_back(ay);
    }
    std::vector<std::vector<std::pair<std::size_t, std::size_t>>> owner(nr);
    std::vector<std::vector<std::pair<std::size_t, std::size_t>>> span(nr);
    for (std::size_t r = 0; r < nr; ++r) {
        owner[r].resize(nc);
        span[r].resize(nc, {1, 1});
        for (std::size_t c = 0; c < nc; ++c)
            owner[r][c] = {r, c};
    }
    auto mark = [&](std::size_t r, std::size_t c, std::size_t w, std::size_t h) {
        if (r >= nr || c >= nc || owner[r][c] != std::make_pair(r, c) || (w <= 1 && h <= 1))
            return;
        auto ww = std::min(w, nc - c), hh = std::min(h, nr - r);
        span[r][c] = {ww, hh};
        for (std::size_t rr = r; rr < r + hh; ++rr)
            for (std::size_t cc = c; cc < c + ww; ++cc)
                owner[rr][cc] = {r, c};
    };
    for (const auto &range : t.merged_ranges)
        mark(range.top, range.left, range.right - range.left + 1, range.bottom - range.top + 1);
    for (std::size_t r = 0; r < t.cells.size(); ++r)
        for (std::size_t c = 0; c < t.cells[r].size() && c < nc; ++c) {
            const auto &cell = t.cells[r][c];
            auto w = static_cast<std::size_t>(std::max<int>(1, cell.merge_width));
            auto h = static_cast<std::size_t>(std::max<int>(1, cell.merge_height));
            mark(r, c, w, h);
        }
    for (std::size_t r = 0; r < t.cells.size() && r < nr; ++r)
        for (std::size_t c = 0; c < nc; ++c) {
            if (owner[r][c] != std::make_pair(r, c))
                continue;
            const TableCell *cell = c < t.cells[r].size() ? &t.cells[r][c] : nullptr;
            const auto flags = cell && cell->style ? cell->style->property_flags : 0;
            if (cell && cell->style && (flags & 512) && cell->style->fill_enabled) {
                auto w = span[r][c].first, h = span[r][c].second;
                TableBlockPlan::Fill fill;
                fill.corners = {{xs[c], -ys[r], 0},
                                {xs[c + w], -ys[r], 0},
                                {xs[c], -ys[r + h], 0},
                                {xs[c + w], -ys[r + h], 0}};
                fill.color = cell->style->background_color;
                plan.fills.push_back(std::move(fill));
            }
            std::string text = cell && cell->value ? table_value_display(*cell->value) : std::string{};
            bool has_field = cell && cell->field;
            if (text.empty() && !has_field)
                continue;
            auto rs = row_style(r);
            auto height = rs.first;
            auto align = rs.second;
            if (cell && cell->style && (flags & 16) && cell->style->alignment >= 1 && cell->style->alignment <= 9)
                align = cell->style->alignment;
            if (cell && cell->style && (flags & 128) && cell->style->text_height > 0)
                height = cell->style->text_height;
            auto w = span[r][c].first, h = span[r][c].second;
            auto x0 = xs[c], x1 = xs[c + w], y0 = ys[r], y1 = ys[r + h];
            auto a = static_cast<std::size_t>(std::max(1, std::min(align, 9)) - 1);
            double x = (a % 3 == 0) ? x0 + margin : (a % 3 == 1) ? (x0 + x1) / 2.0 : x1 - margin;
            double y = (a / 3 == 0) ? -(y0 + vmargin) : (a / 3 == 1) ? -(y0 + y1) / 2.0 : -(y1 - vmargin);
            TableBlockPlan::Text item;
            item.row = r;
            item.column = c;
            item.mtext.text = std::move(text);
            item.mtext.style = "Standard";
            if (cell && cell->style && (flags & 64) && !cell->style->text_style.empty())
                item.mtext.style = cell->style->text_style;
            item.mtext.insertion = {x, y, 0};
            item.mtext.height = height;
            item.mtext.rectangle_width = (x1 - x0) - 2.0 * margin;
            item.mtext.attachment = static_cast<std::int16_t>(table_attachment(align));
            item.mtext.drawing_direction = 5;
            item.color = Color::by_block();
            if (cell && cell->style && (flags & 32))
                item.color = cell->style->content_color;
            item.field = has_field;
            plan.texts.push_back(std::move(item));
        }
    auto add_line = [&](Point3 a, Point3 b) {
        TableBlockPlan::Line line;
        line.a = a;
        line.b = b;
        plan.lines.push_back(line);
    };
    for (std::size_t k = 0; k <= nr; ++k) {
        auto open = [&](std::size_t c) {
            return k == 0 || k == nr || owner[k - 1][c] != owner[k][c];
        };
        std::size_t c = 0;
        while (c < nc) {
            if (!open(c)) {
                ++c;
                continue;
            }
            auto start = c;
            while (c < nc && open(c))
                ++c;
            add_line({xs[start], -ys[k], 0}, {xs[c], -ys[k], 0});
        }
    }
    for (std::size_t k = 0; k <= nc; ++k) {
        auto open = [&](std::size_t r) {
            return k == 0 || k == nc || owner[r][k - 1] != owner[r][k];
        };
        std::size_t r = 0;
        while (r < nr) {
            if (!open(r)) {
                ++r;
                continue;
            }
            auto start = r;
            while (r < nr && open(r))
                ++r;
            add_line({xs[k], -ys[start], 0}, {xs[k], -ys[r], 0});
        }
    }
    TableBlockPlan::Line point;
    point.layer = "Defpoints";
    point.color = Color::from_index(8);
    point.invisible = true;
    point.weight = LineWeight::from_value(0);
    plan.lines.push_back(point);
    return plan;
}

std::string table_plan_text_key(const TableBlockPlan::Text &t) {
    return "T " + table_key_number(t.mtext.insertion.x) + " " + table_key_number(t.mtext.insertion.y) + " " +
           table_key_number(t.mtext.height) + " " + table_key_number(t.mtext.rectangle_width) + " " +
           std::to_string(t.mtext.attachment) + " " + table_color_key(t.color) + " " + t.mtext.style + " " +
           (t.field ? std::string{} : t.mtext.text);
}

std::string table_plan_line_key(const TableBlockPlan::Line &l) {
    return "L " + table_key_number(l.a.x) + " " + table_key_number(l.a.y) + " " + table_key_number(l.b.x) + " " +
           table_key_number(l.b.y) + " " + l.layer;
}

std::string table_plan_fill_key(const TableBlockPlan::Fill &f) {
    auto c0 = f.corners[0], c3 = f.corners[3];
    return "S " + table_key_number(c0.x) + " " + table_key_number(c0.y) + " " + table_key_number(c3.x) + " " +
           table_key_number(c3.y) + " " + table_color_key(f.color);
}
} // namespace

std::optional<std::uint64_t> DxfDocument::set_table_cell_field(std::uint64_t table, std::size_t row, std::size_t column,
                                                               std::string_view template_code,
                                                               const std::vector<FieldDefinition> &children) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Table cell fields require R2007+");
    auto def = table_definition_of(*this, table);
    if (!def || row >= def->cells.size() || column >= def->cells[row].size())
        return std::nullopt;
    auto display = table_fill_template(template_code, children);
    if (!display)
        return std::nullopt;
    DxfDocument staged = *this;
    staged.remove_table_cell_field(table, row, column);
    table_ensure_field_classes(staged);
    auto list = table_ensure_field_list(staged);
    FieldDefinition container;
    container.evaluator_id = "_text";
    container.code = std::string(template_code);
    container.evaluation_option = 63;
    container.state = 9;
    container.evaluation_status = 2;
    container.value.flags = 3;
    DxfField object;
    static_cast<DxfRecord &>(object) = staged.make_record("FIELD");
    object.set_field("", 330, hex_handle(table));
    object.pairs_.push_back({100, "AcDbField"});
    object.set_definition(container);
    auto id = staged.insert_object(object);
    for (const auto &child : children)
        add_owned_field_tree(staged, id, child);
    auto next = table_definition_of(staged, table);
    if (!next)
        return std::nullopt;
    auto &cell = next->cells[row][column];
    TableValue value;
    value.type = 4;
    value.text = *display;
    value.formatted = *display;
    cell.value = value;
    cell.field = id;
    cell.type = 1;
    table_set_definition(staged, table, *next);
    std::vector<std::uint64_t> registered{id};
    collect_owned_field_handles(staged, id, registered);
    table_register_fields(staged, list, registered);
    *this = std::move(staged);
    return id;
}

bool DxfDocument::remove_table_cell_field(std::uint64_t table, std::size_t row, std::size_t column) {
    auto def = table_definition_of(*this, table);
    if (!def || row >= def->cells.size() || column >= def->cells[row].size())
        return false;
    auto field = def->cells[row][column].field;
    if (!field)
        return false;
    def->cells[row][column].field = 0;
    DxfDocument staged = *this;
    table_set_definition(staged, table, *def);
    auto doomed = table_with_child_fields(staged, {field});
    std::unordered_set<std::uint64_t> drop(doomed.begin(), doomed.end());
    staged.for_each<DxfFieldList>([&](auto &list) {
        auto v = list.definition();
        v.fields.erase(std::remove_if(v.fields.begin(), v.fields.end(),
                                      [&](std::uint64_t h) { return drop.count(h); }),
                       v.fields.end());
        list.set_definition(v);
    });
    std::vector<Record> kept;
    kept.reserve(staged.records_.size());
    for (std::size_t i = 0; i < staged.records_.size(); ++i) {
        const auto &r = staged.record_at({i, std::nullopt, false});
        if (r.type() == "FIELD" && drop.count(handle_value(r.handle())))
            continue;
        kept.push_back(std::move(staged.records_[i]));
    }
    staged.records_ = std::move(kept);
    staged.rebuild_indexes();
    *this = std::move(staged);
    return true;
}

std::pair<bool, std::size_t> DxfDocument::update_table_cell_fields(const FieldContext &ctx, std::uint64_t table) {
    auto def = table_definition_of(*this, table);
    if (!def)
        return {false, 0};
    std::vector<std::uint64_t> pending;
    for (const auto &row : def->cells)
        for (const auto &cell : row)
            if (cell.field)
                pending.push_back(cell.field);
    std::unordered_set<std::uint64_t> seen;
    std::vector<std::pair<std::uint64_t, FieldDefinition>> updates;
    std::size_t found = 0;
    while (!pending.empty()) {
        auto handle = pending.back();
        pending.pop_back();
        if (!seen.insert(handle).second)
            continue;
        FieldDefinition stored;
        bool have = false;
        for_each<DxfField>([&](const auto &f) {
            if (handle_value(f.handle()) == handle) {
                stored = f.definition();
                have = true;
            }
        });
        if (!have)
            continue;
        pending.insert(pending.end(), stored.children.begin(), stored.children.end());
        if (stored.evaluator_id != "_text")
            ++found;
        auto fresh = resolve_field_handle(*this, handle, table, ctx);
        if (!fresh)
            continue;
        if (stored.evaluator_id.rfind("AcSm", 0) == 0 && *fresh == "####")
            continue;
        FieldDefinition next = stored;
        double number = 0;
        bool numeric = false;
        try {
            std::size_t used = 0;
            number = std::stod(*fresh, &used);
            numeric = used == fresh->size();
        } catch (...) {
            numeric = false;
        }
        const auto lo = static_cast<double>(std::numeric_limits<std::int32_t>::min());
        const auto hi = static_cast<double>(std::numeric_limits<std::int32_t>::max());
        if (numeric && (stored.value.type == 1 || stored.value.type == 2 || stored.value.type == 0)) {
            if (next.value.type == 1 && (number != std::trunc(number) || number < lo || number > hi))
                next.value.type = 2;
            else if (next.value.type == 0)
                next.value.type = 2;
            next.value.number = number;
            next.value.text = *fresh;
            next.value.formatted = *fresh;
        } else {
            next.value.type = 4;
            next.value.text = *fresh;
            next.value.formatted = *fresh;
        }
        next.value_string = *fresh;
        next.value_string_length = static_cast<std::int32_t>(fresh->size());
        next.evaluation_status = 2;
        if (next.value.formatted != stored.value.formatted || next.value.text != stored.value.text ||
            next.value_string != stored.value_string)
            updates.push_back({handle, std::move(next)});
    }
    if (updates.empty())
        return {false, found};
    DxfDocument staged = *this;
    for (const auto &item : updates)
        staged.set_field_definition(hex_handle(item.first), item.second);
    *this = std::move(staged);
    return {true, found};
}

bool DxfDocument::refresh_table_block(std::uint64_t table) {
    auto def = table_definition_of(*this, table);
    if (!def)
        return false;
    auto plan = table_block_plan(*this, *def);
    std::string block_name = def->block_name;
    std::uint64_t block_handle = def->block_record;
    if (block_name.empty() && block_handle) {
        if (const auto *block = block_record(hex_handle(block_handle)))
            block_name = block->name();
    }
    auto collect_now = [&](const DxfDocument &doc) {
        std::vector<std::string> now;
        if (block_name.empty())
            return now;
        std::uint64_t owner = block_handle;
        if (!owner)
            doc.for_each<DxfBlockRecord>([&](const auto &b) {
                if (ascii_equal(b.name(), block_name))
                    owner = handle_value(b.handle());
            });
        doc.for_each<DxfMText>([&](const auto &m) {
            if (handle_value(m.owner_handle()) != owner)
                return;
            auto d = m.definition();
            bool hide = !doc.extension_dictionary_handle(m.handle()).empty();
            now.push_back("T " + table_key_number(d.insertion.x) + " " + table_key_number(d.insertion.y) + " " +
                          table_key_number(d.height) + " " + table_key_number(d.rectangle_width) + " " +
                          std::to_string(d.attachment) + " " + table_color_key(m.color()) + " " + d.style + " " +
                          (hide ? std::string{} : d.text));
        });
        doc.for_each<DxfLine>([&](const auto &line) {
            if (handle_value(line.owner_handle()) != owner)
                return;
            auto a = line.start(), b = line.end();
            now.push_back("L " + table_key_number(a.x) + " " + table_key_number(a.y) + " " + table_key_number(b.x) +
                          " " + table_key_number(b.y) + " " + line.layer());
        });
        doc.for_each<DxfSolid>([&](const auto &s) {
            if (handle_value(s.owner_handle()) != owner)
                return;
            auto c0 = s.corner(0), c3 = s.corner(3);
            now.push_back("S " + table_key_number(c0.x) + " " + table_key_number(c0.y) + " " + table_key_number(c3.x) +
                          " " + table_key_number(c3.y) + " " + table_color_key(s.color()));
        });
        return now;
    };
    std::vector<std::string> want;
    for (const auto &t : plan.texts)
        want.push_back(table_plan_text_key(t));
    for (const auto &l : plan.lines)
        want.push_back(table_plan_line_key(l));
    for (const auto &f : plan.fills)
        want.push_back(table_plan_fill_key(f));
    auto left = collect_now(*this), right = want;
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    if (!block_name.empty() && left == right)
        return false;
    DxfDocument staged = *this;
    if (block_name.empty()) {
        std::size_t n = 1;
        do {
            block_name = "*T" + std::to_string(n++);
        } while (staged.get_symbol(SymbolTable::BlockRecord, block_name));
        def->block_name = block_name;
        def->block_record = staged.add_symbol_impl(SymbolTable::BlockRecord, block_name, {}, true);
        block_handle = def->block_record;
        staged.for_each<DxfBlock>([&](auto &b) {
            if (b.name() == block_name)
                b.set_flags(b.flags() | 1);
        });
        table_set_definition(staged, table, *def);
    }
    if (!staged.get_layer("Defpoints"))
        staged.add_layer("Defpoints", 8);
    auto old = staged.entity_handles_in_block(block_name);
    for (const auto &handle : old)
        staged.remove_entity(handle);
    auto opts = [&](Color color = Color::by_block()) { return table_block_entity_options(block_name, color); };
    for (const auto &fill : plan.fills)
        staged.add_solid(fill.corners, opts(fill.color));
    for (const auto &text : plan.texts) {
        auto o = opts(text.color);
        o.linetype.clear();
        o.line_weight = LineWeight::by_layer();
        staged.add_mtext(text.mtext, o);
    }
    for (const auto &line : plan.lines) {
        auto o = opts(line.color);
        o.layer = line.layer;
        o.invisible = line.invisible;
        o.line_weight = line.weight;
        if (line.layer == "Defpoints")
            o.linetype.clear();
        staged.add_line(line.a, line.b, o);
    }
    *this = std::move(staged);
    return true;
}

std::optional<std::vector<std::optional<std::uint64_t>>>
DxfDocument::build_table_block(std::uint64_t table, const std::vector<TableCellField> &fields) {
    if (!table_definition_of(*this, table))
        return std::nullopt;
    std::vector<std::optional<std::uint64_t>> out;
    out.reserve(fields.size());
    for (const auto &field : fields)
        out.push_back(set_table_cell_field(table, field.row, field.column, field.template_code, field.children));
    refresh_table_block(table);
    return out;
}

void DxfDocument::prepare_table_recompose_data() {
    std::vector<std::uint64_t> styles;
    for_each<DxfTableStyle>([&](const auto &style) {
        auto dict = extension_dictionary_handle(style.handle());
        if (dict.empty())
            return;
        if (!dictionary_entry(dict, kCellStyleMapKey).empty())
            styles.push_back(handle_value(style.handle()));
    });
    std::sort(styles.begin(), styles.end());
    std::vector<std::uint64_t> targets = styles;
    for_each<DxfTable>([&](const auto &table) {
        auto dict = extension_dictionary_handle(table.handle());
        if (dict.empty())
            return;
        if (!dictionary_entry(dict, kRoundtripKey).empty())
            targets.push_back(handle_value(table.handle()));
    });
    auto nod = root_dictionary_handle();
    auto existing = dictionary_entry(nod, kRecomposeKey);
    std::vector<std::pair<int, std::string>> wanted{{90, "1"}};
    for (auto h : targets)
        wanted.push_back({330, hex_handle(h)});
    auto payload = [&](const DxfRecord &record) {
        std::vector<std::pair<int, std::string>> out;
        bool active = false;
        for (const auto &p : record.pairs_) {
            if (p.code == 100 && p.value == "AcDbXrecord")
                active = true;
            else if (active && (p.code == 90 || p.code == 330))
                out.push_back({p.code, p.value});
        }
        return out;
    };
    if (targets.empty() && existing.empty())
        return;
    if (!existing.empty()) {
        if (targets.empty()) {
            DxfDocument staged = *this;
            auto &dict = staged.mutable_object(handle_value(nod));
            std::optional<std::size_t> key;
            dict.locate_if("AcDbDictionary", [&](std::size_t i, int code) {
                if (code == 3 && detail::symbol_name_key(dict.decode_string(dict.pairs_[i].value)) ==
                                     detail::symbol_name_key(kRecomposeKey)) {
                    key = i;
                    return true;
                }
                return false;
            });
            if (key) {
                auto handle_at = *key + 1;
                if (handle_at < dict.pairs_.size() &&
                    (dict.pairs_[handle_at].code == 350 || dict.pairs_[handle_at].code == 360)) {
                    auto target = handle_value(dict.pairs_[handle_at].value);
                    dict.pairs_.erase(dict.pairs_.begin() + static_cast<std::ptrdiff_t>(handle_at));
                    dict.pairs_.erase(dict.pairs_.begin() + static_cast<std::ptrdiff_t>(*key));
                    if (target) {
                        const auto found = staged.handle_index_.find(target);
                        if (found != staged.handle_index_.end() && found->second.size() == 1 &&
                            !found->second.front().vertex && !found->second.front().seqend)
                            staged.records_.erase(staged.records_.begin() +
                                                  static_cast<std::ptrdiff_t>(found->second.front().record));
                    }
                }
            }
            staged.rebuild_indexes();
            *this = std::move(staged);
            return;
        }
        if (payload(*get_record(existing)) == wanted)
            return;
        DxfDocument staged = *this;
        auto &record = staged.mutable_object(handle_value(existing));
        std::size_t begin = record.pairs_.size();
        for (std::size_t i = 0; i < record.pairs_.size(); ++i)
            if (record.pairs_[i].code == 100 && record.pairs_[i].value == "AcDbXrecord") {
                begin = i + 1;
                if (begin < record.pairs_.size() && record.pairs_[begin].code == 280)
                    ++begin;
                break;
            }
        record.pairs_.erase(record.pairs_.begin() + static_cast<std::ptrdiff_t>(begin), record.pairs_.end());
        for (const auto &item : wanted)
            record.pairs_.push_back({item.first, item.second});
        record.set_short_field("AcDbXrecord", 280, 1);
        staged.rebuild_indexes();
        *this = std::move(staged);
        return;
    }
    std::vector<SymbolProperty> values;
    values.push_back({90, "1"});
    for (auto h : targets)
        values.push_back({330, hex_handle(h)});
    DxfDocument staged = *this;
    auto id = staged.add_xrecord(nod, kRecomposeKey, values);
    staged.mutable_object(id).set_short_field("AcDbXrecord", 280, 1);
    *this = std::move(staged);
}

std::vector<std::uint64_t> DxfDocument::orphan_table_fields(std::uint64_t table) const {
    auto def = table_definition_of(*this, table);
    if (!def)
        return {};
    std::unordered_set<std::uint64_t> shown;
    for (const auto &row : def->cells)
        for (const auto &cell : row)
            if (cell.field)
                shown.insert(cell.field);
    std::vector<std::uint64_t> leftover;
    for_each<DxfField>([&](const auto &f) {
        if (handle_value(f.owner_handle()) != table)
            return;
        auto h = handle_value(f.handle());
        if (!shown.count(h))
            leftover.push_back(h);
    });
    return table_with_child_fields(*this, leftover);
}

std::vector<std::uint64_t> DxfDocument::prune_table_fields(std::uint64_t table) {
    auto doomed = orphan_table_fields(table);
    if (!doomed.empty())
        remove_fields(doomed);
    return doomed;
}
