// SPDX-License-Identifier: MPL-2.0
// acadrust header_writer, object_writer::{write_layer, write_line_type, write_ltype_control} and r12/tables.
namespace {
enum class HeaderVariable { HandleSeed, LinetypeScale };
std::size_t variable_position(std::string_view data, unsigned v, HeaderVariable field) {
    DwgBitReader r(data);
    if (v >= 1021)
        r.little_endian(4); // separate handle stream offset
    if (v >= 1027)
        r.little_endian(r.bits(3));
    for (int i = 0; i < 4; i++)
        r.bit_double();
    auto text = [&] {
        if (v < 1021) {
            auto n = r.bit_short();
            require(n >= 0, "Invalid DWG header string");
            r.seek(r.position() + 8 * n);
        }
    };
    for (int i = 0; i < 4; i++)
        text();
    r.bit_long();
    r.bit_long();
    bool early = v < 1015;
    if (early)
        r.bit_short();
    if (v < 1018)
        r.handle();
    // Boolean mode variables, PROXYGRAPHICS through PDMODE, then USERI1 through TEXTQLTY.
    r.bits(20 + (early ? 6 : 0) + (v >= 1018 ? 1 : 0));
    for (int i = 0; i < (early ? 12 : 8); i++)
        r.bit_short();
    if (v >= 1018)
        for (int i = 0; i < 3; i++)
            r.bit_long();
    for (int i = 0; i < 19; i++)
        r.bit_short();
    if (field == HeaderVariable::LinetypeScale) return r.position();
    for (int i = 0; i < 21; i++)
        r.bit_double();
    text();
    for (int i = 0; i < (v >= 1018 ? 11 : 8); i++)
        r.bit_long();
    r.bit_short();
    if (v >= 1018) {
        r.bit_long();
        auto flags = r.bits(8);
        if (flags & 1)
            text();
        if (flags & 2)
            text();
    }
    return r.position();
}
std::string_view dwg_header_payload(std::string_view header, unsigned v) {
    constexpr unsigned char sentinel[] = {0xcf, 0x7b, 0x1f, 0x23, 0xfd, 0xde, 0x38, 0xa9,
                                          0x5f, 0x7c, 0x68, 0xb8, 0x4e, 0x6d, 0x33, 0x5f};
    require(header.size() >= 38 && std::memcmp(header.data(), sentinel, sizeof sentinel) == 0,
            "Invalid DWG header sentinel");
    auto size = get_u32(header, 16);
    std::size_t start = 20;
    auto valid = [&](std::size_t at) {
        return at + 18 <= header.size() && size <= header.size() - at - 18 &&
               crc16(header.substr(16, at + size - 16)) ==
                   (static_cast<unsigned char>(header[at + size]) +
                    (static_cast<unsigned char>(header[at + size + 1]) << 8));
    };
    if (!valid(start)) {
        start += 4;
        require(v >= 1024 && valid(start), "Invalid DWG header CRC or size");
    }
    for (std::size_t i = 0; i < sizeof sentinel; i++)
        require(static_cast<unsigned char>(header[start + size + 2 + i]) == (sentinel[i] ^ 255),
                "Invalid DWG header end sentinel");
    return header.substr(start, size);
}
std::string update_symbol_seed(std::string_view header, unsigned v, std::uint64_t maximum,
                               std::uint64_t &id, bool reserve) {
    const auto data = dwg_header_payload(header, v);
    const auto start = static_cast<std::size_t>(data.data() - header.data());
    const auto size = data.size();
    auto pos = variable_position(data, v, HeaderVariable::HandleSeed);
    DwgBitReader r(data);
    r.seek(pos);
    auto seed = r.handle();
    require(seed.code == 0 && maximum < UINT64_MAX, "Invalid DWG handle seed");
    id = std::max(seed.value, maximum + 1);
    if (!reserve && id == seed.value)
        return {};
    require(!reserve || id < UINT64_MAX, "DWG handles exhausted");
    auto after = r.position();
    DwgBitWriter field;
    field.absolute_handle(0, id + (reserve ? 1 : 0));
    DwgBitWriter body;
    r.seek(0);
    if (v >= 1021) {
        auto split = r.little_endian(4);
        require(split >= after && split <= size * 8, "Invalid DWG header stream split");
        body.little_endian(4, split - (after - pos) + field.position());
    }
    copy_until(body, r, pos);
    append_bits(body, field);
    r.seek(after);
    copy_until(body, r, size * 8);
    std::string out(header.substr(0, start));
    out += body.bytes();
    out.append(header.substr(start + size));
    set_integer(out, 16, body.bytes().size(), 4);
    set_crc(out, 16, start + body.bytes().size());
    return out;
}
std::string allocate_symbol_handle(std::string_view header, unsigned v, std::uint64_t maximum,
                                   std::uint64_t &id) {
    return update_symbol_seed(header, v, maximum, id, true);
}
void update_aux_seed(std::string &aux, unsigned v, std::uint64_t next) {
    require(aux.size() >= 3 && aux.substr(0, 3) == "\xff\x77\x01", "Invalid DWG auxiliary header");
    set_integer(aux, v >= 1032 ? 85 : 79, next > INT32_MAX ? UINT32_MAX : next, 4);
}
std::size_t start_symbol(DwgBitWriter &main, DwgBitWriter &refs, unsigned v, int type, std::uint64_t id,
                         std::uint64_t owner) {
    if (v >= 1024) {
        main.bits(2, 0);
        main.bits(8, type);
    } else
        main.bit_short(type);
    auto split = main.position();
    if (v >= 1015 && v < 1024)
        main.little_endian(4, 0);
    main.absolute_handle(0, id);
    main.bit_short(0);
    if (v < 1015) {
        split = main.position();
        main.little_endian(4, 0);
    }
    refs.absolute_handle(4, owner);
    main.bit_long(0);
    if (v >= 1018)
        main.bits(1, 1);
    else
        refs.absolute_handle(3, 0);
    if (v >= 1027)
        main.bits(1, 0);
    return split;
}
void symbol_string(DwgBitWriter &main, DwgBitWriter &strings, unsigned v, std::string_view value,
                   const char *page) {
    if (v < 1021) {
        auto text = detail::to_legacy(value, page);
        require(text.size() <= 32767, "DWG symbol text too long");
        main.bit_short(text.size());
        for (unsigned char c : text)
            main.bits(8, c);
    } else {
        std::u16string text;
        for (auto c : detail::utf8_scalars(value)) {
            if (c > 0xffff) {
                c -= 0x10000;
                text.push_back(0xd800 + (c >> 10));
                text.push_back(0xdc00 + (c & 1023));
            } else
                text.push_back(static_cast<char16_t>(c));
        }
        require(text.size() <= 32767, "DWG symbol text too long");
        strings.bit_short(text.size());
        for (auto c : text)
            strings.little_endian(2, c);
    }
}

void symbol_name(DwgBitWriter &main, DwgBitWriter &strings, unsigned v, std::string_view name,
                 const char *page) {
    symbol_string(main, strings, v, name, page);
    if (v < 1021)
        main.bits(1, 0);
    main.bit_short(0);
    if (v < 1021)
        main.bits(1, 0);
}
std::string finish_symbol(DwgBitWriter &main, const DwgBitWriter &strings, const DwgBitWriter &refs,
                          unsigned v, std::size_t split) {
    if (v >= 1021) {
        append_bits(main, strings);
        auto n = strings.position();
        if (n >= 0x8000)
            main.little_endian(2, n >> 15);
        main.little_endian(2, n >= 0x8000 ? (n & 32767) | 32768 : n);
        main.bits(1, 1);
    }
    return pack_record(v, main, split, refs);
}
void write_layer_cmc(DwgBitWriter &main, Color color) {
    const auto rgb = color.to_true_color_value();
    const auto packed = rgb ? (0xc2000000u | *rgb) : (0xc3000000u | color.approximate_index());
    main.bit_short(0);
    main.bit_long(static_cast<std::int32_t>(packed));
    main.bits(8, 0);
}
std::string encode_layer(unsigned v, std::uint64_t id, std::uint64_t owner, std::uint64_t continuous,
                         std::string_view name, const char *page, Color color, unsigned lw_index = 31, bool off = false) {
    DwgBitWriter main, refs, strings;
    auto split = start_symbol(main, refs, v, 51, id, owner);
    symbol_name(main, strings, v, name, page);
    if (v >= 1015)
        main.bit_short(static_cast<std::int16_t>(((lw_index & 31) << 5) | 16 | (off ? 2 : 0)));
    else
        main.bits(4, 0);
    if (v >= 1018) write_layer_cmc(main, color);
    else main.bit_short(color.approximate_index());
    refs.absolute_handle(5, 0);
    if (v >= 1015)
        refs.absolute_handle(5, 0);
    if (v >= 1021)
        refs.absolute_handle(5, 0);
    refs.absolute_handle(5, continuous);
    if (v >= 1027)
        refs.absolute_handle(5, 0);
    return finish_symbol(main, strings, refs, v, split);
}
std::string encode_linetype(unsigned v, std::uint64_t id, std::uint64_t owner, std::string_view name,
                            std::string_view description, const std::vector<double> &segments, double length,
                            const char *page) {
    DwgBitWriter main, refs, strings;
    auto split = start_symbol(main, refs, v, 57, id, owner);
    symbol_name(main, strings, v, name, page);
    symbol_string(main, strings, v, description, page);
    main.bit_double(length);
    main.bits(8, 'A');
    main.bits(8, segments.size());
    refs.absolute_handle(5, 0); // xref block
    for (double segment : segments) {
        main.bit_double(segment);
        main.bit_short(0);   // shape number
        raw_double(main, 0); // shape offsets
        raw_double(main, 0);
        main.bit_double(1); // scale
        main.bit_double(0); // rotation
        main.bit_short(0);  // simple segment
        refs.absolute_handle(5, 0);
    }
    if (v < 1021)
        for (int i = 0; i < 256; ++i)
            main.bits(8, 0);
    return finish_symbol(main, strings, refs, v, split);
}
std::string extend_symbol_control(std::string_view body, DwgR2000Record r, unsigned v, std::uint64_t self,
                                  const std::vector<std::uint64_t> &ids) {
    DwgBitReader in(body);
    in.seek(r.handle_end_bit);
    skip_entity_prefix(in, body.size() * 8, false, v);
    if (v < 1015) {
        r.split_field_bit = in.position();
        r.main_bits = in.little_endian(4);
    }
    DwgBitReader handles(body);
    handles.seek(r.main_bits);
    handles.handle(self);
    auto reactors = in.bit_long();
    require(reactors >= 0, "Invalid symbol control reactors");
    for (int i = 0; i < reactors; i++)
        handles.handle(self);
    if (v < 1018 || !in.bits(1))
        handles.handle(self);
    if (v >= 1027)
        in.bits(1);
    auto pos = in.position();
    auto count = in.bit_long();
    require(count >= 0 && count < INT32_MAX, "Invalid DWG symbol table count");
    for (int i = 0; i < count; i++)
        handles.handle(self);
    DwgBitReader old(body);
    DwgBitWriter main, refs;
    copy_until(main, old, pos);
    require(ids.size() <= std::size_t(INT32_MAX - count), "Too many table entries");
    main.bit_long(count + static_cast<int>(ids.size()));
    old.seek(in.position());
    copy_until(main, old, r.main_bits);
    copy_until(refs, old, handles.position());
    for (auto id : ids) refs.absolute_handle(2, id);
    copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
    return pack_record(v, main, r.split_field_bit, refs);
}
std::string append_legacy_symbol(std::string_view file, unsigned table, std::string row,
                                 std::uint64_t &slot) {
    constexpr unsigned locators[] = {0x2c, 0x36, 0x40, 0x4a, 0x54, 0x3ef, 0x500, 0x512, 0x522, 0x69f};
    const auto locator = locators[table];
    auto shape = get_u32(file, locator);
    auto size = shape & 65535, count = shape >> 16;
    require(size == row.size() && count < (table == 3 ? 32766u : 32767u),
            "Unsupported AC1009 symbol table shape");
    auto start = get_u32(file, locator + 6), at = start + size * count;
    require(at <= file.size() && file.size() - at >= 16 + 170, "Invalid AC1009 symbol table end");
    // Keeping the entity streams in place preserves native JUMP and SEQEND addresses.
    require(get_u32(file, 24) <= at && get_u32(file, 28) + (get_u32(file, 32) & 0x3fffffff) <= at &&
                get_u32(file, 36) + (get_u32(file, 40) & 0x3fffffff) <= at,
            "AC1009 entity stream follows symbol table");
    slot = count;
    set_crc(row, 0, size - 2);
    std::string out(file);
    out.insert(at, row);
    auto aux = out.size() - 170;
    require(get_u32(file, file.size() - 22) == file.size() - 170, "Unsupported AC1009 auxiliary header");
    set_integer(out, locator + 2, count + 1, 2);
    set_integer(out, aux + 52 + 10 * table, count + 1, 2);
    for (unsigned i = 0; i < std::size(locators); i++) {
        auto field = locators[i] + 6;
        auto address = get_u32(file, field);
        if (address >= at && i != table) {
            set_integer(out, field, address + size, 4);
            set_integer(out, aux + 54 + 10 * i, address + size, 4);
        }
    }
    set_integer(out, aux + 148, aux, 4);
    set_crc(out, aux + 16, aux + 152);
    set_crc(out, 0, get_u32(file, 20) - 18);
    return out;
}
// Replace sections and object records using the original physical offsets.
// Map entries change length as records move; its own address precedes its contents.
std::string insert_linear_symbol(std::string_view file, const DwgR2000Index &idx,
                                 const DwgR2000Record &control, std::string header, std::string table,
                                 std::string record, std::uint64_t id, unsigned v) {
    struct Change {
        std::size_t length;
        std::string data;
    };
    std::map<std::size_t, Change> changes;
    auto map = idx.sections.at(2), head = idx.sections.at(0);
    changes.emplace(head.offset, Change{head.size, std::move(header)});
    changes.emplace(control.offset,
                    Change{control.body_offset + control.body_size + 2 - control.offset, std::move(table)});
    auto relocated = [&](std::size_t pos) {
        auto target = pos;
        for (auto &[at, c] : changes)
            if (at < pos) {
                require(at + c.length <= pos, "Overlapping DWG sections");
                target = target - c.length + c.data.size();
            }
        return target;
    };
    auto offsets = idx.object_offsets;
    for (auto &[h, pos] : offsets) {
        require(pos < map.offset, "Unsupported object after DWG map");
        pos = relocated(pos);
    }
    offsets.emplace(id, relocated(map.offset));
    auto new_map = write_map(offsets);
    auto new_offset = offsets.at(id) + record.size();
    changes.emplace(map.offset, Change{map.size, record + new_map});
    std::string out;
    std::size_t cursor = 0;
    for (auto &[at, c] : changes) {
        require(at >= cursor, "Overlapping DWG replacements");
        out.append(file.substr(cursor, at - cursor));
        out += c.data;
        cursor = at + c.length;
    }
    out.append(file.substr(cursor));
    for (std::size_t i = 0; i < idx.sections.size(); i++) {
        auto field = 25 + 9 * i;
        unsigned key = static_cast<unsigned char>(file[field]);
        auto loc = idx.sections.at(key);
        auto pos = relocated(loc.offset);
        set_integer(out, field + 1, key == 2 ? new_offset : pos, 4);
        if (key == 0)
            set_integer(out, field + 5, changes.at(head.offset).data.size(), 4);
        if (key == 2)
            set_integer(out, field + 5, new_map.size(), 4);
        if (key == 3 && loc.size >= 8)
            set_integer(out, pos + 4, offsets.size(), 4);
        if (key == 5 && loc.size) {
            std::string aux(file.substr(loc.offset, loc.size));
            update_aux_seed(aux, v, id + 1);
            out.replace(pos, aux.size(), aux);
        }
    }
    auto preview = get_u32(file, 13);
    if (preview) {
        auto pos = relocated(preview);
        set_integer(out, 13, pos, 4);
        if (pos != preview) {
            require(preview + 37 <= file.size(), "Truncated DWG preview");
            unsigned n = static_cast<unsigned char>(file[preview + 20]);
            for (unsigned i = 0; i < n; i++) {
                auto f = 22 + i * 9;
                set_integer(out, pos + f, relocated(get_u32(file, preview + f)), 4);
            }
        }
    }
    set_crc(out, 0, 25 + 9 * idx.sections.size());
    return out;
}
std::string legacy_symbol_row(std::string_view file, std::string_view name, std::size_t size) {
    const auto encoded = detail::to_legacy(name, drawing_code_page(file, 1009));
    require(encoded.size() <= 32, "AC1009 symbol name exceeds 32 encoded bytes");
    std::string row(size, 0);
    row.replace(1, encoded.size(), encoded);
    set_integer(row, 33, 0xffff, 2);
    return row;
}

std::optional<std::uint64_t> symbol_id(const DwgR2000Objects &o, unsigned v, int type,
                                       std::string_view name) {
    const auto key = detail::symbol_name_key(name);
    std::optional<std::uint64_t> result;
    auto match = [&](std::uint64_t id, const std::string &existing) {
        if (!existing.empty() && detail::symbol_name_key(existing) == key) {
            require(!result, "Ambiguous DWG symbol name");
            result = id;
        }
    };
    if (v == 1009) {
        const auto it = o.legacy_symbol_names.find(type);
        if (it != o.legacy_symbol_names.end())
            for (std::size_t i = 0; i < it->second.size(); ++i)
                match(i, it->second[i]);
    } else {
        for (const auto &[id, s] : o.symbols)
            if (s.type == type)
                match(id, s.name);
    }
    return result;
}

template <class Encode>
std::string insert_symbol(std::string_view file, const DwgR2000Objects &o, unsigned v, int control_type,
                          std::uint64_t &id, Encode encode) {
    std::uint64_t control = 0;
    for (const auto &[h, r] : o.records)
        if (r.type == control_type) {
            require(!control, "Duplicate DWG symbol table control");
            control = h;
        }
    require(control, "Missing DWG symbol table control");
    DwgR2000Index idx;
    std::string header, objects;
    std::string_view data = file;
    if (v >= 1018) {
        header = read_dwg_paged_section(file, "AcDb:Header");
        objects = read_dwg_paged_section(file, "AcDb:AcDbObjects");
        data = objects;
    } else {
        idx = read_dwg_linear_index(file);
        auto loc = idx.sections.at(0);
        header = file.substr(loc.offset, loc.size);
    }
    header = allocate_symbol_handle(header, v, o.records.rbegin()->first, id);
    auto r = o.records.at(control);
    auto table = extend_symbol_control(data.substr(r.body_offset, r.body_size), r, v, control, {id});
    auto record = encode(id, control);
    if (v < 1018)
        return insert_linear_symbol(file, idx, r, std::move(header), std::move(table), std::move(record), id,
                                    v);
    auto old_length = r.body_offset + r.body_size + 2 - r.offset;
    std::map<std::uint64_t, std::size_t> offsets;
    for (auto &[h, entry] : o.records)
        offsets[h] = entry.offset > r.offset ? entry.offset - old_length + table.size() : entry.offset;
    objects.replace(r.offset, old_length, table);
    offsets[id] = objects.size();
    objects += record;
    auto free = read_dwg_paged_section(file, "AcDb:ObjFreeSpace");
    set_integer(free, 4, offsets.size(), 4);
    auto aux = read_dwg_paged_section(file, "AcDb:AuxHeader");
    update_aux_seed(aux, v, id + 1);
    auto replace = v == 1021 ? detail::replace_ac21_sections : detail::replace_ac18_sections;
    return replace(file, {{"AcDb:Header", std::move(header)},
                          {"AcDb:AuxHeader", std::move(aux)},
                          {"AcDb:ObjFreeSpace", std::move(free)},
                          {"AcDb:Handles", write_map(offsets)},
                          {"AcDb:AcDbObjects", std::move(objects)}});
}

} // namespace

