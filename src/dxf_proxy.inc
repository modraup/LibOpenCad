// SPDX-License-Identifier: MPL-2.0
namespace {
std::uint32_t proxy_size(const DxfRawField &p) {
    try {
        std::size_t end;
        auto text = std::string(trim(p.value));
        if (text.empty() || text[0] == '-')
            return 0;
        auto size = std::stoull(text, &end);
        if (end != text.size())
            return 0;
        return static_cast<std::uint32_t>(size);
    } catch (const std::exception &) {
        return 0;
    }
}
ProxyDefinition proxy_read(std::vector<DxfRawField> raw, bool entity) {
    MLFields f(std::move(raw));
    ProxyDefinition v;
    v.proxy_id = entity ? 498 : 499;
    v.dwg_version = 0;
    std::int32_t packed = 0;
    std::uint32_t graphics_size = 0;
    std::vector<std::uint8_t> data;
    bool payload = false;
    for (std::size_t n = 0; n < f.values.size(); ++n) {
        if (f.hidden[n])
            continue;
        const auto &p = f.values[n];
        MLFields one({p});
        switch (p.code) {
        case 90:
            v.proxy_id = one.i(90, v.proxy_id);
            break;
        case 91:
            v.class_id = one.i(91, 0);
            break;
        case 95:
            packed = one.i(95, 0);
            break;
        case 71:
            v.dwg_version = one.i(71, 0);
            break;
        case 97:
            v.maintenance_version = one.i(97, 0);
            break;
        case 70:
            v.from_dxf = one.b(70, false);
            break;
        case 92:
        case 160:
            graphics_size = proxy_size(p);
            payload = false;
            break;
        case 96:
        case 162:
            v.text_payload.bit_count = proxy_size(p);
            payload = true;
            break;
        case 93:
        case 161:
            v.payload.bit_count = proxy_size(p);
            payload = true;
            break;
        case 94:
            break;
        case 330:
        case 340:
        case 350:
        case 360:
            v.references.push_back({p.code, handle_value(p.value)});
            break;
        default:
            if (p.code >= 310 && p.code <= 319) {
                auto bytes = table_bytes(p.value);
                auto &target = payload || !entity || (graphics_size && v.graphics.size() >= graphics_size)
                                   ? data
                                   : v.graphics;
                target.insert(target.end(), bytes.begin(), bytes.end());
            } else
                continue;
        }
        f.used[n] = true;
    }
    if (v.dwg_version == 0 && v.maintenance_version == 0) {
        v.dwg_version = packed & 65535;
        v.maintenance_version = packed >> 16;
    }
    if (v.graphics.size() > graphics_size)
        v.graphics.resize(graphics_size);
    const auto text_bytes = std::min(data.size(), (std::size_t(v.text_payload.bit_count) + 7) / 8);
    v.text_payload.data.assign(data.end() - text_bytes, data.end());
    v.payload.data.assign(data.begin(), data.end() - text_bytes);
    v.extra_fields = f.extras();
    return v;
}
std::vector<std::uint8_t> proxy_data(const ProxyPayload &v) {
    auto count = (std::size_t(v.bit_count) + 7) / 8;
    if (count > v.data.size())
        throw std::invalid_argument("Proxy bit count exceeds available data");
    std::vector<std::uint8_t> data(v.data.begin(), v.data.begin() + count);
    if (!data.empty() && (v.bit_count % 8))
        data.back() &= std::uint8_t(255u << (8 - v.bit_count % 8));
    return data;
}
void proxy_binary(MLFields &f, const std::vector<std::uint8_t> &data) {
    constexpr char hex[] = "0123456789ABCDEF";
    for (std::size_t at = 0; at < data.size(); at += 127) {
        std::string text;
        for (auto i = at; i < std::min(data.size(), at + 127); ++i) {
            text += hex[data[i] >> 4];
            text += hex[data[i] & 15];
        }
        f.put_s(310, text);
    }
}
MLFields proxy_write(const ProxyDefinition &v, bool entity, DxfVersion version) {
    MLFields f;
    f.put_i(90, entity ? v.proxy_id : 499);
    f.put_i(91, v.class_id);
    if (version >= DxfVersion::AC1032) {
        f.put_i(71, v.dwg_version);
        f.put_i(97, v.maintenance_version);
    } else
        f.put_i(95, static_cast<std::int32_t>((std::uint32_t(v.maintenance_version) << 16) |
                                              (v.dwg_version & 65535)));
    f.put_b(70, !entity && v.from_dxf && !v.dxf_data.empty());
    if (!entity && v.from_dxf && !v.dxf_data.empty())
        return f;
    if (entity) {
        f.put_s(version >= DxfVersion::AC1024 ? 160 : 92, std::to_string(v.graphics.size()));
        proxy_binary(f, v.graphics);
    }
    if (version >= DxfVersion::AC1021 && v.dwg_version >= 27)
        f.put_s(version >= DxfVersion::AC1024 ? 162 : 96, std::to_string(v.text_payload.bit_count));
    else if (v.text_payload.bit_count)
        throw std::invalid_argument("Proxy string stream requires DWG 2007+");
    f.put_s(version >= DxfVersion::AC1024 ? 161 : 93, std::to_string(v.payload.bit_count));
    auto data = proxy_data(v.payload), text = proxy_data(v.text_payload);
    data.insert(data.end(), text.begin(), text.end());
    proxy_binary(f, data);
    for (const auto &ref : v.references) {
        if (ref.code != 330 && ref.code != 340 && ref.code != 350 && ref.code != 360)
            throw std::invalid_argument("Invalid proxy reference kind");
        f.put_s(ref.code, hex_handle(ref.handle));
    }
    f.put_i(94, 0);
    mt_append(f, v.extra_fields);
    return f;
}
} // namespace
ProxyDefinition DxfProxyEntity::definition() const {
    return proxy_read(subclass_fields("AcDbProxyEntity"), true);
}
void DxfProxyEntity::set_definition(const ProxyDefinition &v) {
    auto f = proxy_write(v, true, version_);
    set_subclass_fields("AcDbProxyEntity", f.values);
}
ProxyDefinition DxfProxyObject::definition() const {
    auto raw = subclass_fields("AcDbProxyObject");
    auto header = raw;
    auto flag = std::find_if(header.begin(), header.end(), [](const auto &p) { return p.code == 70; });
    if (flag != header.end() && flag->value == "1")
        header.erase(flag + 1, header.end());
    auto v = proxy_read(header, false);
    if (v.from_dxf) {
        bool found = false, body = false;
        for (const auto &p : pairs_) {
            if (p.code == 100 && p.value == "AcDbProxyObject")
                found = true;
            else if (found && p.code == 70 && !body) {
                body = true;
                continue;
            } else if (body) {
                if (p.code == 1001)
                    break;
                v.dxf_data.push_back(
                    {p.code, detail::is_string_value(p.code)
                                 ? decode_string(p.value)
                                 : (binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value)});
            }
        }
        if (!v.dxf_data.empty() && (v.dxf_data.front().code == 92 || v.dxf_data.front().code == 93 ||
                                    v.dxf_data.front().code == 94 || v.dxf_data.front().code == 96 ||
                                    v.dxf_data.front().code == 160 || v.dxf_data.front().code == 161 ||
                                    v.dxf_data.front().code == 162 || v.dxf_data.front().code == 310))
            v.dxf_data.clear();
    }
    return v;
}
void DxfProxyObject::set_definition(const ProxyDefinition &v) {
    auto f = proxy_write(v, false, version_);
    DxfProxyObject staged = *this;
    auto start = std::find_if(staged.pairs_.begin(), staged.pairs_.end(),
                              [](const auto &p) { return p.code == 100 && p.value == "AcDbProxyObject"; });
    if (start == staged.pairs_.end())
        throw std::invalid_argument("Missing proxy object subclass");
    auto xdata = std::find_if(start + 1, staged.pairs_.end(), [](const auto &p) { return p.code == 1001; });
    std::vector<Pair> tail(xdata, staged.pairs_.end());
    staged.pairs_.erase(start + 1, staged.pairs_.end());
    for (const auto &p : f.values)
        staged.pairs_.push_back(
            {p.code, detail::is_string_value(p.code)
                         ? encode_string(p.value)
                         : (binary_ ? detail::binary_value_from_text(p.code, p.value) : p.value)});
    if (v.from_dxf)
        for (const auto &p : v.dxf_data) {
            if (p.code <= 0 || p.code >= 1000)
                throw std::invalid_argument("Invalid proxy DXF data group");
            staged.pairs_.push_back(
                {p.code, detail::is_string_value(p.code)
                             ? encode_string(p.value)
                             : (binary_ ? detail::binary_value_from_text(p.code, p.value) : p.value)});
        }
    staged.pairs_.insert(staged.pairs_.end(), tail.begin(), tail.end());
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_proxy_entity(const ProxyDefinition &v, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Proxy construction requires R2007+");
    for (const auto &r : v.references) {
        if (r.code >= 350)
            throw std::invalid_argument("New proxy cannot adopt owned objects");
        if (r.handle && records_with_handle(hex_handle(r.handle)).size() != 1)
            throw std::invalid_argument("Missing proxy reference");
    }
    DxfDocument staged = *this;
    DxfProxyEntity entity;
    static_cast<DxfEntity &>(entity) = staged.make_entity("ACAD_PROXY_ENTITY", "AcDbProxyEntity", options);
    entity.set_definition(v);
    auto id = staged.insert_entity(entity, options);
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::add_proxy_object(std::string_view dictionary, std::string name,
                                            const ProxyDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Proxy construction requires R2007+");
    for (const auto &r : v.references) {
        if (r.code >= 350)
            throw std::invalid_argument("New proxy cannot adopt owned objects");
        if (r.handle && records_with_handle(hex_handle(r.handle)).size() != 1)
            throw std::invalid_argument("Missing proxy reference");
    }
    DxfDocument staged = *this;
    DxfProxyObject object;
    object.version_ = version_;
    static_cast<DxfRecord &>(object) = staged.make_record("ACAD_PROXY_OBJECT");
    object.pairs_.push_back({100, "AcDbProxyObject"});
    object.set_definition(v);
    auto id = staged.add_dictionary_object(dictionary, std::move(name), object);
    *this = std::move(staged);
    return id;
}
std::vector<ClassDefinition> DxfDocument::classes() const {
    std::vector<ClassDefinition> result;
    for (const auto &entry : records_) {
        const auto &r = std::visit([](const DxfRecord &r) -> const DxfRecord & { return r; }, entry);
        if (r.type() != "CLASS")
            continue;
        std::vector<DxfRawField> raw;
        for (std::size_t i = 1; i < r.pairs_.size(); ++i) {
            const auto &p = r.pairs_[i];
            raw.push_back(
                {p.code, detail::is_string_value(p.code)
                             ? r.decode_string(p.value)
                             : (binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value)});
        }
        MLFields f(std::move(raw));
        ClassDefinition c;
        c.name = f.s(1, {});
        c.cpp_name = f.s(2, {});
        c.application = f.s(3, {});
        c.proxy_flags = f.i(90, 0);
        c.instance_count = f.i(91, 0);
        c.was_zombie = f.b(280, false);
        c.is_entity = f.b(281, false);
        c.extra_fields = f.extras();
        result.push_back(std::move(c));
    }
    return result;
}
void DxfDocument::set_class(const ClassDefinition &v) {
    if (version_ < DxfVersion::AC1012 || v.name.empty())
        throw std::invalid_argument("Invalid CLASS/version");
    DxfDocument staged = *this;
    std::optional<std::size_t> existing, end;
    std::string section;
    for (std::size_t i = 0; i < staged.records_.size(); ++i) {
        const auto &r = staged.record_at({i, std::nullopt, false});
        if (r.type() == "SECTION")
            section = r.field("", 2);
        else if (r.type() == "ENDSEC") {
            if (section == "CLASSES") {
                if (end)
                    throw std::invalid_argument("Multiple CLASSES sections");
                end = i;
            }
            section.clear();
        } else if (section == "CLASSES" && r.type() == "CLASS" &&
                   detail::symbol_name_key(r.text_field("", 1)) == detail::symbol_name_key(v.name)) {
            if (existing)
                throw std::invalid_argument("Duplicate CLASS name");
            existing = i;
        }
    }
    auto r = existing ? staged.record_at({*existing, std::nullopt, false}) : staged.make_record("CLASS");
    MLFields f;
    f.put_s(1, v.name);
    f.put_s(2, v.cpp_name);
    f.put_s(3, v.application);
    f.put_i(90, v.proxy_flags);
    if (version_ >= DxfVersion::AC1018)
        f.put_i(91, v.instance_count);
    f.put_i(280, v.was_zombie);
    f.put_i(281, v.is_entity);
    mt_append(f, v.extra_fields);
    r.pairs_.resize(1);
    for (const auto &p : f.values)
        r.pairs_.push_back(
            {p.code, detail::is_string_value(p.code)
                         ? r.encode_string(p.value)
                         : (binary_ ? detail::binary_value_from_text(p.code, p.value) : p.value)});
    if (existing)
        staged.records_[*existing] = std::move(r);
    else if (end)
        staged.records_.insert(staged.records_.begin() + *end, std::move(r));
    else {
        auto where = std::find_if(staged.records_.begin(), staged.records_.end(), [](const auto &e) {
            return std::visit([](const DxfRecord &r) { return r.type() == "EOF"; }, e);
        });
        auto section = staged.make_record("SECTION");
        section.set_field("", 2, "CLASSES");
        std::vector<Record> added{section, r, staged.make_record("ENDSEC")};
        staged.records_.insert(where, added.begin(), added.end());
    }
    staged.rebuild_indexes();
    *this = std::move(staged);
}
