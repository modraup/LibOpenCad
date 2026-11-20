// SPDX-License-Identifier: MPL-2.0
// rust CadDocument drawing_variable / hatch_origin / current_entity_transparency.
namespace {
constexpr const char *kVariableDictionary = "AcDbVariableDictionary";
constexpr const char *kCurrentTransparency = "CETRANSPARENCY";
} // namespace

std::optional<std::string> DxfDocument::variable_dictionary_handle() const {
    std::string root;
    try {
        root = root_dictionary_handle();
    } catch (const std::invalid_argument &) {
        return std::nullopt;
    }
    auto vars = dictionary_entry(root, kVariableDictionary);
    if (vars.empty())
        return std::string{};
    const auto *record = get_record(vars);
    if (!record || record->type() != "DICTIONARY")
        return std::nullopt;
    return vars;
}

std::optional<std::string> DxfDocument::ensure_variable_dictionary() {
    auto existing = variable_dictionary_handle();
    if (!existing)
        return std::nullopt;
    if (!existing->empty())
        return existing;
    std::string root;
    try {
        root = root_dictionary_handle();
    } catch (const std::invalid_argument &) {
        return std::nullopt;
    }
    return hex_handle(add_dictionary(root, kVariableDictionary));
}

std::optional<std::string> DxfDocument::drawing_variable(std::string_view name) const {
    if (name.empty())
        return std::nullopt;
    auto vars = variable_dictionary_handle();
    if (!vars || vars->empty())
        return std::nullopt;
    auto entry = dictionary_entry(*vars, name);
    if (entry.empty())
        return std::nullopt;
    std::optional<std::string> out;
    for_each<DxfAuxiliaryObject>([&](const auto &object) {
        if (out || handle_value(object.handle()) != handle_value(entry) || object.type() != "DICTIONARYVAR")
            return;
        out = std::get<DictionaryVariableDefinition>(object.definition()).value;
    });
    return out;
}

bool DxfDocument::set_drawing_variable(std::string_view name, std::string_view value) {
    if (name.empty() || version_ < DxfVersion::AC1021)
        return false;
    auto vars = ensure_variable_dictionary();
    if (!vars || vars->empty())
        return false;
    auto entry = dictionary_entry(*vars, name);
    if (!entry.empty()) {
        const auto *record = get_record(entry);
        if (!record || record->type() != "DICTIONARYVAR")
            return false;
        DictionaryVariableDefinition definition;
        for_each<DxfAuxiliaryObject>([&](const auto &object) {
            if (handle_value(object.handle()) == handle_value(entry))
                definition = std::get<DictionaryVariableDefinition>(object.definition());
        });
        definition.value = std::string(value);
        set_auxiliary_definition(entry, definition);
        return true;
    }
    DictionaryVariableDefinition definition;
    definition.value = std::string(value);
    add_auxiliary_object(*vars, std::string(name), definition);
    return true;
}

Vector2 DxfDocument::hatch_origin() const {
    auto packed = drawing_variable("HPORIGIN");
    if (!packed)
        return {};
    auto first = packed->find(';');
    if (first == std::string::npos)
        return {};
    auto second = packed->find(';', first + 1);
    if (second != std::string::npos)
        return {};
    try {
        auto x_text = packed->substr(0, first);
        auto y_text = packed->substr(first + 1);
        auto trim = [](std::string text) {
            auto start = text.find_first_not_of(" \t");
            auto end = text.find_last_not_of(" \t");
            if (start == std::string::npos)
                return std::string{};
            return text.substr(start, end - start + 1);
        };
        x_text = trim(std::move(x_text));
        y_text = trim(std::move(y_text));
        std::istringstream xs(x_text), ys(y_text);
        xs.imbue(std::locale::classic());
        ys.imbue(std::locale::classic());
        double x = 0, y = 0;
        if (!(xs >> x) || !(xs >> std::ws).eof() || !(ys >> y) || !(ys >> std::ws).eof())
            return {};
        if (!std::isfinite(x) || !std::isfinite(y))
            return {};
        return {x, y};
    } catch (...) {
        return {};
    }
}

bool DxfDocument::set_hatch_origin(Vector2 origin) {
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y))
        return false;
    std::ostringstream packed;
    packed.imbue(std::locale::classic());
    packed << origin.x << ';' << origin.y;
    return set_drawing_variable("HPORIGIN", packed.str());
}

Transparency DxfDocument::current_entity_transparency() const {
    auto packed = drawing_variable(kCurrentTransparency);
    if (!packed)
        return Transparency::by_layer();
    std::istringstream in(*packed);
    in.imbue(std::locale::classic());
    unsigned long value = 0;
    if (!(in >> value) || !(in >> std::ws).eof())
        return Transparency::by_layer();
    return Transparency::from_alpha_value(static_cast<std::uint32_t>(value));
}

bool DxfDocument::set_current_entity_transparency(Transparency value) {
    if (value.kind() == Transparency::Kind::Explicit && value.alpha() > 230)
        return false;
    return set_drawing_variable(kCurrentTransparency, std::to_string(value.to_dxf_value()));
}