std::string detail::add_dwg_layer(std::string_view file, const DwgR2000Objects &o, std::string_view name,
                                  int color, std::uint64_t &id, unsigned lw_index) {
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    require(!symbol_id(o, v, 51, name), "Layer already exists");
    auto continuous = symbol_id(o, v, 57, "Continuous");
    require(continuous.has_value(), "Missing Continuous linetype");
    if (v == 1009) {
        auto row = legacy_symbol_row(file, name, 41);
        set_integer(row, 35, color, 2);
        set_integer(row, 37, *continuous, 2);
        return append_legacy_symbol(file, 1, std::move(row), id);
    }
    return insert_symbol(file, o, v, 50, id, [&](auto h, auto owner) {
        return encode_layer(v, h, owner, *continuous, name, drawing_code_page(file, v), Color::from_index(color), lw_index);
    });
}

std::string detail::add_dwg_linetype(std::string_view file, const DwgR2000Objects &o, std::string_view name,
                                     const std::vector<double> &segments, std::string_view description,
                                     std::uint64_t &id) {
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    const auto key = symbol_name_key(name);
    require(key != U"BYBLOCK" && key != U"BYLAYER" && !symbol_id(o, v, 57, name),
            "Duplicate or reserved DWG linetype name");
    require(segments.size() <= (v == 1009 ? 12u : 255u), "Too many DWG linetype segments");
    double length = 0.;
    for (double segment : segments)
        length += std::abs(segment);
    require(std::isfinite(length), "Invalid DWG linetype pattern length");
    auto page = drawing_code_page(file, v);
    if (v == 1009) {
        auto row = legacy_symbol_row(file, name, 191);
        auto text = to_legacy(description, page);
        require(text.size() <= 48, "AC1009 linetype description exceeds 48 encoded bytes");
        row.replace(35, text.size(), text);
        row[83] = 'A';
        row[84] = static_cast<char>(segments.size());
        DwgBitWriter values;
        raw_double(values, length);
        for (double segment : segments)
            raw_double(values, segment);
        row.replace(85, values.bytes().size(), values.bytes());
        return append_legacy_symbol(file, 3, std::move(row), id);
    }
    return insert_symbol(file, o, v, 56, id, [&](auto h, auto owner) {
        return encode_linetype(v, h, owner, name, description, segments, length, page);
    });
}

