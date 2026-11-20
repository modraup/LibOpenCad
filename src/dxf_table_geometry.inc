// SPDX-License-Identifier: MPL-2.0
// Port of data_objects::TableGeometry and its DXF reader/writer.
namespace {
TableContentGeometry tg_content(const std::vector<DxfRawField> &fields) {
    MLFields f(fields);
    TableContentGeometry v;
    v.distance_to_top_left = {f.r(10, 0), f.r(20, 0), f.r(30, 0)};
    v.distance_to_center = {f.r(11, 0), f.r(21, 0), f.r(31, 0)};
    v.width = f.r(43, 0);
    v.height = f.r(44, 0);
    v.outer_width = f.r(45, 0);
    v.outer_height = f.r(46, 0);
    v.flags = f.i(95, 0);
    v.extra_fields = f.extras();
    return v;
}
TableGeometryCell tg_cell(const std::vector<DxfRawField> &fields) {
    MLFields all(fields);
    std::vector<DxfRawField> head;
    std::vector<std::vector<DxfRawField>> contents;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (!all.hidden[i] && fields[i].code == 10)
            contents.emplace_back();
        (contents.empty() ? head : contents.back()).push_back(fields[i]);
    }
    MLFields f(head);
    TableGeometryCell v;
    v.data_flags = f.i(93, 0);
    v.width_with_gap = f.r(40, 0);
    v.height_with_gap = f.r(41, 0);
    v.table_geometry = f.h(330, 0);
    f.take(94);
    for (const auto &c : contents)
        v.contents.push_back(tg_content(c));
    v.extra_fields = f.extras();
    return v;
}
void tg_validate(const TableGeometryDefinition &v) {
    if (v.rows < 0 || v.columns < 0)
        throw std::invalid_argument("Negative TABLEGEOMETRY dimensions");
    mt_count(v.cells.size());
    for (const auto &c : v.cells)
        mt_count(c.contents.size());
}
} // namespace
TableGeometryDefinition DxfTableGeometry::definition() const {
    auto raw = subclass_fields("AcDbTableGeometry");
    MLFields all(raw);
    std::vector<DxfRawField> head;
    std::vector<std::vector<DxfRawField>> cells;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (!all.hidden[i] && raw[i].code == 93)
            cells.emplace_back();
        (cells.empty() ? head : cells.back()).push_back(raw[i]);
    }
    MLFields f(head);
    TableGeometryDefinition v;
    v.rows = f.i(90, 0);
    v.columns = f.i(91, 0);
    f.take(92);
    for (const auto &c : cells)
        v.cells.push_back(tg_cell(c));
    v.extra_fields = f.extras();
    return v;
}
void DxfTableGeometry::set_definition(const TableGeometryDefinition &v) {
    tg_validate(v);
    MLFields f;
    f.put_i(90, v.rows);
    f.put_i(91, v.columns);
    f.put_i(92, mt_count(v.cells.size()));
    mt_append(f, v.extra_fields);
    for (const auto &cell : v.cells) {
        f.put_i(93, cell.data_flags);
        f.put_r(40, cell.width_with_gap);
        f.put_r(41, cell.height_with_gap);
        f.put_s(330, hex_handle(cell.table_geometry));
        f.put_i(94, mt_count(cell.contents.size()));
        mt_append(f, cell.extra_fields);
        for (const auto &c : cell.contents) {
            f.put_p(10, c.distance_to_top_left);
            f.put_p(11, c.distance_to_center);
            f.put_r(43, c.width);
            f.put_r(44, c.height);
            f.put_r(45, c.outer_width);
            f.put_r(46, c.outer_height);
            f.put_i(95, c.flags);
            mt_append(f, c.extra_fields);
        }
    }
    set_subclass_fields("AcDbTableGeometry", f.values);
}
std::uint64_t DxfDocument::add_table_geometry(std::string_view dictionary, std::string name,
                                              const TableGeometryDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("TABLEGEOMETRY construction requires R2007 or later");
    tg_validate(v);
    for (const auto &c : v.cells)
        mleader_check_handle(*this, c.table_geometry, "TABLEGEOMETRY");
    DxfDocument staged = *this;
    DxfTableGeometry object;
    static_cast<DxfRecord &>(object) = staged.make_record("TABLEGEOMETRY");
    object.pairs_.push_back({100, "AcDbTableGeometry"});
    object.set_definition(v);
    const auto id = staged.add_dictionary_object(dictionary, std::move(name), object);
    *this = std::move(staged);
    return id;
}
std::string DxfDocument::table_geometry_handle(std::string_view entity) const {
    const auto links = table_roundtrip_links(entity);
    if (links.second.empty())
        return {};
    const auto *record = get_record(links.second);
    bool active = false;
    int sections = 0;
    std::string geometry;
    for (const auto &p : record->pairs_) {
        if (p.code == 102) {
            active = p.value == "ACAD_ROUNDTRIP_2008_TABLE_ENTITY";
            if (active && ++sections > 1)
                throw std::invalid_argument("Duplicate table roundtrip section");
        } else if (active && p.code == 361) {
            if (!geometry.empty())
                throw std::invalid_argument("Multiple table geometry links");
            geometry = p.value;
        }
    }
    if (geometry.empty())
        return {};
    const auto *object = get_record(geometry);
    if (records_with_handle(geometry).size() != 1 || !object || object->type() != "TABLEGEOMETRY" ||
        handle_value(object->owner_handle()) != handle_value(links.second))
        throw std::invalid_argument("Invalid table geometry ownership");
    return object->handle();
}
TableGeometryDefinition DxfDocument::table_geometry_definition(std::string_view entity) const {
    const auto handle = table_geometry_handle(entity);
    TableGeometryDefinition v;
    if (!handle.empty())
        for_each<DxfTableGeometry>([&](const auto &g) {
            if (handle_value(g.handle()) == handle_value(handle))
                v = g.definition();
        });
    return v;
}
void DxfDocument::set_table_geometry(std::string_view entity, const TableGeometryDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("TABLEGEOMETRY editing requires R2007 or later");
    const std::string id(entity);
    tg_validate(v);
    const auto geometry = table_geometry_handle(id);
    TableDefinition table;
    for_each<DxfTable>([&](const auto &t) {
        if (handle_value(t.handle()) == handle_value(id))
            table = t.definition();
    });
    if (std::size_t(v.rows) != table.cells.size() || std::size_t(v.columns) != table.column_widths.size() ||
        std::uint64_t(v.rows) * std::uint64_t(v.columns) != v.cells.size())
        throw std::invalid_argument("Geometry dimensions must match the table");
    for (const auto &c : v.cells)
        mleader_check_handle(*this, c.table_geometry, "TABLEGEOMETRY");
    if (!geometry.empty())
        for_each<DxfTable>([&](const auto &other) {
            if (handle_value(other.handle()) != handle_value(id) &&
                table_geometry_handle(other.handle()) == geometry)
                throw std::invalid_argument("Shared table geometry roundtrip link");
        });
    extension_tree({get_record(id)});
    DxfDocument staged = *this;
    // When creating adjuncts, keep the content and geometry in the same roundtrip record.
    if (staged.table_content_handle(id).empty())
        staged.synchronize_table_content(id);
    const auto links = staged.table_roundtrip_links(id);
    if (!geometry.empty()) {
        staged.for_each<DxfTableGeometry>([&](auto &g) {
            if (handle_value(g.handle()) == handle_value(geometry))
                g.set_definition(v);
        });
    } else {
        DxfTableGeometry object;
        static_cast<DxfRecord &>(object) = staged.make_record("TABLEGEOMETRY");
        object.set_field("", 330, links.second);
        object.pairs_.push_back({100, "AcDbTableGeometry"});
        object.set_definition(v);
        const auto fresh = hex_handle(staged.insert_object(object));
        auto &record = const_cast<DxfRecord &>(*staged.get_record(links.second));
        bool active = false;
        std::size_t insertion = record.pairs_.size();
        for (std::size_t i = 0; i < record.pairs_.size(); ++i)
            if (record.pairs_[i].code == 102) {
                if (active) {
                    insertion = i;
                    break;
                }
                active = record.pairs_[i].value == "ACAD_ROUNDTRIP_2008_TABLE_ENTITY";
            }
        if (!active)
            throw std::invalid_argument("Missing table roundtrip marker");
        record.pairs_.insert(record.pairs_.begin() + insertion, {361, fresh});
    }
    staged.rebuild_indexes();
    *this = std::move(staged);
}
void DxfDocument::update_table_geometry(std::string_view entity, const TableContentDefinition &v) {
    const auto handle = table_geometry_handle(entity);
    auto geometry = table_geometry_definition(entity);
    const auto rows = mt_count(v.rows.size()), columns = mt_count(v.columns.size());
    const bool same = geometry.rows == rows && geometry.columns == columns;
    bool supplied = false;
    for (const auto &row : v.rows)
        for (const auto &cell : row.cells)
            supplied |= cell.geometry.has_value();
    if (!supplied && (handle.empty() || same))
        return;
    // Changing the grid shape invalidates unsupplied cells. Never reinterpret an old
    // flat index using a different column count; explicitly provided caches win.
    if (!same)
        geometry.cells.clear();
    geometry.rows = rows;
    geometry.columns = columns;
    geometry.cells.resize(std::size_t(rows) * std::size_t(columns));
    std::size_t at = 0;
    for (const auto &row : v.rows)
        for (const auto &cell : row.cells) {
            if (cell.geometry)
                geometry.cells[at] = *cell.geometry;
            ++at;
        }
    set_table_geometry(entity, geometry);
}
void DxfDocument::synchronize_table_geometry(std::string_view entity) {
    auto content = table_content_definition(entity);
    for (auto &row : content.rows)
        for (auto &cell : row.cells)
            if (!cell.geometry)
                cell.geometry = TableGeometryCell{};
    DxfDocument staged = *this;
    staged.update_table_geometry(entity, content);
    *this = std::move(staged);
}
