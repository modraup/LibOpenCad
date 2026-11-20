// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <libopencad/dxf_document.hpp>
#include <libopencad/preview.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

using namespace libopencad;

static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}

static std::string hex(std::uint64_t n) {
    std::ostringstream out;
    out << std::hex << std::uppercase << n;
    return out.str();
}

static const DxfLine *line_by_handle(const DxfDocument &doc, std::uint64_t handle) {
    const DxfLine *found = nullptr;
    auto key = hex(handle);
    doc.for_each<DxfLine>([&](const DxfLine &line) {
        if (line.handle() == key)
            found = &line;
    });
    return found;
}

static const DxfText *text_by_handle(const DxfDocument &doc, std::uint64_t handle) {
    const DxfText *found = nullptr;
    auto key = hex(handle);
    doc.for_each<DxfText>([&](const DxfText &text) {
        if (text.handle() == key)
            found = &text;
    });
    return found;
}

int main(int argc, char **argv) {
    try {
        check(argc == 2, "output prefix");
        const std::string prefix = argv[1];

        {
            auto empty = empty_dwg_preview();
            check(!parse_dwg_preview(empty, 0x1c0), "empty preview is None");
            check(dwg_preview_overall_size(empty) == std::optional<std::size_t>{1}, "empty overall");
            Preview png{PreviewFormat::Png, {0x89, 'P', 'N', 'G', 1, 2, 3, 4, 5}};
            auto bytes = build_dwg_preview(&png, 0x300);
            check(static_cast<unsigned char>(bytes[20]) == 1, "png descriptor count");
            check(parse_dwg_preview(bytes, 0x300) == png, "png roundtrip");
            Preview bmp{PreviewFormat::Bmp, std::vector<std::uint8_t>(1200, 0x28)};
            auto dib = build_dwg_preview(&bmp, 0x1c0);
            check(static_cast<unsigned char>(dib[20]) == 2, "bmp header+image");
            auto overall = dwg_preview_overall_size(dib);
            check(overall && dwg_preview_container_len(*overall) == dib.size(), "bmp container len");
            check(parse_dwg_preview(dib, 0x1c0) == bmp, "bmp roundtrip");
            Preview small{PreviewFormat::Bmp, std::vector<std::uint8_t>(100, 7)};
            auto abs = build_dwg_preview(&small, 0x1c0);
            auto start = static_cast<unsigned char>(abs[31]) |
                         (static_cast<unsigned>(static_cast<unsigned char>(abs[32])) << 8) |
                         (static_cast<unsigned>(static_cast<unsigned char>(abs[33])) << 16) |
                         (static_cast<unsigned>(static_cast<unsigned char>(abs[34])) << 24);
            check(start == 0x1c0 + 16 + 4 + 1 + 18 + 80, "absolute BMP start");
        }

        auto doc = DxfDocument::create();
        auto layer = doc.add_layer("TEST");
        EntityOptions on_test;
        on_test.layer = "test";
        auto line = doc.add_line({0, 0, 0}, {1, 0, 0}, on_test);
        check(doc.rename_layer("test", "Renamed") == layer, "rename_layer handle");
        check(doc.get_layer("Renamed") && !doc.get_layer("TEST"), "renamed layer table");
        auto *renamed_line = line_by_handle(doc, line);
        check(renamed_line && renamed_line->layer() == "Renamed", "entity layer");

        auto unicode = DxfDocument::create();
        unicode.add_layer("é");
        unicode.rename_layer("é", "É");
        check(unicode.get_layer("é") && unicode.get_layer("é")->name() == "É", "unicode case");

        auto rejected = DxfDocument::create();
        rejected.add_layer("First");
        rejected.add_layer("Second");
        EntityOptions on_first;
        on_first.layer = "First";
        auto kept = rejected.add_line({0, 0, 0}, {1, 0, 0}, on_first);
        auto before_first = rejected.get_layer("First")->name();
        try {
            rejected.rename_layer("First", "Second");
            check(false, "duplicate rename");
        } catch (const std::invalid_argument &) {
        }
        try {
            rejected.rename_layer("0", "Zero");
            check(false, "layer 0 rename");
        } catch (const std::invalid_argument &) {
        }
        check(rejected.get_layer("First") && rejected.get_layer("Second"), "reject leaves layers");
        auto *kept_line = line_by_handle(rejected, kept);
        check(kept_line && kept_line->layer() == before_first, "reject leaves entity");

        auto stated = DxfDocument::create();
        stated.add_layer("First");
        stated.capture_layer_state("Saved", "");
        EntityOptions xdata_line;
        xdata_line.layer = "First";
        auto tagged = stated.add_line({0, 0, 0}, {2, 0, 0}, xdata_line);
        stated.set_extended_data(hex(tagged), {"APP", {{1003, std::string("fIrSt")}}});
        stated.rename_layer("First", "Renamed");
        auto saved = stated.layer_state("Saved");
        check(saved.has_value(), "layer state present");
        bool renamed_in_state = false;
        for (const auto &entry : saved->layers)
            renamed_in_state |= entry.layer_name == "Renamed";
        check(renamed_in_state, "layer state layer name");
        auto xdata = stated.get_record(hex(tagged))->extended_data();
        check(!xdata.empty() && xdata.front().values.size() == 1, "xdata present");
        check(std::get<std::string>(xdata.front().values.front().value) == "Renamed", "xdata 1003");

        {
            std::ofstream out(prefix + "-rename.dxf", std::ios::binary);
            stated.write(out);
        }

        auto reserved = DxfDocument::create();
        auto seed = reserved.next_handle();
        auto zero = reserved.try_reserve_handles(0);
        check(zero.empty() && !zero.get(0) && reserved.next_handle() == seed, "zero reservation");
        auto range = reserved.try_reserve_handles(3);
        check(range.len() == 3 && range.get(0) == seed && range.get(2) == seed + 2 && !range.get(3),
              "reserve 3");
        check(reserved.next_handle() == seed + 3, "next_handle advanced");
        auto after = reserved.try_reserve_handles(1);
        check(after.get(0) == seed + 3, "next reservation");
        {
            std::ofstream out(prefix + "-reserve.dxf", std::ios::binary);
            reserved.write(out);
        }
        {
            std::ofstream meta(prefix + "-reserve.txt");
            meta << reserved.next_handle() << '\n';
        }

        auto mirrored = DxfDocument::create();
        auto src = mirrored.add_line({2, 3, 7}, {5, 6, 8});
        check(mirrored.mirror3d_entity(src, MirrorPlane::xy({0, 0, 2})), "mirror3d XY");
        auto *geom = line_by_handle(mirrored, src);
        check(geom && std::abs(geom->start().x - 2) < 1e-12 && std::abs(geom->start().y - 3) < 1e-12 &&
                  std::abs(geom->start().z + 3) < 1e-12,
              "mirror3d height");
        check(!mirrored.mirror_entity(src, Vector3::ZERO, Vector3::UNIT_Z), "vertical line is identity");
        check(!mirrored.mirror3d_entity(src, MirrorPlane::point_normal(Vector3::ZERO, Vector3::ZERO)),
              "zero normal");
        check(!mirrored.mirror3d_entity(
                  src, MirrorPlane::three_point(Vector3::ZERO, Vector3::UNIT_X, Vector3::UNIT_X * 2)),
              "colinear three-point");
        check(!mirrored.mirror_entity(0, Vector3::ZERO, Vector3::UNIT_Y), "null handle");
        auto label = mirrored.add_text("Label", {5, 2, 3}, 1);
        check(mirrored.mirror_entity_with_options(label, Vector3::ZERO, Vector3::UNIT_Y,
                                                  MirrorOptions{false, false, 9.0}),
              "explicit text options");
        auto *text = text_by_handle(mirrored, label);
        check(text && std::abs(text->insertion_point().x + 14) < 1e-12, "exact width insertion");
        check((text->definition().generation_flags & 2) == 0, "readable text");
        {
            std::ofstream out(prefix + "-mirror.dxf", std::ios::binary);
            mirrored.write(out);
        }

        auto live = DxfDocument::create();
        auto live_line = live.add_line({4, 5, 6}, {7, 8, 9});
        auto snap = live.snapshot_structure();
        check(!snap.get_record(hex(live_line)), "snapshot strips entities");
        check(live.get_record(hex(live_line)), "original keeps entities");
        live.add_layer("Extra");
        auto displaced = live.swap_structure(std::move(snap));
        check(live.get_record(hex(live_line)), "swap keeps live entities");
        check(!live.get_layer("Extra"), "swap restores snapshot structure");
        check(displaced.get_layer("Extra") && !displaced.get_record(hex(live_line)),
              "displaced structure has Extra");

        auto graph_doc = DxfDocument::create();
        auto src_block = graph_doc.add_symbol(SymbolTable::BlockRecord, "GraphSrc");
        auto xdict = graph_doc.add_extension_dictionary(hex(src_block));
        graph_doc.add_xrecord(hex(xdict), "DATA", {{70, "1"}, {1, std::string("payload")}});
        auto graph = graph_doc.export_block_graph(src_block);
        check(graph && graph->record == src_block && graph->root && !graph->objects.empty(),
              "export graph");
        auto dst_block = graph_doc.add_symbol(SymbolTable::BlockRecord, "GraphDst");
        graph_doc.import_block_graph(*graph, dst_block, {});
        check(!graph_doc.extension_dictionary_handle(hex(dst_block)).empty(), "imported xdict");
        auto imported_root = graph_doc.extension_dictionary_handle(hex(dst_block));
        check(!graph_doc.dictionary_entry(imported_root, "DATA").empty(), "imported xrecord entry");
        {
            std::ofstream out(prefix + "-block-graph.dxf", std::ios::binary);
            graph_doc.write(out);
        }

        {
            std::istringstream in(create_dwg(DxfVersion::AC1032, {}));
            auto dwg = DwgDocument::read(in);
            Preview png{PreviewFormat::Png, {0x89, 'P', 'N', 'G', 9, 8, 7}};
            dwg.set_preview(png);
            check(dwg.preview() == png, "DWG preview getter");
            std::ostringstream out;
            dwg.write(out);
            std::istringstream again_in(out.str());
            auto again = DwgDocument::read(again_in);
            check(again.preview() == png, "DWG preview persist");
            std::ofstream file(prefix + "-preview.dwg", std::ios::binary);
            again.write(file);
        }

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
