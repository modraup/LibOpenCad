// SPDX-License-Identifier: MPL-2.0
// Port of acadrust CadDocument sheet_set_data / set_sheet_set_data.
std::optional<SheetSetData> DxfDocument::sheet_set_data() const {
    auto nod = named_object_dictionary(kSheetSetData);
    if (nod.empty())
        return std::nullopt;
    SheetSetData data;
    auto text = [&](std::string_view key) -> std::string {
        auto h = dictionary_entry(nod, key);
        if (h.empty())
            return {};
        auto *rec = object_record(handle_value(h));
        if (!rec || rec->type() != "XRECORD")
            return {};
        std::string value;
        rec->locate_if("AcDbXrecord", [&](std::size_t i, int code) {
            if (code != 280)
                value = rec->decode_string(rec->pairs_[i].value);
            return false;
        });
        return value;
    };
    auto integer = [&](std::string_view key) -> std::int32_t {
        auto h = dictionary_entry(nod, key);
        if (h.empty())
            return 0;
        auto *rec = object_record(handle_value(h));
        if (!rec || rec->type() != "XRECORD")
            return 0;
        std::int32_t value = 0;
        rec->locate_if("AcDbXrecord", [&](std::size_t i, int code) {
            if (code == 90) {
                if (auto bits = detail::as_i32_bits(rec->pairs_[i].value, rec->binary_))
                    value = static_cast<std::int32_t>(*bits);
            }
            return false;
        });
        return value;
    };
    data.layout_handle = text("LayoutHandle");
    data.layout_name = text("LayoutName");
    data.sheet_dwg_name = text("SheetDwgName");
    data.sheet_set_file_name = text("ShSetFileName");
    data.sheet_set_version = integer("ShSetVersion");
    data.update_count = integer("UpdateCount");
    data.update_time = text("UpdateTime");
    return data;
}

void DxfDocument::set_sheet_set_data(const SheetSetData &data) {
    DxfDocument staged = *this;
    auto nod = staged.root_dictionary_handle();
    auto existing = staged.named_object_dictionary(kSheetSetData);
    std::uint64_t dict_handle = 0;
    if (!existing.empty()) {
        auto *rec = staged.object_record(handle_value(existing));
        if (rec && rec->type() == "DICTIONARY")
            dict_handle = handle_value(existing);
    }
    if (!dict_handle)
        dict_handle = staged.add_dictionary(nod, kSheetSetData);
    struct Item {
        const char *key;
        std::string text;
        std::int32_t number = 0;
        int code = 1;
    };
    Item records[] = {
        {"LayoutHandle", data.layout_handle, 0, 1},
        {"LayoutName", data.layout_name, 0, 1},
        {"SheetDwgName", data.sheet_dwg_name, 0, 1},
        {"ShSetFileName", data.sheet_set_file_name, 0, 1},
        {"ShSetVersion", {}, data.sheet_set_version, 90},
        {"UpdateCount", {}, data.update_count, 90},
        {"UpdateTime", data.update_time, 0, 1},
    };
    for (const auto &item : records) {
        auto old = staged.dictionary_entry(hex_handle(dict_handle), item.key);
        std::vector<SymbolProperty> values;
        if (item.code == 90)
            values.push_back({90, std::to_string(item.number)});
        else
            values.push_back({1, item.text});
        std::uint64_t handle = 0;
        if (!old.empty()) {
            auto *rec = staged.object_record(handle_value(old));
            if (rec && rec->type() == "XRECORD") {
                handle = handle_value(old);
                auto &record = staged.mutable_object(handle);
                record.pairs_.erase(std::remove_if(record.pairs_.begin(), record.pairs_.end(),
                                                   [](const DxfRecord::Pair &p) { return p.code != 100 && p.code != 280 && p.code != 5 && p.code != 330; }),
                                    record.pairs_.end());
                for (const auto &v : values)
                    record.pairs_.push_back({v.code, v.value});
                record.set_short_field("AcDbXrecord", 280, 1);
                continue;
            }
        }
        handle = staged.add_xrecord(hex_handle(dict_handle), item.key, values);
        staged.mutable_object(handle).set_short_field("AcDbXrecord", 280, 1);
    }
    *this = std::move(staged);
}
