// SPDX-License-Identifier: MPL-2.0
// Port of acadrust MTEXT DXF I/O, including embedded and ACAD XDATA columns.
std::vector<DxfRecord::Pair> DxfMText::text_pairs(std::string_view value) const {
    std::string sanitized;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r' || value[i] == '\n') {
            if (value[i] == '\r' && i + 1 < value.size() && value[i + 1] == '\n')
                ++i;
            sanitized += "\\P";
        } else
            sanitized += value[i];
    }
    // Validate encoding before splitting so a malformed UTF-8 sequence cannot be hidden.
    (void)detail::utf8_scalars(sanitized);
    std::vector<Pair> result;
    std::size_t first = 0;
    while (sanitized.size() - first > 250) {
        auto end = first + 250;
        while (end > first && (static_cast<unsigned char>(sanitized[end]) & 0xc0) == 0x80)
            --end;
        if (end == first)
            throw std::invalid_argument("Invalid MTEXT UTF-8 boundary");
        result.push_back({3, encode_string(std::string_view(sanitized).substr(first, end - first))});
        first = end;
    }
    result.push_back({1, encode_string(std::string_view(sanitized).substr(first))});
    return result;
}
void DxfMText::replace_fields(const std::vector<int> &codes, const std::vector<Pair> &values) {
    std::vector<std::size_t> old;
    const auto end = locate_if("AcDbMText", [&](std::size_t i, int code) {
        if (std::find(codes.begin(), codes.end(), code) != codes.end())
            old.push_back(i);
        return false;
    });
    if (end.index == std::string_view::npos)
        throw std::invalid_argument("Missing AcDbMText subclass");
    auto insertion = old.empty() ? end.index : old.front();
    std::vector<Pair> updated;
    std::size_t remove = 0;
    for (std::size_t i = 0; i <= pairs_.size(); ++i) {
        if (i == insertion)
            updated.insert(updated.end(), values.begin(), values.end());
        if (i == pairs_.size())
            break;
        if (remove < old.size() && old[remove] == i)
            ++remove;
        else
            updated.push_back(pairs_[i]);
    }
    pairs_.swap(updated);
}
std::string DxfMText::text() const {
    std::string result;
    locate_if("AcDbMText", [&](std::size_t i, int code) {
        if (code == 1 || code == 3)
            result += decode_string(pairs_[i].value);
        return false;
    });
    return result;
}
void DxfMText::set_text(std::string_view value) {
    const auto replacement = text_pairs(value);
    replace_fields({1, 3}, replacement);
}
std::pair<std::size_t, std::size_t> DxfMText::embedded_range() const {
    const auto stop = locate_if("AcDbMText", [](std::size_t, int) { return false; }).index;
    if (stop >= pairs_.size() || pairs_[stop].code != 101)
        return {pairs_.size(), pairs_.size()};
    auto end = stop + 1;
    while (end < pairs_.size() && pairs_[end].code < 1000)
        ++end;
    return {stop, end};
}
MTextDefinition DxfMText::definition() const {
    MTextDefinition d;
    d.text = text();
    d.style = text_field("AcDbMText", 7, "Standard");
    d.insertion = point_or("AcDbMText", 10, {});
    d.normal = point_or("AcDbMText", 210, {0, 0, 1});
    d.height = number("AcDbMText", 40, 1);
    d.rectangle_width = number("AcDbMText", 41, 0);
    auto height = number("AcDbMText", 46, 0);
    if (height != 0)
        d.rectangle_height = height;
    d.attachment = static_cast<std::int16_t>(short_field("AcDbMText", 71, 1));
    if (d.attachment < 1 || d.attachment > 9)
        d.attachment = 1;
    d.drawing_direction = static_cast<std::int16_t>(short_field("AcDbMText", 72, 1));
    if (d.drawing_direction != 3 && d.drawing_direction != 5)
        d.drawing_direction = 1;
    d.line_spacing_style = short_field("AcDbMText", 73, 1) == 2 ? 2 : 1;
    d.line_spacing_factor = number("AcDbMText", 44, 1);
    d.extents_width = number("AcDbMText", 42, 0);
    d.extents_height = number("AcDbMText", 43, 0);
    d.background_scale = number("AcDbMText", 45, 1.5);
    auto int_at = [&](std::size_t i) {
        return integer(binary_ ? detail::text_value_from_binary(pairs_[i].code, pairs_[i].value)
                               : pairs_[i].value);
    };
    bool columns = false;
    std::optional<double> rotation;
    locate_if("AcDbMText", [&](std::size_t i, int code) {
        switch (code) {
        case 90:
            d.background_flags = int_at(i);
            break;
        case 441:
            d.background_transparency = int_at(i);
            break;
        case 63:
            d.background_color = Color::from_index(static_cast<std::int16_t>(int_at(i)));
            break;
        case 421:
            d.background_color = Color::from_true_color_value(static_cast<std::uint32_t>(int_at(i)));
            break;
        case 75:
            d.columns.type = static_cast<std::int16_t>(int_at(i));
            columns = d.columns.type != 0;
            break;
        case 76:
            if (columns)
                d.columns.count = int_at(i);
            break;
        case 78:
            if (columns)
                d.columns.flow_reversed = int_at(i) != 0;
            break;
        case 79:
            if (columns)
                d.columns.auto_height = int_at(i) != 0;
            break;
        case 48:
            if (columns)
                d.columns.width = number_at(i);
            break;
        case 49:
            if (columns)
                d.columns.gutter = number_at(i);
            break;
        case 50:
            if (!columns)
                rotation = number_at(i) * 3.14159265358979323846 / 180.;
            else if (d.columns.type == 2 && !d.columns.auto_height &&
                     d.columns.heights.size() < static_cast<std::size_t>(std::max(d.columns.count, 0)))
                d.columns.heights.push_back(number_at(i));
            break;
        }
        return false;
    });
    if (locate("AcDbMText", 11).found && locate("AcDbMText", 21).found) {
        const auto direction = point_or("AcDbMText", 11, {});
        if (direction.x != 0 || direction.y != 0) {
            d.x_direction = direction;
            d.rotation = std::atan2(direction.y, direction.x);
        }
    } else if (rotation) {
        d.x_direction =
            Matrix3::arbitrary_axis(d.normal) * Vector3{std::cos(*rotation), std::sin(*rotation), 0};
        d.rotation = std::atan2(d.x_direction->y, d.x_direction->x);
    }
    const auto [begin, end] = embedded_range();
    if (begin != end) {
        d.columns = {};
        unsigned depth = 0;
        for (auto i = begin + 1; i < end; ++i) {
            if (pairs_[i].code == 102) {
                if (!pairs_[i].value.empty() && pairs_[i].value.front() == '{')
                    ++depth;
                else if (depth && pairs_[i].value == "}")
                    --depth;
                continue;
            }
            if (depth)
                continue;
            switch (pairs_[i].code) {
            case 70:
                if (!locate("AcDbMText", 71).found) {
                    int a = int_at(i);
                    d.attachment = a >= 1 && a <= 9 ? static_cast<std::int16_t>(a) : 1;
                }
                break;
            case 71:
                d.columns.type = static_cast<std::int16_t>(int_at(i));
                break;
            case 72:
                d.columns.count = int_at(i);
                break;
            case 73:
                d.columns.auto_height = int_at(i) != 0;
                break;
            case 74:
                d.columns.flow_reversed = int_at(i) != 0;
                break;
            case 44:
                d.columns.width = number_at(i);
                break;
            case 45:
                d.columns.gutter = number_at(i);
                break;
            case 46:
                d.columns.heights.push_back(number_at(i));
                break;
            }
        }
    }
    if (d.columns.type == 0) {
        bool acad = false;
        for (std::size_t i = 0; i < pairs_.size(); ++i) {
            if (pairs_[i].code == 1001)
                acad = decode_string(pairs_[i].value) == "ACAD";
            if (!acad || pairs_[i].code != 1000 ||
                decode_string(pairs_[i].value) != "ACAD_MTEXT_COLUMN_INFO_BEGIN")
                continue;
            MTextColumns c;
            ++i;
            while (i + 1 < pairs_.size() && pairs_[i].code == 1070) {
                int code = int_at(i++);
                auto v = i++;
                switch (code) {
                case 75:
                    if (pairs_[v].code == 1070)
                        c.type = static_cast<std::int16_t>(int_at(v));
                    break;
                case 79:
                    if (pairs_[v].code == 1070)
                        c.auto_height = int_at(v) != 0;
                    break;
                case 76:
                    if (pairs_[v].code == 1070)
                        c.count = int_at(v);
                    break;
                case 78:
                    if (pairs_[v].code == 1070)
                        c.flow_reversed = int_at(v) != 0;
                    break;
                case 48:
                    if (pairs_[v].code == 1040)
                        c.width = number_at(v);
                    break;
                case 49:
                    if (pairs_[v].code == 1040)
                        c.gutter = number_at(v);
                    break;
                case 50:
                    if (pairs_[v].code == 1070) {
                        auto count = int_at(v);
                        while (count-- > 0 && i < pairs_.size() && pairs_[i].code == 1040)
                            c.heights.push_back(number_at(i++));
                    }
                    break;
                }
            }
            if (c.type != 0)
                d.columns = std::move(c);
            break;
        }
    }
    return d;
}
void DxfMText::set_definition(const MTextDefinition &d) {
    if (d.attachment < 1 || d.attachment > 9 ||
        (d.drawing_direction != 1 && d.drawing_direction != 3 && d.drawing_direction != 5) ||
        (d.line_spacing_style != 1 && d.line_spacing_style != 2) || d.columns.type < 0 || d.columns.type > 2)
        throw std::invalid_argument("Invalid MTEXT enum");
    if (d.columns.type != 0 && version_ < DxfVersion::AC1021)
        throw std::invalid_argument("MTEXT columns require R2007 or later");
    if (d.columns.heights.size() > INT16_MAX || d.columns.count < 0 || d.columns.count > INT16_MAX)
        throw std::invalid_argument("MTEXT column count out of range");
    finite(d.rotation);
    finite(d.extents_width);
    finite(d.extents_height);
    std::vector<Pair> fields;
    auto integer = [&](int code, std::int32_t value) {
        const auto text = std::to_string(value);
        fields.push_back({code, binary_ ? detail::binary_value_from_text(code, text) : text});
    };
    auto real = [&](int code, double value) { fields.push_back({code, encode_number(value)}); };
    auto point = [&](int code, Point3 p) {
        real(code, p.x);
        real(code + 10, p.y);
        real(code + 20, p.z);
    };
    point(10, d.insertion);
    real(40, d.height);
    real(41, d.rectangle_width);
    integer(71, d.attachment);
    integer(72, d.drawing_direction);
    const auto text = text_pairs(d.text);
    fields.insert(fields.end(), text.begin(), text.end());
    fields.push_back({7, encode_string(d.style)});
    point(11, d.x_direction.value_or(Vector3{std::cos(d.rotation), std::sin(d.rotation), 0}));
    if (d.extents_width > 0)
        real(42, d.extents_width);
    if (d.extents_height > 0)
        real(43, d.extents_height);
    integer(73, d.line_spacing_style);
    real(44, d.line_spacing_factor);
    if (d.rectangle_height)
        real(46, *d.rectangle_height);
    if (d.background_flags) {
        integer(90, d.background_flags);
        real(45, d.background_scale);
        integer(63, d.background_color.index().value_or(d.background_color.approximate_index()));
        if (auto rgb = d.background_color.to_true_color_value())
            integer(421, static_cast<std::int32_t>(*rgb));
        if (d.background_transparency)
            integer(441, d.background_transparency);
    }
    point(210, d.normal);
    DxfMText staged = *this;
    staged.replace_fields({1,  3,  7,  10, 20, 30, 11, 21, 31, 40, 41, 42,  43,  44,  45,  46, 48,
                           49, 50, 63, 71, 72, 73, 75, 76, 78, 79, 90, 210, 220, 230, 421, 441},
                          fields);
    // Remove only known embedded layout fields. Unknown extension payload survives.
    const auto [begin, end] = staged.embedded_range();
    std::vector<Pair> extra;
    const std::set<int> known{101, 70, 10, 20, 30, 11, 21, 31, 40, 41, 42, 43, 71, 72, 44, 45, 73, 74, 46};
    unsigned depth = 0;
    for (auto i = begin; i < end; ++i) {
        const auto &p = staged.pairs_[i];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (depth && p.value == "}")
                --depth;
            extra.push_back(p);
        } else if (depth || !known.count(p.code))
            extra.push_back(p);
    }
    staged.pairs_.erase(staged.pairs_.begin() + begin, staged.pairs_.begin() + end);
    const bool manual = d.columns.type == 2 && !d.columns.auto_height;
    const auto count = manual ? static_cast<int>(d.columns.heights.size()) : d.columns.count;
    fields.clear();
    if (d.columns.type != 0 && version_ >= DxfVersion::AC1032) {
        fields.push_back({101, "Embedded Object"});
        integer(70, d.attachment);
        point(10, {std::cos(d.rotation), std::sin(d.rotation), 0});
        point(11, d.insertion);
        real(40, d.rectangle_width);
        real(41, d.rectangle_height.value_or(0));
        real(42, d.extents_width);
        real(43, d.extents_height);
        integer(71, d.columns.type);
        integer(72, count);
        real(44, d.columns.width);
        real(45, d.columns.gutter);
        integer(73, d.columns.auto_height);
        integer(74, d.columns.flow_reversed);
        if (manual)
            for (auto height : d.columns.heights)
                real(46, height);
    } else if (!extra.empty())
        fields.push_back({101, "Embedded Object"});
    fields.insert(fields.end(), extra.begin(), extra.end());
    auto insert = staged.locate_if("AcDbMText", [](std::size_t, int) { return false; }).index;
    staged.pairs_.insert(staged.pairs_.begin() + insert, fields.begin(), fields.end());
    // Replace ACAD's column block, retaining unrelated XDATA in every application.
    bool acad = false;
    std::size_t acad_end = staged.pairs_.size();
    for (std::size_t i = 0; i < staged.pairs_.size(); ++i) {
        if (staged.pairs_[i].code == 1001) {
            if (acad)
                acad_end = i;
            acad = staged.decode_string(staged.pairs_[i].value) == "ACAD";
            if (acad)
                acad_end = staged.pairs_.size();
        }
        if (!acad || staged.pairs_[i].code != 1000 ||
            staged.decode_string(staged.pairs_[i].value) != "ACAD_MTEXT_COLUMN_INFO_BEGIN")
            continue;
        auto last = i + 1;
        while (last < staged.pairs_.size() && staged.pairs_[last].code != 1001 &&
               !(staged.pairs_[last].code == 1000 &&
                 staged.decode_string(staged.pairs_[last].value) == "ACAD_MTEXT_COLUMN_INFO_END"))
            ++last;
        if (last == staged.pairs_.size() || staged.pairs_[last].code == 1001)
            throw std::invalid_argument("Unterminated MTEXT column XDATA block");
        staged.pairs_.erase(staged.pairs_.begin() + i, staged.pairs_.begin() + last + 1);
        --i;
        acad_end = staged.pairs_.size();
    }
    fields.clear();
    if (d.columns.type != 0 && version_ < DxfVersion::AC1032) {
        bool has_acad = false;
        for (const auto &pair : staged.pairs_)
            if (pair.code == 1001 && staged.decode_string(pair.value) == "ACAD")
                has_acad = true;
        if (!has_acad)
            fields.push_back({1001, "ACAD"});
        fields.push_back({1000, "ACAD_MTEXT_COLUMN_INFO_BEGIN"});
        integer(1070, 75);
        integer(1070, d.columns.type);
        integer(1070, 79);
        integer(1070, d.columns.auto_height);
        integer(1070, 76);
        integer(1070, count);
        integer(1070, 78);
        integer(1070, d.columns.flow_reversed);
        integer(1070, 48);
        real(1040, d.columns.width);
        integer(1070, 49);
        real(1040, d.columns.gutter);
        integer(1070, 50);
        integer(1070, manual ? count : 0);
        if (manual)
            for (auto height : d.columns.heights)
                real(1040, height);
        fields.push_back({1000, "ACAD_MTEXT_COLUMN_INFO_END"});
        staged.pairs_.insert(staged.pairs_.begin() + acad_end, fields.begin(), fields.end());
    }
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_mtext(const MTextDefinition &definition, const EntityOptions &options) {
    if (version_ == DxfVersion::AC1009)
        throw std::invalid_argument("MTEXT requires R13 or later");
    if (!get_symbol(SymbolTable::TextStyle, definition.style))
        throw std::invalid_argument("Unknown MTEXT text style");
    DxfMText entity;
    static_cast<DxfEntity &>(entity) = make_entity("MTEXT", "AcDbMText", options);
    entity.set_definition(definition);
    return insert_entity(std::move(entity), options);
}
