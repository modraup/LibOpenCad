// SPDX-License-Identifier: MPL-2.0
// Port of raster_image, wipeout and underlay DXF readers/writers in acadrust.
namespace {
const char *underlay_name(UnderlayType type, bool object) {
    switch (type) {
    case UnderlayType::Pdf:
        return object ? "PDFDEFINITION" : "PDFUNDERLAY";
    case UnderlayType::Dwf:
        return object ? "DWFDEFINITION" : "DWFUNDERLAY";
    case UnderlayType::Dgn:
        return object ? "DGNDEFINITION" : "DGNUNDERLAY";
    }
    throw std::invalid_argument("Invalid underlay type");
}
UnderlayType underlay_kind(std::string_view name) {
    return name.substr(0, 3) == "DGN"   ? UnderlayType::Dgn
           : name.substr(0, 3) == "DWF" ? UnderlayType::Dwf
                                        : UnderlayType::Pdf;
}
std::vector<Vector2> external_boundary(MLFields &f, int code, std::vector<Vector2> initial = {}) {
    for (std::size_t n = 0; n < f.values.size(); ++n) {
        if (f.hidden[n])
            continue;
        const auto &p = f.values[n];
        MLFields one({p});
        if (p.code == code) {
            initial.push_back({one.r(code, 0), 0});
            f.used[n] = true;
        } else if (p.code == code + 10) {
            if (!initial.empty())
                initial.back().y = one.r(code + 10, 0);
            f.used[n] = true;
        }
    }
    return initial;
}
void write_external_boundary(MLFields &f, int code, const std::vector<Vector2> &v) {
    mt_count(v.size());
    for (const auto &p : v) {
        f.put_r(code, p.x);
        f.put_r(code + 10, p.y);
    }
}
} // namespace
RasterImageDefinition DxfRasterImage::definition() const {
    MLFields f(subclass_fields(is_wipeout() ? "AcDbWipeout" : "AcDbRasterImage"));
    RasterImageDefinition v;
    v.class_version = f.i(90, 0);
    v.insertion = f.p(10, {});
    v.u_vector = f.p(11, {1, 0, 0});
    v.v_vector = f.p(12, {0, 1, 0});
    v.size = {f.r(13, 1), f.r(23, 1)};
    v.definition = f.h(340, 0);
    v.reactor = f.h(360, 0);
    v.flags = f.i(70, is_wipeout() ? 7 : 5);
    v.clip_type = f.i(71, 1) == 2 ? 2 : 1;
    v.clipping = f.b(280, is_wipeout());
    v.clip_inside = f.b(290, false);
    auto tone = [&](int code, int fallback) {
        auto value = f.i(code, fallback);
        return static_cast<std::uint8_t>(is_wipeout() ? std::clamp(value, 0, 100) : value);
    };
    v.brightness = tone(281, 50);
    v.contrast = tone(282, 50);
    v.fade = tone(283, 0);
    auto seed = f.take(91).empty() ? v.boundary : std::vector<Vector2>{};
    v.boundary = external_boundary(f, 14, std::move(seed));
    v.extra_fields = f.extras();
    return v;
}
void DxfRasterImage::set_definition(const RasterImageDefinition &v) {
    if (v.clip_type != 1 && v.clip_type != 2)
        throw std::invalid_argument("Invalid image clipping type");
    MLFields f;
    f.put_i(90, v.class_version);
    f.put_p(10, v.insertion);
    f.put_p(11, v.u_vector);
    f.put_p(12, v.v_vector);
    f.put_r(13, v.size.x);
    f.put_r(23, v.size.y);
    f.put_h(340, v.definition);
    f.put_i(70, v.flags);
    f.put_b(280, v.clipping);
    f.put_i(281, v.brightness);
    f.put_i(282, v.contrast);
    f.put_i(283, v.fade);
    f.put_h(360, v.reactor);
    // Preserve IMAGE's reader-supported clip mode even though the pinned Rust writer omits it.
    f.put_b(290, v.clip_inside);
    f.put_i(71, v.clip_type);
    auto boundary = v.boundary;
    if (is_wipeout() && v.clip_type == 2 && boundary.size() >= 3 && boundary.front() != boundary.back())
        boundary.push_back(boundary.front());
    f.put_i(91, mt_count(boundary.size()));
    write_external_boundary(f, 14, boundary);
    mt_append(f, v.extra_fields);
    set_subclass_fields(is_wipeout() ? "AcDbWipeout" : "AcDbRasterImage", f.values);
}
ImageDefinition DxfImageDefinition::definition() const {
    MLFields f(subclass_fields("AcDbRasterImageDef"));
    ImageDefinition v;
    v.file_name = f.s(1, {});
    v.class_version = f.i(90, 0);
    auto size = [&](int code) {
        double n = f.r(code, 0);
        return std::isnan(n) ? 0u : static_cast<std::uint32_t>(std::clamp(n, 0., double(UINT32_MAX)));
    };
    v.width = size(10);
    v.height = size(20);
    v.pixel_size = {f.r(11, 0), f.r(21, 0)};
    v.loaded = f.b(280, false);
    auto unit = f.i(281, 0);
    v.resolution_unit = unit == 2 || unit == 5 ? unit : 0;
    v.extra_fields = f.extras();
    return v;
}
void DxfImageDefinition::set_definition(const ImageDefinition &v) {
    MLFields f;
    f.put_i(90, v.class_version);
    f.put_s(1, v.file_name);
    f.put_r(10, v.width);
    f.put_r(20, v.height);
    f.put_r(11, v.pixel_size.x);
    f.put_r(21, v.pixel_size.y);
    f.put_b(280, v.loaded);
    f.put_i(281, v.resolution_unit);
    mt_append(f, v.extra_fields);
    set_subclass_fields("AcDbRasterImageDef", f.values);
}
std::uint64_t DxfImageReactor::image_handle() const {
    return handle_value(field("AcDbRasterImageDefReactor", 330));
}
RasterVariablesDefinition DxfRasterVariables::definition() const {
    MLFields f(subclass_fields("AcDbRasterVariables"));
    RasterVariablesDefinition v;
    v.class_version = f.i(90, 0);
    v.frame = f.i(70, 1);
    v.quality = f.i(71, 1);
    v.units = f.i(72, 0);
    v.extra_fields = f.extras();
    return v;
}
void DxfRasterVariables::set_definition(const RasterVariablesDefinition &v) {
    MLFields f;
    f.put_i(90, v.class_version);
    f.put_i(70, v.frame);
    f.put_i(71, v.quality);
    f.put_i(72, v.units);
    mt_append(f, v.extra_fields);
    set_subclass_fields("AcDbRasterVariables", f.values);
}
UnderlayType DxfUnderlay::underlay_type() const { return underlay_kind(type()); }
UnderlayType DxfUnderlayDefinition::underlay_type() const { return underlay_kind(type()); }
UnderlayDefinition DxfUnderlay::definition() const {
    MLFields f(subclass_fields("AcDbUnderlayReference"));
    UnderlayDefinition v;
    v.definition = f.h(340, 0);
    v.insertion = f.p(10, {});
    v.scale = {f.r(41, 1), f.r(42, 1), f.r(43, 1)};
    v.normal = f.p(210, {0, 0, 1});
    v.rotation = f.r(50, 0) * std::acos(-1.) / 180;
    v.flags = f.i(280, 2);
    v.contrast = f.i(281, 100);
    v.fade = f.i(282, 0);
    v.boundary = external_boundary(f, 11);
    v.extra_fields = f.extras();
    return v;
}
void DxfUnderlay::set_definition(const UnderlayDefinition &v) {
    MLFields f;
    f.put_s(340, hex_handle(v.definition));
    f.put_p(10, v.insertion);
    f.put_r(41, v.scale.x);
    f.put_r(42, v.scale.y);
    f.put_r(43, v.scale.z);
    f.put_r(50, v.rotation * 180 / std::acos(-1.));
    f.put_p(210, v.normal);
    f.put_i(280, v.flags);
    f.put_i(281, v.contrast);
    f.put_i(282, v.fade);
    write_external_boundary(f, 11, v.boundary);
    mt_append(f, v.extra_fields);
    set_subclass_fields("AcDbUnderlayReference", f.values);
}
UnderlayFileDefinition DxfUnderlayDefinition::definition() const {
    MLFields f(subclass_fields("AcDbUnderlayDefinition"));
    UnderlayFileDefinition v;
    v.file_name = f.s(1, {});
    v.page_name = f.s(2, {});
    v.extra_fields = f.extras();
    for (const auto &app : extended_data())
        if (detail::symbol_name_key(app.application_name) == U"ACAD")
            for (const auto &p : app.values)
                if (p.code == 1000 && detail::symbol_name_key(std::get<std::string>(p.value)) == U"NOLOAD")
                    v.unloaded = true;
    return v;
}
void DxfUnderlayDefinition::set_definition(const UnderlayFileDefinition &v) {
    DxfUnderlayDefinition staged = *this;
    MLFields f;
    f.put_s(1, v.file_name);
    f.put_s(2, v.page_name);
    mt_append(f, v.extra_fields);
    staged.set_subclass_fields("AcDbUnderlayDefinition", f.values);
    ExtendedDataRecord data{"ACAD", {}};
    bool found = false;
    for (const auto &app : extended_data())
        if (detail::symbol_name_key(app.application_name) == U"ACAD") {
            data = app;
            found = true;
            break;
        }
    data.values.erase(std::remove_if(data.values.begin(), data.values.end(),
                                     [](const auto &p) {
                                         return p.code == 1000 &&
                                                detail::symbol_name_key(std::get<std::string>(p.value)) ==
                                                    U"NOLOAD";
                                     }),
                      data.values.end());
    if (v.unloaded)
        data.values.push_back({1000, std::string("NOLOAD")});
    if (found || v.unloaded)
        staged.upsert_extended_data(data);
    *this = std::move(staged);
}
void DxfDocument::link_image_reactors(std::uint64_t definition) {
    if (!definition)
        return;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> links;
    for_each<DxfImageDefinition>([&](const auto &d) {
        if (handle_value(d.handle()) == definition)
            if (auto owner = handle_value(d.owner_handle()))
                links[handle_value(d.handle())].push_back(owner);
    });
    for_each<DxfRasterImage>([&](const auto &image) {
        auto v = image.definition();
        if (v.definition == definition && v.reactor) {
            mleader_check_handle(*this, v.definition, "IMAGEDEF");
            mleader_check_handle(*this, v.reactor, "IMAGEDEF_REACTOR");
            links[v.definition].push_back(v.reactor);
        }
    });
    for_each<DxfUnderlay>([&](const auto &e) {
        auto v = e.definition();
        if (v.definition == definition) {
            mleader_check_handle(*this, v.definition, underlay_name(e.underlay_type(), true));
            links[definition].push_back(handle_value(e.handle()));
        }
    });
    for_each<DxfExtendedEntity>([&](const auto &e) {
        if (e.type() != "ACDBPOINTCLOUD" && e.type() != "ACDBPOINTCLOUDEX")
            return;
        std::visit(
            [&](const auto &p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<T, PointCloudDefinition> ||
                              std::is_same_v<T, PointCloudExDefinition>)
                    if (p.definition == definition && p.reactor)
                        links[definition].push_back(p.reactor);
            },
            e.definition());
    });
    for (auto &entry : links) {
        auto &record = const_cast<DxfRecord &>(*get_record(hex_handle(entry.first)));
        auto &pairs = record.pairs_;
        std::size_t close = pairs.size(), insert = pairs.size();
        std::unordered_set<std::uint64_t> existing;
        unsigned depth = 0;
        for (std::size_t i = 1; i < pairs.size(); ++i) {
            const auto &p = pairs[i];
            if (!depth && p.code == 100) {
                insert = i;
                break;
            }
            if (!depth && p.code == 102 && p.value == "{ACAD_REACTORS") {
                auto j = i + 1;
                for (; j < pairs.size() && pairs[j].code == 330; ++j)
                    existing.insert(handle_value(pairs[j].value));
                if (j >= pairs.size() || pairs[j].code != 102 || pairs[j].value != "}")
                    throw std::invalid_argument("Malformed image reactor group");
                close = j;
                i = j;
                continue;
            }
            if (p.code == 102) {
                if (!p.value.empty() && p.value.front() == '{')
                    ++depth;
                else if (depth && p.value == "}")
                    --depth;
            }
        }
        std::vector<DxfRecord::Pair> added;
        for (auto h : entry.second)
            if (existing.insert(h).second)
                added.push_back({330, hex_handle(h)});
        if (added.empty())
            continue;
        if (close == pairs.size()) {
            added.insert(added.begin(), {102, "{ACAD_REACTORS"});
            added.push_back({102, "}"});
            close = insert;
        }
        pairs.insert(pairs.begin() + close, added.begin(), added.end());
    }
}
std::uint64_t DxfDocument::add_image_definition(std::string name, const ImageDefinition &v) {
    if (version_ < DxfVersion::AC1024)
        throw std::invalid_argument("Image construction requires R2010+");
    DxfDocument staged = *this;
    auto root = staged.root_dictionary_handle();
    auto dictionary = staged.dictionary_entry(root, "ACAD_IMAGE_DICT");
    if (dictionary.empty())
        dictionary = hex_handle(staged.add_dictionary(root, "ACAD_IMAGE_DICT"));
    DxfImageDefinition object;
    static_cast<DxfRecord &>(object) = staged.make_record("IMAGEDEF");
    object.pairs_.push_back({100, "AcDbRasterImageDef"});
    object.set_definition(v);
    auto id = staged.add_dictionary_object(dictionary, std::move(name), object);
    staged.link_image_reactors(id);
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::add_raster_image(const RasterImageDefinition &value,
                                            const EntityOptions &options) {
    if (version_ < DxfVersion::AC1024)
        throw std::invalid_argument("Image construction requires R2010+");
    if (!value.definition || value.reactor)
        throw std::invalid_argument("New image requires definition and fresh reactor");
    mleader_check_handle(*this, value.definition, "IMAGEDEF");
    DxfDocument staged = *this;
    DxfRasterImage image;
    static_cast<DxfEntity &>(image) = staged.make_entity("IMAGE", "AcDbRasterImage", options);
    image.set_definition(value);
    auto id = staged.insert_entity(image, options);
    auto reactor = staged.make_record("IMAGEDEF_REACTOR");
    reactor.set_field("", 330, hex_handle(id));
    reactor.pairs_.push_back({100, "AcDbRasterImageDefReactor"});
    reactor.set_field("AcDbRasterImageDefReactor", 90,
                      binary_ ? detail::binary_value_from_text(90, "2") : "2");
    reactor.set_field("AcDbRasterImageDefReactor", 330, hex_handle(id));
    auto rid = staged.insert_object(reactor);
    staged.for_each<DxfRasterImage>([&](auto &e) {
        if (handle_value(e.handle()) == id) {
            auto v = value;
            v.reactor = rid;
            e.set_definition(v);
        }
    });
    staged.link_image_reactors(value.definition);
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::add_wipeout(const RasterImageDefinition &v, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1024)
        throw std::invalid_argument("WIPEOUT construction requires R2010+");
    if (v.reactor)
        throw std::invalid_argument("New wipeout cannot adopt a reactor");
    mleader_check_handle(*this, v.definition, "IMAGEDEF");
    DxfDocument staged = *this;
    DxfRasterImage image;
    static_cast<DxfEntity &>(image) = staged.make_entity("WIPEOUT", "AcDbWipeout", options);
    image.set_definition(v);
    auto id = staged.insert_entity(image, options);
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::add_underlay_definition(UnderlayType type, std::string name,
                                                   const UnderlayFileDefinition &v) {
    if (version_ < DxfVersion::AC1024)
        throw std::invalid_argument("Underlay construction requires R2010+");
    DxfDocument staged = *this;
    auto root = staged.root_dictionary_handle();
    std::string key = type == UnderlayType::Pdf   ? "ACAD_PDFDEFINITIONS"
                      : type == UnderlayType::Dwf ? "ACAD_DWFDEFINITIONS"
                                                  : "ACAD_DGNDEFINITIONS";
    auto dict = staged.dictionary_entry(root, key);
    if (dict.empty())
        dict = hex_handle(staged.add_dictionary(root, key));
    DxfUnderlayDefinition object;
    static_cast<DxfRecord &>(object) = staged.make_record(underlay_name(type, true));
    object.pairs_.push_back({100, "AcDbUnderlayDefinition"});
    object.set_definition(v);
    auto id = staged.add_dictionary_object(dict, std::move(name), object);
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::add_underlay(UnderlayType type, const UnderlayDefinition &v,
                                        const EntityOptions &options) {
    if (version_ < DxfVersion::AC1024)
        throw std::invalid_argument("Underlay construction requires R2010+");
    if (!v.definition)
        throw std::invalid_argument("Underlay requires a definition");
    mleader_check_handle(*this, v.definition, underlay_name(type, true));
    DxfDocument staged = *this;
    DxfUnderlay image;
    static_cast<DxfEntity &>(image) =
        staged.make_entity(underlay_name(type, false), "AcDbUnderlayReference", options);
    image.set_definition(v);
    auto id = staged.insert_entity(image, options);
    staged.link_image_reactors(v.definition);
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::set_raster_variables(const RasterVariablesDefinition &v) {
    DxfDocument staged = *this;
    auto root = staged.root_dictionary_handle();
    auto h = staged.dictionary_entry(root, "ACAD_IMAGE_VARS");
    std::uint64_t id = 0;
    if (h.empty()) {
        DxfRasterVariables object;
        static_cast<DxfRecord &>(object) = staged.make_record("RASTERVARIABLES");
        object.pairs_.push_back({100, "AcDbRasterVariables"});
        object.set_definition(v);
        id = staged.add_dictionary_object(root, "ACAD_IMAGE_VARS", object);
    } else {
        mleader_check_handle(staged, handle_value(h), "RASTERVARIABLES");
        id = handle_value(h);
        staged.for_each<DxfRasterVariables>([&](auto &r) {
            if (handle_value(r.handle()) == id)
                r.set_definition(v);
        });
    }
    *this = std::move(staged);
    return id;
}

std::string DxfDocument::root_dictionary_handle() const {
    std::string root, section;
    for (const auto &entry : records_) {
        const auto &r = std::visit([](const DxfRecord &r) -> const DxfRecord & { return r; }, entry);
        if (r.type() == "SECTION")
            section = r.field("", 2);
        else if (r.type() == "ENDSEC")
            section.clear();
        else if (section == "OBJECTS" && r.type() == "DICTIONARY" && !handle_value(r.owner_handle())) {
            if (!root.empty())
                throw std::invalid_argument("Ambiguous root dictionary");
            root = r.handle();
        }
    }
    if (root.empty() || records_with_handle(root).size() != 1)
        throw std::invalid_argument("Missing unique root dictionary");
    return root;
}

std::int32_t DxfImageReactor::class_version() const {
    MLFields f(subclass_fields("AcDbRasterImageDefReactor"));
    return f.i(90, 2);
}
void DxfImageReactor::set_class_version(std::int32_t value) {
    auto fields = subclass_fields("AcDbRasterImageDefReactor");
    bool found = false;
    for (auto &p : fields)
        if (p.code == 90) {
            p.value = std::to_string(value);
            found = true;
        }
    if (!found)
        fields.insert(fields.begin(), {90, std::to_string(value)});
    set_subclass_fields("AcDbRasterImageDefReactor", fields);
}
