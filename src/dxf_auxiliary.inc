// SPDX-License-Identifier: MPL-2.0
namespace {
constexpr const char *auxiliary_names[] = {"GROUP",         "SCALE",   "SORTENTSTABLE",   "SPATIAL_FILTER",
                                           "DICTIONARYVAR", "DBCOLOR", "WIPEOUTVARIABLES"};
constexpr const char *auxiliary_scopes[] = {"AcDbGroup",           "AcDbScale",           "AcDbSortentsTable",
                                            "AcDbSpatialFilter",   "DictionaryVariables", "AcDbColor",
                                            "AcDbWipeoutVariables"};
int auxiliary_index(std::string_view name) {
    for (int i = 0; i < 7; ++i)
        if (name == auxiliary_names[i])
            return i;
    return -1;
}
void validate_auxiliary(const DxfDocument &d, const AuxiliaryDefinition &value) {
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            auto entity = [&](std::uint64_t h) {
                if (!h || d.records_with_handle(hex_handle(h)).size() != 1)
                    throw std::invalid_argument("Missing auxiliary entity reference");
                bool found = false;
                d.for_each<DxfEntity>([&](const auto &e) {
                    if (handle_value(e.handle()) == h)
                        found = true;
                });
                if (!found)
                    throw std::invalid_argument("Expected entity reference");
            };
            if constexpr (std::is_same_v<T, GroupDefinition>) {
                std::set<std::uint64_t> seen;
                for (auto h : v.entities) {
                    entity(h);
                    if (!seen.insert(h).second)
                        throw std::invalid_argument("Duplicate group member");
                }
            }
            if constexpr (std::is_same_v<T, SortEntitiesDefinition>) {
                mleader_check_handle(d, v.block, "BLOCK_RECORD");
                std::set<std::uint64_t> seen;
                for (const auto &e : v.entries) {
                    entity(e.entity);
                    if (!seen.insert(e.entity).second ||
                        handle_value(d.get_record(hex_handle(e.entity))->owner_handle()) != v.block)
                        throw std::invalid_argument("Sort entry must belong to its block");
                }
            }
        },
        value);
}
} // namespace
AuxiliaryDefinition DxfAuxiliaryObject::definition() const {
    auto kind = auxiliary_index(type());
    if (kind < 0)
        throw std::invalid_argument("Unknown auxiliary object");
    MLFields f(subclass_fields(auxiliary_scopes[kind]));
    switch (kind) {
    case 0: {
        GroupDefinition v;
        v.description = f.s(300, {});
        v.unnamed = f.b(70, false);
        v.selectable = f.b(71, true);
        std::set<std::uint64_t> seen;
        for (auto n : f.take(340)) {
            auto h = handle_value(f.values[n].value);
            if (seen.insert(h).second)
                v.entities.push_back(h);
        }
        v.extra_fields = f.extras();
        return v;
    }
    case 1: {
        ScaleDefinition v;
        f.take(70);
        v.name = f.s(300, "1:1");
        v.paper_units = f.r(140, 1);
        v.drawing_units = f.r(141, 1);
        v.unit_scale = f.b(290, true);
        v.extra_fields = f.extras();
        return v;
    }
    case 2: {
        SortEntitiesDefinition v;
        v.block = f.h(330, 0);
        std::uint64_t pending = 0;
        for (std::size_t i = 0; i < f.values.size(); ++i) {
            if (f.hidden[i])
                continue;
            auto p = f.values[i];
            if (p.code == 331) {
                pending = handle_value(p.value);
                f.used[i] = true;
            } else if (p.code == 5 && pending) {
                auto found = std::find_if(v.entries.begin(), v.entries.end(),
                                          [&](const auto &e) { return e.entity == pending; });
                SortEntry e{pending, handle_value(p.value)};
                if (found == v.entries.end())
                    v.entries.push_back(e);
                else
                    *found = e;
                pending = 0;
                f.used[i] = true;
            }
        }
        v.extra_fields = f.extras();
        return v;
    }
    case 3: {
        SpatialFilterDefinition v;
        f.take(70);
        std::optional<double> x;
        for (std::size_t i = 0; i < f.values.size(); ++i) {
            if (f.hidden[i])
                continue;
            MLFields p({f.values[i]});
            if (f.values[i].code == 10) {
                x = p.r(10, 0);
                f.used[i] = true;
            } else if (f.values[i].code == 20) {
                if (x) {
                    v.boundary.push_back({*x, p.r(20, 0)});
                    x.reset();
                }
                f.used[i] = true;
            }
        }
        v.normal = {f.r(210, 0), f.r(220, 0), f.r(230, 1)};
        v.origin = {f.r(11, 0), f.r(21, 0), f.r(31, 0)};
        v.enabled = f.b(71, true);
        bool front = f.b(72, false);
        f.take(73);
        if (!f.take(41).empty())
            v.back_clip = f.r(41, 0);
        auto matrix = f.take(40);
        std::size_t start = 0;
        if (front && !matrix.empty()) {
            MLFields p({f.values[matrix[0]]});
            v.front_clip = p.r(40, 0);
            start = 1;
        }
        for (int m = 0; m < 2; ++m) {
            if (matrix.size() < start + 12 * (m + 1))
                break;
            auto &out = m ? v.clip_transform : v.inverse_block_transform;
            for (int i = 0; i < 12; ++i) {
                MLFields p({f.values[matrix[start + 12 * m + i]]});
                out.m[i / 4][i % 4] = p.r(40, 0);
            }
        }
        v.extra_fields = f.extras();
        return v;
    }
    case 4: {
        DictionaryVariableDefinition v;
        v.schema = f.i(280, 0);
        v.value = f.s(1, {});
        v.extra_fields = f.extras();
        return v;
    }
    case 5: {
        BookColorDefinition v;
        v.color = Color::from_index(static_cast<std::int16_t>(f.i(62,256)));
        for(auto at:f.take(420))if(auto rgb=detail::as_i32_bits(f.values[at].value,false))v.color=Color::from_true_color_value(*rgb);
        f.take(1);f.take(2);f.take(430);
        for(std::size_t i=0;i<f.values.size();++i){if(f.hidden[i])continue;const auto& field=f.values[i];if(field.code==1)v.name=field.value;else if(field.code==2)v.book=field.value;else if(field.code==430){auto split=field.value.find('$');v.book=split==std::string::npos?"":field.value.substr(0,split);v.name=split==std::string::npos?field.value:field.value.substr(split+1);}}
        v.extra_fields = f.extras();
        return v;
    }
    default: {
        WipeoutVariablesDefinition v;
        v.frame = f.i(70, 1);
        v.extra_fields = f.extras();
        return v;
    }
    }
}
void DxfAuxiliaryObject::set_definition(const AuxiliaryDefinition &value) {
    if (auxiliary_index(type()) != int(value.index()))
        throw std::invalid_argument("Auxiliary definition type mismatch");
    MLFields f;
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, GroupDefinition>) {
                f.put_s(300, v.description);
                f.put_i(70, v.unnamed);
                f.put_i(71, v.selectable);
                for (auto h : v.entities)
                    f.put_s(340, hex_handle(h));
            } else if constexpr (std::is_same_v<T, ScaleDefinition>) {
                f.put_i(70, 0);
                f.put_s(300, v.name);
                f.put_r(140, v.paper_units);
                f.put_r(141, v.drawing_units);
                f.put_b(290, v.unit_scale);
            } else if constexpr (std::is_same_v<T, SortEntitiesDefinition>) {
                f.put_s(330, hex_handle(v.block));
                for (const auto &e : v.entries) {
                    f.put_s(331, hex_handle(e.entity));
                    f.put_s(5, hex_handle(e.sort_handle));
                }
            } else if constexpr (std::is_same_v<T, SpatialFilterDefinition>) {
                if (v.boundary.size() > 32767)
                    throw std::invalid_argument("Too many clip points");
                f.put_i(70, v.boundary.size());
                write_external_boundary(f, 10, v.boundary);
                f.put_p(210, v.normal);
                f.put_p(11, v.origin);
                f.put_i(71, v.enabled);
                f.put_i(72, v.front_clip.has_value());
                if (v.front_clip)
                    f.put_r(40, *v.front_clip);
                f.put_i(73, v.back_clip.has_value());
                if (v.back_clip)
                    f.put_r(41, *v.back_clip);
                for (const auto *m : {&v.inverse_block_transform, &v.clip_transform}) {
                    if (m->m[3] != Matrix4::Row{0, 0, 0, 1})
                        throw std::invalid_argument("Spatial filter requires an affine matrix");
                    for (int row = 0; row < 3; ++row)
                        for (int col = 0; col < 4; ++col)
                            f.put_r(40, m->m[row][col]);
                }
            } else if constexpr (std::is_same_v<T, DictionaryVariableDefinition>) {
                f.put_i(280, v.schema);
                f.put_s(1, v.value);
            } else if constexpr (std::is_same_v<T, BookColorDefinition>) {
                cs_color(f, v.color);
                if (!v.book.empty() || !v.name.empty())
                    f.put_s(430, v.book.empty() ? v.name : v.book + "$" + v.name);
            } else if constexpr (std::is_same_v<T, WipeoutVariablesDefinition>)
                f.put_i(70, v.frame);
            mt_append(f, v.extra_fields);
        },
        value);
    set_subclass_fields(auxiliary_scopes[value.index()], f.values);
}
std::uint64_t DxfDocument::add_auxiliary_object(std::string_view dictionary, std::string name,
                                                const AuxiliaryDefinition &value) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Auxiliary construction requires R2007+");
    validate_auxiliary(*this, value);
    DxfDocument staged = *this;
    auto v = value;
    if (auto *order = std::get_if<SortEntitiesDefinition>(&v)) {
        std::vector<std::size_t> indices(order->entries.size());
        for (std::size_t i = 0; i < indices.size(); ++i)
            indices[i] = i;
        std::stable_sort(indices.begin(), indices.end(), [&](auto a, auto b) {
            return order->entries[a].sort_handle < order->entries[b].sort_handle;
        });
        for (auto i : indices)
            order->entries[i].sort_handle = staged.allocate_handle();
    }
    DxfAuxiliaryObject object;
    static_cast<DxfRecord &>(object) = staged.make_record(auxiliary_names[v.index()]);
    if (std::holds_alternative<SpatialFilterDefinition>(v))
        object.pairs_.push_back({100, "AcDbFilter"});
    object.pairs_.push_back({100, auxiliary_scopes[v.index()]});
    object.set_definition(v);
    auto id = staged.add_dictionary_object(dictionary, std::move(name), object);
    *this = std::move(staged);
    return id;
}
void DxfDocument::set_auxiliary_definition(std::string_view handle, const AuxiliaryDefinition &v) {
    if (records_with_handle(handle).size() != 1 || get_record(handle)->type() != auxiliary_names[v.index()])
        throw std::invalid_argument("Expected unique matching auxiliary object");
    validate_auxiliary(*this, v);
    DxfDocument staged = *this;
    staged.for_each<DxfAuxiliaryObject>([&](auto &o) {
        if (handle_value(o.handle()) == handle_value(handle))
            o.set_definition(v);
    });
    staged.rebuild_indexes();
    *this = std::move(staged);
}

