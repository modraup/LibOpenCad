// SPDX-License-Identifier: MPL-2.0
// DWG dynamic-block queries and typed object rewrite.
namespace {
std::optional<std::uint64_t> dictionary_child(const DwgR2000Objects &o, std::uint64_t dict,
                                             std::string_view key) {
    auto i = o.metadata_records.find(dict);
    if (i == o.metadata_records.end())
        return {};
    for (auto &e : i->second.dictionary_entries)
        if (e.first == key)
            return e.second;
    return {};
}
std::uint64_t owner_of(const DwgR2000Objects &o, std::uint64_t h) {
    if (auto i = o.dynamic_records.find(h); i != o.dynamic_records.end())
        return i->second.owner;
    if (auto i = o.history_records.find(h); i != o.history_records.end())
        return i->second.owner;
    if (auto i = o.metadata_records.find(h); i != o.metadata_records.end())
        return i->second.owner;
    if (auto i = o.inserts.find(h); i != o.inserts.end())
        return i->second.properties.owner;
    if (auto i = o.symbols.find(h); i != o.symbols.end())
        return i->second.owner;
    if (auto i = o.associative_records.find(h); i != o.associative_records.end())
        return i->second.owner;
    if (auto i = o.arch_entities.find(h); i != o.arch_entities.end())
        return i->second.owner;
    if (auto i = o.arch_objects.find(h); i != o.arch_objects.end())
        return i->second.owner;
    return 0;
}
bool reaches_owner(const DwgR2000Objects &o, std::uint64_t start, std::uint64_t target) {
    std::unordered_set<std::uint64_t> seen;
    while (start && seen.insert(start).second) {
        if (start == target)
            return true;
        start = owner_of(o, start);
    }
    return false;
}
std::optional<std::uint64_t> extension_dictionary(const DwgR2000Objects &o, std::uint64_t h) {
    if (auto i = o.symbols.find(h); i != o.symbols.end() && i->second.extension_dictionary)
        return i->second.extension_dictionary;
    if (auto i = o.inserts.find(h); i != o.inserts.end() && i->second.properties.extension_dictionary)
        return i->second.properties.extension_dictionary;
    if (auto i = o.dynamic_records.find(h); i != o.dynamic_records.end() && i->second.extension_dictionary)
        return i->second.extension_dictionary;
    if (auto i = o.history_records.find(h); i != o.history_records.end() && i->second.extension_dictionary)
        return i->second.extension_dictionary;
    if (auto i = o.modelers.find(h); i != o.modelers.end() && i->second.properties.extension_dictionary)
        return i->second.properties.extension_dictionary;
    return {};
}
std::string metadata_text(std::string_view s, const detail::MetadataField &f, bool unicode, const char *page) {
    if (unicode) {
        auto n = detail::metadata_uint(s, f.offset, 2);
        std::u32string text;
        std::size_t p = f.offset + 2;
        unsigned lead = 0;
        for (std::uint64_t i = 0; i < n; ++i) {
            auto unit = static_cast<unsigned>(detail::metadata_uint(s, p, 2));
            p += 2;
            if (lead && unit >= 0xdc00 && unit <= 0xdfff) {
                text.push_back(0x10000 + ((lead - 0xd800) << 10) + (unit - 0xdc00));
                lead = 0;
                continue;
            }
            if (lead)
                text.push_back(0xfffd);
            lead = unit >= 0xd800 && unit <= 0xdbff ? unit : 0;
            if (!lead && unit)
                text.push_back(unit >= 0xdc00 && unit <= 0xdfff ? 0xfffd : unit);
        }
        if (lead)
            text.push_back(0xfffd);
        return detail::utf8_string(text);
    }
    auto n = detail::metadata_uint(s, f.offset, 2);
    return detail::from_legacy(s.substr(f.offset + 3, n), page);
}
struct ParameterCache {
    bool present = false;
    std::vector<Vector3> points;
    std::optional<std::string> text;
    std::optional<std::int16_t> flip;
};
ParameterCache xrecord_cache(const DwgHistoryRecord &r, bool unicode, const char *page) {
    ParameterCache c;
    c.present = true;
    auto fields = detail::metadata_fields(r.metadata, unicode);
    std::size_t index = 0;
    for (auto &f : fields) {
        if ((f.code >= 10 && f.code <= 37) || (f.code >= 110 && f.code <= 139) ||
            (f.code >= 210 && f.code <= 269) || (f.code >= 1010 && f.code <= 1039) ||
            (f.code >= 1043 && f.code <= 1069)) {
            auto x = detail::metadata_uint(r.metadata, f.offset, 8);
            auto y = detail::metadata_uint(r.metadata, f.offset + 8, 8);
            auto z = detail::metadata_uint(r.metadata, f.offset + 16, 8);
            Vector3 p;
            std::memcpy(&p.x, &x, 8);
            std::memcpy(&p.y, &y, 8);
            std::memcpy(&p.z, &z, 8);
            c.points.push_back(p);
        } else if (f.code == 1 && !c.text)
            c.text = metadata_text(r.metadata, f, unicode, page);
        else if (index >= 4 && ((f.code >= 60 && f.code <= 79) || (f.code >= 170 && f.code <= 179) ||
                                (f.code >= 270 && f.code <= 279) || (f.code >= 370 && f.code <= 389) ||
                                (f.code >= 400 && f.code <= 409) || f.code == 1070)) {
            auto n = std::int64_t(detail::metadata_uint(r.metadata, f.offset, unsigned(f.size)));
            if (f.size == 2)
                n = std::int16_t(n);
            if (n >= -32768 && n <= 32767)
                c.flip = static_cast<std::int16_t>(n);
        }
        ++index;
    }
    return c;
}
double wrap_angle(double a) {
    auto t = std::fmod(a, 6.28318530717958647692);
    if (t < 0)
        t += 6.28318530717958647692;
    return t;
}
double vector_angle(Vector3 v) { return wrap_angle(std::atan2(v.y, v.x)); }
std::vector<BlockEvalValue> value_choices(const BlockParameterValueSet &s) {
    std::vector<BlockEvalValue> out;
    for (auto x : s.values)
        out.emplace_back(x);
    return out;
}
std::vector<std::uint64_t> eed_handles(std::string_view payload, bool wide) {
    std::vector<std::uint64_t> out;
    std::size_t i = 0;
    while (i < payload.size()) {
        auto code = static_cast<unsigned char>(payload[i++]);
        auto need = [&](std::size_t n) {
            if (i > payload.size() || n > payload.size() - i)
                throw std::invalid_argument("Truncated dynamic EED");
        };
        if (code == 0) {
            if (wide) {
                need(2);
                auto n = std::uint16_t(static_cast<unsigned char>(payload[i]) |
                                       (unsigned(static_cast<unsigned char>(payload[i + 1])) << 8));
                i += 2;
                need(std::size_t(n) * 2);
                i += std::size_t(n) * 2;
            } else {
                need(1);
                auto n = static_cast<unsigned char>(payload[i++]);
                need(2 + n);
                i += 2 + n;
            }
        } else if (code == 2) {
            need(1);
            ++i;
        } else if (code == 3 || code == 5) {
            need(8);
            std::uint64_t h = 0;
            for (int b = 0; b < 8; ++b)
                h = (h << 8) | static_cast<unsigned char>(payload[i++]);
            if (code == 5)
                out.push_back(h);
        } else if (code == 4) {
            need(1);
            auto n = static_cast<unsigned char>(payload[i++]);
            need(n);
            i += n;
        } else if (code >= 10 && code <= 13) {
            need(24);
            i += 24;
        } else if (code >= 40 && code <= 42) {
            need(8);
            i += 8;
        } else if (code == 70) {
            need(2);
            i += 2;
        } else if (code == 71) {
            need(4);
            i += 4;
        } else
            throw std::invalid_argument("Unknown dynamic EED item");
    }
    return out;
}
} // namespace
const DwgDynamicRecord &DwgDocument::dynamic_record(std::uint64_t h) const {
    auto i = objects_.dynamic_records.find(h);
    if (i == objects_.dynamic_records.end()) {
        auto e = objects_.dynamic_errors.find(h);
        throw std::invalid_argument(e == objects_.dynamic_errors.end() ? "Missing dynamic object"
                                                                      : e->second);
    }
    return i->second;
}
const DwgInsert &DwgDocument::insert(std::uint64_t h) const {
    auto i = objects_.inserts.find(h);
    if (i == objects_.inserts.end()) {
        auto e = objects_.insert_errors.find(h);
        throw std::invalid_argument(e == objects_.insert_errors.end() ? "Missing INSERT"
                                                                      : e->second);
    }
    return i->second;
}
void DwgDocument::update_dynamic_record(const DwgDynamicRecord &record) {
    if (!record.handle || !objects_.dynamic_records.count(record.handle))
        throw std::invalid_argument("Missing dynamic object");
    *this = DwgDocument(detail::edit_dwg_dynamic_record(bytes_, objects_, record.handle, record));
}
std::uint64_t DwgDocument::insert_dynamic_record(DwgDynamicRecord record, std::uint64_t block_or_layout) {
    if (record.handle && objects_.dynamic_records.count(record.handle)) {
        update_dynamic_record(record);
        return record.handle;
    }
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_dynamic_record(bytes_, objects_, std::move(record), block_or_layout, id));
    return id;
}
std::optional<BlockEvaluationGraph> DwgDocument::dynamic_block_graph(std::uint64_t definition) const {
    auto dict = extension_dictionary(objects_, definition);
    if (!dict)
        return {};
    auto graph = dictionary_child(objects_, *dict, "ACAD_ENHANCEDBLOCK");
    if (!graph)
        return {};
    auto i = objects_.history_records.find(*graph);
    if (i == objects_.history_records.end() || !i->second.graph)
        return {};
    return i->second.graph;
}
bool DwgDocument::is_dynamic_block(std::uint64_t definition) const {
    return dynamic_block_graph(definition).has_value();
}
std::optional<std::uint64_t> DwgDocument::dynamic_definition_for_insert(std::uint64_t insert) const {
    if (!objects_.inserts.count(insert))
        return {};
    if (auto dict = extension_dictionary(objects_, insert)) {
        std::optional<std::uint64_t> definition;
        for (auto &[h, r] : objects_.dynamic_records) {
            if (r.kind != DynamicBlockKind::Representation || !reaches_owner(objects_, h, *dict))
                continue;
            auto *d = std::get_if<BlockRepresentationData>(&r.data);
            if (!d)
                continue;
            if (definition && *definition != d->block)
                throw std::invalid_argument("Ambiguous dynamic block representation");
            definition = d->block;
        }
        if (definition)
            return definition;
    }
    auto block = objects_.inserts.at(insert).block;
    return block ? std::optional<std::uint64_t>(block) : std::nullopt;
}
std::optional<std::uint64_t> DwgDocument::dynamic_block_definition(std::uint64_t block) const {
    if (is_dynamic_block(block))
        return block;
    auto v = unsigned(dxf_version_from_string(std::string_view(bytes_).substr(0, 6)));
    auto eed = objects_.eed.find(block);
    if (eed != objects_.eed.end()) {
        for (auto &item : eed->second) {
            auto app = objects_.symbols.find(item.application);
            if (app == objects_.symbols.end() || app->second.type != 67 ||
                app->second.name != "AcDbBlockRepBTag")
                continue;
            for (auto h : eed_handles(item.payload, v >= 1021))
                if (is_dynamic_block(h))
                    return h;
        }
    }
    std::optional<std::uint64_t> found;
    for (auto &[h, ins] : objects_.inserts) {
        if (ins.block != block)
            continue;
        auto d = dynamic_definition_for_insert(h);
        if (d && *d != block) {
            if (found && found != d)
                throw std::invalid_argument("Ambiguous anonymous block definition");
            found = d;
        }
    }
    return found;
}
std::optional<BlockVisibilityParameter>
DwgDocument::block_visibility_parameter(std::uint64_t definition) const {
    std::optional<BlockVisibilityParameter> result;
    for (auto &[h, r] : objects_.dynamic_records) {
        if (r.kind != DynamicBlockKind::VisibilityParameter)
            continue;
        if (!reaches_owner(objects_, r.owner ? r.owner : h, definition))
            continue;
        auto *p = std::get_if<BlockVisibilityParameter>(&r.data);
        if (!p)
            continue;
        if (result)
            throw std::invalid_argument("Ambiguous block visibility parameter");
        result = *p;
        result->handle = h;
    }
    return result;
}
std::optional<std::pair<std::uint64_t, BlockVisibilityParameter>>
DwgDocument::dynamic_visibility_for_insert(std::uint64_t insert) const {
    auto d = dynamic_definition_for_insert(insert);
    if (!d)
        return {};
    auto v = block_visibility_parameter(*d);
    if (!v)
        return {};
    return std::make_pair(*d, std::move(*v));
}
std::vector<DynamicBlockProperty>
DwgDocument::dynamic_block_properties(std::uint64_t definition, std::optional<std::uint64_t> insert) const {
    double scale = 1;
    if (insert) {
        auto i = objects_.inserts.find(*insert);
        if (i != objects_.inserts.end()) {
            auto s = i->second.scale;
            scale = std::abs(s.x);
            if (std::abs(std::abs(s.y) - scale) > 1e-10 * std::max(scale, 1.) ||
                std::abs(std::abs(s.z) - scale) > 1e-10 * std::max(scale, 1.))
                return {};
        }
    }
    auto graph = dynamic_block_graph(definition);
    if (!graph)
        return {};
    auto v = unsigned(dxf_version_from_string(std::string_view(bytes_).substr(0, 6)));
    auto page = detail::legacy_page(bytes_.substr(0, 6), {});
    auto saved = [&](std::int32_t node) -> ParameterCache {
        ParameterCache c;
        if (!insert)
            return c;
        auto dict = extension_dictionary(objects_, *insert);
        for (const char *key : {"AcDbBlockRepresentation", "AppDataCache", "ACAD_ENHANCEDBLOCKDATA"}) {
            if (!dict)
                return c;
            dict = dictionary_child(objects_, *dict, key);
        }
        if (!dict)
            return c;
        auto rec = dictionary_child(objects_, *dict, std::to_string(node));
        if (!rec)
            return c;
        auto x = objects_.metadata_records.find(*rec);
        if (x == objects_.metadata_records.end() || x->second.type != "XRECORD")
            return c;
        try {
            return xrecord_cache(x->second, v >= 1021, page);
        } catch (const std::invalid_argument &) {
            throw std::invalid_argument("Malformed dynamic parameter cache for node " +
                                        std::to_string(node));
        }
    };
    auto points_of = [](const BlockTwoPointParameter &p, const ParameterCache &c) {
        return std::pair<Vector3, Vector3>{c.points.empty() ? p.definition_base_point : c.points[0],
                                           c.points.size() < 2 ? p.definition_end_point : c.points[1]};
    };
    std::vector<DynamicBlockProperty> result;
    auto add = [&](std::uint64_t handle, const BlockParameter &parameter, std::string name,
                   std::string description, std::int16_t units, BlockEvalValue value,
                   std::vector<BlockEvalValue> allowed = {}) {
        DynamicBlockProperty p;
        p.parameter_handle = handle;
        p.name = std::move(name);
        p.description = std::move(description);
        p.units = units;
        p.show = parameter.show_properties;
        p.value = std::move(value);
        p.type_code = std::holds_alternative<std::string>(p.value)    ? 5
                      : std::holds_alternative<std::int16_t>(p.value) ? 3
                      : std::holds_alternative<std::int32_t>(p.value) ? 2
                                                                      : 1;
        p.allowed_values = std::move(allowed);
        result.push_back(std::move(p));
    };
    for (const auto &node : graph->nodes) {
        auto i = objects_.dynamic_records.find(node.expression);
        if (i == objects_.dynamic_records.end()) {
            if (objects_.history_records.count(node.expression))
                continue;
            throw std::invalid_argument("Missing dynamic evaluation node " + hex(node.expression));
        }
        const auto &rec = i->second;
        auto require_cache = [&](const ParameterCache &c, bool text, bool flip, std::size_t points,
                                 std::int32_t node) {
            if (!c.present)
                return;
            if ((text && !c.text) || (flip && !c.flip) || (!text && !flip && c.points.size() < points))
                throw std::invalid_argument("Malformed dynamic parameter cache for node " +
                                            std::to_string(node));
        };
        switch (rec.kind) {
        case DynamicBlockKind::VisibilityParameter: {
            auto *p = std::get_if<BlockVisibilityParameter>(&rec.data);
            auto c = saved(p->eval_node_id);
            if (c.present && !c.text)
                throw std::invalid_argument("Malformed visibility cache");
            std::vector<BlockEvalValue> choices;
            for (auto &st : p->states)
                choices.emplace_back(st.name);
            add(rec.handle, BlockParameter{{}, p->show_properties, p->chain_actions}, p->name, p->description, 0,
                c.text.value_or(p->states.empty() ? std::string() : p->states.front().name), std::move(choices));
            break;
        }
        case DynamicBlockKind::LinearParameter: {
            auto *p = std::get_if<BlockLinearParameter>(&rec.data);
            auto c = saved(p->parameter.parameter.element.eval.node_id);
            require_cache(c, false, false, 2, p->parameter.parameter.element.eval.node_id);
            auto [a, b] = points_of(p->parameter, c);
            add(rec.handle, p->parameter.parameter, p->distance_name, p->distance_description, 2,
                (b - a).length(), value_choices(p->value_set));
            break;
        }
        case DynamicBlockKind::PolarParameter: {
            auto *p = std::get_if<BlockPolarParameter>(&rec.data);
            auto c = saved(p->parameter.parameter.element.eval.node_id);
            require_cache(c, false, false, 2, p->parameter.parameter.element.eval.node_id);
            auto [a, b] = points_of(p->parameter, c);
            add(rec.handle, p->parameter.parameter, p->distance_name, p->distance_description, 2,
                (b - a).length(), value_choices(p->distance_value_set));
            add(rec.handle, p->parameter.parameter, p->angle_name, p->angle_description, 1,
                vector_angle(b - a), value_choices(p->angle_value_set));
            break;
        }
        case DynamicBlockKind::RotationParameter: {
            auto *p = std::get_if<BlockRotationParameter>(&rec.data);
            auto c = saved(p->parameter.parameter.element.eval.node_id);
            require_cache(c, false, false, 4, p->parameter.parameter.element.eval.node_id);
            auto [a, b] = points_of(p->parameter, c);
            auto base = c.points.size() >= 4 ? c.points[3] : p->definition_base_angle_point;
            auto value = vector_angle(b - a) - vector_angle(base - a);
            if (insert)
                for (auto &n : graph->nodes) {
                    auto a_it = objects_.dynamic_records.find(n.expression);
                    if (a_it == objects_.dynamic_records.end() ||
                        a_it->second.kind != DynamicBlockKind::FlipAction)
                        continue;
                    auto *act = std::get_if<BlockFlipAction>(&a_it->second.data);
                    if (!act || std::find(act->action.dependencies.begin(), act->action.dependencies.end(),
                                          node.expression) == act->action.dependencies.end())
                        continue;
                    auto flip = saved(act->connections[0].code);
                    if (flip.flip.value_or(0) == 1)
                        value = -value;
                }
            add(rec.handle, p->parameter.parameter, p->angle_name, p->angle_description, 1, wrap_angle(value),
                value_choices(p->value_set));
            break;
        }
        case DynamicBlockKind::XYParameter: {
            auto *p = std::get_if<BlockXYParameter>(&rec.data);
            auto c = saved(p->parameter.parameter.element.eval.node_id);
            require_cache(c, false, false, 2, p->parameter.parameter.element.eval.node_id);
            auto [a, b] = points_of(p->parameter, c);
            add(rec.handle, p->parameter.parameter, p->x_label, p->x_label_description, 2, std::abs(b.x - a.x),
                value_choices(p->x_value_set));
            add(rec.handle, p->parameter.parameter, p->y_label, p->y_label_description, 2, std::abs(b.y - a.y),
                value_choices(p->y_value_set));
            break;
        }
        case DynamicBlockKind::PointParameter: {
            auto *p = std::get_if<BlockPointParameter>(&rec.data);
            auto c = saved(p->parameter.parameter.element.eval.node_id);
            require_cache(c, false, false, 1, p->parameter.parameter.element.eval.node_id);
            auto pt = c.points.empty() ? p->parameter.definition_point : c.points.front();
            add(rec.handle, p->parameter.parameter, p->position_name + " X", p->position_description, 2, pt.x);
            add(rec.handle, p->parameter.parameter, p->position_name + " Y", p->position_description, 2, pt.y);
            break;
        }
        case DynamicBlockKind::FlipParameter: {
            auto *p = std::get_if<BlockFlipParameter>(&rec.data);
            auto c = saved(p->parameter.parameter.element.eval.node_id);
            require_cache(c, false, true, 0, p->parameter.parameter.element.eval.node_id);
            add(rec.handle, p->parameter.parameter, p->flip_label, p->flip_label_description, 0,
                c.flip.value_or(0), {std::int16_t(0), std::int16_t(1)});
            break;
        }
        case DynamicBlockKind::LookupParameter: {
            auto *p = std::get_if<BlockLookupParameter>(&rec.data);
            const BlockLookupAction *action = nullptr;
            for (auto &n : graph->nodes) {
                auto a_it = objects_.dynamic_records.find(n.expression);
                if (a_it == objects_.dynamic_records.end() || a_it->second.kind != DynamicBlockKind::LookupAction)
                    continue;
                auto *a = std::get_if<BlockLookupAction>(&a_it->second.data);
                if (a && a->action.element.eval.node_id == p->index) {
                    action = a;
                    break;
                }
            }
            if (!action)
                throw std::invalid_argument("Missing lookup action for " + p->lookup_name);
            auto id = p->parameter.parameter.element.eval.node_id;
            std::size_t column = 0;
            while (column < action->columns.size() && action->columns[column].node_id != id)
                ++column;
            if (column == action->columns.size())
                throw std::invalid_argument("Missing lookup column");
            auto width = action->column_count;
            if (width <= 0 || column >= std::size_t(width) ||
                action->expressions.size() != std::size_t(width) * std::size_t(action->row_count) ||
                action->columns.size() != std::size_t(width) || action->row_count < 0)
                throw std::invalid_argument("Incomplete lookup table");
            std::vector<BlockEvalValue> allowed;
            for (int row = 0; row < action->row_count; ++row) {
                BlockEvalValue value = action->expressions[std::size_t(row) * std::size_t(width) + column];
                if (std::find(allowed.begin(), allowed.end(), value) == allowed.end())
                    allowed.push_back(std::move(value));
            }
            bool inputs = std::any_of(action->columns.begin(), action->columns.end(),
                                      [](const auto &c) { return !c.lookup_property; });
            auto fallback = inputs ? BlockEvalValue(action->columns[column].unmatched_name)
                                   : (allowed.empty() ? BlockEvalValue(std::string()) : allowed.front());
            auto c = saved(id);
            require_cache(c, true, false, 0, id);
            auto current = c.text ? BlockEvalValue(*c.text) : fallback;
            if (inputs && current == fallback &&
                std::find(allowed.begin(), allowed.end(), fallback) == allowed.end())
                allowed.push_back(fallback);
            add(rec.handle, p->parameter.parameter, p->lookup_name, p->lookup_description, 0, current,
                std::move(allowed));
            result.back().read_only = !action->columns[column].writable;
            break;
        }
        case DynamicBlockKind::UserParameter: {
            auto *p = std::get_if<BlockUserParameter>(&rec.data);
            add(rec.handle, p->parameter.parameter, p->parameter.parameter.element.name, "", p->value_type,
                p->value);
            break;
        }
        case DynamicBlockKind::AlignedConstraintParameter:
        case DynamicBlockKind::LinearConstraintParameter:
        case DynamicBlockKind::HorizontalConstraintParameter:
        case DynamicBlockKind::VerticalConstraintParameter: {
            auto *p = std::get_if<BlockLinearConstraintParameter>(&rec.data);
            auto c = saved(p->constraint.parameter.parameter.element.eval.node_id);
            require_cache(c, false, false, 2, p->constraint.parameter.parameter.element.eval.node_id);
            auto [a, b] = points_of(p->constraint.parameter, c);
            auto value = rec.kind == DynamicBlockKind::HorizontalConstraintParameter ? std::abs(b.x - a.x)
                         : rec.kind == DynamicBlockKind::VerticalConstraintParameter ? std::abs(b.y - a.y)
                                                                                     : (b - a).length();
            add(rec.handle, p->constraint.parameter.parameter, p->expression_name, p->expression_description, 2,
                value, value_choices(p->value_set));
            break;
        }
        case DynamicBlockKind::AngularConstraintParameter:
        case DynamicBlockKind::DiametricConstraintParameter:
        case DynamicBlockKind::RadialConstraintParameter:
            throw std::invalid_argument("Unsupported dynamic constraint property");
        case DynamicBlockKind::Unknown:
            throw std::invalid_argument("Unsupported dynamic evaluation object " + hex(node.expression));
        default:
            break;
        }
    }
    if (auto vis = block_visibility_parameter(definition)) {
        auto property = std::find_if(result.begin(), result.end(),
                                     [&](const auto &p) { return p.parameter_handle == vis->handle; });
        if (property != result.end())
            if (auto selected = std::get_if<std::string>(&property->value))
                for (const auto &state : vis->states)
                    if (state.name == *selected)
                        for (auto &p : result)
                            p.visible = p.parameter_handle == vis->handle ||
                                        std::find(state.visible_params.begin(), state.visible_params.end(),
                                                  p.parameter_handle) != state.visible_params.end();
    }
    for (auto &p : result) {
        auto multiplier = p.units == 2 ? scale : p.units == 3 ? scale * scale : 1;
        if (auto d = std::get_if<double>(&p.value))
            *d *= multiplier;
        for (auto &item : p.allowed_values)
            if (auto d = std::get_if<double>(&item))
                *d *= multiplier;
    }
    return result;
}
