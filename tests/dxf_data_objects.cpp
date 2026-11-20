// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3];
        int count = 0;
        d.for_each<DxfDataObject>([&](auto &e) {
            e.set_definition(e.definition());
            ++count;
        });
        check(count == 18, "data object count");
        write(d, prefix + "-roundtrip.dxf");
        d.for_each<DxfDataObject>([](auto &e) {
            auto v = e.definition();
            if (auto p = std::get_if<BreakDataDefinition>(&v.payload)) {
                p->points[0].first.z = 99;
                p->points[0].subentity_object = std::stoull(e.handle(), nullptr, 16);
                p->points[0].subentity_type = 3;
                p->points[0].subentity_index = 17;
            }
            if (auto p = std::get_if<CloudColorMapDefinition>(&v.payload))
                p->ramps[0].colors[1].color = 7;
            e.set_definition(v);
        });
        write(d, prefix + "-edited.dxf");
        auto fresh = DxfDocument::create(binary);
        CloudFileDefinition file;
        file.extended = true;
        file.filename = "新点云.rcs";
        file.point_count = 123456789012LL;
        auto def = fresh.add_data_object(fresh.root_dictionary_handle(), "点云", {file, {}});
        PointCloudExDefinition cloud;
        cloud.definition = def;
        cloud.name = "场景";
        cloud.current_scheme = "真彩色";
        cloud.intensity_scheme = "强度";
        cloud.classification_scheme = "分类";
        cloud.stylization = 2;
        cloud.intensity_out_of_range = 1;
        cloud.show_cropping = true;
        cloud.hidden_scans = {"扫描甲", "扫描乙"};
        cloud.hidden_regions = {4, 8};
        PointCloudCrop crop;
        crop.type = 2;
        crop.inside = true;
        crop.points = {{0, 0, 0}, {1, 2, 3}, {5, 6, 7}};
        cloud.crops.push_back(crop);
        crop.inverted = true;
        crop.plane = {3, 4, 5};
        cloud.crops.push_back(crop);
        auto source = hex(fresh.add_extended_entity(cloud));
        auto copy = hex(fresh.copy_entity(source, EntitySpace::Paper));
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "cloud references");
        check(fresh.remove_entity(copy), "cloud cascade");
        write(fresh, prefix + "-deleted.dxf");
        auto reread = read(save(fresh));
        int seen = 0;
        reread.for_each<DxfExtendedEntity>([&](auto &e) {
            auto v = std::get<PointCloudExDefinition>(e.definition());
            check(v.crops.size() == 2 && v.crops[1].inverted && v.hidden_regions.size() == 2,
                  "cloud crop tail");
            ++seen;
        });
        check(seen == 1, "cloud survivor");
        auto before = save(fresh);
        bool bad = false;
        try {
            cloud.definition = 0xFFFFFF;
            fresh.add_extended_entity(cloud);
        } catch (const std::invalid_argument &) {
            bad = true;
        }
        check(bad && save(fresh) == before, "cloud missing reference rollback");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
