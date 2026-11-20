// SPDX-License-Identifier: MPL-2.0
// acadrust VisualStyle::{core_properties,legacy_properties} and
// write_visualstyle.
void visual_style(DxfDrawingObject &style) {
    auto layout = [](V version) { return version < V::AC1024 ? 0 : version < V::AC1027 ? 1 : 2; };
    const int old = layout(report_.source), next = layout(target());
    if (old == next)
        return;
    const int core[] = {71, 72, 73, 90, 40, 41, 63, 74, 91,  64,  65,  75, 175, 42,
                        92, 66, 43, 76, 77, 78, 67, 79, 170, 171, 290, 93, 44,  173};
    const int legacy[] = {40, 41, 63, 64,  65,  75,  42,  92,  66, 43, 76,  77,
                          78, 67, 79, 170, 171, 290, 174, 175, 93, 44, 173, 45};
    using Fields = std::vector<DxfRawField>;
    const auto scope = "AcDbVisualStyle";
    const auto input = style.subclass_fields(scope);
    // Defaults supply the thirty extended properties absent from the older
    // layouts.
    auto prototype = style;
    prototype.set_definition(VisualStyleDefinition::defaults());
    auto groups = [&](const Fields &fields, std::size_t first) {
        std::vector<Fields> slots;
        Fields slot;
        for (std::size_t i = first; i < fields.size(); ++i) {
            auto f = fields[i];
            if (slot.empty() && !code_in(f.code, {90, 40, 62, 290, 1}))
                throw std::invalid_argument("Invalid visual style property");
            if (!slot.empty() && f.code != 176 &&
                !(f.code == 420 && slot.size() == 1 && slot.front().code == 62))
                throw std::invalid_argument("Ambiguous visual style property");
            slot.push_back(f);
            if (f.code == 176) {
                slots.push_back(std::move(slot));
                slot.clear();
            }
        }
        if (!slot.empty())
            throw std::invalid_argument("Unfinished visual style property");
        return slots;
    };
    auto slots = groups(prototype.subclass_fields(scope), 5);
    Fields output;
    std::string description, kind = "0", lighting = "2", internal = "0";
    if (old == 2) {
        if (input.size() < 5 || input[0].code != 2 || input[1].code != 70 || input[2].code != 177 ||
            input[3].code != 291 || input[4].code != 70)
            throw std::invalid_argument("Invalid visual style header");
        description = input[0].value;
        kind = input[1].value;
        lighting = input[2].value;
        internal = input[3].value;
        auto source = groups(input, 5);
        if (source.size() != std::stoul(input[4].value) || source.size() > slots.size())
            throw std::invalid_argument("Unsupported visual style property count");
        for (std::size_t i = 0; i < source.size(); ++i) {
            if (source[i].front().code != slots[i].front().code)
                throw std::invalid_argument("Unexpected visual style property type");
            slots[i] = source[i];
        }
    } else {
        std::set<int> seen;
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto &f = input[i];
            if (!seen.insert(f.code).second)
                throw std::invalid_argument("Duplicate visual style property");
            if (f.code == 2) {
                description = f.value;
                continue;
            }
            if (f.code == 70) {
                kind = f.value;
                continue;
            }
            if (f.code == 177) {
                lighting = f.value;
                continue;
            }
            if (f.code == 291) {
                internal = f.value;
                continue;
            }
            if (!old && (f.code == 174 || f.code == 45)) {
                if (std::stod(f.value) != 0)
                    issue(Kind::Loss, "visual-style-legacy-property", &style, f.code,
                          "Property has no modern slot");
                continue;
            }
            const auto found = std::find(std::begin(core), std::end(core), f.code);
            if (found == std::end(core))
                throw std::invalid_argument("Unknown visual style property");
            auto &slot = slots[found - std::begin(core)];
            slot.front().value = f.value;
            if (!old && (f.code == 40 || f.code == 41) && slot.front().value.front() == '-')
                slot.front().value.erase(0, 1);
            if (slot.front().code == 62 && i + 1 < input.size() && input[i + 1].code == 420)
                slot.insert(slot.begin() + 1, input[++i]);
            if (old == 1 && i + 1 < input.size() && input[i + 1].code == 176)
                slot.back().value = input[++i].value;
        }
    }
    output = {{2, description}, {70, kind}};
    if (next)
        output.insert(output.end(), {{177, lighting}, {291, internal}});
    if (next == 2) {
        output.push_back({70, std::to_string(slots.size())});
        for (auto &fields : slots)
            output.insert(output.end(), fields.begin(), fields.end());
    } else {
        auto write_slot = [&](int code) {
            auto pos = std::find(std::begin(core), std::end(core), code);
            if (pos == std::end(core)) {
                output.push_back({code, "0"});
                return;
            }
            auto slot = slots[pos - std::begin(core)];
            slot.front().code = code;
            if (!next) {
                if (code == 40 || code == 41) {
                    const auto modifier = std::stoll(slots[3].front().value);
                    if (slot.front().value.front() == '-')
                        slot.front().value.erase(0, 1);
                    if (!(modifier & (code == 40 ? 1 : 2)))
                        slot.front().value.insert(0, "-");
                }
                if (slot.back().value != "1")
                    issue(Kind::Loss, "visual-style-enabled-removed", &style, 176,
                          "Pre-R2010 styles omit property enable switches");
                slot.pop_back();
            }
            output.insert(output.end(), slot.begin(), slot.end());
        };
        if (next == 1)
            for (int code : core)
                write_slot(code);
        else {
            for (int code : {71, 72, 73, 90, 74, 91})
                write_slot(code);
            for (int code : legacy)
                write_slot(code);
            output.push_back({291, internal});
            if (lighting != "2")
                issue(Kind::Loss, "visual-style-lighting-removed", &style, 177,
                      "Pre-R2010 styles omit the extended lighting model");
        }
        if (old == 2)
            issue(Kind::Loss, "visual-style-extended-removed", &style, 70,
                  "Target layout carries only the core visual properties");
    }
    style.set_subclass_fields(scope, output);
    changed_ = true;
}
