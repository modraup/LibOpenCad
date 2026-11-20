// SPDX-License-Identifier: MPL-2.0
#include "dwg_edit_internal.hpp"
#include "dwg_metadata_internal.hpp"
#include "dwg_sections_internal.hpp"
#include "surface_profiles.hpp"
#include "dxf_code_page.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <istream>
#include <iterator>
#include <limits>
#include <map>
#include <libopencad/dwg_document.hpp>
#include <string_view>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
namespace libopencad {
namespace {
std::uint64_t identity(const DxfEntity &e) {
    auto text = e.handle();
    std::uint64_t h{};
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), h, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !h)
        throw std::invalid_argument("Missing DWG entity handle");
    return h;
}
std::string hex(std::uint64_t n) {
    char buf[16];
    auto end = std::to_chars(buf, buf + sizeof(buf), n, 16).ptr;
    for (auto p = buf; p != end; p++)
        if (*p >= 'a')
            *p -= 'a' - 'A';
    return {buf, end};
}
template <class T, class V> void extrusion(T &e, const V &v) {
    e.set_thickness(v.thickness);
    e.set_normal(v.normal);
}
} // namespace
DwgDocument::DwgDocument(std::string bytes) : bytes_(std::move(bytes)), objects_(read_dwg_objects(bytes_)) {
    for (auto &[h, rec] : objects_.associative_records) {
        auto record = objects_.records.find(h);
        if (record == objects_.records.end())
            continue;
        for (auto &c : objects_.classes)
            if (c.number == record->second.type) {
                if (!c.dxf_name.empty())
                    rec.dxf_name = c.dxf_name;
                if (!c.cpp_name.empty())
                    rec.cpp_class_name = c.cpp_name;
                break;
            }
        if (rec.cpp_class_name.empty())
            rec.cpp_class_name = associative_cpp_class_name(rec.dxf_name).value_or("");
    }
    for (auto &[h, rec] : objects_.class_objects) {
        auto record = objects_.records.find(h);
        if (record == objects_.records.end())
            continue;
        for (auto &c : objects_.classes)
            if (c.number == record->second.type) {
                if (!c.dxf_name.empty())
                    rec.dxf_name = c.dxf_name;
                if (!c.cpp_name.empty())
                    rec.cpp_class_name = c.cpp_name;
                break;
            }
        if (rec.cpp_class_name.empty())
            rec.cpp_class_name = class_object_cpp_name(rec.dxf_name);
    }
    for (auto &[h, m] : objects_.modelers) {
        if (!m.surface || m.surface->type != "LOFTEDSURFACE")
            continue;
        auto &p = m.surface->profiles;
        if (!p.sections.empty() || !p.guides.empty() || p.path)
            continue;
        try {
            if (auto r = detail::metadata_record(objects_, h, m.properties.extension_dictionary,
                                                 "CADCODEC_LOFT_CURVES_V1"))
                p = detail::decode_loft_profile_metadata(detail::metadata_profile_fields(r->metadata));
        } catch (const std::invalid_argument
                     &) { /* Retain the native geometry and opaque metadata, as Rust does. */
        }
    }
}
DwgDocument DwgDocument::read(std::istream &input) {
    std::string bytes{std::istreambuf_iterator<char>(input), {}};
    if (input.bad())
        throw std::runtime_error("Cannot read DWG");
    return DwgDocument(std::move(bytes));
}
void DwgDocument::write(std::ostream &out) const {
    out.write(bytes_.data(), bytes_.size());
    if (!out)
        throw std::runtime_error("Cannot write DWG");
}
void DwgDocument::initialize(DxfEntity &e, const char *type, std::uint64_t h,
                             const DwgR2000EntityProperties &p) const {
    e.pairs_ = {{0, type}, {5, hex(h)}};
    e.version_ = dxf_version_from_string(std::string_view(bytes_).substr(0, 6));
    e.set_color(p.color);
    e.uses_dwg_color_book_ = p.color_book != 0;
    e.set_transparency(Transparency::from_alpha_value(p.transparency));
    if (p.linetype_scale != 1.0) e.set_linetype_scale(p.linetype_scale);
    e.set_invisible(p.invisible);
    if (e.version_ >= DxfVersion::AC1015)
        e.set_line_weight(LineWeight::from_dwg_index(p.lineweight_index));
    if (p.linetype_flags == 1)
        e.set_linetype("ByBlock");
    if (p.linetype_flags == 2)
        e.set_linetype("Continuous");
    if (p.linetype && objects_.symbols.count(p.linetype))
        e.set_linetype(objects_.symbols.at(p.linetype).name);
    auto owner = p.owner;
    if (bytes_.compare(0, 6, "AC1009") == 0) {
        auto slot = p.legacy_linetype_index;
        if (slot == 32766 || slot == 65534)
            e.set_linetype("ByBlock");
        else if (slot != 32767 && slot != 65535)
            e.set_linetype(objects_.legacy_symbol_names.at(57).at(slot));
        e.set_layer(objects_.legacy_symbol_names.at(51).at(p.legacy_layer_index));
    } else {
        if (p.mode)
            owner = p.mode == 1 ? objects_.paper_space : objects_.model_space;
        e.set_layer(objects_.symbols.at(p.layer).name);
    }
    if (owner)
        e.set_field("", 330, hex(owner));
}
DxfLine DwgDocument::line(std::uint64_t h) const {
    auto &v = objects_.lines.at(h);
    DxfLine e;
    initialize(e, "LINE", h, v.properties);
    e.set_start(v.start);
    e.set_end(v.end);
    extrusion(e, v);
    return e;
}
DxfCircle DwgDocument::circle(std::uint64_t h) const {
    auto &v = objects_.circles.at(h);
    DxfCircle e;
    initialize(e, "CIRCLE", h, v.properties);
    e.set_center(v.center);
    e.set_radius(v.radius);
    extrusion(e, v);
    return e;
}
DxfArc DwgDocument::arc(std::uint64_t h) const {
    auto &v = objects_.arcs.at(h);
    DxfArc e;
    initialize(e, "ARC", h, v.properties);
    e.set_center(v.center);
    e.set_radius(v.radius);
    e.set_start_angle_radians(v.start_angle);
    e.set_end_angle_radians(v.end_angle);
    extrusion(e, v);
    return e;
}
#include "dwg_history_document.inc"
#include "dwg_dynamic_document.inc"
#include "dwg_associative_document.inc"
#include "dwg_class_object_document.inc"
#include "arch_document.inc"
#include "civil_document.inc"
const DwgSurface &DwgDocument::surface(std::uint64_t h) const {
    const auto &m = modeler(h);
    if (!m.surface)
        throw std::invalid_argument("Handle is not a DWG surface");
    return *m.surface;
}
void DwgDocument::update_surface(std::uint64_t h, const DwgSurface &value) {
    if (surface(h).type != value.type)
        throw std::invalid_argument("Cannot change DWG surface class");
    const DwgHistoryRecord *fallback = nullptr;
    if (value.type == "LOFTEDSURFACE" && modeler(h).properties.extension_dictionary) {
        fallback = detail::metadata_record(objects_, h, modeler(h).properties.extension_dictionary,
                                           "CADCODEC_LOFT_CURVES_V1");
        if (fallback) {
            if (!fallback->reference_values.empty() || detail::metadata_value(fallback->metadata, 90) != 1)
                throw std::invalid_argument("Unsupported loft fallback metadata");
            for (auto f : detail::metadata_fields(fallback->metadata))
                if (f.code != 90 && f.code != 70 && f.code != 91 && f.code != 92 && f.code != 310)
                    throw std::invalid_argument("Private loft fallback fields");
        }
    }
    std::optional<std::pair<std::uint64_t, std::string>> replacement;
    if (fallback)
        replacement = {{fallback->handle, detail::metadata_profile_bytes(
                                              detail::encode_loft_profile_metadata(value.profiles))}};
    DwgDocument staged(detail::edit_dwg_surface(bytes_, objects_, h, value));
    if (replacement)
        staged = DwgDocument(detail::edit_dwg_metadata(staged.bytes_, staged.objects_, replacement->first,
                                                       replacement->second));
    (void)staged.surface(h);
    *this = std::move(staged);
}
const DwgModeler &DwgDocument::modeler(std::uint64_t h) const {
    if (auto it = objects_.modeler_errors.find(h); it != objects_.modeler_errors.end())
        throw std::invalid_argument("DWG modeler " + hex(h) + ": " + it->second);
    return objects_.modelers.at(h);
}
std::optional<acis::SatDocument> DwgDocument::modeler_geometry(std::uint64_t h) const {
    auto &data = modeler(h).data.geometry;
    if (!data.sab.empty())
        return acis::SabReader::read(data.sab);
    if (data.sat.find_first_not_of(" \t\r\n") == std::string::npos)
        return {};
    return acis::SatDocument::parse(data.sat);
}
std::uint64_t DwgDocument::add_modeler(ModelerKind kind, const acis::SatDocument &geometry,
                                       std::uint64_t target) {
    unsigned type = kind == ModelerKind::Solid3D  ? 38
                    : kind == ModelerKind::Region ? 37
                    : kind == ModelerKind::Body   ? 39
                                                  : 0;
    if (!type)
        throw std::invalid_argument("Use add_surface for surface construction inputs");
    std::uint64_t id;
    DwgDocument staged(detail::add_dwg_modeler(bytes_, objects_, type, nullptr, target, id));
    staged.set_modeler_geometry(id, geometry);
    *this = std::move(staged);
    return id;
}
std::uint64_t DwgDocument::add_surface(const DwgSurface &value, const acis::SatDocument &geometry,
                                       std::uint64_t target) {
    DwgDocument staged = *this;
    auto it = std::find(staged.objects_.class_names.begin(), staged.objects_.class_names.end(), value.type);
    if (it == staged.objects_.class_names.end())
        staged = DwgDocument(detail::register_dwg_surface(staged.bytes_, staged.objects_, value.type));
    it = std::find(staged.objects_.class_names.begin(), staged.objects_.class_names.end(), value.type);
    std::uint64_t id;
    auto type = 500 + std::distance(staged.objects_.class_names.begin(), it);
    staged = DwgDocument(detail::add_dwg_modeler(staged.bytes_, staged.objects_, type, &value, target, id));
    staged.set_modeler_geometry(id, geometry);
    *this = std::move(staged);
    return id;
}
std::uint64_t DwgDocument::copy_modeler(std::uint64_t h, std::uint64_t target,
                                        std::map<std::uint64_t, std::uint64_t> *remapping) {
    (void)modeler(h);
    std::map<std::uint64_t, std::uint64_t> ids;
    DwgDocument staged(detail::copy_dwg_modeler(bytes_, objects_, h, target, ids));
    auto id = ids.at(h);
    (void)staged.modeler(id);
    if (staged.objects_.history_errors.size() > objects_.history_errors.size() ||
        staged.objects_.metadata_errors.size() > objects_.metadata_errors.size())
        throw std::invalid_argument("Copied modeler graph failed validation");
    if (remapping)
        *remapping = ids;
    *this = std::move(staged);
    return id;
}
void DwgDocument::remove_modeler(std::uint64_t h) {
    (void)modeler(h);
    DwgDocument staged(detail::remove_dwg_modeler(bytes_, objects_, h));
    if (staged.objects_.records.count(h))
        throw std::runtime_error("Deleted modeler still indexed");
    *this = std::move(staged);
}
void DwgDocument::set_modeler_geometry(std::uint64_t h, const acis::SatDocument &geometry) {
    (void)modeler(h);
    if (geometry.records.empty())
        throw std::invalid_argument("Replacement modeler geometry is empty");
    DwgDocument staged(detail::edit_dwg_modeler(bytes_, objects_, h, geometry));
    (void)staged.modeler(h);
    *this = std::move(staged);
}
void DwgDocument::clear_modeler_geometry(std::uint64_t h) {
    if (modeler(h).acis_empty)
        return;
    DwgDocument staged(detail::clear_dwg_modeler(bytes_, objects_, h));
    if (!staged.modeler(h).acis_empty)
        throw std::runtime_error("Modeler clear did not synchronize state");
    *this = std::move(staged);
}
Vector3 DwgDocument::modeler_reference_point(std::uint64_t h) const {
    auto stored = modeler(h).data.reference_point;
    if (stored != Vector3{})
        return stored;
    auto g = modeler_geometry(h);
    if (!g)
        return {};
    auto placement = g->placement();
    Vector3 low{}, high{};
    bool found = false;
    for (auto &record : g->records)
        if (record.entity_type == "point") {
            std::optional<Vector3> point;
            std::vector<double> numbers;
            for (auto &token : record.tokens)
                if (auto c = token.coordinates()) {
                    if (c->second == 3)
                        point = Vector3{c->first[0], c->first[1], c->first[2]};
                } else if (auto n = token.as_float())
                    numbers.push_back(*n);
            if (!point && numbers.size() >= 3) {
                auto n = numbers.size();
                point = Vector3{numbers[n - 3], numbers[n - 2], numbers[n - 1]};
            }
            if (!point)
                continue;
            auto p = placement.apply(*point);
            if (!found) {
                low = high = p;
                found = true;
            } else {
                low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
            }
        }
    return found ? (low + high) * 0.5 : placement.translation;
}
DxfText DwgDocument::text(std::uint64_t h) const {
    const auto &t = objects_.texts.at(h);
    DxfText e;
    initialize(e, "TEXT", h, t.properties);
    TextDefinition d;
    d.text = t.value;
    d.insertion = t.insertion;
    d.normal = t.normal;
    if (t.has_alignment)
        d.alignment_point = t.alignment;
    d.height = t.height;
    d.thickness = t.thickness;
    d.rotation = t.rotation;
    d.oblique_angle = t.oblique_angle;
    d.width_factor = t.width_factor;
    d.generation_flags = t.generation;
    d.horizontal_alignment = t.horizontal;
    d.vertical_alignment = t.vertical;
    if (bytes_.compare(0, 6, "AC1009") == 0)
        d.style = objects_.legacy_symbol_names.at(53).at(t.style);
    else if (objects_.symbols.count(t.style))
        d.style = objects_.symbols.at(t.style).name;
    e.set_definition(d);
    return e;
}
void DwgDocument::update(const DxfText &entity) {
    auto h = identity(entity);
    auto before = text(h);
    auto fixed = [](const DxfText &e) {
        std::vector<std::pair<int, std::string>> fields;
        for (const auto &p : e.pairs_)
            // DWG stores one elevation for insertion and alignment. Group 31
            // is derived on read, including when reapplying an edited snapshot.
            if (p.code != 1 && p.code != 10 && p.code != 20 && p.code != 30 && p.code != 31 && p.code != 7 &&
                p.code != 40 && p.code != 41 && p.code != 50 && p.code != 11 && p.code != 21 &&
                p.code != 72 && p.code != 73 && p.code != 51 && p.code != 71 && p.code != 39 &&
                p.code != 210 && p.code != 220 && p.code != 230 && p.code != 8 && p.code != 6 &&
                p.code != 48 && p.code != 60 && p.code != 62 && p.code != 370 && p.code != 420 &&
                p.code != 440)
                fields.emplace_back(p.code, p.value);
        return fields;
    };
    // Preserve unchanged angles through the shared DXF degree/radian projection.
    auto projected = before;
    projected.set_definition(before.definition());
    if (fixed(entity) != fixed(before))
        throw std::invalid_argument("Unsupported DWG TEXT property edit");
    const auto requested = entity.definition(), original = before.definition();
    auto desired = objects_.texts.at(h);
    bool format = false;
    auto assign = [&](auto &field, auto value, auto previous) {
        if (value != previous && value != field) {
            field = value;
            format = true;
        }
    };
    assign(desired.height, requested.height, original.height);
    assign(desired.width_factor, requested.width_factor, original.width_factor);
    const auto projection = projected.definition();
    if (requested.rotation != projection.rotation)
        assign(desired.rotation, requested.rotation, original.rotation);
    if (requested.oblique_angle != projection.oblique_angle)
        assign(desired.oblique_angle, requested.oblique_angle, original.oblique_angle);
    assign(desired.thickness, requested.thickness, original.thickness);
    assign(desired.normal, requested.normal, original.normal);
    const bool legacy = bytes_.compare(0, 6, "AC1009") == 0;
    // acadrust's AC1009 record writer stores generation flags as an unsigned byte.
    const auto generation =
        legacy ? static_cast<std::int16_t>(static_cast<std::uint8_t>(requested.generation_flags))
               : requested.generation_flags;
    assign(desired.generation, generation, original.generation_flags);
    assign(desired.horizontal, requested.horizontal_alignment, original.horizontal_alignment);
    assign(desired.vertical, requested.vertical_alignment, original.vertical_alignment);
    const auto alignment = requested.alignment_point.value_or(Point3{});
    const auto previous_alignment = original.alignment_point.value_or(Point3{});
    if (alignment.x != previous_alignment.x || alignment.y != previous_alignment.y ||
        (legacy && requested.alignment_point.has_value() != original.alignment_point.has_value())) {
        desired.alignment = alignment;
        desired.has_alignment = requested.alignment_point.has_value();
        format = true;
    }
    auto name = detail::symbol_name_key(requested.style);
    if (name != detail::symbol_name_key(original.style)) {
        std::optional<std::uint64_t> style;
        auto match = [&](std::uint64_t id, std::string_view candidate) {
            if (detail::symbol_name_key(candidate) != name)
                return;
            if (style)
                throw std::invalid_argument("Ambiguous DWG text style");
            style = id;
        };
        if (legacy) {
            const auto &names = objects_.legacy_symbol_names.at(53);
            for (std::size_t i = 0; i < names.size(); ++i)
                match(i, names[i]);
        } else {
            for (const auto &[id, symbol] : objects_.symbols)
                if (symbol.type == 53)
                    match(id, symbol.name);
        }
        if (!style)
            throw std::invalid_argument("Unknown DWG text style");
        if (legacy && *style > 255)
            throw std::invalid_argument("AC1009 TEXT style slot exceeds one byte");
        desired.style = *style;
        format = true;
    }
    std::optional<DwgDocument> staged;
    if (format)
        staged = DwgDocument(detail::format_dwg_text(bytes_, objects_, h, desired));
    const auto &source = staged ? *staged : *this;
    if (entity.text() != before.text() || entity.insertion_point() != before.insertion_point())
        staged = DwgDocument(detail::edit_dwg_text(source.bytes_, source.objects_, h, entity.text(),
                                                   entity.insertion_point()));
    if (detail::symbol_name_key(entity.layer()) != detail::symbol_name_key(before.layer())) {
        const auto &current = staged ? *staged : *this;
        staged =
            DwgDocument(detail::set_dwg_entity_layer(current.bytes_, current.objects_, h, entity.layer()));
    }
    apply_common(entity, before, staged);
    if (staged)
        *this = std::move(*staged);
}
template <class E>
std::optional<std::string> DwgDocument::edit_geometry_layer(const E &entity, const E &before) const {
    auto h = identity(entity);
    auto is_geometry_or_layer = [](int code) {
        switch (code) {
        case 8:
        case 10:
        case 20:
        case 30:
        case 39:
        case 210:
        case 220:
        case 230:
            return true;
        case 11:
        case 21:
        case 31:
            return std::is_same_v<E, DxfLine>;
        case 40:
            return !std::is_same_v<E, DxfLine>;
        case 50:
        case 51:
            return std::is_same_v<E, DxfArc>;
        default:
            return false;
        }
    };
    struct Fields {
        std::vector<std::pair<int, std::string_view>> editable, fixed;
    };
    auto fields = [&](const E &e) {
        Fields result;
        for (const auto &p : e.pairs_) {
            if (p.code == 6 || p.code == 48 || p.code == 60 || p.code == 62 || p.code == 370 || p.code == 420 || p.code == 440)
                continue;
            auto &group = is_geometry_or_layer(p.code) ? result.editable : result.fixed;
            group.emplace_back(p.code, p.value);
        }
        return result;
    };
    auto desired = fields(entity), original_fields = fields(before);
    if (desired.fixed != original_fields.fixed)
        throw std::invalid_argument("DWG update supports geometry, layer, linetype, linetype scale, color, visibility, lineweight and transparency edits");
    if (desired.editable == original_fields.editable)
        return {};
    DwgPrimitive value;
    value.normal = entity.normal();
    value.thickness = entity.thickness();
    if constexpr (std::is_same_v<E, DxfLine>)
        value.geometry = DwgLineGeometry{entity.start(), entity.end()};
    else if constexpr (std::is_same_v<E, DxfCircle>)
        value.geometry = DwgCircleGeometry{entity.center(), entity.radius()};
    else {
        auto &original = objects_.arcs.at(h);
        value.geometry = DwgArcGeometry{
            entity.center(), entity.radius(),
            entity.start_angle_radians() == before.start_angle_radians() ? original.start_angle
                                                                         : entity.start_angle_radians(),
            entity.end_angle_radians() == before.end_angle_radians() ? original.end_angle
                                                                     : entity.end_angle_radians()};
    }
    auto name = detail::symbol_name_key(entity.layer());
    std::optional<std::uint64_t> layer;
    const DwgR2000EntityProperties *props;
    if constexpr (std::is_same_v<E, DxfLine>)
        props = &objects_.lines.at(h).properties;
    else if constexpr (std::is_same_v<E, DxfCircle>)
        props = &objects_.circles.at(h).properties;
    else
        props = &objects_.arcs.at(h).properties;
    if (name == detail::symbol_name_key(before.layer()))
        layer = bytes_.compare(0, 6, "AC1009") == 0 ? props->legacy_layer_index : props->layer;
    else if (bytes_.compare(0, 6, "AC1009") == 0) {
        auto &names = objects_.legacy_symbol_names.at(51);
        for (std::size_t i = 0; i < names.size(); i++)
            if (detail::symbol_name_key(names[i]) == name)
                layer = i;
    } else
        for (auto &[id, s] : objects_.symbols)
            if (s.type == 51 && detail::symbol_name_key(s.name) == name)
                layer = id;
    if (!layer)
        throw std::invalid_argument("Unknown DWG layer");
    return detail::edit_dwg_primitive(bytes_, objects_, h, value, *layer);
}

