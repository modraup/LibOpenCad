// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static std::uint64_t h(const std::string &s) { return s.empty() ? 0 : std::stoull(s, nullptr, 16); }
static std::string hex(std::uint64_t v) {
    std::ostringstream s;
    s << std::hex << std::uppercase << v;
    return s.str();
}
static std::string text(const std::string &v) {
    if (v.empty())
        return "-";
    std::ostringstream s;
    s << std::hex << std::uppercase << std::setfill('0');
    for (unsigned char c : v)
        s << std::setw(2) << unsigned(c);
    return s.str();
}
static std::string value(const BlockEvalValue &value) {
    std::ostringstream s;
    s << std::setprecision(17);
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                s << "N";
            else if constexpr (std::is_same_v<T, double>)
                s << "R" << v;
            else if constexpr (std::is_same_v<T, std::string>)
                s << "S" << text(v);
            else if constexpr (std::is_same_v<T, std::int16_t>)
                s << "J" << v;
            else if constexpr (std::is_same_v<T, std::int32_t>)
                s << "I" << v;
            else if constexpr (std::is_same_v<T, std::uint64_t>)
                s << "H" << hex(v);
            else
                s << "P" << v.x << " " << v.y;
        },
        value);
    return s.str();
}
static std::string bytes(const DxfDocument &d, bool binary) {
    std::ostringstream s;
    d.write(s, binary);
    return s.str();
}
static DxfDocument read(const std::string &s) {
    std::istringstream i(s);
    return DxfDocument::read(i);
}
static std::string trace(const DxfDocument &d) {
    std::ostringstream out;
    std::vector<std::uint64_t> blocks, inserts;
    d.for_each<DxfBlockRecord>([&](const auto &b) { blocks.push_back(h(b.handle())); });
    d.for_each<DxfInsert>([&](const auto &i) { inserts.push_back(h(i.handle())); });
    std::sort(blocks.begin(), blocks.end());
    std::sort(inserts.begin(), inserts.end());
    for (auto i : inserts)
        out << "I\t" << hex(i) << "\t" << hex(d.dynamic_definition_for_insert(i).value_or(0)) << '\n';
    for (auto b : blocks) {
        out << "B\t" << hex(b) << "\t" << d.is_dynamic_block(b) << "\t"
            << hex(d.dynamic_block_definition(b).value_or(0)) << '\n';
        auto queries = inserts;
        queries.insert(queries.begin(), 0);
        for (auto i : queries) {
            out << "Q\t" << hex(b) << "\t" << hex(i) << "\t";
            std::vector<DynamicBlockProperty> props;
            try {
                props = d.dynamic_block_properties(b, i ? std::optional<std::uint64_t>(i) : std::nullopt);
            } catch (const std::invalid_argument &) {
                out << "ERR\n";
                continue;
            }
            out << props.size() << '\n';
            for (const auto &p : props) {
                out << "P\t" << hex(p.parameter_handle) << "\t" << text(p.name) << "\t" << text(p.description)
                    << "\t" << p.type_code << "\t" << p.units << "\t" << p.show << "\t" << p.read_only << "\t"
                    << p.visible << "\t" << value(p.value) << "\t" << p.allowed_values.size();
                for (const auto &v : p.allowed_values)
                    out << "\t" << value(v);
                out << '\n';
            }
        }
    }
    return out.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto d = DxfDocument::read(in);
    auto original = bytes(d, d.is_binary());
    auto t = trace(d);
    auto b = d.get_symbol(SymbolTable::BlockRecord, "Dyn主块");
    check(b.has_value(), "Missing fixture definition");
    auto def = h(b->handle);
    check(d.is_dynamic_block(def), "Definition classification");
    auto graph = d.dynamic_block_graph(def);
    check(graph && graph->nodes.size() == 23 && graph->edges.size() == 1, "Graph structure");
    check(graph->edges[0].outgoing_edges == std::array<std::int32_t, 5>{3, 4, 5, 6, 7}, "Graph edge order");
    auto props = d.dynamic_block_properties(def);
    check(props.size() == 20, "Default properties count");
    check(std::get<double>(props[0].value) == 13., "Definition 3D distance");
    check(props[0].allowed_values.size() == 3 && std::get<double>(props[0].allowed_values[2]) == 13.,
          "Allowed values");
    check(std::all_of(props.begin(), props.end(), [](const auto &p) { return p.visible; }),
          "Default visibility");
    check(d.dynamic_block_properties(0).empty() && !d.dynamic_block_definition(0) &&
              !d.dynamic_definition_for_insert(def),
          "Unknown/non-insert queries");
    auto vis = d.block_visibility_parameter(def);
    check(vis && vis->states.size() == 2 && vis->states[1].visible_params.size() == 2,
          "Visibility membership");
    check(!d.dynamic_visibility_for_insert(0), "Missing insertion visibility");
    int failures = 0, nonuniform = 0, reflected = 0, anonymous = 0;
    d.for_each<DxfInsert>([&](const auto &i) {
        auto id = h(i.handle());
        auto resolved = d.dynamic_definition_for_insert(id);
        check(resolved == def, "Insert definition");
        check(d.dynamic_visibility_for_insert(id)->first == def, "Insert visibility definition");
        try {
            auto p = d.dynamic_block_properties(def, id);
            auto s = i.definition().scale;
            if (s.x == 2 && s.y == 3) {
                check(p.empty(), "Nonuniform scale");
                ++nonuniform;
            }
            if (s.x == -2) {
                ++reflected;
                check(std::get<double>(p[0].value) == 20., "Mirrored uniform length");
                auto area = std::find_if(p.begin(), p.end(), [](const auto &p) { return p.units == 3; });
                check(area != p.end() && std::get<double>(area->value) == 29., "Area scale");
                check(std::count_if(p.begin(), p.end(), [](const auto &p) { return p.visible; }) == 3,
                      "Saved visibility");
            }
            if (i.definition().block_name == "*U1") {
                ++anonymous;
                check(std::get<double>(p[0].value) == 10., "Anonymous insertion cache");
            }
        } catch (const std::invalid_argument &) {
            ++failures;
        }
    });
    check(failures == 1 && nonuniform == 1 && reflected == 1 && anonymous == 1, "Fixture coverage");
    // Querying must not rewrite geometry, XDATA-like parameter groups, caches,
    // common ownership or any unrecognized fields.
    check(original == bytes(d, d.is_binary()), "Read-only API mutated document");
    std::ofstream(std::string(argv[2]) + ".trace") << t;
    for (bool binary : {false, true}) {
        auto saved = bytes(d, binary);
        auto reopened = read(saved);
        check(trace(reopened) == t, "Native roundtrip properties");
        std::ofstream out(std::string(argv[2]) + (binary ? "-binary.dxf" : "-ascii.dxf"), std::ios::binary);
        out << saved;
    }
    // Refuse partial graph nodes instead of silently dropping a parameter.
    auto broken = bytes(d, false);
    auto start = broken.find("\n100\r\nAcDbEvalGraph\r\n");
    check(start != std::string::npos, "Graph scope");
    auto code = broken.find("\n360\r\n", start);
    check(code != std::string::npos, "Graph expression");
    broken.replace(code, 6, "\n361\r\n");
    auto damaged = read(broken);
    bool refused = false;
    try {
        damaged.dynamic_block_properties(def);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "Partial graph accepted");
    std::cout
        << "Dynamic block properties, graph, cache, scale, visibility and both DXF encodings verified\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
