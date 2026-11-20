// SPDX-License-Identifier: MPL-2.0
// acadrust section_writer.rs: write_table_cell, write_field_object_dxf,
// write_cell_value_dxf_masked and write_table_modern_content_dxf.
using CellFields = std::vector<DxfRawField>;

// Snapshot the effective styles before rewriting linked content values.
std::map<std::string, std::vector<int>> legacy_table_bits_;
void collect_legacy_table_styles() {
    doc_.for_each<DxfTable>([&](DxfTable &table) {
        auto merged = doc_.table_content_definition(table.handle());
        auto base = table.definition();
        if (report_.source >= V::AC1021 && target() < V::AC1024) {
            doc_.project_legacy_table_cells(base, merged);
            // R2007 already uses legacy cell overrides and still has the modern value frame.
            table.version_ = V::AC1021;
            table.set_definition(base);
        }
        std::size_t rowidx = 0;
        auto &bits = legacy_table_bits_[table.handle()];
        for (auto &row : merged.rows) {
            std::size_t col = 0;
            for (auto &cell : row.cells) {
                auto p = cell.format ? cell.format->properties : 0u;
                const auto &style = base.cells.at(rowidx).at(col++).style;
                if (style)
                    p |= style->property_flags;
                const int overrides = target() < V::AC1024 && style ? style->overrides : 0;
                bits.push_back(overrides | ((p & 16) ? 1 : 0) | ((p & 512) ? 6 : 0) |
                               ((p & 32) ? 8 : 0) | ((p & 64) ? 16 : 0) | ((p & 128) ? 32 : 0));
            }
            ++rowidx;
        }
    });
}
void legacy_table_record(DxfRecord &r) {
    if (report_.source < V::AC1021)
        return; // Both versions use the old layout.
    const bool field = r.type() == "FIELD", table = r.type() == "ACAD_TABLE";
    const bool linked = one_of(r.type(), {"TABLECONTENT", "TABLETEMPLATE"});
    const bool datalink = r.type() == "DATALINK";
    if (!field && !table && !linked && !datalink)
        return; // FIELDLIST and cache/style links are version-independent.
    const auto scope = field      ? "AcDbField"
                       : table    ? "AcDbTable"
                       : datalink ? "AcDbDataLink"
                                  : "AcDbLinkedTableData";
    const auto input = r.subclass_fields(scope);
    CellFields out;
    bool cell = false;
    int cell_type = 0;
    std::size_t cell_index = 0;
    std::string own_format;
    std::size_t format_pos = std::string::npos, eval_pos = std::string::npos;
    const auto loss = [&](int group, const char *message) {
        issue(Kind::Loss, "legacy-table-field-loss", &r, group, message);
    };
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto &p = input[i];
        if (p.code == 102) { // Copy private groups without interpreting their contents.
            int depth = 0;
            do {
                const auto &q = input.at(i);
                if (q.code == 102 && !q.value.empty() && q.value.front() == '{')
                    ++depth;
                else if (q.code == 102 && q.value == "}")
                    --depth;
                if (depth < 0)
                    throw std::invalid_argument("Malformed application group");
                out.push_back(q);
                if (!depth)
                    break;
                ++i;
            } while (true);
            continue;
        }
        if (table && p.code == 171) {
            cell = true;
            ++cell_index;
            cell_type = std::stoi(p.value);
        }
        if (table && cell && p.code == 91) {
            auto flags = std::stoll(p.value) | legacy_table_bits_.at(r.handle()).at(cell_index - 1);
            if (flags < INT16_MIN || flags > INT16_MAX)
                loss(91, "Legacy group 177 retains only the low 16 override bits");
            auto low = static_cast<unsigned>(flags) & 65535u;
            out.push_back({177, std::to_string(low < 32768 ? int(low) : int(low) - 65536)});
            continue;
        }
        if (table && cell && p.code == 92) {
            if (std::stoll(p.value))
                loss(92, "Extended cell state is unavailable before R2007");
            continue;
        }
        const bool marker = (table && cell && p.code == 301 && p.value == "CELL_VALUE") ||
                            (field && (p.code == 6 || p.code == 7)) ||
                            (datalink && p.code == 301 && p.value == "DATAMAP_VALUE") ||
                            (linked && ((p.code == 301 && p.value == "DATAMAP_VALUE") ||
                                        (p.code == 300 && p.value == "VALUE")));
        if (!marker) {
            if (field && p.code == 4)
                format_pos = out.size();
            if (field && p.code == 91 && eval_pos == std::string::npos)
                eval_pos = out.size();
            if (field && p.code == 93 && i + 1 < input.size() && input[i + 1].code == 90)
                throw std::invalid_argument("Unkeyed inline FIELD value requires separate conversion");
            out.push_back(p);
            continue;
        }
        if (!table)
            out.push_back(p);
        int type = -1, flags = 0, unit = 0;
        std::string text, formatted, format;
        CellFields body;
        for (++i; i < input.size() && !(input[i].code == 304 && input[i].value == "ACVALUE_END"); ++i) {
            const auto &q = input[i];
            switch (q.code) {
            case 93:
                flags = std::stoi(q.value);
                break;
            case 90:
                if (type != -1)
                    throw std::invalid_argument("Duplicate ACVALUE type");
                type = std::stoi(q.value);
                break;
            case 94:
                unit = std::stoi(q.value);
                break;
            case 302:
                formatted = q.value;
                break;
            case 300:
                format = q.value;
                break;
            case 1:
            case 2:
                text += q.value;
                body.push_back(q);
                break;
            case 91:
            case 92:
            case 140:
            case 310:
            case 11:
            case 21:
            case 31:
            case 330:
                body.push_back(q);
                break;
            default:
                throw std::invalid_argument("Unreviewed data in ACVALUE body");
            }
        }
        if (i == input.size() || type < 0)
            throw std::invalid_argument("Incomplete ACVALUE");
        if (table) {
            if (type != 4 || flags || unit || !format.empty() || !formatted.empty() || cell_type != 1)
                loss(301, "Typed cell value projected to legacy display text or block "
                          "reference");
            if (cell_type == 1) {
                const auto &display = text.empty() ? formatted : text;
                std::size_t start = 0;
                while (display.size() - start > 250) {
                    std::size_t end = start + 250;
                    while (end > start && (static_cast<unsigned char>(display[end]) & 0xc0) == 0x80)
                        --end;
                    if (end == start)
                        throw std::invalid_argument("Invalid UTF-8 cell text");
                    out.push_back({2, display.substr(start, end - start)});
                    start = end;
                }
                out.push_back({1, display.substr(start)});
            }
        } else {
            if (field && p.code == 7)
                own_format = format;
            if (flags)
                loss(93, "ACVALUE flags are omitted; a suppressed body becomes a "
                         "legacy default");
            if (unit)
                loss(94, "Legacy ACVALUE does not retain the unit code");
            if (!formatted.empty())
                loss(302, "Legacy ACVALUE does not retain the formatted display string");
            if (!format.empty() && !(field && p.code == 7))
                loss(300, "Legacy child or linked value has no format string");
            if (type & 512)
                loss(90, "Legacy ACVALUE clears type bit 0x200");
            const int legacy_type = type & ~512;
            out.push_back({90, std::to_string(legacy_type)});
            if (!code_in(legacy_type, {0, 1, 2, 4, 8, 16, 32, 64, 128, 256}))
                throw std::invalid_argument("Unsupported ACVALUE type");
            if (type != legacy_type || (flags & 1))
                body.clear();
            if (body.empty()) {
                switch (legacy_type) {
                case 0:
                case 1:
                    body.push_back({91, "0"});
                    break;
                case 2:
                    body.push_back({140, "0"});
                    break;
                case 4:
                    body.push_back({1, ""});
                    break;
                case 8:
                    body.push_back({92, "0"});
                    break;
                case 16:
                case 32:
                    if (linked || datalink)
                        body.push_back({92, legacy_type == 16 ? "16" : "24"});
                    body.push_back({11, "0"});
                    body.push_back({21, "0"});
                    if (legacy_type == 32)
                        body.push_back({31, "0"});
                    break;
                case 64:
                    body.push_back({330, "0"});
                    break;
                case 128:
                case 256:
                    break;
                default:
                    throw std::invalid_argument("Unsupported ACVALUE type");
                }
            }
            for (const auto &q : body)
                if (!(field && (legacy_type == 16 || legacy_type == 32) && q.code == 92))
                    out.push_back(q);
            if (linked)
                out.push_back({304, "ACVALUE_END"});
        }
    }
    if (field) {
        if (format_pos == std::string::npos) {
            if (eval_pos == std::string::npos)
                throw std::invalid_argument("Missing FIELD evaluation options");
            out.insert(out.begin() + eval_pos, {4, own_format});
        } else if (out[format_pos].value.empty())
            out[format_pos].value = own_format;
        else if (!own_format.empty() && out[format_pos].value != own_format)
            loss(4, "Legacy FIELD keeps only one format");
    }
    r.set_subclass_fields(scope, out);
    changed_ = true;
    issue(Kind::Information, "legacy-table-field-converted", &r, 0,
          "Converted value framing for the pre-R2007 DXF writer");
}