template <class E> void DwgDocument::apply(const E &entity, const E &before) {
    auto changed = edit_geometry_layer(entity, before);
    std::optional<DwgDocument> staged;
    if (changed)
        staged = DwgDocument(std::move(*changed));
    apply_common(entity, before, staged);
    if (staged)
        *this = std::move(*staged);
}
void DwgDocument::apply_common(const DxfEntity &entity, const DxfEntity &before,
                               std::optional<DwgDocument> &staged) const {
    auto name_key = [](std::string_view name) {
        return detail::symbol_name_key(name.empty() ? "ByLayer" : name);
    };
    auto name = entity.linetype();
    auto source = [&]() -> const DwgDocument & { return staged ? *staged : *this; };
    auto handle = identity(entity);
    if (name_key(name) != name_key(before.linetype()))
        staged =
            DwgDocument(detail::set_dwg_entity_linetype(source().bytes_, source().objects_, handle, name));
    auto color = entity.color();
    if (color != before.color() || (before.uses_dwg_color_book_ && !entity.uses_dwg_color_book_))
        staged = DwgDocument(detail::set_dwg_entity_color(source().bytes_, source().objects_, handle, color));
    std::optional<bool> invisible;
    if (before.version_ >= DxfVersion::AC1012 && entity.is_invisible() != before.is_invisible())
        invisible = entity.is_invisible();
    std::optional<std::uint8_t> weight;
    if (before.version_ >= DxfVersion::AC1015) {
        auto index = entity.line_weight().to_dwg_index();
        if (LineWeight::from_dwg_index(index) != before.line_weight())
            weight = index;
    }
    std::optional<double> scale;
    if (before.version_ >= DxfVersion::AC1012 && entity.linetype_scale() != before.linetype_scale())
        scale = entity.linetype_scale();
    std::optional<Transparency> transparency;
    if (before.version_ >= DxfVersion::AC1018 && entity.transparency() != before.transparency())
        transparency = entity.transparency();
    if (transparency || scale || invisible || weight)
        staged = DwgDocument(detail::set_dwg_entity_display(source().bytes_, source().objects_, handle,
                                                            transparency, scale, invisible, weight));
}
void DwgDocument::update(const DxfLine &e) { apply(e, line(identity(e))); }
void DwgDocument::update(const DxfCircle &e) { apply(e, circle(identity(e))); }
void DwgDocument::update(const DxfArc &e) { apply(e, arc(identity(e))); }
std::uint64_t DwgDocument::add_layer(std::string_view name, int color) {
    detail::valid_symbol_name(name);
    if (color < 1 || color > 255)
        throw std::invalid_argument("Expected an ACI layer color between 1 and 255");
    std::uint64_t id{};
    *this = DwgDocument(detail::add_dwg_layer(bytes_, objects_, name, color, id));
    return id;
}
std::uint64_t DwgDocument::add_linetype(std::string_view name, const std::vector<double> &segments,
                                        std::string_view description) {
    detail::valid_symbol_name(name);
    if (description.find('\0') != std::string_view::npos)
        throw std::invalid_argument("DWG linetype description contains NUL");
    detail::utf8_scalars(description);
    std::uint64_t id{};
    *this = DwgDocument(detail::add_dwg_linetype(bytes_, objects_, name, segments, description, id));
    return id;
}
void DwgDocument::set_layer_linetype(std::string_view layer, std::string_view linetype) {
    auto bytes = detail::set_dwg_layer_linetype(bytes_, objects_, layer, linetype);
    if (bytes) *this = DwgDocument(std::move(*bytes));
}
void DwgDocument::move_entity(std::uint64_t entity, std::uint64_t target) {
    std::uint64_t block = 0;
    for (auto &[id, s] : objects_.symbols)
        if (s.type == 49 && (id == target || (s.layout && s.layout == target))) {
            if (block && block != id)
                throw std::invalid_argument("Ambiguous DWG layout");
            block = id;
        }
    if (!block)
        throw std::invalid_argument("Expected a block record or layout handle");
    *this = DwgDocument(detail::move_dwg_entity(bytes_, objects_, entity, block));
}
void DwgDocument::move_entity(std::uint64_t entity, std::string_view name) {
    if (bytes_.compare(0, 6, "AC1009") == 0) {
        *this = DwgDocument(detail::move_legacy_entity(bytes_, objects_, entity, name));
        return;
    }
    auto key = detail::symbol_name_key(name);
    if (key == U"*MODEL_SPACE" || key == U"*PAPER_SPACE") {
        move_entity(entity, key == U"*MODEL_SPACE" ? objects_.model_space : objects_.paper_space);
        return;
    }
    std::uint64_t block = 0;
    for (auto &[id, s] : objects_.symbols)
        if (s.type == 49 && detail::symbol_name_key(s.name) == key) {
            if (block)
                throw std::invalid_argument("Ambiguous DWG block name");
            block = id;
        }
    move_entity(entity, block);
}

