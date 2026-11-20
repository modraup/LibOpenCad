// SPDX-License-Identifier: MPL-2.0
// Ported from acadrust xdata/mod.rs and DXF read_extended_data/write_xdata.
namespace {
bool xdata_code(int code) {
    return (code >= 1000 && code <= 1005) || (code >= 1010 && code <= 1013) ||
           (code >= 1020 && code <= 1023) || (code >= 1030 && code <= 1033) ||
           (code >= 1040 && code <= 1042) || code == 1070 || code == 1071;
}
} // namespace
std::vector<std::pair<std::size_t, std::size_t>> DxfRecord::xdata_ranges() const {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    unsigned depth = 0;
    for (std::size_t i = 1; i < pairs_.size(); ++i) {
        const auto &p = pairs_[i];
        // Codes >= 1000 inside an XRECORD payload are ordinary payload values.
        if (!depth && type() == "XRECORD" && p.code == 100 && p.value == "AcDbXrecord")
            break;
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (depth && p.value == "}")
                --depth;
        }
        if (depth || p.code != 1001)
            continue;
        auto end = i + 1;
        while (end < pairs_.size() && pairs_[end].code != 1001 && xdata_code(pairs_[end].code))
            ++end;
        ranges.push_back({i, end});
        i = end - 1;
    }
    return ranges;
}
std::vector<ExtendedDataRecord> DxfRecord::extended_data() const {
    std::vector<ExtendedDataRecord> result;
    for (auto [start, end] : xdata_ranges()) {
        ExtendedDataRecord record{decode_string(pairs_[start].value), {}};
        Point3 point{};
        bool x = false, y = false;
        for (auto i = start + 1; i < end; ++i) {
            const auto &p = pairs_[i];
            if (p.code == 1000 || p.code == 1002 || p.code == 1003)
                record.values.push_back({p.code, decode_string(p.value)});
            else if (p.code == 1004) {
                std::vector<std::uint8_t> bytes;
                if (binary_)
                    bytes.assign(p.value.begin(), p.value.end());
                else
                    for (std::size_t at = 0; at < p.value.size(); at += 2) {
                        const auto end_byte = std::min(at + 2, p.value.size());
                        unsigned byte = 0;
                        const auto parsed =
                            std::from_chars(p.value.data() + at, p.value.data() + end_byte, byte, 16);
                        if (parsed.ec == std::errc{} && parsed.ptr == p.value.data() + end_byte)
                            bytes.push_back(static_cast<std::uint8_t>(byte));
                    }
                record.values.push_back({p.code, std::move(bytes)});
            } else if (p.code == 1005) {
                if (auto value = parse_handle(p.value))
                    record.values.push_back({p.code, *value});
            } else if (p.code >= 1010 && p.code <= 1033) {
                const auto component = p.code / 10 - 101;
                if (component == 0) {
                    point = {};
                    x = true;
                    y = false;
                    point.x = number_at(i);
                }
                if (component == 1) {
                    point.y = number_at(i);
                    y = true;
                }
                if (component == 2) {
                    point.z = number_at(i);
                    if (x && y)
                        record.values.push_back({p.code - 20, point});
                    x = y = false;
                }
            } else if (p.code >= 1040 && p.code <= 1042)
                record.values.push_back({p.code, number_at(i)});
            else if (p.code == 1070) {
                if (auto value = detail::as_i16(p.code, p.value, binary_))
                    record.values.push_back({p.code, *value});
            } else if (p.code == 1071) {
                if (auto bits = detail::as_i32_bits(p.value, binary_)) {
                    const auto n = *bits <= INT32_MAX
                                       ? static_cast<std::int64_t>(*bits)
                                       : static_cast<std::int64_t>(*bits) - (std::int64_t{1} << 32);
                    record.values.push_back({p.code, static_cast<std::int32_t>(n)});
                }
            }
        }
        result.push_back(std::move(record));
    }
    return result;
}
void DxfRecord::upsert_extended_data(const ExtendedDataRecord &data) {
    if (type() == "XRECORD")
        throw std::invalid_argument("XRECORD payload is not XDATA");
    valid_symbol_name(data.application_name);
    std::vector<Pair> replacement{{1001, encode_string(data.application_name)}};
    for (const auto &item : data.values) {
        const int code = item.code;
        if (code == 1000 || code == 1002 || code == 1003) {
            const auto &value = std::get<std::string>(item.value);
            detail::utf8_scalars(value);
            replacement.push_back({code, encode_string(value)});
        } else if (code == 1004) {
            const auto &value = std::get<std::vector<std::uint8_t>>(item.value);
            if (value.size() > 255)
                throw std::invalid_argument("XDATA binary chunk exceeds 255 bytes");
            const std::string bytes(value.begin(), value.end());
            replacement.push_back({code, binary_ ? bytes : detail::text_value_from_binary(code, bytes)});
        } else if (code == 1005)
            replacement.push_back({code, hex_handle(std::get<std::uint64_t>(item.value))});
        else if (code >= 1010 && code <= 1013) {
            const auto &value = std::get<Point3>(item.value);
            replacement.push_back({code, encode_number(value.x)});
            replacement.push_back({code + 10, encode_number(value.y)});
            replacement.push_back({code + 20, encode_number(value.z)});
        } else if (code >= 1040 && code <= 1042)
            replacement.push_back({code, encode_number(std::get<double>(item.value))});
        else if (code == 1070) {
            const auto value = std::get<std::int16_t>(item.value);
            replacement.push_back({code, binary_ ? detail::binary_short(value) : std::to_string(value)});
        } else if (code == 1071) {
            const auto value = std::get<std::int32_t>(item.value);
            replacement.push_back({code, binary_
                                             ? detail::binary_int32_bits(static_cast<std::uint32_t>(value))
                                             : std::to_string(value)});
        } else
            throw std::invalid_argument("Invalid XDATA value code");
    }
    for (auto [start, end] : xdata_ranges()) {
        if (decode_string(pairs_[start].value) != data.application_name)
            continue;
        pairs_.erase(pairs_.begin() + start, pairs_.begin() + end);
        pairs_.insert(pairs_.begin() + start, replacement.begin(), replacement.end());
        return; // Rust upsert replaces the first exact application name.
    }
    pairs_.insert(pairs_.end(), replacement.begin(), replacement.end());
}
void DxfDocument::set_extended_data(std::string_view handle, const ExtendedDataRecord &data) {
    const auto found = handle_index_.find(handle_value(handle));
    if (found == handle_index_.end() || found->second.size() != 1)
        throw std::invalid_argument("XDATA owner must resolve uniquely");
    DxfDocument staged = *this;
    auto &record = const_cast<DxfRecord &>(staged.record_at(found->second.front()));
    record.upsert_extended_data(data);
    if (!staged.get_symbol(SymbolTable::AppId, data.application_name))
        staged.add_symbol(SymbolTable::AppId, data.application_name);
    staged.rebuild_indexes();
    *this = std::move(staged);
}
bool DxfDocument::remove_extended_data(std::string_view handle, std::string_view application_name) {
    const auto found = handle_index_.find(handle_value(handle));
    if (found == handle_index_.end() || found->second.size() != 1)
        throw std::invalid_argument("XDATA owner must resolve uniquely");
    auto &record = const_cast<DxfRecord &>(record_at(found->second.front()));
    for (auto [start, end] : record.xdata_ranges()) {
        if (record.decode_string(record.pairs_[start].value) != application_name)
            continue;
        record.pairs_.erase(record.pairs_.begin() + start, record.pairs_.begin() + end);
        rebuild_indexes();
        return true;
    }
    return false;
}
std::vector<std::uint64_t> DxfDocument::unused_app_ids() const {
    auto upper = [](std::string s) {
        for (char &c : s)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        return s;
    };
    std::unordered_set<std::uint64_t> handles;
    std::unordered_set<std::string> names;
    auto note_name = [&](std::string_view s) { names.insert(upper(std::string(s))); };
    auto visit = [&](const DxfRecord &r) {
        for (const auto &xd : r.extended_data())
            note_name(xd.application_name);
        for (const auto &p : r.pairs_)
            if (p.code == 1001)
                note_name(r.decode_string(p.value));
        if (auto owner = handle_value(r.owner_handle()))
            handles.insert(owner);
        unsigned depth = 0;
        for (std::size_t i = 1; i < r.pairs_.size(); ++i) {
            const auto &p = r.pairs_[i];
            if (!depth && (p.code == 100 || p.code == 101 || p.code >= 1000))
                break;
            if (p.code != 102)
                continue;
            if (!depth && p.value == "{ACAD_XDICTIONARY")
                if (auto h = handle_value(r.handle()))
                    handles.insert(h);
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (depth && p.value == "}")
                --depth;
        }
    };
    for (std::size_t i = 0; i < records_.size(); ++i) {
        visit(record_at({i, {}, false}));
        if (const auto *group = sequence(records_[i])) {
            for (const auto &child : group->children_)
                visit(child);
            if (group->seqend_)
                visit(*group->seqend_);
        }
    }
    for (const char *app : {"ACAD", "AcadAnnotative", "AcCmTransparency", "AcAecLayerStandard"})
        note_name(app);
    std::vector<std::uint64_t> unused;
    for (const auto &entry : symbols(SymbolTable::AppId)) {
        const auto h = handle_value(entry.handle);
        if (!handles.count(h) && !names.count(upper(entry.name)))
            unused.push_back(h);
    }
    return unused;
}