void upgrade_value_frames(DxfRecord &r) {
    const bool table = r.type() == "ACAD_TABLE";
    const bool datalink = r.type() == "DATALINK";
    const auto scope = table ? "AcDbTable" : datalink ? "AcDbDataLink" : "AcDbLinkedTableData";
    auto input = r.subclass_fields(scope);
    CellFields out;
    bool cell = false;
    int depth = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto &p = input[i];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (p.value == "}" && --depth < 0)
                throw std::invalid_argument("Invalid private group");
        }
        if (depth || p.code == 102) {
            out.push_back(p);
            continue;
        }
        if (table && p.code == 171)
            cell = true;
        if (table && cell && p.code == 177) {
            out.push_back({91, p.value});
            out.push_back({92, "0"});
            continue;
        }
        out.push_back(p);
        if (table || !((p.code == 301 && p.value == "DATAMAP_VALUE") ||
                       (!datalink && p.code == 300 && p.value == "VALUE")))
            continue;
        if (++i >= input.size() || input[i].code != 90)
            throw std::invalid_argument("Missing legacy ACVALUE type");
        const int type = std::stoi(input[i].value);
        out.push_back({93, "0"});
        out.push_back(input[i]);
        auto take = [&](int code) {
            if (i + 1 == input.size() || input[i + 1].code != code)
                throw std::invalid_argument("Incomplete legacy ACVALUE body");
            out.push_back(input[++i]);
        };
        switch (type) {
        case 0:
        case 1:
            take(91);
            break;
        case 2:
            take(140);
            break;
        case 4:
            take(1);
            break;
        case 8:
            take(92);
            while (i + 1 < input.size() && input[i + 1].code == 310)
                take(310);
            break;
        case 16:
        case 32:
            if (i + 1 < input.size() && input[i + 1].code == 92)
                take(92);
            else
                out.push_back({92, type == 16 ? "16" : "24"});
            take(11);
            take(21);
            if (type == 32)
                take(31);
            break;
        case 64:
            take(330);
            break;
        case 128:
        case 256:
            break;
        default:
            throw std::invalid_argument("Unknown legacy ACVALUE type");
        }
        out.insert(out.end(), {{94, "0"}, {300, ""}, {302, ""}});
        if (datalink)
            out.push_back({304, "ACVALUE_END"});
        else {
            take(304);
            if (out.back().value != "ACVALUE_END")
                throw std::invalid_argument("Invalid ACVALUE end marker");
        }
    }
    if (depth)
        throw std::invalid_argument("Unclosed private group");
    r.set_subclass_fields(scope, out);
    changed_ = true;
    issue(Kind::Information, "legacy-values-upgraded", &r, 0,
          "Expanded legacy cell flags and ACVALUE framing");
}