namespace {
std::uint32_t preview_seeker(const std::string &bytes) {
    if (bytes.size() < 17)
        return 0;
    return static_cast<unsigned char>(bytes[13]) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[14])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[15])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[16])) << 24);
}
} // namespace

std::optional<Preview> DwgDocument::preview() const {
    const auto base = preview_seeker(bytes_);
    if (!base || base >= bytes_.size())
        return std::nullopt;
    return parse_dwg_preview(std::string_view(bytes_).substr(base), base);
}

void DwgDocument::set_preview(std::optional<Preview> value) {
    auto base = preview_seeker(bytes_);
    auto payload = build_dwg_preview(value ? &*value : nullptr, base);
    if (bytes_.size() >= 6 && bytes_.compare(0, 6, "AC1018") >= 0) {
        auto replace = bytes_.compare(0, 6, "AC1021") == 0 ? detail::replace_ac21_sections
                                                           : detail::replace_ac18_sections;
        *this = DwgDocument(replace(bytes_, {{"AcDb:Preview", std::move(payload)}}));
        return;
    }
    if (!base || base >= bytes_.size())
        throw std::invalid_argument("DWG preview seeker is missing");
    auto overall = dwg_preview_overall_size(std::string_view(bytes_).substr(base));
    auto old_len = overall ? dwg_preview_container_len(*overall) : 0;
    if (old_len && base + old_len <= bytes_.size() && payload.size() == old_len) {
        bytes_.replace(base, old_len, payload);
        return;
    }
    if (old_len && base + old_len == bytes_.size()) {
        bytes_.resize(base);
        bytes_ += payload;
        return;
    }
    throw std::invalid_argument("DWG preview rewrite requires a paged preview section");
}
} // namespace libopencad
