// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
MultiLeaderDefinition definition(std::uint64_t block, std::uint64_t style, std::uint64_t text,
                                 bool with_block) {
    MultiLeaderDefinition v;
    v.style_handle = style;
    v.text_style_handle = text;
    v.path_type = 2;
    v.line_color = Color::from_rgb(0x12, 0x34, 0x56);
    v.text_color = Color::from_index(4);
    v.enable_landing = false;
    v.dogleg_length = 2;
    v.arrowhead_size = 1.5;
    v.text_frame = true;
    v.block_scale = {2, 3, 4};
    v.block_rotation = .7;
    v.scale_factor = 2;
    v.extend_leader_to_text = true;
    auto &c = v.context;
    if (with_block) {
        v.content_type = 1;
        v.block_content_handle = block;
        c.has_block_contents = true;
        c.block_content_handle = block;
        c.block_content_location = {7, 8, 9};
        c.content_base_point = {7, 8, 9};
    } else {
        c.has_text_contents = true;
        c.text_string = "中文\\P多重引线";
        c.text_location = {1, 2, 3};
        c.content_base_point = {1, 2, 3};
    }
    c.text_style_handle = text;
    c.scale_factor = 3;
    c.text_height = 4;
    c.text_width = 30;
    c.text_boundary_height = 20;
    c.text_rotation = .25;
    c.text_color = Color::from_rgb(0xab, 0xcd, 0xef);
    c.background_fill_color = Color::by_layer();
    c.background_fill_enabled = true;
    c.background_transparency = 25;
    c.column_type = 2;
    c.column_width = 10;
    c.column_gutter = 2;
    c.column_sizes = {10, 20};
    c.column_flow_reversed = true;
    c.block_content_scale = {2, 3, 4};
    c.block_rotation = .5;
    c.block_content_color = Color::from_index(2);
    c.transform_matrix[12] = 7;
    c.transform_matrix[13] = 8;
    c.transform_matrix[14] = 9;
    for (int i = 0; i < 2; ++i) {
        MultiLeaderRoot r;
        r.leader_index = i;
        r.connection_point = {10. + i, 20, 30};
        r.direction = {0, 1, 0};
        r.landing_distance = 3;
        r.break_points = {{{1, 2, 3}, {4, 5, 6}}};
        for (int j = 0; j < 2; ++j) {
            MultiLeaderLine l;
            l.index = j;
            l.points = {{double(i), double(j), 1}, {5, 6, 7}, {8, 9, 10}};
            l.line_color = Color::from_index(3);
            l.line_weight = 35;
            l.arrowhead_size = 2;
            l.breaks.push_back({0, {{{2, 3, 4}, {3, 4, 5}}}});
            l.breaks.push_back({1, {{{6, 7, 8}, {7, 8, 9}}}});
            r.lines.push_back(l);
        }
        c.leader_roots.push_back(r);
    }
    v.block_attributes = {{0, 7, 2.5, "中文属性"}};
    v.arrowhead_overrides = {{0, 0}, {1, block}};
    return v;
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "multileader rollback");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        auto block = made.add_symbol(SymbolTable::BlockRecord, "标签块");
        MultiLeaderStyleDefinition sd;
        sd.description = "样式说明";
        sd.default_text = "默认引线";
        sd.text_height = 5;
        sd.first_segment_angle = .5;
        sd.second_segment_angle = 1.25;
        sd.line_color = Color::from_rgb(0x33, 0xaa, 0x77);
        sd.text_color = Color::from_index(6);
        sd.block_content_color = Color::none();
        sd.enable_block_scale = true;
        sd.block_content_scale_x = 2;
        sd.block_content_scale_y = 3;
        sd.block_content_scale_z = 4;
        sd.enable_block_rotation = true;
        sd.block_content_rotation = .75;
        sd.align_space = 1.5;
        sd.break_gap_size = .5;
        sd.max_leader_points = 5;
        sd.unknown_flag_298 = true;
        sd.text_always_left = true;
        auto style = made.add_multileader_style("中文引线样式", sd);
        check(made.get_multileader_style("STANDARD") && made.get_multileader_style("中文引线样式") &&
                  !made.get_multileader_style("missing"),
              "dictionary style lookup");
        auto text = std::stoull(made.get_symbol(SymbolTable::TextStyle, "Standard")->handle, nullptr, 16);
        for (auto space : {EntitySpace::Model, EntitySpace::Paper})
            for (bool b : {false, true}) {
                EntityOptions o;
                o.space = space;
                made.add_multileader(definition(block, style, text, b), o);
            }
        write(made, prefix + "-created.dxf");
        auto anchored = DxfDocument::create(binary);
        for (int attachment = 1; attachment <= 9; ++attachment) {
            MultiLeaderDefinition v;
            v.text_attachment_point = attachment;
            v.context.has_text_contents = true;
            v.context.text_attachment_point = 10 - attachment;
            anchored.add_multileader(v);
        }
        auto roundtrip = read(save(anchored));
        std::set<int> attachments;
        roundtrip.for_each<DxfMultiLeader>([&](const auto &leader) {
            const auto result = leader.definition();
            check(result.text_attachment_point + result.context.text_attachment_point == 10,
                  "multileader MTEXT attachment roundtrip");
            attachments.insert(result.text_attachment_point);
        });
        check(attachments == std::set<int>{1, 2, 3, 4, 5, 6, 7, 8, 9}, "all nine attachment points");
        write(anchored, prefix + "-attachment-points.dxf");
        fails(made, [&] {
            auto v = definition(block, style, text, false);
            v.context.text_attachment_point = 10;
            made.add_multileader(v);
        });
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        auto sources = d.model_space_entity_handles();
        check(sources.size() == 2, "model multileaders");
        for (auto h : sources) {
            auto dict = d.add_extension_dictionary(h);
            d.add_xrecord(hex(dict), "Data", {{340, h}});
        }
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        d.for_each<DxfMultiLeader>([&](auto &e) {
            auto v = e.definition();
            check(v.block_attributes.size() == 1 && v.block_attributes[0].text == "中文属性" &&
                      v.block_attributes[0].index == 7 && v.block_attributes[0].width == 2.5,
                  "multileader block attributes");
            check(v.context.leader_roots.size() == 2 && v.context.leader_roots[0].lines.size() == 2,
                  "nested root/line scopes");
            check(v.context.leader_roots[1].lines[1].breaks.size() == 2, "multiple break segments");
            if (std::find(sources.begin(), sources.end(), e.handle()) == sources.end())
                return;
            v.context.leader_roots[0].lines[0].points[1] = {50, 60, 70};
            v.context.leader_roots[1].lines[1].breaks[1].points[0].end = {17, 18, 19};
            if (v.context.has_text_contents)
                v.context.text_string = "修改\\P引线";
            v.arrowhead_size = 3;
            e.set_definition(v);
        });
        check(d.get_multileader_style("中文引线样式") != nullptr, "custom style fixture missing");
        const auto custom_style = d.get_multileader_style("中文引线样式")->handle();
        d.for_each<DxfMultiLeaderStyle>([&](auto &s) {
            if (s.handle() == custom_style) {
                auto v = s.definition();
                v.default_text = "修改默认";
                v.line_color = Color::by_layer();
                v.text_height = 7;
                s.set_definition(v);
            }
        });
        write(d, prefix + "-edited.dxf");
        auto edited = records(save(d), binary);
        for (auto h : d.paper_space_entity_handles())
            check(at(before, h) == at(edited, h), "paper leaders unchanged");
        std::vector<std::string> copies;
        for (auto h : sources)
            copies.push_back(hex(d.copy_entity(h, EntitySpace::Paper)));
        write(d, prefix + "-copied.dxf");
        for (auto h : copies)
            check(d.remove_entity(h), "delete multileader copy");
        for (auto h : sources)
            check(d.remove_entity(h), "delete source");
        check(d.reference_issues().empty(), "multileader references");
        write(d, prefix + "-deleted.dxf");
        fails(made, [&] { made.add_multileader_style("中文引线样式", sd); });
        fails(made, [&] {
            auto v = definition(block, style, text, false);
            v.context.leader_roots[0].lines[0].points[0].x = std::numeric_limits<double>::infinity();
            made.add_multileader(v);
        });
        fails(made, [&] {
            auto v = definition(block, style, text, true);
            v.style_handle = 0xabcdef;
            made.add_multileader(v);
        });
        // Unknown nested fields and application groups stay at their respective levels.
        made.for_each<DxfMultiLeader>([&](auto &e) {
            auto v = e.definition();
            v.context.extra_fields = {{999, "context"}};
            v.context.leader_roots[0].extra_fields = {{999, "root"}};
            v.context.leader_roots[0].lines[0].extra_fields = {
                {102, "{PRIVATE"}, {300, "CONTEXT_DATA{"}, {40, "999"}, {102, "}"}, {999, "line"}};
            e.set_definition(v);
            auto read = e.definition();
            check(read.context.scale_factor == 3 && read.context.extra_fields[0].value == "context" &&
                      read.context.leader_roots[0].lines[0].extra_fields.size() == 5,
                  "unknown field scopes");
        });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
