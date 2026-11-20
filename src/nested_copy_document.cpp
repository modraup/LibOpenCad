// SPDX-License-Identifier: MPL-2.0
// Port of acadrust CadDocument nested_copy_symbol_names / localize_nested_copy_symbols.
#include <algorithm>
#include <cctype>

NestedCopySymbolNames DxfDocument::nested_copy_symbol_names(NestedCopyMode mode) const {
    std::vector<std::string> layers;
    std::vector<std::string> lines;
    for_each<DxfLayer>([&](const DxfLayer &layer) { layers.push_back(layer.name()); });
    for_each<DxfLineType>([&](const DxfLineType &line) { lines.push_back(line.name()); });
    NestedCopySymbolNames names;
    names.layers = nested_copy_destinations(std::move(layers), mode);
    names.line_types = nested_copy_destinations(std::move(lines), mode);
    return names;
}

std::size_t DxfDocument::localize_nested_copy_symbols(const std::vector<std::uint64_t> &entities,
                                                      const NestedCopySymbolNames &names) {
    auto upper = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return s;
    };
    auto simple_entity = [](std::string_view type) {
        return type == "POINT" || type == "LINE" || type == "CIRCLE" || type == "ARC" || type == "ELLIPSE" ||
               type == "POLYLINE" || type == "LWPOLYLINE" || type == "SPLINE" || type == "HELIX" || type == "SOLID" ||
               type == "3DFACE" || type == "RAY" || type == "XLINE" || type == "MESH";
    };
    std::size_t retained = 0;
    DxfDocument staged = *this;
    for (auto handle : entities) {
        auto *record = staged.object_record(handle);
        if (!record)
            continue;
        auto layer = record->text_field("AcDbEntity", 8, "0");
        auto linetype = record->text_field("AcDbEntity", 6, {});
        if (layer.find('|') == std::string::npos && linetype.find('|') == std::string::npos)
            continue;
        auto material = record->field("AcDbEntity", 347);
        auto plot = record->field("AcDbEntity", 390);
        if (!material.empty() || !plot.empty() || !simple_entity(record->type())) {
            ++retained;
            continue;
        }
        auto local_line = [&](const std::string &source) -> std::optional<std::string> {
            if (source.find('|') == std::string::npos)
                return source;
            auto it = names.line_types.find(upper(source));
            if (it == names.line_types.end())
                return std::nullopt;
            if (staged.get_linetype(it->second))
                return it->second;
            auto *src = staged.get_linetype(source);
            if (!src)
                return std::nullopt;
            for (const auto &el : src->elements())
                if (el.complex)
                    return std::nullopt;
            LineTypeDefinition def;
            def.name = it->second;
            def.description = src->description();
            def.elements = src->elements();
            def.pattern_length = src->pattern_length();
            def.xref_dependent = false;
            staged.add_linetype(def);
            return it->second;
        };
        std::string layer_name = layer;
        if (layer.find('|') != std::string::npos) {
            auto it = names.layers.find(upper(layer));
            if (it == names.layers.end()) {
                ++retained;
                continue;
            }
            if (auto *existing = staged.get_layer(it->second))
                layer_name = existing->name();
            else {
                auto *imported = staged.get_layer(layer);
                if (!imported) {
                    ++retained;
                    continue;
                }
                auto material = imported->field("AcDbLayerTableRecord", 347);
                auto plot = imported->field("AcDbLayerTableRecord", 390);
                std::string zero_material, zero_plot;
                if (auto *zero = staged.get_layer("0")) {
                    zero_material = zero->field("AcDbLayerTableRecord", 347);
                    zero_plot = zero->field("AcDbLayerTableRecord", 390);
                }
                auto implicit_ref = [](const std::string &value, const std::string &fallback) {
                    return value.empty() || value == fallback;
                };
                if (!implicit_ref(material, zero_material) || !implicit_ref(plot, zero_plot)) {
                    ++retained;
                    continue;
                }
                auto source_line = imported->linetype();
                auto color = imported->signed_color_index();
                auto dest_layer = it->second;
                auto prefix = layer.substr(0, layer.rfind('|'));
                auto prefixed = prefix + "|" + source_line;
                if (source_line.find('|') == std::string::npos && staged.get_linetype(prefixed))
                    source_line = prefixed;
                auto dest_line = local_line(source_line);
                if (!dest_line) {
                    ++retained;
                    continue;
                }
                staged.add_layer(dest_layer, color, *dest_line);
                layer_name = dest_layer;
            }
        }
        auto line_name = local_line(linetype);
        if (!line_name) {
            ++retained;
            continue;
        }
        auto &mutable_entity = staged.mutable_object(handle);
        mutable_entity.set_text_field("AcDbEntity", 8, layer_name);
        if (line_name->empty() || *line_name == "ByLayer")
            mutable_entity.set_text_field("AcDbEntity", 6, {});
        else
            mutable_entity.set_text_field("AcDbEntity", 6, *line_name);
    }
    *this = std::move(staged);
    return retained;
}
