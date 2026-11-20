// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>

#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

using namespace libopencad;

static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}

static std::string property(const std::optional<SymbolEntry> &symbol, int code) {
    if (!symbol)
        return {};
    for (const auto &p : symbol->properties) {
        if (p.code == code)
            return p.value;
    }
    return {};
}

static void write_file(const std::string &path, const DxfDocument &d) {
    std::ofstream out(path, std::ios::binary);
    d.write(out);
}

static void exercise(DxfDocument d, const std::string &prefix, bool write_oracle) {
    check(d.layer_states().empty(), "blank drawing has no layer states");
    check(!d.layer_state("missing"), "missing state");
    check(!d.restore_layer_state("missing"), "restore missing");
    check(!d.rename_layer_state("missing", "other"), "rename missing");
    check(!d.delete_layer_state("missing"), "delete missing");

    d.add_layer("Named color");
    d.set_symbol_properties(SymbolTable::Layer, "Named color",
                            {{420, "795192"}, {430, "Brand colors$Accent"}});
    d.add_layer("Frozen", -5);
    d.set_symbol_properties(SymbolTable::Layer, "Frozen", {{70, "1"}});

    const auto first = d.capture_layer_state("Named state", "book");
    const auto again = d.capture_layer_state("named state", "updated");
    check(first == again, "store updates in place");
    auto named = d.layer_state("NAMED STATE");
    check(named.has_value(), "case-insensitive lookup");
    check(named->name == "named state", "dictionary key takes latest casing");
    check(named->description == "updated", "description updated");
    check(named->mask.bits == LayerStateMask::SUPPORTED, "capture mask");
    check(named->current_layer == "0", "current layer");
    check(named->layers.size() == 3, "layer 0 + two named layers");

    LayerStateLayer *book = nullptr;
    LayerStateLayer *frozen = nullptr;
    for (auto &layer : named->layers) {
        if (layer.layer_name == "Named color")
            book = &layer;
        if (layer.layer_name == "Frozen")
            frozen = &layer;
    }
    check(book && frozen, "captured named layers");
    check(book->color == Color::from_rgb(12, 34, 56), "captured RGB");
    check(book->color_name == std::optional<std::string>{"Accent"}, "captured color name");
    check(book->book_name == std::optional<std::string>{"Brand colors"}, "captured book name");
    check(book->plot_style.find("Color_") == 0, "synthesized plot style");
    check(book->transparency == std::optional<Transparency>{Transparency::explicit_alpha(0)},
          "opaque default");
    check(frozen->off && frozen->frozen, "captured off/frozen");
    check(frozen->color == Color::from_index(5), "captured off-layer ACI");

    if (write_oracle)
        write_file(prefix + "-captured.dxf", d);

    d.set_symbol_properties(SymbolTable::Layer, "Named color",
                            {{62, "7"}, {420, "0"}, {430, "x"}});
    d.set_symbol_properties(SymbolTable::Layer, "Frozen", {{70, "0"}, {62, "5"}});
    check(d.restore_layer_state("named state") == std::optional<std::size_t>{3}, "restore count");
    check(property(d.get_symbol(SymbolTable::Layer, "Named color"), 420) == "795192",
          "restored RGB");
    check(property(d.get_symbol(SymbolTable::Layer, "Named color"), 430) == "Brand colors$Accent",
          "restored book identity");
    check(d.get_layer("Frozen")->signed_color_index() == -5, "restored off");
    check((std::stoi(property(d.get_symbol(SymbolTable::Layer, "Frozen"), 70)) & 1) == 1,
          "restored frozen");
    if (write_oracle)
        write_file(prefix + "-restored.dxf", d);

    check(d.rename_layer_state("named state", "Renamed"), "rename");
    check(d.layer_state("Renamed") && !d.layer_state("named state"), "renamed key");
    check(d.rename_layer_state("Renamed", "Renamed"), "rename to self is allowed");
    d.capture_layer_state("Other", "");
    check(!d.rename_layer_state("Renamed", "other"), "rename onto existing");
    if (write_oracle)
        write_file(prefix + "-renamed.dxf", d);

    check(d.delete_layer_state("Renamed"), "delete");
    check(!d.layer_state("Renamed"), "deleted");
    check(!d.delete_layer_state("Renamed"), "delete missing");
    check(d.delete_layer_state("Other"), "delete remaining");
    check(d.layer_states().empty(), "no states left");
    if (write_oracle)
        write_file(prefix + "-gone.dxf", d);

    const auto unicode = d.capture_layer_state("方案", "说明");
    check(d.layer_state("方案")->description == "说明", "unicode state");
    check(d.rename_layer_state("方案", "备份"), "unicode rename");
    check(d.delete_layer_state("备份") && unicode, "unicode delete");
}

int main(int argc, char **argv) {
    try {
        check(argc == 2, "output prefix");
        exercise(DxfDocument::create(false), argv[1], true);
        exercise(DxfDocument::create(true), argv[1], false);
        std::cout << "layer states capture/restore/rename/delete passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
