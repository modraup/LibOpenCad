// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
namespace {
template <class T> T value(const DxfDocument &d, std::string_view h) {
    T v;
    d.for_each<DxfAuxiliaryObject>([&](const auto &o) {
        if (o.handle() == h)
            v = std::get<T>(o.definition());
    });
    return v;
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3];
        auto root = d.root_dictionary_handle();
        auto clip = d.dictionary_entry(root, "裁剪"), neighbor = d.dictionary_entry(root, "邻居裁剪");
        d.for_each<DxfAuxiliaryObject>([](auto &o) { o.set_definition(o.definition()); });
        write(d, prefix + "-roundtrip.dxf");
        auto v = value<SpatialFilterDefinition>(d, clip);
        check(v.front_clip == 12 && v.back_clip == -7 && v.inverse_block_transform.m[0][3] == 1 &&
                  v.clip_transform.m[2][2] == 4,
              "clip distances and matrix order");
        v.front_clip = 22;
        v.clip_transform.m[1][3] = 44;
        v.boundary[0].x = -3;
        d.set_auxiliary_definition(clip, v);
        auto gh = d.dictionary_entry(root, "组");
        auto g = value<GroupDefinition>(d, gh);
        g.entities.pop_back();
        g.selectable = false;
        d.set_auxiliary_definition(gh, g);
        auto sh = d.dictionary_entry(root, "比例");
        auto scale = value<ScaleDefinition>(d, sh);
        scale.drawing_units = 100;
        d.set_auxiliary_definition(sh, scale);
        write(d, prefix + "-edited.dxf");
        check(value<SpatialFilterDefinition>(d, neighbor).front_clip == 12, "neighbor clip unchanged");
        auto fresh = DxfDocument::create(binary);
        auto a = fresh.add_line({0, 0, 0}, {1, 0, 0}), b = fresh.add_line({0, 1, 0}, {1, 1, 0});
        auto dictionary = fresh.root_dictionary_handle();
        SpatialFilterDefinition f;
        f.front_clip = 5;
        f.inverse_block_transform = Matrix4::translation(8, 9, 10);
        f.boundary = {{0, 0}, {4, 5}};
        fresh.add_auxiliary_object(dictionary, "新裁剪", f);
        SortEntitiesDefinition sort;
        sort.block = std::stoull(fresh.get_record(hex(a))->owner_handle(), nullptr, 16);
        sort.entries = {{a, 20}, {b, 10}};
        auto order = fresh.add_auxiliary_object(dictionary, "新排序", sort);
        auto sorted = value<SortEntitiesDefinition>(fresh, hex(order));
        check(sorted.entries[0].sort_handle > sorted.entries[1].sort_handle, "preserve requested draw order");
        auto restored = read(save(fresh));
        check(restored.allocate_handle() > sorted.entries[0].sort_handle, "reserve imported sort handles");
        GroupDefinition group;
        group.entities = {a, b};
        fresh.add_auxiliary_object(dictionary, "新分组", group);
        ScaleDefinition s;
        s.name = "中文比例";
        s.drawing_units = 20;
        s.unit_scale = false;
        fresh.add_auxiliary_object(dictionary, "新比例", s);
        DictionaryVariableDefinition dv;
        dv.value = "修改变量";
        fresh.add_auxiliary_object(dictionary, "新变量", dv);
        BookColorDefinition color;
        color.book = "色本";
        color.name = "绿";
        color.color = Color::from_true_color_value(0x123456);
        auto color_id=hex(fresh.add_auxiliary_object(dictionary, "新颜色", color));
        fresh.add_auxiliary_object(dictionary, "新遮罩变量", WipeoutVariablesDefinition{2, {}});
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "auxiliary graph");
        auto color_order=records(save(fresh),binary);auto& raw_color=at(color_order,color_id);
        raw_color.push_back({62,binary?detail::binary_value_from_text(62,"7"):"7"});
        raw_color.push_back({430,"前色本$前颜色"});raw_color.push_back({1,"最终颜色"});
        auto reordered=read(encode(color_order,binary));auto final_color=value<BookColorDefinition>(reordered,color_id);
        check(final_color.color==Color::from_true_color_value(0x123456)&&final_color.name=="最终颜色"&&final_color.book=="前色本","true color priority and ordered names");
        write(reordered,prefix+"-color-order.dxf");
        auto saved = save(fresh);
        bool rejected = false;
        try {
            sort.entries[0].entity = 0xfffffff;
            fresh.add_auxiliary_object(dictionary, "坏排序", sort);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && save(fresh) == saved, "invalid sort atomic");
        f.clip_transform.m[3][0] = 1;
        rejected = false;
        try {
            fresh.add_auxiliary_object(dictionary, "坏裁剪", f);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && save(fresh) == saved, "reject perspective clip transform");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
