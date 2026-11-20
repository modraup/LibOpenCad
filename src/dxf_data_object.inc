// SPDX-License-Identifier: MPL-2.0
// acadrust data_objects.rs and point-cloud/context-manager class objects.
namespace {
const char *empty_data_names[] = {
    "BREAKPOINTREF",         "ACDSRECORD", "ACDSSCHEMA", "DUMMY", "LONG_TRANSACTION", "OBJECT_PTR",
    "PARTIAL_VIEWING_FILTER"};
const char *empty_data_scopes[] = {"AcDbBreakPointRef",   "AcDsRecord",          "AcDsSchema", "",
                                   "AcDbLongTransaction", "CAseDLPNTableRecord", "AcDbFilter"};
std::pair<std::string, std::string> data_object_names(const DataObjectDefinition &d) {
    return std::visit(
        [](const auto &p) -> std::pair<std::string, std::string> {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BreakDataDefinition>)
                return {"BREAKDATA", "AcDbBreakData"};
            else if constexpr (std::is_same_v<T, IdBufferDefinition>)
                return {"IDBUFFER", "AcDbIdBuffer"};
            else if constexpr (std::is_same_v<T, IndexDefinition>)
                return {"INDEX", "AcDbIndex"};
            else if constexpr (std::is_same_v<T, LayerIndexDefinition>)
                return {"LAYER_INDEX", "AcDbIndex"};
            else if constexpr (std::is_same_v<T, EmptyDataDefinition>) {
                int k = int(p.kind);
                if (k < 0 || k > 6)
                    throw std::invalid_argument("Invalid empty data class");
                return {empty_data_names[k], empty_data_scopes[k]};
            } else if constexpr (std::is_same_v<T, CloudFileDefinition>)
                return p.extended ? std::make_pair("ACDBPOINTCLOUDDEF_EX", "AcDbPointCloudDefEx")
                                  : std::make_pair("ACDBPOINTCLOUDDEF", "AcDbPointCloudDef");
            else if constexpr (std::is_same_v<T, CloudReactorDefinition>)
                return p.extended
                           ? std::make_pair("ACDBPOINTCLOUDDEF_REACTOR_EX", "AcDbPointCloudDefReactorEx")
                           : std::make_pair("ACDBPOINTCLOUDDEF_REACTOR", "AcDbPointCloudDefReactor");
            else if constexpr (std::is_same_v<T, CloudColorMapDefinition>)
                return {"ACDBPOINTCLOUDCOLORMAP", "AcDbPointCloudColorMap"};
            else if constexpr (std::is_same_v<T, CoordinationFileDefinition>)
                return {"NAVISWORKSMODELDEF", "AcDbNavisworksModelDef"};
            else
                return {"CONTEXTDATAMANAGER", "AcDbContextDataManager"};
        },
        d.payload);
}
// Counted positional collections are bounded by the available wire fields before allocating.
struct DataCursor {
    std::vector<DxfRawField> fields;
    std::size_t at = 0;
    explicit DataCursor(std::vector<DxfRawField> f) : fields(std::move(f)) {}
    MLFields take(int code) {
        if (at >= fields.size() || fields[at].code != code)
            throw std::invalid_argument("Malformed data object collection");
        return MLFields({fields[at++]});
    }
    int i(int code) { return take(code).i(code, 0); }
    std::uint64_t h(int code) { return take(code).h(code, 0); }
    std::string s(int code) { return take(code).s(code, {}); }
    int count(int code) {
        auto n = i(code);
        if (n < 0 || std::size_t(n) > fields.size() - at)
            throw std::invalid_argument("Invalid data object count");
        return n;
    }
};
IndexDefinition data_timestamp(double n) {
    if (!std::isfinite(n) || std::floor(n) < INT32_MIN || std::floor(n) > INT32_MAX)
        throw std::invalid_argument("Invalid index timestamp");
    return {static_cast<std::int32_t>(std::floor(n)),
            static_cast<std::int32_t>(std::round((n - std::floor(n)) * 86400000.))};
}
} // namespace
DataObjectDefinition DxfDataObject::definition() const {
    DataObjectDefinition v;
    auto name = type();
    if (name == "BREAKDATA") {
        MLFields f(subclass_fields("AcDbBreakData"));
        BreakDataDefinition p;
        p.version = f.i(70, 0);
        p.dimension = f.h(330, f.h(331, 0));
        f.take(90);
        v.extra_fields = f.extras();
        for (std::size_t i = 0; i < pairs_.size(); ++i) {
            if (pairs_[i].code != 100 || pairs_[i].value != "AcDbBreakPointRef")
                continue;
            std::vector<DxfRawField> raw;
            for (++i; i < pairs_.size() && pairs_[i].code != 100 && pairs_[i].code < 1000; ++i) {
                auto &p = pairs_[i];
                raw.push_back(
                    {p.code, detail::is_string_value(p.code)
                                 ? decode_string(p.value)
                                 : (binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value)});
            }
            --i;
            MLFields point(raw);
            p.points.push_back({static_cast<std::int16_t>(point.i(71, 0)),
                                static_cast<std::int16_t>(point.i(72, 0)), point.i(91, 0), point.p(10, {}),
                                point.p(11, {}), point.h(331, 0), point.i(73, 0), point.i(92, 0)});
        }
        v.payload = p;
    } else if (name == "IDBUFFER") {
        MLFields f(subclass_fields("AcDbIdBuffer"));
        IdBufferDefinition p;
        for (auto n : f.take(330)) {
            MLFields one({f.values[n]});
            p.objects.push_back(one.h(330, 0));
        }
        v.payload = p;
        v.extra_fields = f.extras();
    } else if (name == "INDEX" || name == "LAYER_INDEX") {
        MLFields f(subclass_fields("AcDbIndex"));
        auto stamp = data_timestamp(f.r(40, 0));
        v.extra_fields = f.extras();
        if (name == "INDEX")
            v.payload = stamp;
        else {
            LayerIndexDefinition p;
            p.updated = stamp;
            MLFields entries(subclass_fields("AcDbLayerIndex"));
            auto starts = entries.take(8);
            for (std::size_t i = 0; i < starts.size(); ++i) {
                auto end = i + 1 < starts.size() ? starts[i + 1] : entries.values.size();
                MLFields e({entries.values.begin() + starts[i], entries.values.begin() + end});
                p.entries.push_back({e.i(90, 0), e.s(8, {}), e.h(360, 0)});
            }
            v.payload = p;
        }
    } else if (name == "ACDBPOINTCLOUDDEF" || name == "ACDBPOINTCLOUDDEF_EX") {
        CloudFileDefinition p;
        p.extended = name == "ACDBPOINTCLOUDDEF_EX";
        MLFields f(subclass_fields(p.extended ? "AcDbPointCloudDefEx" : "AcDbPointCloudDef"));
        p.version = f.i(90, 0);
        p.filename = f.s(1, {});
        p.loaded = f.b(280, false);
        for (auto n : f.take(160))
            p.point_count = std::stoll(f.values[n].value);
        p.min = f.p(10, {});
        p.max = f.p(11, {});
        v.payload = p;
        v.extra_fields = f.extras();
    } else if (name == "ACDBPOINTCLOUDDEF_REACTOR" || name == "ACDBPOINTCLOUDDEF_REACTOR_EX") {
        CloudReactorDefinition p;
        p.extended = name == "ACDBPOINTCLOUDDEF_REACTOR_EX";
        MLFields f(subclass_fields(p.extended ? "AcDbPointCloudDefReactorEx" : "AcDbPointCloudDefReactor"));
        p.version = f.i(90, 0);
        v.payload = p;
        v.extra_fields = f.extras();
    } else if (name == "NAVISWORKSMODELDEF") {
        MLFields f(subclass_fields("AcDbNavisworksModelDef"));
        CoordinationFileDefinition p;
        p.flags = f.i(70, 0);
        p.path = f.s(1, {});
        p.min = f.p(10, {});
        p.max = f.p(11, {});
        auto flags = f.take(290);
        if (!flags.empty()) {
            MLFields one({f.values[flags[0]]});
            p.loaded = one.b(290, false);
        }
        if (flags.size() > 1) {
            MLFields one({f.values[flags[1]]});
            p.host_visibility = one.b(290, false);
        }
        v.payload = p;
        v.extra_fields = f.extras();
    } else if (name == "ACDBPOINTCLOUDCOLORMAP") {
        DataCursor c(subclass_fields("AcDbPointCloudColorMap"));
        CloudColorMapDefinition p;
        p.version = c.i(70);
        p.intensity = c.s(1);
        p.elevation = c.s(1);
        p.classification = c.s(1);
        auto ramps = [&](auto &out) {
            auto n = c.count(90);
            for (int i = 0; i < n; ++i) {
                CloudColorRamp r;
                r.id = c.s(1);
                r.version = c.i(70);
                auto colors = c.count(90);
                for (int j = 0; j < colors; ++j) {
                    auto color = c.i(91);
                    auto visible = c.i(290) != 0;
                    r.colors.push_back({color, visible});
                }
                r.name = c.s(1);
                out.push_back(r);
            }
        };
        ramps(p.ramps);
        ramps(p.classification_ramps);
        v.payload = p;
        v.extra_fields.assign(c.fields.begin() + c.at, c.fields.end());
    } else if (name == "CONTEXTDATAMANAGER") {
        DataCursor c(subclass_fields("AcDbContextDataManager"));
        ContextManagerDefinition p;
        p.context = c.h(340);
        auto n = c.count(90);
        for (int i = 0; i < n; ++i) {
            ContextSubManager m;
            m.handle = c.h(340);
            auto entries = c.count(91);
            for (int j = 0; j < entries; ++j) {
                auto h = c.h(350);
                m.entries.push_back({h, c.s(3)});
            }
            p.managers.push_back(m);
        }
        v.payload = p;
        v.extra_fields.assign(c.fields.begin() + c.at, c.fields.end());
    } else {
        int kind = -1;
        for (int i = 0; i < 7; ++i)
            if (name == empty_data_names[i])
                kind = i;
        if (kind < 0)
            throw std::invalid_argument("Unknown data object");
        v.payload = EmptyDataDefinition{static_cast<EmptyDataDefinition::Kind>(kind)};
        if (*empty_data_scopes[kind])
            v.extra_fields = subclass_fields(empty_data_scopes[kind]);
    }
    return v;
}
void DxfDataObject::set_definition(const DataObjectDefinition &v) {
    auto names = data_object_names(v);
    if (type() != names.first)
        throw std::invalid_argument("Data object kind cannot change");
    MLFields f;
    if (!names.second.empty())
        f.put_s(100, names.second);
    auto count = [&](int code, std::size_t n) {
        if (n > INT32_MAX)
            throw std::invalid_argument("Data object collection too large");
        f.put_i(code, n);
    };
    auto timestamp = [&](IndexDefinition p) {
        if (p.milliseconds < 0 || p.milliseconds >= 86400000)
            throw std::invalid_argument("Invalid index milliseconds");
        f.put_r(40, double(p.julian_day) + double(p.milliseconds) / 86400000.);
    };
    std::visit(
        [&](const auto &p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BreakDataDefinition>) {
                f.put_i(70, p.version);
                f.put_s(330, hex_handle(p.dimension));
                count(90, p.points.size());
                mt_append(f, v.extra_fields);
                for (const auto &point : p.points) {
                    f.put_s(100, "AcDbBreakPointRef");
                    f.put_i(71, point.type);
                    f.put_i(72, point.flags);
                    f.put_i(91, point.identifier);
                    f.put_p(10, point.first);
                    f.put_p(11, point.second);
                    f.put_s(331, hex_handle(point.subentity_object));
                    f.put_i(73, point.subentity_type);
                    f.put_i(92, point.subentity_index);
                }
                return;
            } else if constexpr (std::is_same_v<T, IdBufferDefinition>) {
                for (auto h : p.objects)
                    f.put_s(330, hex_handle(h));
            } else if constexpr (std::is_same_v<T, IndexDefinition>)
                timestamp(p);
            else if constexpr (std::is_same_v<T, LayerIndexDefinition>) {
                timestamp(p.updated);
                f.put_s(100, "AcDbLayerIndex");
                f.put_i(90, 0);
                for (const auto &e : p.entries) {
                    f.put_s(8, e.name);
                    f.put_s(360, hex_handle(e.id_buffer));
                    f.put_i(90, e.count);
                }
            } else if constexpr (std::is_same_v<T, CloudFileDefinition>) {
                f.put_i(90, p.version);
                f.put_s(1, p.filename);
                f.put_b(280, p.loaded);
                f.put_s(160, std::to_string(p.point_count));
                f.put_p(10, p.min);
                f.put_p(11, p.max);
            } else if constexpr (std::is_same_v<T, CloudReactorDefinition>)
                f.put_i(90, p.version);
            else if constexpr (std::is_same_v<T, CloudColorMapDefinition>) {
                f.put_i(70, p.version);
                f.put_s(1, p.intensity);
                f.put_s(1, p.elevation);
                f.put_s(1, p.classification);
                auto ramps = [&](const auto &list) {
                    count(90, list.size());
                    for (const auto &r : list) {
                        f.put_s(1, r.id);
                        f.put_i(70, r.version);
                        count(90, r.colors.size());
                        for (const auto &c : r.colors) {
                            f.put_i(91, c.color);
                            f.put_b(290, c.visible);
                        }
                        f.put_s(1, r.name);
                    }
                };
                ramps(p.ramps);
                ramps(p.classification_ramps);
            } else if constexpr (std::is_same_v<T, CoordinationFileDefinition>) {
                f.put_i(70, p.flags);
                f.put_s(1, p.path);
                f.put_b(290, p.loaded);
                f.put_p(10, p.min);
                f.put_p(11, p.max);
                f.put_b(290, p.host_visibility);
            } else if constexpr (std::is_same_v<T, ContextManagerDefinition>) {
                f.put_s(340, hex_handle(p.context));
                count(90, p.managers.size());
                for (const auto &m : p.managers) {
                    f.put_s(340, hex_handle(m.handle));
                    count(91, m.entries.size());
                    for (const auto &e : m.entries) {
                        f.put_s(350, hex_handle(e.item));
                        f.put_s(3, e.name);
                    }
                }
            }
            mt_append(f, v.extra_fields);
        },
        v.payload);
    auto staged = pairs_;
    auto begin = std::find_if(staged.begin(), staged.end(), [](const auto &p) { return p.code == 100; });
    auto end = std::find_if(begin, staged.end(), [](const auto &p) { return p.code == 1001; });
    auto at = begin - staged.begin();
    staged.erase(begin, end);
    std::vector<Pair> body;
    for (const auto &p : f.values) {
        auto encoded = detail::binary_value_from_text(p.code, p.value);
        body.push_back({p.code, detail::is_string_value(p.code) ? encode_string(p.value)
                                                                : (binary_ ? encoded : p.value)});
    }
    staged.insert(staged.begin() + at, body.begin(), body.end());
    pairs_ = std::move(staged);
}
std::uint64_t DxfDocument::add_data_object(std::string_view dictionary, std::string name,
                                           const DataObjectDefinition &v) {
    if (version_ < DxfVersion::AC1027)
        throw std::invalid_argument("Data object construction requires R2013+");
    auto names = data_object_names(v);
    DxfDataObject object;
    static_cast<DxfRecord &>(object) = make_record(names.first);
    object.set_definition(v);
    for (auto [at, owner] : reference_positions(object)) {
        auto h = parse_handle(object.pairs_[at].value);
        if (h && *h && records_with_handle(hex_handle(*h)).size() != 1)
            throw std::invalid_argument("Missing data object reference");
    }
    return add_dictionary_object(dictionary, std::move(name), object);
}
