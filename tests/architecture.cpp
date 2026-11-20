// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include <libopencad/dxf_document.hpp>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static std::string dwg_bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static DwgDocument parse_dwg(const std::string &s) {
    std::istringstream i(s);
    return DwgDocument::read(i);
}
static std::string dxf_bytes(const DxfDocument &d, bool binary) {
    std::ostringstream o;
    d.write(o, binary);
    return o.str();
}
static std::string hex(std::uint64_t v) {
    std::ostringstream s;
    s << std::uppercase << std::hex << v;
    return s.str();
}
static DxfDocument with_arch_entity(bool graphics, bool body) {
    auto d = DxfDocument::create(false);
    d.add_line({0, 0, 0}, {10, 0, 0});
    ClassDefinition cls;
    cls.name = "AEC_WALL";
    cls.cpp_name = "AecDbWall";
    cls.is_entity = true;
    d.set_class(cls);
    std::string owner = "1F";
    d.for_each<DxfBlockRecord>([&](const DxfBlockRecord &block) {
        if (block.is_model_space())
            owner = block.handle();
    });
    auto saved = dxf_bytes(d, false);
    auto entities = saved.find("  2\r\nENTITIES\r\n");
    if (entities == std::string::npos)
        entities = saved.find("2\r\nENTITIES\r\n");
    check(entities != std::string::npos, "Cannot locate ENTITIES");
    auto endsec = saved.find("  0\r\nENDSEC\r\n", entities);
    check(endsec != std::string::npos, "Cannot locate ENTITIES end");
    std::ostringstream entity;
    entity << "  0\r\nAEC_WALL\r\n  5\r\n" << hex(0x1A00) << "\r\n330\r\n" << owner
           << "\r\n100\r\nAcDbEntity\r\n  8\r\n0\r\n";
    if (graphics)
        entity << "160\r\n8\r\n310\r\n0102030405060708\r\n";
    if (body)
        entity << "100\r\nAecDbWall\r\n 40\r\n12.0\r\n";
    saved.insert(endsec, entity.str());
    std::istringstream in(saved);
    return DxfDocument::read(in);
}
int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    const std::string prefix = argv[1];
    Vector3 start{-120, 50, 0}, end{2880, 4050, 0};
    auto wall = DwgDocument::create_wall(start, end, 175, 2800);
    check(wall.objects().arch_entities.size() == 1, "Constructed wall count");
    auto wall_handle = wall.objects().arch_entities.begin()->first;
    auto geometry = wall.architecture_entity(wall_handle)->straight_wall_geometry();
    check(geometry && (geometry->start - start).length() < 1e-6 && (geometry->end - end).length() < 1e-6 &&
              std::abs(geometry->width - 175) < 1e-6 && std::abs(geometry->height - 2800) < 1e-6,
          "Constructed wall geometry");
    check(wall.architecture_entity(wall_handle)->layer_name == "A-Wall", "Wall layer");
    check(wall.entities_anchored_to(wall_handle).empty(), "Standalone wall anchors");
    auto wall_bytes = dwg_bytes(wall);
    auto wall_round = parse_dwg(wall_bytes);
    auto g2 = wall_round.architecture_entity(wall_handle)->straight_wall_geometry();
    check(g2 && (g2->start - start).length() < 1e-6 && std::abs(g2->width - 175) < 1e-6, "Wall DWG roundtrip");
    wall_round.set_wall(wall_handle, {10, 20, 0}, {10, 80, 0}, 8, 96);
    auto g3 = wall_round.architecture_entity(wall_handle)->straight_wall_geometry();
    check(g3 && std::abs(g3->start.x - 10) < 1e-6 && std::abs(g3->end.y - 80) < 1e-6 &&
              std::abs(g3->width - 8) < 1e-6 && std::abs(g3->height - 96) < 1e-6,
          "set_wall");
    std::ofstream(prefix + "-wall.dwg", std::ios::binary) << wall_bytes;
    std::ofstream(prefix + "-wall-edited.dwg", std::ios::binary) << dwg_bytes(wall_round);

    auto door = DwgDocument::create_door(2600, 2100);
    auto door_handle = door.objects().arch_entities.begin()->first;
    auto door_size_v = door_size(*door.architecture_entity(door_handle));
    check(door_size_v && std::abs(door_size_v->width - 2600) < 1e-6 &&
              std::abs(door_size_v->height - 2100) < 1e-6,
          "Constructed door size");
    check(door.architecture_entity(door_handle)->layer_name == "A-Door", "Door layer");
    std::ofstream(prefix + "-door.dwg", std::ios::binary) << dwg_bytes(door);
    door.set_door(door_handle, 1800, 2200);
    auto door_edited = door_size(*door.architecture_entity(door_handle));
    check(door_edited && std::abs(door_edited->width - 1800) < 1e-6 &&
              std::abs(door_edited->height - 2200) < 1e-6,
          "set_door");

    auto window = DwgDocument::create_window(1800, 2400);
    auto window_handle = window.objects().arch_entities.begin()->first;
    auto window_size_v = window_size(*window.architecture_entity(window_handle));
    check(window_size_v && std::abs(window_size_v->width - 1800) < 1e-6 &&
              std::abs(window_size_v->height - 2400) < 1e-6,
          "Constructed window size");
    check(window.architecture_entity(window_handle)->layer_name == "A-Glaz", "Window layer");
    std::ofstream(prefix + "-window.dwg", std::ios::binary) << dwg_bytes(window);
    window.set_window(window_handle, 1200, 1500);
    auto window_edited = window_size(*window.architecture_entity(window_handle));
    check(window_edited && std::abs(window_edited->width - 1200) < 1e-6 &&
              std::abs(window_edited->height - 1500) < 1e-6,
          "set_window");

    auto leftover = dwg_bytes(door);
    auto leftover_error = [&](auto &&fn, ArchEditError expected, const char *msg) {
        bool threw = false;
        try {
            fn();
        } catch (const ArchEditException &e) {
            threw = e.error() == expected;
        }
        check(threw && dwg_bytes(door) == leftover, msg);
    };
    leftover_error([&] { door.set_opening(door_handle, 1800, 2200); }, ArchEditError::UnsupportedLayout,
                   "set_opening kind");
    leftover_error([&] { door.set_stair(door_handle, 1000, 1200, 2800); }, ArchEditError::UnsupportedLayout,
                   "set_stair layout");
    leftover_error([&] { door.set_stair(door_handle, 0, 1200, 2800); }, ArchEditError::InvalidValue,
                   "set_stair zero");
    leftover_error([&] { door.set_stair(door_handle, std::numeric_limits<double>::quiet_NaN(), 1200, 2800); },
                   ArchEditError::InvalidValue, "set_stair NaN");
    leftover_error([&] { door.set_member(door_handle, 1000, 200, 300); }, ArchEditError::UnsupportedLayout,
                   "set_member kind");
    leftover_error([&] { door.set_member(door_handle, 0, 200, 300); }, ArchEditError::UnsupportedLayout,
                   "set_member length");
    leftover_error([&] { door.set_slab(door_handle, {0, 0, 0}, {1, 1, 1}); }, ArchEditError::UnsupportedLayout,
                   "set_slab kind");
    leftover_error([&] { door.set_window_assembly(door_handle, {0, 0, 0}, {1, 1, 1}); },
                   ArchEditError::UnsupportedLayout, "set_window_assembly kind");
    leftover_error(
        [&] {
            door.set_slab(door_handle, {std::numeric_limits<double>::quiet_NaN(), 0, 0}, {1, 1, 1});
        },
        ArchEditError::UnsupportedLayout, "set_slab kind before NaN");
    leftover_error([&] { door.set_railing(door_handle, {0, 0, 0}, {1, 1, 1}); }, ArchEditError::UnsupportedLayout,
                   "set_railing");
    leftover_error([&] { door.set_roof(door_handle, {0, 0, 0}, {1, 1, 1}); }, ArchEditError::UnsupportedLayout,
                   "set_roof");
    leftover_error([&] { door.set_roof_slab(door_handle, {0, 0, 0}, {1, 1, 1}); }, ArchEditError::UnsupportedLayout,
                   "set_roof_slab");
    leftover_error([&] { door.set_space(door_handle, {0, 0, 0}, {1, 1, 1}); }, ArchEditError::UnsupportedLayout,
                   "set_space");
    leftover_error([&] { door.set_curtain_wall(door_handle, {0, 0, 0}, {1, 1, 1}); },
                   ArchEditError::UnsupportedLayout, "set_curtain_wall");
    leftover_error([&] { door.set_column_grid(door_handle, {0, 0, 0}, {1, 1, 1}); },
                   ArchEditError::UnsupportedLayout, "set_column_grid");
    leftover_error([&] { door.set_mass_element(door_handle, {0, 0, 0}, {1, 1, 1}); },
                   ArchEditError::UnsupportedLayout, "set_mass_element");
    leftover_error([&] { door.set_display_theme(door_handle, {0, 0, 0}, {1, 1, 1}); },
                   ArchEditError::UnsupportedLayout, "set_display_theme");
    leftover_error([&] { door.set_ceiling_grid(door_handle, {0, 0, 0}, {1, 1, 1}); },
                   ArchEditError::UnsupportedLayout, "set_ceiling_grid");

    for (double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity()}) {
        bool threw = false;
        try {
            DwgDocument::create_door(bad, 2100);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        check(threw, "Invalid door width");
        threw = false;
        try {
            DwgDocument::create_wall({}, {1, 0, 0}, bad, 3000);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        check(threw, "Invalid wall width");
    }
    bool threw = false;
    try {
        DwgDocument::create_wall({}, {}, 240, 3000);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    check(threw, "Zero-length wall");
    threw = false;
    try {
        DwgDocument::create_wall({}, {1, 0, 1}, 240, 3000);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    check(threw, "Non-planar wall");

    auto omit = with_arch_entity(false, false);
    check(omit.omitted_architecture_records().size() == 1, "Omitted Arch count");
    check(omit.omitted_architecture_records()[0].name == "AEC_WALL", "Omitted Arch name");
    check(omit.omitted_architecture_records()[0].handle == 0x1A00, "Omitted Arch handle");
    for (bool binary : {false, true}) {
        auto saved = dxf_bytes(omit, binary);
        std::istringstream in(saved);
        auto restored = DxfDocument::read(in);
        bool found = false;
        restored.for_each<DxfEntity>([&](const DxfEntity &r) {
            if (r.type() == "AEC_WALL")
                found = true;
        });
        check(!found, "Omitted entity survived readback");
        check(!restored.get_record("1A00"), "Omitted handle survived readback");
        std::ofstream(prefix + (binary ? "-omit-binary.dxf" : "-omit-ascii.dxf"), std::ios::binary)
            << saved;
    }
    auto proxy = with_arch_entity(true, false);
    check(proxy.omitted_architecture_records().empty(), "Graphics-only not omitted");
    auto proxy_text = dxf_bytes(proxy, false);
    check(proxy_text.find("ACAD_PROXY_ENTITY") != std::string::npos, "Proxy entity written");
    {
        std::istringstream in(proxy_text);
        auto restored = DxfDocument::read(in);
        auto *record = restored.get_record("1A00");
        check(record && record->type() == "ACAD_PROXY_ENTITY", "Proxy identity");
    }
    std::ofstream(prefix + "-proxy-ascii.dxf", std::ios::binary) << proxy_text;
    std::ofstream(prefix + "-proxy-binary.dxf", std::ios::binary) << dxf_bytes(proxy, true);

    auto raw = with_arch_entity(false, true);
    check(raw.omitted_architecture_records().empty(), "Raw body not omitted");
    auto raw_text = dxf_bytes(raw, false);
    check(raw_text.find("AEC_WALL") != std::string::npos, "Raw Arch class written");
    {
        std::istringstream in(raw_text);
        auto restored = DxfDocument::read(in);
        auto *record = restored.get_record("1A00");
        check(record && record->type() == "AEC_WALL", "Raw Arch identity");
    }
    std::ofstream(prefix + "-raw-ascii.dxf", std::ios::binary) << raw_text;
    std::ofstream(prefix + "-raw-binary.dxf", std::ios::binary) << dxf_bytes(raw, true);

    auto wall_entity = wall.architecture_entity(wall_handle);
    check(wall_entity.has_value(), "Wall entity");
    auto wall_decoded = wall_entity->decode();
    check(wall_decoded.schema == 569, "Wall schema");
    check(wall_decoded.placement && wall_decoded.placement->rigid, "Wall placement");
    check(!wall_decoded.strings.empty() && !wall_decoded.handles.empty(), "Wall streams");
    bool wall_width_field = false;
    for (auto &field : wall_entity->fields)
        if (field.role == NumberRole::Width && std::abs(field.value - 175) < 1e-6)
            wall_width_field = true;
    check(wall_width_field, "Wall width field");
    bool style_decoded = false;
    for (auto &[h, object] : wall.objects().arch_objects) {
        if (object.dxf_name != "AEC_WALL_STYLE")
            continue;
        auto decoded = object.decode();
        check(decoded.schema == 547, "Wall style schema");
        check(!decoded.placement, "Wall style placement");
        bool unnamed = false;
        for (auto &text : decoded.strings)
            unnamed |= text == "Unnamed";
        check(unnamed, "Wall style Unnamed");
        style_decoded = true;
    }
    check(style_decoded, "Wall style object");
    auto door_decoded = door.architecture_entity(door_handle)->decode();
    check(door_decoded.schema == 558 && door_decoded.placement && door_decoded.placement->rigid, "Door decode");
    auto window_decoded = window.architecture_entity(window_handle)->decode();
    check(window_decoded.schema == 559 && window_decoded.placement && window_decoded.placement->rigid,
          "Window decode");

    auto moved = DwgDocument::create_wall({0, 0, 0}, {8000, 0, 0}, 240, 3000);
    auto moved_handle = moved.objects().arch_entities.begin()->first;
    moved.translate_architecture(moved_handle, {10, 20, 0});
    auto moved_geometry = moved.architecture_entity(moved_handle)->straight_wall_geometry();
    check(moved_geometry && (moved_geometry->start - Vector3{10, 20, 0}).length() < 1e-6 &&
              (moved_geometry->end - Vector3{8010, 20, 0}).length() < 1e-6,
          "translate_architecture");
    std::ofstream(prefix + "-translated.dwg", std::ios::binary) << dwg_bytes(moved);
    auto cascade = DwgDocument::create_wall({240, 0, 0}, {240, 6000, 0}, 240, 3000);
    auto cascade_handle = cascade.objects().arch_entities.begin()->first;
    auto before_profiles = cascade.architecture_entity(cascade_handle)->profiles;
    cascade.translate_architecture(cascade_handle, {240, 0, 0});
    auto after = cascade.architecture_entity(cascade_handle);
    check(before_profiles.size() == after->profiles.size(), "Translate profile count");
    for (std::size_t i = 0; i < before_profiles.size(); ++i)
        for (std::size_t j = 0; j < before_profiles[i].size(); ++j)
            check((after->profiles[i][j] - before_profiles[i][j] - Vector3{240, 0, 0}).length() < 1e-6,
                  "Translate cascade");

    auto compact = DwgDocument::create_wall({}, {1, 0, 0}, 1, 1);
    auto compact_handle = compact.objects().arch_entities.begin()->first;
    compact.set_wall(compact_handle, {}, {2, 0, 0}, 2, 3);
    auto compact_geometry = compact.architecture_entity(compact_handle)->straight_wall_geometry();
    check(compact_geometry && std::abs((compact_geometry->end - compact_geometry->start).length() - 2) < 1e-6 &&
              std::abs(compact_geometry->width - 2) < 1e-6 && std::abs(compact_geometry->height - 3) < 1e-6,
          "Compact 0/1 set_wall");

    auto aligned = DwgDocument::create_wall({0, 0, 0}, {1000, 0, 0}, 240, 3000);
    auto aligned_handle = aligned.objects().arch_entities.begin()->first;
    aligned.set_profile_bounds(aligned_handle, {-120, -120, 0}, {1120, 120, 0});
    auto aligned_geometry = aligned.architecture_entity(aligned_handle)->straight_wall_geometry();
    check(aligned_geometry && (aligned_geometry->start - Vector3{-120, 0, 0}).length() < 1e-6 &&
              (aligned_geometry->end - Vector3{1120, 0, 0}).length() < 1e-6 &&
              std::abs(aligned_geometry->width - 240) < 1e-6,
          "set_profile_bounds");
    aligned.reshape_rectangle(aligned_handle, {0, 0, 0}, {800, 0, 0}, 200);
    auto reshaped = aligned.architecture_entity(aligned_handle)->straight_wall_geometry();
    auto expected_offset = (*rectangle_profile({0, 0, 0}, {800, 0, 0}, 200))[3] * 0.5;
    check(reshaped && (reshaped->start - expected_offset).length() < 1e-6 &&
              (reshaped->end - (Vector3{800, 0, 0} + expected_offset)).length() < 1e-6 &&
              std::abs(reshaped->width - 200) < 1e-6,
          "reshape_rectangle");

    auto door_bytes = dwg_bytes(door);
    threw = false;
    try {
        door.translate_architecture(door_handle, {10, 20, 0});
    } catch (const ArchEditException &e) {
        threw = e.error() == ArchEditError::UnsupportedLayout;
    }
    check(threw && dwg_bytes(door) == door_bytes, "Door translate rollback");
    threw = false;
    try {
        door.set_profile_bounds(door_handle, {0, 0, 0}, {5000, 5000, 0});
    } catch (const ArchEditException &e) {
        threw = e.error() == ArchEditError::UnsupportedLayout;
    }
    check(threw && dwg_bytes(door) == door_bytes, "Door bounds rollback");
    threw = false;
    try {
        parse_dwg(wall_bytes).set_profile_bounds(wall_handle, {0, 0, 0}, {100, 100, 0});
    } catch (const ArchEditException &e) {
        threw = e.error() == ArchEditError::Rejected;
    }
    check(threw, "Diagonal profile bounds");

    auto chain_doc = DwgDocument::create_wall({0, 0, 0}, {1000, 0, 0}, 240, 3000);
    chain_doc.add_wall({1000, 0, 0}, {1000, 2000, 0}, 240, 3000);
    chain_doc.add_wall({1000, 2000, 0}, {4000, 2000, 0}, 240, 3000);
    auto chains = chain_doc.continuous_walls();
    check(chains.size() == 1 && chains[0].handles.size() == 3 && chains[0].points.size() == 4, "continuous_walls");
    std::ofstream(prefix + "-continuous.dwg", std::ios::binary) << dwg_bytes(chain_doc);
    auto original_chain = dwg_bytes(chain_doc);
    auto points = chains[0].points;
    for (auto &point : points)
        point = point + Vector3{240, 0, 0};
    auto missing = chains[0];
    missing.handles.back() = 0xFFFFFFFFFFFFFFFFULL;
    threw = false;
    try {
        chain_doc.set_continuous_wall(missing, points, 240, 3000);
    } catch (const ArchEditException &e) {
        threw = e.error() == ArchEditError::UnsupportedLayout;
    }
    check(threw && dwg_bytes(chain_doc) == original_chain, "Continuous missing handle");
    auto duplicate = chains[0];
    duplicate.handles[1] = duplicate.handles[0];
    threw = false;
    try {
        chain_doc.set_continuous_wall(duplicate, points, 240, 3000);
    } catch (const ArchEditException &e) {
        threw = e.error() == ArchEditError::Rejected;
    }
    check(threw && dwg_bytes(chain_doc) == original_chain, "Continuous duplicate handle");
    threw = false;
    try {
        chain_doc.set_continuous_wall(chains[0], points, std::numeric_limits<double>::quiet_NaN(), 3000);
    } catch (const ArchEditException &e) {
        threw = e.error() == ArchEditError::InvalidValue;
    }
    check(threw && dwg_bytes(chain_doc) == original_chain, "Continuous NaN width");
    chain_doc.set_continuous_wall(chains[0], points, 240, 3000);
    auto edited_chain = chain_doc.continuous_walls();
    check(edited_chain.size() == 1 && (edited_chain[0].points.front() - Vector3{240, 0, 0}).length() < 1e-6,
          "set_continuous_wall");
    std::ofstream(prefix + "-continuous-edited.dwg", std::ios::binary) << dwg_bytes(chain_doc);
    auto gap_doc = DwgDocument::create_wall({0, 0, 0}, {1000, 0, 0}, 240, 3000);
    gap_doc.add_wall({1000.5, 0, 0}, {2000.5, 0, 0}, 240, 3000);
    check(gap_doc.continuous_walls().size() == 2, "continuous gap");
    std::ofstream(prefix + "-continuous-gap.dwg", std::ios::binary) << dwg_bytes(gap_doc);
    check(!continuous_wall_outline({{0, 0, 0}, {1, 2, std::numeric_limits<double>::quiet_NaN()}}, 240),
          "NaN outline");
    check(!continuous_wall_outline({{0, 0, 0}, {1, 2, 3}}, 240), "Non-planar outline");
    auto outline = continuous_wall_outline({{0, 0, 0}, {1000, 0, 0}, {1000, 2000, 0}}, 240);
    check(outline && outline->size() == 7 && (*outline)[0] == outline->back(), "Mitered outline");

    auto nod = DxfDocument::create(false);
    ClassDefinition style;
    style.name = "AEC_WALL_STYLE";
    style.cpp_name = "AecDbWallStyle";
    nod.set_class(style);
    auto line_id = nod.add_line({0, 0, 0}, {10, 0, 0});
    auto nod_text = dxf_bytes(nod, false);
    auto group = nod_text.find("ACAD_GROUP\r\n");
    check(group != std::string::npos, "NOD ACAD_GROUP");
    auto name_end = nod_text.find("\r\n", group);
    auto code_end = nod_text.find("\r\n", name_end + 2);
    auto value_end = nod_text.find("\r\n", code_end + 2);
    nod_text.insert(value_end + 2, "  3\r\nAEC test style\r\n350\r\n1000\r\n");
    auto objects = nod_text.find("  2\r\nOBJECTS\r\n");
    auto endsec = nod_text.find("  0\r\nENDSEC\r\n", objects);
    check(endsec != std::string::npos, "OBJECTS end");
    nod_text.insert(endsec, "  0\r\nAEC_WALL_STYLE\r\n  5\r\n1000\r\n330\r\nC\r\n");
    {
        std::istringstream in(nod_text);
        nod = DxfDocument::read(in);
    }
    check(nod.omitted_architecture_records().size() == 1, "Omitted style count");
    check(nod.omitted_architecture_records()[0].name == "AEC_WALL_STYLE", "Omitted style name");
    auto layer = nod.get_symbol(SymbolTable::Layer, "0");
    check(layer && !layer->handle.empty(), "Layer 0");
    ExtendedDataRecord xdata;
    xdata.application_name = "AECBASE";
    xdata.values.push_back({1070, std::int16_t(10000)});
    xdata.values.push_back({1005, std::uint64_t(0x1000)});
    xdata.values.push_back({1005, std::stoull(layer->handle, nullptr, 16)});
    nod.set_extended_data(hex(line_id), xdata);
    for (bool binary : {false, true}) {
        auto saved = dxf_bytes(nod, binary);
        std::istringstream in(saved);
        auto restored = DxfDocument::read(in);
        check(!restored.get_record("1000"), "Omitted style survived readback");
        if (!binary) {
            check(saved.find("AEC test style") == std::string::npos, "NOD entry omitted");
            check(saved.find("AEC_WALL_STYLE") != std::string::npos, "CLASS kept");
            auto pos = saved.find("1005\r\n");
            check(pos != std::string::npos, "XDATA 1005 present");
            check(saved.find("1005\r\n0\r\n") != std::string::npos ||
                      saved.find("1005\r\n  0\r\n") != std::string::npos,
                  "Omitted XDATA handle rewritten");
            check(saved.find("1005\r\n1000\r\n") == std::string::npos, "Omitted XDATA handle written");
        }
        std::ofstream(prefix + (binary ? "-nod-omit-binary.dxf" : "-nod-omit-ascii.dxf"), std::ios::binary)
            << saved;
        std::ofstream(prefix + (binary ? "-xdata-omit-binary.dxf" : "-xdata-omit-ascii.dxf"), std::ios::binary)
            << saved;
    }

    std::ostringstream r12;
    bool r12_refused = false;
    try {
        omit.write_r12(r12);
    } catch (const R12ExportError &) {
        r12_refused = true;
        check(r12.str().empty(), "R12 wrote bytes on refusal");
    }
    check(r12_refused, "R12 accepted Arch");
    std::cout << "Architecture constructors, geometry, DWG I/O and DXF omit/proxy/R12 verified\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
