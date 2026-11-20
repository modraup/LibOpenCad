// SPDX-License-Identifier: MPL-2.0
// acadrust objects/object_context_data.rs and DXF context reader/writer.
namespace {
const char *context_names[] = {"ACDB_ANNOTSCALEOBJECTCONTEXTDATA_CLASS",
                               "ACDB_BLKREFOBJECTCONTEXTDATA_CLASS",
                               "ACDB_TEXTOBJECTCONTEXTDATA_CLASS",
                               "ACDB_MTEXTOBJECTCONTEXTDATA_CLASS",
                               "",
                               "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS",
                               "ACDB_MTEXTATTRIBUTEOBJECTCONTEXTDATA_CLASS",
                               "ACDB_LEADEROBJECTCONTEXTDATA_CLASS",
                               "ACDB_FCFOBJECTCONTEXTDATA_CLASS",
                               "ACDB_HATCHSCALECONTEXTDATA_CLASS",
                               "ACDB_HATCHVIEWCONTEXTDATA_CLASS",
                               "ACDB_OBJECTCONTEXTDATA_CLASS"};
const char *context_dim_names[] = {
    "ACDB_ALDIMOBJECTCONTEXTDATA_CLASS",   "ACDB_ANGDIMOBJECTCONTEXTDATA_CLASS",
    "ACDB_DMDIMOBJECTCONTEXTDATA_CLASS",   "ACDB_RADIMOBJECTCONTEXTDATA_CLASS",
    "ACDB_RADIMLGOBJECTCONTEXTDATA_CLASS", "ACDB_ORDDIMOBJECTCONTEXTDATA_CLASS"};
const char *context_dim_scopes[] = {
    "AcDbAlignedDimensionObjectContextData",     "AcDbAngularDimensionObjectContextData",
    "AcDbDiametricDimensionObjectContextData",   "AcDbRadialDimensionObjectContextData",
    "AcDbRadialDimensionLargeObjectContextData", "AcDbOrdinateDimensionObjectContextData"};
bool known_object_context(std::string_view name) {
    for (auto p : context_names)
        if (name == p && !name.empty())
            return true;
    for (auto p : context_dim_names)
        if (name == p)
            return true;
    return false;
}
std::string context_name(const ObjectContextDefinition &v) {
    if (auto dim = std::get_if<DimensionContext>(&v.payload)) {
        auto n = int(dim->kind);
        if (n < 0 || n >= 6)
            throw std::invalid_argument("Invalid dimension context kind");
        return context_dim_names[n];
    }
    return context_names[v.payload.index()];
}
MTextContext context_mtext(MLFields &f) {
    MTextContext v;
    v.attachment = f.i(70, 1);
    v.x_axis = f.p(10, {1, 0, 0});
    v.insertion = f.p(11, {});
    v.width = f.r(40, 0);
    v.height = f.r(41, 0);
    v.extents_width = f.r(42, 0);
    v.extents_height = f.r(43, 0);
    v.column_type = f.i(71, 0);
    if (v.column_type) {
        ContextColumns c;
        c.count = f.i(72, 0);
        c.width = f.r(44, 0);
        c.gutter = f.r(45, 0);
        c.auto_height = f.b(73, false);
        c.reversed = f.b(74, false);
        for (auto at : f.take(46)) {
            MLFields one({f.values[at]});
            c.heights.push_back(one.r(46, 0));
        }
        v.columns = c;
    }
    return v;
}
void context_write_mtext(MLFields &f, const MTextContext &v) {
    f.put_i(70, v.attachment);
    f.put_p(10, v.x_axis);
    f.put_p(11, v.insertion);
    f.put_r(40, v.width);
    f.put_r(41, v.height);
    f.put_r(42, v.extents_width);
    f.put_r(43, v.extents_height);
    f.put_i(71, v.column_type);
    if (v.column_type && v.columns) {
        const auto &c = *v.columns;
        f.put_i(72, c.count);
        f.put_r(44, c.width);
        f.put_r(45, c.gutter);
        f.put_b(73, c.auto_height);
        f.put_b(74, c.reversed);
        if (v.column_type == 2 && !c.auto_height)
            for (auto h : c.heights)
                f.put_r(46, h);
    }
}
TextContext context_text(MLFields &f) {
    TextContext v;
    v.horizontal_mode = f.i(70, 0);
    v.rotation = f.r(50, 0) * radians_per_degree;
    v.insertion = {f.r(10, 0), f.r(20, 0)};
    v.alignment = {f.r(11, 0), f.r(21, 0)};
    return v;
}
void context_write_text(MLFields &f, const TextContext &v) {
    f.put_i(70, v.horizontal_mode);
    f.put_r(50, v.rotation / radians_per_degree);
    f.put_r(10, v.insertion.x);
    f.put_r(20, v.insertion.y);
    f.put_r(11, v.alignment.x);
    f.put_r(21, v.alignment.y);
}
} // namespace
ObjectContextDefinition DxfObjectContext::definition() const {
    ObjectContextDefinition v;
    MLFields base(subclass_fields("AcDbObjectContextData")),
        scale(subclass_fields("AcDbAnnotScaleObjectContextData"));
    v.version = base.i(70, 3);
    v.is_default = base.b(290, false);
    v.scale = scale.h(340, 0);
    v.base_extra = base.extras();
    auto name = type();
    int kind = -1, dim_kind = -1;
    for (int i = 0; i < 12; ++i)
        if (name == context_names[i])
            kind = i;
    for (int i = 0; i < 6; ++i)
        if (name == context_dim_names[i]) {
            kind = 4;
            dim_kind = i;
        }
    if (kind < 0)
        throw std::invalid_argument("Unknown annotation context");
    // Historical files may add a subtype marker; native files keep simple payloads in the scale subclass.
    auto simple = [&](const char *scope) {
        if (std::none_of(pairs_.begin(), pairs_.end(),
                         [&](const auto &p) { return p.code == 100 && p.value == scope; }))
            return scale;
        return MLFields(subclass_fields(scope));
    };
    if (kind == 0)
        v.payload = AnnotationScaleContext{};
    else if (kind == 1) {
        auto f = simple("AcDbBlkRefObjectContextData");
        BlockReferenceContext c;
        c.rotation = f.r(50, 0) * radians_per_degree;
        c.insertion = f.p(10, {});
        c.scale_factor = {f.r(41, 1), f.r(42, 1), f.r(43, 1)};
        v.payload = c;
        scale = f;
    } else if (kind == 2) {
        auto f = simple("AcDbTextObjectContextData");
        v.payload = context_text(f);
        scale = f;
    } else if (kind == 3) {
        auto f = simple("AcDbMTextObjectContextData");
        v.payload = context_mtext(f);
        scale = f;
    } else if (kind == 4) {
        MLFields f(subclass_fields("AcDbDimensionObjectContextData"));
        DimensionContext c;
        c.kind = static_cast<DimensionContext::Kind>(dim_kind);
        c.block_name = f.s(2, {});
        c.reserved = f.b(293, false);
        c.text_location = {f.r(10, 0), f.r(20, 0)};
        f.take(30);
        c.default_text_location = f.b(294, false);
        c.text_rotation = f.r(140, 0);
        c.dimtofl = f.b(298, false);
        c.dimosxd = f.b(291, false);
        c.dimatfit = f.b(70, false);
        c.dimtix = f.b(292, false);
        c.dimtmove = f.b(71, false);
        c.override_code = f.i(280, 0);
        c.has_arrow2 = f.b(295, false);
        c.flip_arrow2 = f.b(296, false);
        c.flip_arrow1 = f.b(297, false);
        v.payload_extra = f.extras();
        MLFields points(subclass_fields(context_dim_scopes[dim_kind]));
        c.first = points.p(dim_kind == 4 ? 12 : 11, {});
        if (dim_kind == 2 || dim_kind >= 4)
            c.second = points.p(dim_kind == 4 ? 13 : 12, {});
        v.payload = c;
    } else if (kind == 5) {
        auto raw = subclass_fields("AcDbMLeaderObjectContextData");
        std::size_t pos = 0;
        auto node = mleader_node(raw, pos, 0);
        MultiLeaderContext c;
        for (const auto &child : node.children)
            c = mleader_context(child, std::move(c));
        v.payload = c;
        v.payload_extra = node.fields;
    } else if (kind == 6) {
        MTextAttributeContext c;
        c.placement = context_text(scale);
        c.enabled = scale.b(290, false);
        auto embedded =
            std::find_if(pairs_.begin(), pairs_.end(), [](const auto &p) { return p.code == 101; });
        if (embedded != pairs_.end()) {
            DxfObjectContext child;
            child.binary_ = binary_;
            child.code_page_ = code_page_;
            child.pairs_.push_back({0, context_names[3]});
            child.pairs_.insert(child.pairs_.end(), embedded + 1, pairs_.end());
            auto def = child.definition();
            c.embedded = EmbeddedMTextContext{def.version, def.is_default, def.scale,
                                              std::get<MTextContext>(def.payload)};
        }
        v.payload = c;
    } else if (kind == 7) {
        auto f = simple("AcDbLeaderObjectContextData");
        LeaderContext c;
        f.take(70);
        c.points = extended_points(f, 10);
        c.x_direction = f.p(11, {1, 0, 0});
        c.annotation_enabled = f.b(290, false);
        c.insertion_offset = f.p(12, {});
        c.endpoint_projection = f.p(13, {});
        v.payload = c;
        scale = f;
    } else if (kind == 8) {
        MLFields f(subclass_fields("AcDbFcfObjectContextData"));
        v.payload = FeatureControlFrameContext{f.p(10, {}), f.p(11, {1, 0, 0})};
        v.payload_extra = f.extras();
    } else if (kind == 9 || kind == 10) {
        MLFields f(subclass_fields("AcDbHatchObjectContextData"));
        HatchScaleContext c;
        auto raw = f.values;
        auto loops = f.take(90);
        std::size_t head_end = loops.empty() ? raw.size() : loops.front();
        MLFields head({raw.begin(), raw.begin() + head_end});
        c.scale = head.r(40, 1);
        c.base = head.p(10, {});
        auto lines = head.take(53);
        head.take(78);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            auto end = i + 1 < lines.size() ? lines[i + 1] : head.values.size();
            MLFields line({head.values.begin() + lines[i], head.values.begin() + end});
            HatchPatternLine p;
            p.angle = line.r(53, 0) * radians_per_degree;
            p.base_point = {line.r(43, 0), line.r(44, 0)};
            p.offset = {line.r(45, 0), line.r(46, 0)};
            for (auto at : line.take(49)) {
                MLFields one({line.values[at]});
                p.dash_lengths.push_back(one.r(49, 0));
            }
            c.pattern_lines.push_back(p);
        }
        for (std::size_t i = 1; i < loops.size(); ++i) {
            auto end = i + 1 < loops.size() ? loops[i + 1] : raw.size();
            MLFields loop({raw.begin() + loops[i], raw.begin() + end});
            HatchLoopContext l;
            l.type = loop.i(90, 0);
            l.supports_context = loop.b(290, false);
            if (!l.supports_context) {
                DxfHatch hatch;
                hatch.pairs_ = {{0, "HATCH"}, {100, "AcDbHatch"}, {92, std::to_string(l.type)}};
                for (auto p : loop.extras())
                    hatch.pairs_.push_back({p.code, p.value});
                hatch.pairs_.push_back({97, "0"});
                auto value = hatch.definition();
                if (!value.paths.empty())
                    l.boundary = value.paths.front();
            }
            c.loops.push_back(l);
        }
        if (kind == 9)
            v.payload = c;
        else {
            MLFields view(subclass_fields("AcDbHatchViewContextData"));
            HatchViewContext h;
            h.hatch = c;
            h.view = view.h(330, 0);
            h.normal = view.p(10, {0, 0, 1});
            h.rotation = view.r(51, 0) * radians_per_degree;
            h.evaluate = view.b(290, false);
            v.payload = h;
            v.payload_extra = view.extras();
        }
    } else
        v.payload = OpaqueObjectContext{};
    scale.take(340);
    v.scale_extra = scale.extras();
    return v;
}
void DxfObjectContext::set_definition(const ObjectContextDefinition &v) {
    if (type() != context_name(v))
        throw std::invalid_argument("Context kind cannot change");
    MLFields out;
    auto scope = [&](const std::string &s) { out.put_s(100, s); };
    scope("AcDbObjectContextData");
    out.put_i(70, v.version);
    out.put_b(290, v.is_default);
    mt_append(out, v.base_extra);
    scope("AcDbAnnotScaleObjectContextData");
    out.put_s(340, hex_handle(v.scale));
    mt_append(out, v.scale_extra);
    auto hatch = [&](const HatchScaleContext &h) {
        scope("AcDbHatchObjectContextData");
        if (h.pattern_lines.size() > 32767 || h.loops.size() > INT32_MAX)
            throw std::invalid_argument("Too many hatch context records");
        out.put_i(78, h.pattern_lines.size());
        for (const auto &p : h.pattern_lines) {
            out.put_r(53, p.angle / radians_per_degree);
            out.put_r(43, p.base_point.x);
            out.put_r(44, p.base_point.y);
            out.put_r(45, p.offset.x);
            out.put_r(46, p.offset.y);
            if (p.dash_lengths.size() > 32767)
                throw std::invalid_argument("Too many hatch dashes");
            out.put_i(79, p.dash_lengths.size());
            for (auto n : p.dash_lengths)
                out.put_r(49, n);
        }
        out.put_r(40, h.scale);
        out.put_p(10, h.base);
        out.put_i(90, h.loops.size());
        for (const auto &l : h.loops) {
            out.put_i(90, l.type);
            out.put_b(290, l.supports_context);
            if (l.supports_context)
                continue;
            HatchPath path = l.boundary.value_or(HatchPath{});
            path.flags = l.type;
            path.boundary_handles.clear();
            if ((l.type & 2) && path.edges.empty())
                path.edges.push_back(HatchPolylineEdge{});
            DxfHatch record;
            record.pairs_ = {{0, "HATCH"}, {100, "AcDbHatch"}};
            HatchDefinition def;
            def.paths.push_back(path);
            record.set_definition(def);
            auto fields = record.subclass_fields("AcDbHatch");
            auto begin =
                std::find_if(fields.begin(), fields.end(), [](const auto &p) { return p.code == 92; });
            auto end = std::find_if(begin, fields.end(), [](const auto &p) { return p.code == 75; });
            if (begin == fields.end() || end == fields.end() || end - begin < 2)
                throw std::invalid_argument("Invalid context boundary");
            out.values.insert(out.values.end(), begin + 1, end - 1);
        }
    };
    std::visit(
        [&](const auto &p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BlockReferenceContext>) {
                out.put_r(50, p.rotation / radians_per_degree);
                out.put_p(10, p.insertion);
                out.put_r(41, p.scale_factor.x);
                out.put_r(42, p.scale_factor.y);
                out.put_r(43, p.scale_factor.z);
            } else if constexpr (std::is_same_v<T, TextContext>)
                context_write_text(out, p);
            else if constexpr (std::is_same_v<T, MTextContext>)
                context_write_mtext(out, p);
            else if constexpr (std::is_same_v<T, DimensionContext>) {
                scope("AcDbDimensionObjectContextData");
                out.put_s(2, p.block_name);
                out.put_b(293, p.reserved);
                out.put_p(10, {p.text_location.x, p.text_location.y, 0});
                out.put_b(294, p.default_text_location);
                out.put_r(140, p.text_rotation);
                out.put_b(298, p.dimtofl);
                out.put_b(291, p.dimosxd);
                out.put_b(70, p.dimatfit);
                out.put_b(292, p.dimtix);
                out.put_b(71, p.dimtmove);
                out.put_i(280, p.override_code);
                out.put_b(295, p.has_arrow2);
                out.put_b(296, p.flip_arrow2);
                out.put_b(297, p.flip_arrow1);
                mt_append(out, v.payload_extra);
                auto n = int(p.kind);
                scope(context_dim_scopes[n]);
                out.put_p(n == 4 ? 12 : 11, p.first);
                if (n == 2 || n >= 4)
                    out.put_p(n == 4 ? 13 : 12, p.second);
                return;
            } else if constexpr (std::is_same_v<T, MultiLeaderContext>) {
                scope("AcDbMLeaderObjectContextData");
                mleader_write_context(out, p);
            } else if constexpr (std::is_same_v<T, MTextAttributeContext>) {
                context_write_text(out, p.placement);
                out.put_b(290, p.enabled && p.embedded.has_value());
                if (p.enabled && p.embedded) {
                    auto &e = *p.embedded;
                    out.put_s(101, "Embedded Object");
                    scope("AcDbObjectContextData");
                    out.put_i(70, e.version);
                    out.put_b(290, e.is_default);
                    scope("AcDbAnnotScaleObjectContextData");
                    out.put_s(340, hex_handle(e.scale));
                    context_write_mtext(out, e.text);
                }
            } else if constexpr (std::is_same_v<T, LeaderContext>) {
                if (p.points.size() > 32767)
                    throw std::invalid_argument("Too many context leader points");
                out.put_i(70, p.points.size());
                for (auto point : p.points)
                    out.put_p(10, point);
                out.put_p(11, p.x_direction);
                out.put_b(290, p.annotation_enabled);
                out.put_p(12, p.insertion_offset);
                out.put_p(13, p.endpoint_projection);
            } else if constexpr (std::is_same_v<T, FeatureControlFrameContext>) {
                scope("AcDbFcfObjectContextData");
                out.put_p(10, p.location);
                out.put_p(11, p.horizontal_direction);
            } else if constexpr (std::is_same_v<T, HatchScaleContext>)
                hatch(p);
            else if constexpr (std::is_same_v<T, HatchViewContext>) {
                hatch(p.hatch);
                scope("AcDbHatchViewContextData");
                out.put_s(330, hex_handle(p.view));
                out.put_p(10, p.normal);
                out.put_r(51, p.rotation / radians_per_degree);
                out.put_b(290, p.evaluate);
            }
            mt_append(out, v.payload_extra);
        },
        v.payload);
    auto staged = pairs_;
    auto begin = std::find_if(staged.begin(), staged.end(), [](const auto &p) { return p.code == 100; });
    auto end = std::find_if(begin, staged.end(), [](const auto &p) { return p.code == 1001; });
    auto at = begin - staged.begin();
    staged.erase(begin, end);
    std::vector<Pair> body;
    for (const auto &p : out.values) {
        auto encoded = detail::binary_value_from_text(p.code, p.value);
        body.push_back({p.code, detail::is_string_value(p.code) ? encode_string(p.value)
                                                                : (binary_ ? encoded : p.value)});
    }
    staged.insert(staged.begin() + at, body.begin(), body.end());
    pairs_ = std::move(staged);
}
std::uint64_t DxfDocument::add_object_context(std::string_view dictionary, std::string name,
                                              const ObjectContextDefinition &v) {
    if (version_ < DxfVersion::AC1027)
        throw std::invalid_argument("Context construction requires R2013+");
    mleader_check_handle(*this, v.scale, "SCALE");
    if (auto p = std::get_if<DimensionContext>(&v.payload))
        if (!p->block_name.empty() && !get_symbol(SymbolTable::BlockRecord, p->block_name))
            throw std::invalid_argument("Missing dimension context block");
    if (auto p = std::get_if<HatchViewContext>(&v.payload))
        mleader_check_handle(*this, p->view, "VIEW");
    if (auto p = std::get_if<MTextAttributeContext>(&v.payload))
        if (p->embedded)
            mleader_check_handle(*this, p->embedded->scale, "SCALE");
    DxfObjectContext object;
    static_cast<DxfRecord &>(object) = make_record(context_name(v));
    object.pairs_.insert(object.pairs_.end(),
                         {{102, "{ACAD_REACTORS"}, {330, std::string(dictionary)}, {102, "}"}});
    object.set_definition(v);
    for (auto [at, editable] : reference_positions(object)) {
        static_cast<void>(editable);
        const auto h = handle_value(object.pairs_[at].value);
        if (h && records_with_handle(hex_handle(h)).size() != 1)
            throw std::invalid_argument("Missing annotation context reference");
    }
    return add_dictionary_object(dictionary, std::move(name), object);
}
std::uint64_t DxfDocument::add_annotation_context(std::string_view entity, std::string name,
                                                  const ObjectContextDefinition &value) {
    DxfDocument staged = *this;
    auto root = staged.extension_dictionary_handle(entity);
    if (root.empty())
        root = hex_handle(staged.add_extension_dictionary(entity));
    auto manager = staged.dictionary_entry(root, "AcDbContextDataManager");
    if (manager.empty())
        manager = hex_handle(staged.add_dictionary(root, "AcDbContextDataManager"));
    auto scales = staged.dictionary_entry(manager, "ACDB_ANNOTATIONSCALES");
    if (scales.empty())
        scales = hex_handle(staged.add_dictionary(manager, "ACDB_ANNOTATIONSCALES"));
    auto h = staged.add_object_context(scales, std::move(name), value);
    *this = std::move(staged);
    return h;
}