std::optional<std::string> detail::set_dwg_layer_linetype(std::string_view file, const DwgR2000Objects &o,
                                                          std::string_view layer, std::string_view linetype) {
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    auto layer_id = symbol_id(o, v, 51, layer);
    require(layer_id.has_value(), "Unknown DWG layer");
    auto id = symbol_id(o, v, 57, linetype);
    if (v == 1009) {
        auto key = symbol_name_key(linetype);
        if (key == U"BYLAYER")
            id = 32767;
        if (key == U"BYBLOCK")
            id = 32766;
        require(id.has_value(), "Unknown DWG linetype");
        require((get_u32(file, 0x36) & 65535) == 41, "Unsupported AC1009 layer row");
        auto row = get_u32(file, 0x3c) + 41 * *layer_id;
        auto old = get_u32(file, row + 37) & 65535;
        if (old == *id || (old >= 65534 && old - 32768 == *id))
            return {};
        std::string out(file);
        set_integer(out, row + 37, *id, 2);
        set_crc(out, row, row + 39);
        return out;
    }
    require(id.has_value(), "Unknown DWG linetype");
    if (o.symbols.at(*layer_id).linetype == *id)
        return {};
    const auto &r = o.records.at(*layer_id);
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, refs;
        copy_until(main, old, r.main_bits);
        copy_until(refs, old, r.linetype_bit);
        old.handle(*layer_id);
        refs.absolute_handle(5, *id);
        copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
        return pack_record(v, main, r.split_field_bit, refs);
    });
}

