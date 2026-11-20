// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

using namespace libopencad;

static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}

int main(int argc, char **argv) {
    try {
        check(argc == 2, "output prefix");
        std::string prefix = argv[1];

        auto doc = DxfDocument::create();
        check(!doc.drawing_variable("TEST"), "missing variable");
        check(doc.hatch_origin() == Vector2{}, "default hatch origin");
        check(doc.current_entity_transparency() == Transparency::by_layer(), "default CETRANSPARENCY");

        check(doc.set_drawing_variable("TEST", "first"), "set TEST");
        check(doc.drawing_variable("TEST") == std::optional<std::string>{"first"}, "get TEST");
        check(doc.set_drawing_variable("test", "second"), "update TEST case-insensitive");
        check(doc.drawing_variable("TEST") == std::optional<std::string>{"second"}, "updated TEST");
        check(!doc.set_drawing_variable("", "x"), "empty name");

        check(doc.set_hatch_origin({12.5, -8.25}), "set hatch origin");
        auto origin = doc.hatch_origin();
        check(std::abs(origin.x - 12.5) < 1e-12 && std::abs(origin.y + 8.25) < 1e-12, "hatch origin");
        check(doc.set_drawing_variable("HPORIGIN", "1;not-a-number"), "malformed HPORIGIN");
        check(doc.hatch_origin() == Vector2{}, "malformed origin is zero");
        check(!doc.set_hatch_origin({std::numeric_limits<double>::quiet_NaN(), 2.0}), "reject NaN origin");
        check(doc.drawing_variable("HPORIGIN") == std::optional<std::string>{"1;not-a-number"},
              "NaN set leaves malformed");
        check(doc.set_hatch_origin({12.5, -8.25}), "restore hatch origin");

        check(doc.set_current_entity_transparency(Transparency::explicit_alpha(77)), "T_30");
        check(doc.current_entity_transparency() == Transparency::explicit_alpha(77), "read T_30");
        check(!doc.set_current_entity_transparency(Transparency::explicit_alpha(231)), "reject alpha>230");
        check(doc.current_entity_transparency() == Transparency::explicit_alpha(77), "reject leaves T_30");

        auto conflict = DxfDocument::create();
        auto variables = conflict.dictionary_entry(conflict.root_dictionary_handle(), "AcDbVariableDictionary");
        check(!variables.empty(), "default AcDbVariableDictionary");
        conflict.add_auxiliary_object(variables, "TEST", ScaleDefinition{});
        check(!conflict.set_drawing_variable("TEST", "value"), "conflict type");
        check(!conflict.drawing_variable("TEST"), "conflict lookup");

        std::ofstream out(prefix + "-drawing-vars.dxf", std::ios::binary);
        doc.write(out);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