std::int16_t DxfDocument::wipeout_frame() const {
    auto root = root_dictionary_handle();
    if (root.empty()) return 1;
    auto h = dictionary_entry(root, "ACAD_WIPEOUT_VARS");
    std::int16_t mode = 1;
    for_each<DxfAuxiliaryObject>([&](const auto& o) {
        if (o.handle() == h && o.type() == "WIPEOUTVARIABLES")
            mode = static_cast<std::int16_t>(std::get<WipeoutVariablesDefinition>(o.definition()).frame);
    });
    return mode;
}
void DxfDocument::set_wipeout_frame(std::int16_t mode) {
    auto root = root_dictionary_handle();
    if (root.empty()) return;
    auto h = dictionary_entry(root, "ACAD_WIPEOUT_VARS");
    auto record = h.empty() ? nullptr : get_record(h);
    if (record && record->type() == "WIPEOUTVARIABLES") {
        WipeoutVariablesDefinition value;
        for_each<DxfAuxiliaryObject>([&](const auto& o) {
            if (o.handle() == h) value = std::get<WipeoutVariablesDefinition>(o.definition());
        });
        value.frame = mode;
        set_auxiliary_definition(h, value);
    } else {
        WipeoutVariablesDefinition value;
        value.frame = mode;
        add_auxiliary_object(root, "ACAD_WIPEOUT_VARS", value);
    }
}