// The modern Rust writer refreshes TABLECONTENT from the resolved table.
// Preserve adjunct-only content and copy the entity's authoritative cell
// values.
void refresh_table_adjuncts() {
    std::vector<std::string> handles;
    doc_.for_each<DxfTable>([&](const auto &table) { handles.push_back(table.handle()); });
    std::set<std::string> updated;
    for (const auto &h : handles) {
        auto &table =
            std::get<DxfTable>(doc_.records_.at(doc_.handle_index_.at(upgrade_id(h)).front().record));
        {
            auto &t = table;
            auto fields = t.subclass_fields("AcDbTable");
            int depth = 0;
            bool cell = false, pending = false;
            std::size_t index = 0;
            for (auto &p : fields) {
                if (p.code == 102) {
                    if (!p.value.empty() && p.value.front() == '{')
                        ++depth;
                    else if (p.value == "}")
                        --depth;
                    continue;
                }
                if (depth)
                    continue;
                if (p.code == 171) {
                    cell = true;
                    pending = true;
                    ++index;
                }
                if (cell && pending && p.code == 91) {
                    p.value = std::to_string(std::stoll(p.value) | legacy_table_bits_.at(h).at(index - 1));
                    pending = false;
                }
            }
            t.set_subclass_fields("AcDbTable", fields);
        }
        auto link = doc_.table_content_handle(h);
        if (link.empty()) {
            doc_.synchronize_table_content(h);
            continue;
        }
        if (!updated.insert(link).second)
            throw std::invalid_argument("Shared TABLECONTENT");
        auto &entry = doc_.records_.at(doc_.handle_index_.at(upgrade_id(link)).front().record);
        auto &object = std::get<DxfTableContent>(entry);
        auto value = object.definition();
        const auto base = table.definition();
        if (value.rows.size() != base.cells.size())
            throw std::invalid_argument("TABLECONTENT row count mismatch");
        for (std::size_t i = 0; i < value.rows.size(); ++i) {
            auto &cells = value.rows[i].cells;
            if (cells.size() != base.cells[i].size())
                throw std::invalid_argument("TABLECONTENT column count mismatch");
            for (std::size_t j = 0; j < cells.size(); ++j) {
                auto &cell = cells[j];
                const auto &src = base.cells[i][j];
                cell.state |= src.state;
                if (src.value && cell.contents.size() <= 1 &&
                    (cell.contents.empty() || cell.contents.front().type == 1)) {
                    if (!cell.contents.empty() || !src.value->text.empty() || src.value->type != 4) {
                        if (cell.contents.empty())
                            cell.contents.emplace_back();
                        cell.contents.front().type = 1;
                        cell.contents.front().value = *src.value;
                    }
                }
                if (src.style) {
                    const auto &old = cell.format;
                    if (old) {
                        if (!old->extra_fields.empty() || !old->content.extra_fields.empty() ||
                            !old->margin_extra.empty() ||
                            std::any_of(old->borders.begin(), old->borders.end(),
                                        [](const auto &b) { return !b.extra_fields.empty(); }))
                            throw std::invalid_argument(
                                "Private cell style fields cannot be projected onto a legacy style");
                        if ((old->properties & ~(src.style->property_flags | 3u)) || old->margin_flags ||
                            !old->borders.empty())
                            issue(Kind::Loss, "table-adjunct-style-projected", &object, 91,
                                  "The visible cell style supersedes linked formatting during upgrade");
                    }
                    TableFormat format;
                    format.overrides = src.style->overrides;
                    format.properties = src.style->property_flags;
                    format.background = src.style->background_color;
                    format.content.color = src.style->content_color;
                    format.content.alignment = src.style->alignment;
                    format.content.text_height = src.style->text_height;
                    if (!src.style->text_style.empty()) {
                        auto symbol = doc_.get_symbol(SymbolTable::TextStyle, src.style->text_style);
                        if (!symbol)
                            throw std::invalid_argument("Unresolved table text style");
                        format.content.text_style = upgrade_id(symbol->handle);
                    }
                    if (old) {
                        if (old->properties & 1u) {
                            format.properties |= 1u;
                            format.content.data_type = old->content.data_type;
                            format.content.unit_type = old->content.unit_type;
                        }
                        if (old->properties & 2u) {
                            format.properties |= 2u;
                            format.content.value_format = old->content.value_format;
                        }
                    }
                    cell.format = std::move(format);
                }
            }
        }
        object.set_definition(value);
    }
}
