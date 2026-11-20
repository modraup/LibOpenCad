// SPDX-License-Identifier: MPL-2.0
// acadrust entities/insert.rs and document/explode.rs. Recursive affine baking.
std::vector<std::uint64_t> DxfDocument::expand_insert_into(std::string_view handle,
                                                           const EntityOptions &target, bool remove_source,
                                                           bool expand_arrays, std::size_t max_entities) {
    auto lookup = [&](std::string_view h) -> std::size_t {
        auto found = handle_index_.find(handle_value(h));
        if (found == handle_index_.end() || found->second.size() != 1 || found->second.front().vertex ||
            found->second.front().seqend)
            throw std::invalid_argument("Expansion requires unique top-level identities");
        return found->second.front().record;
    };
    const auto root_index = lookup(handle);
    const auto *root = std::get_if<DxfInsert>(&records_[root_index]);
    if (!root)
        throw std::invalid_argument("Expansion requires INSERT");
    if (!expand_arrays && (root->definition().columns > 1 || root->definition().rows > 1))
        return {};
    struct Part {
        Record geometry;
        std::string source;
    };
    std::vector<Part> parts;
    auto transform = [&](Record &record, const Transform &t) {
        std::visit(
            [&](auto &e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, DxfLine> || std::is_same_v<T, DxfCircle> ||
                              std::is_same_v<T, DxfArc> || std::is_same_v<T, DxfEllipse> ||
                              std::is_same_v<T, DxfPoint> || std::is_same_v<T, DxfRay> ||
                              std::is_same_v<T, DxfXLine> || std::is_same_v<T, DxfSolid> ||
                              std::is_same_v<T, DxfFace3D> || std::is_same_v<T, DxfSpline> ||
                              std::is_same_v<T, DxfMesh> || std::is_same_v<T, DxfLwPolyline> ||
                              std::is_same_v<T, DxfPolyline2D> || std::is_same_v<T, DxfPolyline3D> ||
                              std::is_same_v<T, DxfPolygonMesh> || std::is_same_v<T, DxfPolyfaceMesh> ||
                              std::is_same_v<T, DxfText> || std::is_same_v<T, DxfMText> ||
                              std::is_same_v<T, DxfAttribute> || std::is_same_v<T, DxfInsert> ||
                              std::is_same_v<T, DxfLeader> || std::is_same_v<T, DxfTolerance> ||
                              std::is_same_v<T, DxfMLine> || std::is_same_v<T, DxfMultiLeader> ||
                              std::is_same_v<T, DxfDimension> || std::is_same_v<T, DxfHatch> ||
                              std::is_same_v<T, DxfShape> || std::is_same_v<T, DxfUnderlay> ||
                              std::is_same_v<T, DxfRasterImage> || std::is_same_v<T, DxfViewport> ||
                              std::is_same_v<T, DxfTable> || std::is_same_v<T, DxfExtendedEntity>) {
                    // Rust's Entity trait path used by INSERT leaves XDATA unchanged.
                    auto original = e.extended_data();
                    e.apply_transform(t);
                    for (const auto &data : original)
                        e.upsert_extended_data(data);
                } else
                    throw std::invalid_argument("Unsupported block member transformation: " + e.type());
            },
            record);
    };
    auto inherit = [](DxfEntity &e, const DxfEntity &parent) {
        if (e.layer() == "0")
            e.set_layer(parent.layer());
        if (e.color() == Color::by_block()) {
            e.set_color(parent.color());
            auto p = e.display_properties();
            p.color_name = parent.display_properties().color_name;
            e.set_display_properties(p);
        }
        if (e.line_weight() == LineWeight::by_block())
            e.set_line_weight(parent.line_weight());
        if (e.transparency() == Transparency::by_block())
            e.set_transparency(parent.transparency());
        auto name = e.linetype();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : char(c); });
        if (name == "byblock")
            e.set_linetype(parent.linetype());
    };
    auto similarity = [](const Transform &t, Vector3 n) {
        auto o = Matrix3::arbitrary_axis(n);
        auto x = t.apply_rotation(o * Vector3::UNIT_X), y = t.apply_rotation(o * Vector3::UNIT_Y);
        auto a = x.length(), b = y.length();
        return a > 1e-12 && b > 1e-12 && std::abs(a - b) <= 1e-10 * std::max(a, b) &&
               std::abs(x.dot(y)) <= 1e-10 * a * b;
    };
    auto normal_transform = [](const Transform &t, Vector3 n) {
        if (auto inv = t.matrix.to_matrix3().inverse()) {
            auto v = inv->transpose() * n;
            auto len = v.length();
            if (len >= 1e-10)
                return v * (1. / len);
        }
        return n;
    };
    auto curve = [&](Record &r, const Transform &t) {
        const DxfCircle *c = std::get_if<DxfCircle>(&r);
        const auto *a = std::get_if<DxfArc>(&r);
        if (a)
            c = a;
        if (!c) {
            transform(r, t);
            return;
        }
        auto n = c->normal(), center = c->center();
        auto radius = c->radius(), thickness = c->thickness();
        if (!similarity(t, n)) {
            if (version_ == DxfVersion::AC1009)
                throw std::invalid_argument("Nonuniform curve expansion requires ELLIPSE (R13+)");
            auto axis = Matrix3::arbitrary_axis(n);
            DxfEllipse ellipse;
            static_cast<DxfEntity &>(ellipse) = make_entity("ELLIPSE", "AcDbEllipse", {});
            ellipse.set_center(axis * center);
            ellipse.set_major_axis(axis * (Vector3::UNIT_X * radius));
            ellipse.set_minor_axis_ratio(1);
            ellipse.set_normal(n);
            ellipse.set_start_parameter(a ? a->start_angle_radians() : 0);
            ellipse.set_end_parameter(a ? a->end_angle_radians() : 2 * pi);
            ellipse.apply_transform(t);
            // Common/owned graph data will come from the separately allocated clone.
            ellipse.set_layer(c->layer());
            ellipse.set_linetype(c->linetype());
            ellipse.set_color(c->color());
            ellipse.set_line_weight(c->line_weight());
            ellipse.set_invisible(c->is_invisible());
            ellipse.set_display_properties(c->display_properties());
            r = std::move(ellipse);
            return;
        }
        if (a) {
            auto axis = Matrix3::arbitrary_axis(n);
            auto world = axis * center;
            auto nc = t.apply(world), start = t.apply(a->start_point_wcs()),
                 end = t.apply(a->end_point_wcs());
            auto nn = normal_transform(t, n);
            if (t.matrix.to_matrix3().determinant() < 0)
                nn = -nn;
            auto inv = Matrix3::arbitrary_axis(nn).transpose();
            auto ds = inv * (start - nc), de = inv * (end - nc);
            auto updated = *a;
            updated.set_center(inv * nc);
            updated.set_radius((start - nc).length());
            updated.set_normal(nn);
            updated.set_start_angle_radians(std::atan2(ds.y, ds.x));
            updated.set_end_angle_radians(std::atan2(de.y, de.x));
            updated.set_thickness(thickness * t.apply_rotation(n).length());
            r = std::move(updated);
        } else {
            auto axis = Matrix3::arbitrary_axis(n);
            auto nn = normal_transform(t, n);
            if (t.matrix.to_matrix3().determinant() < 0)
                nn = -nn;
            auto updated = *c;
            updated.set_center(Matrix3::arbitrary_axis(nn).transpose() * t.apply(axis * center));
            updated.set_radius(radius * t.apply_rotation(axis * Vector3::UNIT_X).length());
            updated.set_normal(nn);
            updated.set_thickness(thickness * t.apply_rotation(n).length());
            r = std::move(updated);
        }
    };
    std::unordered_set<std::uint64_t> path;
    auto shear = [](const Transform &t) {
        Vector3 v[] = {t.apply_rotation(Vector3::UNIT_X), t.apply_rotation(Vector3::UNIT_Y),
                       t.apply_rotation(Vector3::UNIT_Z)};
        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j) {
                double length = v[i].length() * v[j].length();
                if (length > 1e-12 && std::abs(v[i].dot(v[j])) > 1e-10 * length)
                    return true;
            }
        return false;
    };
    std::function<void(DxfInsert, const Transform &, std::size_t, bool)> walk;
    walk = [&](DxfInsert insert, const Transform &parent, std::size_t depth, bool detached) {
        if (depth > 64)
            throw std::invalid_argument("Block expansion depth exceeds 64");
        auto d = insert.definition();
        auto locations = symbol_locations(SymbolTable::BlockRecord, d.block_name);
        if (locations.size() != 1)
            throw std::invalid_argument("Block name is missing or ambiguous");
        auto &br = std::get<DxfBlockRecord>(records_[locations.front()]);
        auto owner = handle_value(br.handle());
        if (br.short_field("AcDbBlockTableRecord", 280, 1) == 0)
            throw std::invalid_argument("Block is not explodable");
        const DxfBlock *block = nullptr;
        for_each<DxfBlock>([&](const auto &b) {
            if (b.owner_handle() == br.handle())
                block = &b;
        });
        if (!block)
            throw std::invalid_argument("Missing block marker");
        if ((block->flags() & (4 | 8 | 16)) || !block->xref_path().empty())
            throw std::invalid_argument("External block cannot be expanded");
        if (!path.insert(owner).second)
            throw std::invalid_argument("Cyclic block expansion");
        if (!expand_arrays && (d.columns > 1 || d.rows > 1))
            throw std::invalid_argument("Nested MINSERT requires array expansion opt-in");
        if (std::size_t(d.rows) * d.columns > max_entities)
            throw std::length_error("Block array expansion limit exceeded");
        auto cells = insert.array_points();
        auto members = entity_handles_in_block(d.block_name);
        auto to_base = Transform::from_translation(-block->base_point());
        for (auto position : cells) {
            auto cell = insert;
            auto definition = d;
            definition.insertion = position;
            definition.rows = definition.columns = 1;
            cell.set_definition(definition);
            auto world = cell.get_transform().then(parent);
            for (const auto &member : members) {
                auto index = lookup(member);
                Record r = records_[index];
                transform(r, to_base);
                if (auto nested = std::get_if<DxfInsert>(&r)) {
                    inherit(*nested, insert);
                    walk(*nested, world, depth + 1, detached || shear(nested->get_transform().then(world)));
                    continue;
                }
                bool bulge = false;
                Vector3 normal = Vector3::UNIT_Z;
                if (auto p = std::get_if<DxfLwPolyline>(&r)) {
                    normal = p->normal();
                    for (auto v : p->vertices())
                        bulge |= std::abs(v.bulge) > 1e-10;
                }
                if (auto p = std::get_if<DxfPolyline2D>(&r)) {
                    normal = p->normal();
                    for (std::size_t i = 0; i < p->vertex_count(); ++i)
                        bulge |= std::abs(p->vertex(i).bulge) > 1e-10;
                }
                if (detached &&
                    (std::holds_alternative<DxfLeader>(r) || std::holds_alternative<DxfTolerance>(r) ||
                     std::holds_alternative<DxfMLine>(r) || std::holds_alternative<DxfMultiLeader>(r) ||
                     std::holds_alternative<DxfDimension>(r) || std::holds_alternative<DxfHatch>(r) ||
                     std::holds_alternative<DxfShape>(r) || std::holds_alternative<DxfUnderlay>(r) ||
                     std::holds_alternative<DxfRasterImage>(r) || std::holds_alternative<DxfViewport>(r) ||
                     std::holds_alternative<DxfTable>(r) || std::holds_alternative<DxfExtendedEntity>(r))) {
                    // A detached shear leaf cannot keep an owned payload from
                    // the source (image reactors/table content). Reject until a
                    // dedicated detached graph policy can preserve that schema.
                    if (extension_tree({get_record(member)}).size() > 1)
                        throw std::invalid_argument("Owned specialized shear expansion is unsupported");
                }
                std::vector<Part> outputs;
                if (bulge && !similarity(world, normal)) {
                    auto scratch = *this;
                    scratch.records_[index] = r;
                    for (auto p : scratch.explode_entity(member))
                        std::visit([&](auto &&e) { outputs.push_back({Record{std::move(e)}, {}}); },
                                   std::move(p));
                } else
                    outputs.push_back({std::move(r), detached ? std::string{} : member});
                for (auto &part : outputs) {
                    if (parts.size() >= max_entities)
                        throw std::length_error("Block expansion entity limit exceeded");
                    curve(part.geometry, world);
                    std::visit(
                        [&](auto &e) {
                            if constexpr (std::is_base_of_v<DxfEntity, std::decay_t<decltype(e)>>)
                                inherit(e, insert);
                        },
                        part.geometry);
                    parts.push_back(std::move(part));
                }
            }
        }
        path.erase(owner);
    };
    walk(*root, Transform::identity(), 0, false);
    if (parts.empty())
        return {};
    DxfDocument staged = *this;
    std::vector<std::uint64_t> result;
    auto splice = [&](DxfRecord &out, const DxfRecord &fresh, const DxfRecord &geometry) {
        auto split = [](const DxfRecord &r) {
            std::pair<std::vector<DxfRecord::Pair>, std::vector<DxfRecord::Pair>> out;
            bool modern = std::any_of(r.pairs_.begin(), r.pairs_.end(),
                                      [](const auto &p) { return p.code == 100 && p.value == "AcDbEntity"; }),
                 body = false, xdata = false;
            int depth = 0;
            for (const auto &p : r.pairs_) {
                if (p.code == 0)
                    continue;
                if (p.code == 1001)
                    xdata = true;
                if (p.code == 100 && p.value != "AcDbEntity")
                    body = true;
                bool group = depth > 0 || p.code == 102;
                if (p.code == 102) {
                    if (!p.value.empty() && p.value.front() == '{')
                        ++depth;
                    else if (depth && p.value == "}")
                        --depth;
                }
                bool common = xdata || (group && !body) || p.code == 5 || (p.code == 330 && !body) ||
                              (modern ? !body
                                      : (p.code == 8 || p.code == 6 || p.code == 62 || p.code == 67 ||
                                         p.code == 60 || p.code == 48));
                (common ? out.first : out.second).push_back(p);
            }
            return out;
        };
        auto common = split(fresh).first, body = split(geometry).second;
        auto at = std::find_if(common.begin(), common.end(), [](const auto &p) { return p.code == 1001; });
        common.insert(at, body.begin(), body.end());
        common.insert(common.begin(), {0, geometry.type()});
        out = fresh;
        out.pairs_ = std::move(common);
    };
    auto detach_reactors = [](DxfRecord &r) {
        std::vector<DxfRecord::Pair> kept;
        int depth = 0;
        for (const auto &p : r.pairs_) {
            if (!depth && p.code == 102 && p.value == "{ACAD_REACTORS") {
                depth = 1;
                continue;
            }
            if (depth) {
                if (p.code == 102) {
                    if (!p.value.empty() && p.value.front() == '{')
                        ++depth;
                    else if (p.value == "}")
                        --depth;
                }
                continue;
            }
            kept.push_back(p);
        }
        if (depth)
            throw std::invalid_argument("Unterminated reactor group in expanded member");
        r.pairs_ = std::move(kept);
    };
    for (auto &part : parts) {
        if (part.source.empty()) {
            // Anonymous shear fallbacks in Rust detach leaf graphs and identities.
            // Bake directly to leaves here, with the same detached graph boundary.
            auto detach = [](DxfRecord &r) {
                std::vector<DxfRecord::Pair> kept;
                int depth = 0;
                bool body = false;
                for (const auto &p : r.pairs_) {
                    if (!depth && p.code == 102 &&
                        (p.value == "{ACAD_REACTORS" || p.value == "{ACAD_XDICTIONARY")) {
                        depth = 1;
                        continue;
                    }
                    if (depth) {
                        if (p.code == 102) {
                            if (!p.value.empty() && p.value.front() == '{')
                                ++depth;
                            else if (p.value == "}")
                                --depth;
                        }
                        continue;
                    }
                    if (p.code == 100 && p.value != "AcDbEntity")
                        body = true;
                    if (p.code == 5 || p.code == 330 || (!body && (p.code == 92 || p.code == 310)))
                        continue;
                    kept.push_back(p);
                }
                if (depth)
                    throw std::invalid_argument("Unterminated detached application group");
                r.pairs_ = std::move(kept);
            };
            std::visit([&](DxfRecord &r) { detach(r); }, part.geometry);
            if (auto *s = sequence(part.geometry)) {
                for (auto &c : s->children_)
                    detach(c);
                if (s->seqend_)
                    detach(*s->seqend_);
            }
            result.push_back(staged.insert_entity(std::move(part.geometry), target));
            continue;
        }
        // The cloned root can contain payload references to its owned objects
        // (IMAGE reactors, table content, field/XRECORD data). Keep the clone's
        // identity graph when installing transformed source geometry.
        std::vector<std::pair<std::uint64_t, std::string>> old_tree;
        if (!sequence(part.geometry))
            for (const auto *r : staged.extension_tree({staged.get_record(part.source)}))
                old_tree.emplace_back(handle_value(r->handle()), r->type());
        auto id = staged.copy_entity(part.source, target);
        if (!old_tree.empty()) {
            auto new_tree = staged.extension_tree({staged.get_record(hex_handle(id))});
            if (new_tree.size() != old_tree.size())
                throw std::invalid_argument("Expanded ownership graph size mismatch");
            std::unordered_map<std::uint64_t, std::uint64_t> remap;
            for (std::size_t i = 0; i < old_tree.size(); ++i) {
                if (old_tree[i].second != new_tree[i]->type())
                    throw std::invalid_argument("Expanded ownership graph type mismatch");
                remap.emplace(old_tree[i].first, handle_value(new_tree[i]->handle()));
            }
            std::visit(
                [&](DxfRecord &r) {
                    for (const auto &position : staged.reference_positions(r)) {
                        auto &p = r.pairs_[position.first];
                        if (auto it = remap.find(handle_value(p.value)); it != remap.end()) {
                            if (!position.second)
                                throw std::invalid_argument("Opaque internal reference in expanded member");
                            p.value = hex_handle(it->second);
                        }
                    }
                },
                part.geometry);
        }
        auto index = staged.handle_index_.at(id).front().record;
        auto fresh = staged.records_[index];
        std::visit(
            [&](auto &e) {
                if constexpr (std::is_base_of_v<DxfEntity, std::decay_t<decltype(e)>>) {
                    DxfEntity visual = e;
                    std::visit(
                        [&](const DxfRecord &r) {
                            auto desired = static_cast<const DxfRecord &>(e);
                            splice(e, r, desired);
                        },
                        fresh);
                    detach_reactors(e);
                    e.set_layer(visual.layer());
                    e.set_linetype(visual.linetype());
                    e.set_color(visual.color());
                    e.set_line_weight(visual.line_weight());
                    e.set_invisible(visual.is_invisible());
                    e.set_display_properties(visual.display_properties());
                }
            },
            part.geometry);
        auto from = sequence(fresh), to = sequence(part.geometry);
        if (from && to) {
            if (from->children_.size() != to->children_.size())
                throw std::invalid_argument("Expanded child count mismatch");
            for (std::size_t i = 0; i < to->children_.size(); ++i) {
                auto desired = to->children_[i];
                splice(to->children_[i], from->children_[i], desired);
                detach_reactors(to->children_[i]);
            }
            to->seqend_ = from->seqend_;
        }
        if (auto *viewport = std::get_if<DxfViewport>(&part.geometry)) {
            // copy_entity allocates a unique id in the target layout. Geometry
            // splicing must not restore the block member's zero/local id.
            const auto &copied = std::get<DxfViewport>(fresh);
            viewport->set_short_field("AcDbViewport", 69, copied.definition().id);
        }
        staged.records_[index] = std::move(part.geometry);
        staged.rebuild_indexes();
        if (std::holds_alternative<DxfTable>(staged.records_[index]))
            staged.synchronize_table_dimensions(hex_handle(id));
        result.push_back(id);
    }
    if (remove_source && !staged.remove_entity(handle))
        throw std::invalid_argument("Expansion source disappeared");
    *this = std::move(staged);
    return result;
}
