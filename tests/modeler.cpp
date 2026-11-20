// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static void save(const DxfDocument &d, const std::string &path, bool binary) {
    std::ofstream o;
    o.exceptions(std::ios::failbit | std::ios::badbit);
    o.open(path, std::ios::binary);
    d.write(o, binary);
}
using Pairs = std::vector<std::pair<int, std::string>>;
static Pairs fields(const DxfDocument &d) {
    std::ostringstream out;
    d.write(out, false);
    std::istringstream in(out.str());
    Pairs v;
    std::string c, s;
    while (std::getline(in, c) && std::getline(in, s)) {
        if (!s.empty() && s.back() == '\r')
            s.pop_back();
        v.emplace_back(std::stoi(c), s);
    }
    return v;
}
static DxfDocument load(const Pairs &p, bool binary) {
    std::ostringstream out;
    for (const auto &[c, s] : p)
        out << c << '\n' << s << '\n';
    std::istringstream in(out.str());
    auto d = DxfDocument::read(in);
    if (binary) {
        std::ostringstream b;
        d.write(b, true);
        std::istringstream bi(b.str());
        return DxfDocument::read(bi);
    }
    return d;
}
static std::pair<std::size_t, std::size_t> blob(const Pairs &p, std::uint64_t h) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].first == 0)
            start = i;
        if (p[i].first == 320 && std::stoull(p[i].second, nullptr, 16) == h) {
            auto end = i + 1;
            while (end < p.size() && p[end].first != 0)
                ++end;
            return {start, end};
        }
    }
    throw std::runtime_error("Missing test blob");
}
static void boundaries() {
    auto base = DxfDocument::create();
    auto geometry = acis::build_box({1, 2, 3}, 4, 6, 8);
    auto h = base.add_modeler_entity(ModelerKind::Solid3D, geometry);
    auto neighbor = base.add_modeler_entity(ModelerKind::Body, geometry);
    auto raw = fields(base);
    for (bool binary : {false, true}) {
        for (int failure = 0; failure < 5; ++failure) {
            auto p = raw;
            auto [start, end] = blob(p, h);
            if (failure == 0) {
                for (auto i = start; i < end; ++i)
                    if (p[i].first == 94)
                        p[i].second = "1";
            }
            if (failure == 1) {
                p.insert(p.begin() + end, {320, "0"});
            }
            if (failure == 2) {
                Pairs dup(p.begin() + start, p.begin() + end);
                p.insert(p.begin() + end, dup.begin(), dup.end());
            }
            if (failure == 3) {
                p.insert(p.begin() + end, {310, "00"});
            }
            if (failure == 4) {
                p.insert(p.begin() + end, {2, "ASM_Data"});
            }
            auto d = load(p, binary);
            auto before = bytes(d);
            bool rejected = false;
            try {
                d.transform_modeler(h, operation(1));
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected && bytes(d) == before, "Malformed payload edit did not roll back");
            check(bool(d.modeler_geometry(neighbor)), "Malformed neighbor blocked unrelated modeler");
            rejected = false;
            try {
                d.copy_entity(h, EntityOptions{});
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected && bytes(d) == before, "Malformed modeler copy did not roll back");
            rejected = false;
            try {
                d.remove_entity(h);
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected && bytes(d) == before, "Malformed modeler delete did not roll back");
        }
        {
            auto p = raw;
            bool schema = false, first = true;
            unsigned seen = 0;
            for (auto &[code, value] : p) {
                if (code == 0) {
                    schema = value == "ACDSSCHEMA";
                    first = true;
                }
                if (schema && code == 90 && first) {
                    first = false;
                    if (seen++ == 1) {
                        value = "0";
                        break;
                    }
                }
            }
            auto d = load(p, binary);
            auto before = bytes(d);
            bool rejected = false;
            try {
                d.add_modeler_entity(ModelerKind::Body, geometry);
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected && bytes(d) == before, "Duplicate schema allocation did not roll back");
        }
        auto p = raw;
        std::size_t start = 0;
        for (std::size_t i = 0; i < p.size(); ++i) {
            if (p[i].first == 0)
                start = i;
            if (p[i].first == 1 && p[i].second == "AcDb3DSolid_ASM_Data")
                for (auto j = start; j < i; ++j)
                    if (p[j].first == 90)
                        p[j].second = "42";
            if (p[start].second == "ACDSRECORD" && p[i].first == 90)
                p[i].second = "42";
        }
        auto d = load(p, binary);
        auto fresh = d.add_modeler_entity(ModelerKind::Region, geometry);
        auto output = fields(d);
        auto [begin, end] = blob(output, fresh);
        check(output[begin + 1].first == 90 && std::stoi(output[begin + 1].second) == 42,
              "Custom ASM schema index ignored");
        // Other properties on an ASM record are not a replacement target.
        p = raw;
        auto range = blob(p, h);
        p.insert(p.begin() + range.second, {{2, "Retained_Metadata"}, {280, "1"}, {1, "opaque A^"}});
        d = load(p, binary);
        d.transform_modeler(h, operation(1));
        auto saved = fields(d);
        check(std::find(saved.begin(), saved.end(), std::make_pair(1, std::string("opaque A^ "))) !=
                      saved.end() ||
                  std::find(saved.begin(), saved.end(), std::make_pair(1, std::string("opaque A^"))) !=
                      saved.end(),
              "Other ACDS property lost");
        // The attached SAB graph can carry unknown tagged records; transforms preserve them.
        auto g = *d.modeler_geometry(h);
        acis::SatRecord record;
        record.entity_type = "custom-attrib";
        record.index = static_cast<std::int32_t>(g.records.size());
        record.tokens = {acis::SatToken(acis::SatString{"中文 A^ metadata"}),
                         acis::SatToken(std::int64_t(123))};
        g.records.push_back(record);
        d.set_modeler_geometry(h, g);
        d.transform_modeler(h, operation(1));
        auto restored = d.modeler_geometry(h);
        check(std::any_of(restored->records.begin(), restored->records.end(),
                          [](const auto &r) { return r.entity_type == "custom-attrib"; }),
              "Unknown SAB record lost");
    }
    // Version guards must fail before allocation or insertion.
    for (auto version : {"AC1009", "AC1018"}) {
        auto p = fields(DxfDocument::create());
        for (auto &[c, s] : p)
            if (c == 1 && s == "AC1032")
                s = version;
        auto d = load(p, false);
        auto before = bytes(d);
        bool rejected = false;
        try {
            d.add_modeler_entity(
                version == std::string("AC1009") ? ModelerKind::Solid3D : ModelerKind::Surface, geometry);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && bytes(d) == before, "Modeler version guard was not atomic");
    }
}
int main(int argc, char **argv) try {
    if (argc == 1) {
        boundaries();
        return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--binary-fixture") {
        std::ifstream in(argv[2]);
        auto d = DxfDocument::read(in);
        save(d, argv[3], true);
        return 0;
    }
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto source = DxfDocument::read(in);
    auto original = bytes(source);
    std::string prefix = argv[2];
    std::vector<std::uint64_t> targets, controls;
    source.for_each<DxfModelerEntity>([&](const auto &e) {
        auto h = std::stoull(e.handle(), nullptr, 16);
        (e.layer() == "Control" ? controls : targets).push_back(h);
        auto g = source.modeler_geometry(h);
        check(bool(g), "Missing geometry");
        std::ofstream o(prefix + ".before-" + e.handle() + ".sat");
        o << g->to_sat_string();
    });
    check(targets.size() == (source.version() >= DxfVersion::AC1021 ? 5 : 3) &&
              targets.size() == controls.size(),
          "Fixture coverage");
    for (int step = 1; step < 14; ++step) {
        if (step == 11)
            continue;
        auto d = source;
        for (auto h : targets)
            d.transform_modeler(h, operation(step));
        if (step == 10)
            d.for_each<DxfModelerEntity>([](auto &e) {
                if (e.layer() != "Control" &&
                    (e.kind() == ModelerKind::Surface || e.kind() == ModelerKind::PlaneSurface))
                    e.set_isolines(-3, 32767);
            });
        for (auto h : controls)
            check(d.modeler_geometry(h)->to_sat_string() == source.modeler_geometry(h)->to_sat_string(),
                  "Control changed");
        for (bool b : {false, true})
            save(d, prefix + "." + std::to_string(step) + (b ? ".binary.dxf" : ".ascii.dxf"), b);
    }
    auto d = source;
    auto before = bytes(d);
    bool rejected = false;
    try {
        d.transform_modeler(targets[0], operation(11));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected && bytes(d) == before, "Nonfinite edit must be atomic");
    check(!d.modeler_geometry(UINT64_MAX), "Unknown geometry");
    rejected = false;
    try {
        d.set_modeler_geometry(UINT64_MAX, *source.modeler_geometry(targets[0]));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected && bytes(d) == before, "Unknown handle rollback");
    std::ofstream manifest(prefix + ".added");
    EntityOptions options;
    options.space = EntitySpace::Paper;
    for (int k = 0; k < (source.version() >= DxfVersion::AC1021 ? 5 : 3); ++k) {
        auto h =
            d.add_modeler_entity(static_cast<ModelerKind>(k), acis::build_box({1, 2, 3}, 4, 6, 8), options);
        manifest << std::hex << h << ' ' << k << '\n';
        check(bool(d.modeler_geometry(h)), "Added geometry missing");
    }
    manifest.close();
    for (bool b : {false, true})
        save(d, prefix + (b ? ".added.binary.dxf" : ".added.ascii.dxf"), b);
    for (bool binary : {false, true}) {
        auto empty = DxfDocument::create(binary);
        auto h = empty.add_modeler_entity(ModelerKind::Solid3D, acis::build_sphere({1, 2, 3}, 4));
        check(bool(empty.modeler_geometry(h)), "Initial ACDSDATA missing");
        save(empty, prefix + (binary ? ".new.binary.dxf" : ".new.ascii.dxf"), binary);
    }
    auto tagged = source;
    auto graph = *tagged.modeler_geometry(targets[0]);
    acis::SatRecord extra;
    extra.entity_type = "custom-attrib";
    std::string label;
    for (int i = 0; i < 700; ++i)
        label += "中文 A^ # ";
    extra.tokens = {acis::SatToken(acis::SatString{label}), acis::SatToken(std::int64_t(123))};
    graph.add_record(extra);
    tagged.set_modeler_geometry(targets[0], graph);
    tagged.transform_modeler(targets[0], operation(1));
    for (bool b : {false, true})
        save(tagged, prefix + (b ? ".tagged.binary.dxf" : ".tagged.ascii.dxf"), b);
    auto copied = source;
    std::ofstream copies(prefix + ".copies");
    std::vector<std::uint64_t> new_handles;
    for (auto h : targets) {
        auto fresh = copied.copy_entity(h, options);
        new_handles.push_back(fresh);
        copies << std::hex << h << ' ' << fresh << '\n';
        copied.transform_modeler(fresh, operation(1));
        check(copied.modeler_geometry(h)->to_sat_string() == source.modeler_geometry(h)->to_sat_string(),
              "Copy edit modified source");
    }
    copies.close();
    for (bool b : {false, true})
        save(copied, prefix + (b ? ".copies.binary.dxf" : ".copies.ascii.dxf"), b);
    for (auto h : targets)
        check(copied.remove_entity(h), "Source modeler removal failed");
    for (bool b : {false, true})
        save(copied, prefix + (b ? ".removed.binary.dxf" : ".removed.ascii.dxf"), b);
    for (auto h : new_handles)
        check(copied.remove_entity(h), "Copied modeler removal failed");
    for (bool b : {false, true})
        save(copied, prefix + (b ? ".cleaned.binary.dxf" : ".cleaned.ascii.dxf"), b);
    for (const auto &[code, value] : fields(copied))
        if (code == 320) {
            auto h = std::stoull(value, nullptr, 16);
            check(std::find(controls.begin(), controls.end(), h) != controls.end(),
                  "Orphaned ACDS payload after deletion");
        }
    check(bytes(source) == original, "Source changed");
    std::cout << "Modeler read/transform/create matrix passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