std::string detail::set_dwg_entity_linetype(std::string_view file, const DwgR2000Objects &objects,
                                            std::uint64_t entity, std::string_view name) {
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    auto key = symbol_name_key(name);
    unsigned mode = key.empty() || key == U"BYLAYER" ? 0
                    : key == U"BYBLOCK"              ? 1
                    : key == U"CONTINUOUS"           ? 2
                                                     : 3;
    // R13/R14 only encode ByLayer inline. R11/R12 store ByBlock as a reserved slot.
    std::uint64_t target = 0;
    bool table_reference = v == 1009 ? mode >= 2 : v < 1015 ? mode != 0 : mode == 3;
    if (table_reference) {
        auto id = symbol_id(objects, v, 57, name);
        require(id.has_value(), "Unknown DWG entity linetype");
        target = *id;
    }
    const auto &r = objects.records.at(entity);
    if (v == 1009) {
        std::string live(file.substr(r.offset, r.body_size + 2));
        const auto pos = r.linetype_bit / 8;
        if (live[1] & 2)
            live.erase(pos, 2);
        live[1] &= ~2;
        if (mode) {
            live[1] |= 2;
            live.insert(pos, 2, char(0));
            set_integer(live, pos, mode == 1 ? 32766 : target, 2);
        }
        return replace_legacy_entity(file, r, std::move(live));
    }
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, refs;
        copy_until(main, old, r.linetype_flag_bit);
        const bool early = v < 1015;
        auto old_mode = old.bits(early ? 1 : 2);
        main.bits(early ? 1 : 2, early ? mode == 0 : mode);
        copy_until(main, old, r.main_bits);
        copy_until(refs, old, r.linetype_bit);
        if (early ? old_mode == 0 : old_mode == 3)
            old.handle(entity);
        if (table_reference)
            refs.absolute_handle(5, target);
        copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
        return pack_record(v, main, r.split_field_bit, refs);
    });
}

// acadrust common entity layer reference: an AC1009 table slot or a hard pointer.
std::string detail::set_dwg_entity_layer(std::string_view file, const DwgR2000Objects &objects,
                                         std::uint64_t entity, std::string_view name) {
    const auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    const auto layer = symbol_id(objects, v, 51, name);
    require(layer.has_value(), "Unknown DWG entity layer");
    const auto &r = objects.records.at(entity);
    if (v == 1009) {
        std::string live(file.substr(r.offset, r.body_size + 2));
        set_integer(live, 4, *layer, 2);
        return replace_legacy_entity(file, r, std::move(live));
    }
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, refs;
        copy_until(main, old, r.main_bits);
        copy_until(refs, old, r.layer_bit);
        old.handle(entity);
        refs.absolute_handle(5, *layer);
        copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
        return pack_record(v, main, r.split_field_bit, refs);
    });
}
