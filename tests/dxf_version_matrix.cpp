// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
// Exercise both input transports independently from every output transport.
void cross_version_matrix(const std::string &prefix) {
    auto original = DxfDocument::create(false);
    original.add_line({1, 2, 3}, {4, 5, 6});
    original.add_circle({7, 8, 9}, 10);
    original.add_text("中文版本测试", {1, 2, 3}, 2.5);
    DxfConversionOptions modern;
    modern.allow_loss = true;
    R12ExportOptions legacy;
    legacy.allow_loss = true;
    for (auto source : detail::dxf_versions)
        for (bool in_binary : {false, true}) {
            auto document = original;
            if (source.version == DxfVersion::AC1009) {
                auto exported = original.export_r12(legacy);
                check(exported.report.can_write, "R12 matrix source");
                document = read(exported.ascii);
                std::ostringstream stream;
                document.write(stream, in_binary);
                document = read(stream.str());
            } else {
                auto exported = original.export_version(source.version, in_binary, modern);
                check(exported.report.can_write, "modern matrix source");
                document = read(exported.bytes);
            }
            const auto before = save(document);
            for (auto target : detail::dxf_versions)
                for (bool out_binary : {false, true}) {
                    std::string bytes;
                    if (target.version == DxfVersion::AC1009 && !out_binary) {
                        auto result = document.export_r12(legacy);
                        check(result.report.can_write, "R12 target");
                        bytes = std::move(result.ascii);
                    } else {
                        auto result = document.export_version(target.version, out_binary, modern);
                        if (target.version == DxfVersion::AC1009) {
                            check(!result.report.can_write && result.bytes.empty(),
                                  "explicit binary R12 rejection");
                            continue;
                        }
                        check(result.report.can_write, "modern target");
                        bytes = std::move(result.bytes);
                    }
                    auto again = read(bytes);
                    check(again.version() == target.version && again.is_binary() == out_binary,
                          "matrix output declaration");
                    auto path = prefix + "-cross-" + std::string(source.name) + (in_binary ? "-b-" : "-a-") +
                                std::string(target.name) + (out_binary ? "-b.dxf" : "-a.dxf");
                    std::ofstream output(path, std::ios::binary);
                    output << bytes;
                    check(bool(output), "matrix fixture write");
                }
            check(before == save(document), "matrix source changed");
        }
}
int main(int argc, char **argv) try {
    check(argc == 2, "output prefix");
    std::string prefix = argv[1];
    cross_version_matrix(prefix);
    std::ofstream matrix(prefix + "-plans.txt");
    for (const auto &v : detail::dxf_versions)
        for (bool binary : {false, true}) {
            const auto name = std::string(v.name);
            std::string source = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\n" + name +
                                 "\n9\n$DWGCODEPAGE\n3\nANSI_"
                                 "936\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nA\n8\n0\n10\n1"
                                 "\n20\n2\n30\n3\n11\n4\n21"
                                 "\n5\n31\n6\n0\nCIRCLE\n5\nB\n8\n0\n10\n7\n20\n8\n30\n9\n40\n10\n0\nT"
                                 "EXT\n5\nC\n8\n0\n10\n1\n"
                                 "20\n2\n30\n3\n40\n2.5\n1\nASCII\n0\nENDSEC\n0\nEOF\n";
            auto doc = read(source);
            doc.for_each<DxfText>([](auto &text) { text.set_text("中文版本测试"); });
            std::ostringstream out;
            doc.write(out, binary);
            DxfReadStatistics stats;
            std::istringstream input(out.str());
            auto decoded = DxfDocument::read(input, DxfReadOptions{}, stats);
            check(decoded.version() == v.version && stats.binary == binary && stats.diagnostics.empty(),
                  "native version-preserving matrix");
            decoded.for_each<DxfText>(
                [](auto &text) { check(text.text() == "中文版本测试", "version encoding text"); });
            std::ofstream file(prefix + "-" + name + (binary ? "-binary.dxf" : "-ascii.dxf"),
                               std::ios::binary);
            file << out.str();
            file.close();
            auto preserve = dxf_output_plan(v.version, binary);
            check(preserve.effective == v.version, "default policy preserves source version");
            auto rust = dxf_output_plan(v.version, binary, {}, DxfOutputPolicy::UpstreamCompatible);
            matrix << name << ' ' << binary << " 0 " << dxf_version_string(rust.effective) << '\n';
            if (v.version == DxfVersion::AC1009 && binary) {
                bool rejected = false;
                try {
                    dxf_output_plan(v.version, binary, v.version, DxfOutputPolicy::UpstreamCompatible);
                } catch (const std::invalid_argument &) {
                    rejected = true;
                }
                check(rejected, "explicit binary R12 rejected upstream");
                matrix << name << " 1 1 REJECT\n";
            } else {
                auto explicit_plan =
                    dxf_output_plan(v.version, binary, v.version, DxfOutputPolicy::UpstreamCompatible);
                check(explicit_plan.requires_r12_writer == (v.version == DxfVersion::AC1009),
                      "separate R12 path");
                matrix << name << ' ' << binary << " 1 " << dxf_version_string(explicit_plan.effective)
                       << '\n';
            }
        }
    bool rejected = false;
    try {
        dxf_output_plan(DxfVersion::Unknown, false);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "unknown version not silently relabeled");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
