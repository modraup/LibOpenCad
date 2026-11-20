// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
int main(int argc, char **argv) {
    try {
        check(argc == 3, "format prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[2];
        auto d = DxfDocument::create(binary);
        d.add_layer("零件", 3);
        d.add_layer("标注", 5);
        d.add_symbol(SymbolTable::BlockRecord, "零件块");
        d.add_symbol(SymbolTable::BlockRecord, "装配块");
        d.add_symbol(SymbolTable::BlockRecord, "容器块");
        d.add_symbol(SymbolTable::BlockRecord, "外参块");
        d.for_each<DxfBlock>([](auto &b) {
            if (b.name() == "外参块") {
                b.set_flags(4);
                b.set_xref_path("../参考/底图.dwg");
                b.set_description("外部参照");
            }
        });
        EntityOptions block;
        block.block_name = "零件块";
        block.layer = "零件";
        d.add_line({0, 0, 0}, {10, 0, 0}, block);
        AttributeDefinition attribute;
        attribute.tag = "编号";
        attribute.prompt = "零件编号";
        attribute.text.text = "A-001";
        attribute.text.height = 2;
        d.add_attribute_definition(attribute, block);
        block.block_name = "装配块";
        InsertDefinition part;
        part.block_name = "零件块";
        part.insertion = {5, 6, 0};
        auto nested = hex(d.add_insert(part, {attribute}, block));
        InsertDefinition assembly;
        assembly.block_name = "装配块";
        assembly.insertion = {10, 20, 0};
        auto model = hex(d.add_insert(assembly));
        auto dict = hex(d.add_extension_dictionary(model));
        d.add_xrecord(dict, "自引用", {{340, model}});
        HatchDefinition hatch;
        for (double size : {20., 5.}) {
            HatchPath path;
            path.flags = 3;
            path.edges.push_back(
                HatchPolylineEdge{{{0, 0, 0}, {size, 0, .5}, {size, size, 0}, {0, size, 0}}, true});
            hatch.paths.push_back(path);
        }
        EntityOptions annotation;
        annotation.layer = "标注";
        d.add_hatch(hatch, annotation);
        d.add_dim_style("工程尺寸");
        DimensionDefinition dim;
        dim.style_name = "工程尺寸";
        dim.text_override = "长度<>";
        dim.geometry = AlignedDimension{{0, 0, 0}, {10, 0, 0}, 0};
        dim.definition_point = {0, 3, 0};
        d.add_dimension(dim, annotation);
        std::vector<std::string> detail;
        for (auto name : {"图纸一", "图纸二"}) {
            d.add_layout(name);
            EntityOptions sheet;
            sheet.layout_name = name;
            auto circle = d.add_circle({50, 50, 0}, 40, sheet);
            ViewportDefinition view;
            view.center = {50, 50, 0};
            view.width = 80;
            view.height = 80;
            view.view_height = 30;
            view.clip_boundary = circle;
            view.status_flags |= 0x10000;
            detail.push_back(hex(d.add_viewport(view, sheet)));
            d.add_insert(assembly, sheet);
        }
        ImageDefinition file;
        file.file_name = "参考图片.png";
        file.width = 32;
        file.height = 16;
        auto image_def = d.add_image_definition("参考图片", file);
        RasterImageDefinition image;
        image.definition = image_def;
        image.size = {32, 16};
        auto image_id = hex(d.add_raster_image(image));
        ScaleDefinition scale;
        scale.name = "1:50";
        scale.drawing_units = 50;
        auto scale_id = d.add_auxiliary_object(d.root_dictionary_handle(), "比例", scale);
        ObjectContextDefinition context;
        context.scale = scale_id;
        context.payload = BlockReferenceContext{.25, {1, 2, 3}, {1, 1, 1}};
        d.add_annotation_context(model, "*A1", context);
        write(d, prefix + "-baseline.dxf");
        check(d.reference_issues().empty(), "combined initial graph");
        auto before = records(save(d), binary);
        EntityOptions target;
        target.block_name = "容器块";
        auto block_copy = hex(d.copy_entity(nested, target));
        target.block_name.clear();
        target.layout_name = "图纸二";
        auto sheet_copy = hex(d.copy_entity(model, target));
        auto viewport_copy = hex(d.copy_entity(detail[0], target));
        auto image_copy = hex(d.copy_entity(image_id, target));
        d.for_each<DxfInsert>([&](auto &e) {
            if (e.handle() == block_copy) {
                auto a = e.attribute(0);
                auto v = a.attribute_definition();
                v.text.text = "副本编号";
                a.set_attribute_definition(v);
                e.set_attribute(0, a);
            }
        });
        check(d.entity_handles_in_block("容器块") == std::vector<std::string>{block_copy},
              "copy target block");
        std::set<int> ids;
        auto owner = d.get_layout("图纸二")->block_record_handle();
        d.for_each<DxfViewport>([&](auto &v) {
            if (v.owner_handle() == owner)
                check(ids.insert(v.definition().id).second, "unique copied viewport ID");
        });
        check(ids.size() == 3, "overall and two detail viewports");
        write(d, prefix + "-copied.dxf");
        check(d.reference_issues().empty(), "combined copy graph");
        for (auto h : {block_copy, sheet_copy, viewport_copy, image_copy})
            check(d.remove_entity(h), "combined delete copies");
        write(d, prefix + "-deleted.dxf");
        auto after = records(save(d), binary);
        for (const auto &record : before) {
            auto h = identity(record);
            if (h.empty() || record.front().second == "CLASS")
                continue;
            check(at(after, h) == record, "combined source or neighboring object changed");
        }
        auto stable = save(d);
        bool bad = false;
        try {
            target.layout_name = "missing";
            d.copy_entity(model, target);
        } catch (const std::invalid_argument &) {
            bad = true;
        }
        check(bad && save(d) == stable, "bad target copy rollback");
        auto loaded = read(save(d));
        check(loaded.reference_issues().empty(), "combined saved graph");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
